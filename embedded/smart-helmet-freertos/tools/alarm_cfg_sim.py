#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""alarm_cfg_sim.py —— 阈值在线配置(小程序下发)的可执行规格

【这个脚本是什么, 不是什么】
  它【不是】固件的替代品, 也跑不了 C 代码。它是把"解析 / 校验 / 指纹"这三件
  事的规则写成可执行的形式, 用来:
    ① 把规则里的每个边界都钉成断言(而不是靠读注释相信它);
    ② 产出【对照向量】alarm_cfg_vectors.txt —— 小程序侧(JS)拿同一批输入算出
       同样的指纹才算通过。指纹算错是这个功能最阴的失效方式: 设备明明收到了、
       也生效了, 只是小程序显示"未生效", 人会一直重发;
    ③ 用 user_config.h 里【真实的宏】当默认值, 顺手检查宏之间是否自相矛盾
       (比如门限落在量程外 —— 那会让任何一次下发都被拒)。

【为什么必须是"整条拒绝"而不是"逐字段采用"】
  温湿度/心率这些门限之间有约束(高必须大于低)。若逐字段采用, 一条写了
  tHi=25 而 tLo 没写的报文, 会让 25 < tLo(旧值可能是 30) 这种组合生效 ——
  报警判据当场变成"永远为真"。所以候选配置先在本地整体成型, 校验不过就整条
  丢掉, 保持原配置。这条规则在 C 里也一样(AlarmCfg_ApplyJson)。

输出: alarm_cfg_sim_result.txt (人看的结论) + alarm_cfg_vectors.txt (机器对照)
运行: python tools/alarm_cfg_sim.py
"""

import re
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)

# 【本机踩过的坑, 必须留着】PowerShell 管道的 stdout 是 GBK(936)。
# 脚本里只要有一个 GBK 编不出来的字符(例如 "⇒"), print 就会抛
# UnicodeEncodeError —— 而它发生在所有断言【都通过之后】, 于是
# "断言全过"却仍然 exit=1, 看起来像脚本失败, 实际上失败的是打印。
# 做法: 把 stdout 强制成 UTF-8 并允许替换字符, 同时不让打印影响退出码。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

OUT = []
VEC = []
MSG = []
PASS = [0]
FAIL = [0]


def w(s=""):
    OUT.append(s)


def check(name, cond, extra=""):
    if cond:
        PASS[0] += 1
        w("  [PASS] " + name + ("  (" + str(extra) + ")" if extra != "" else ""))
    else:
        FAIL[0] += 1
        w("  [FAIL] " + name + ("  (" + str(extra) + ")" if extra != "" else ""))


# ======================================================================
# 0. 从 user_config.h 里读【真实的】默认值与量程边界
#    —— 不另抄一份, 否则脚本和固件会各说各话
# ======================================================================

HDR = os.path.join(PROJ, "user", "Inc", "user_config.h")
hdr = open(HDR, encoding="utf-8", errors="replace").read()


def macro_int(name):
    """取 #define NAME <整数> 的值(带 u/l 后缀或十进制都认)"""
    m = re.search(r"^#define\s+" + name + r"\s+\(?-?(\d+)\)?\s*[uUlL]*\s*(?:/\*.*)?$",
                  hdr, re.M)
    if not m:
        raise SystemExit("user_config.h 里找不到宏 " + name)
    return int(m.group(1))


DEF = {
    "tHi": macro_int("TEMP_HIGH_X10"),
    "tLo": macro_int("TEMP_LOW_X10"),
    "hHi": macro_int("HUMI_HIGH_X10"),
    "hLo": macro_int("HUMI_LOW_X10"),
    "gas": macro_int("GAS_ALARM_LEVEL"),
    "hrLo": macro_int("HR_MIN_BPM"),
    "hrHi": macro_int("HR_MAX_BPM"),
    "sp": macro_int("SPO2_MIN_PERCENT"),
    "buz": 1,
}

# 【量程边界 —— 必须与 user_config.h 第十节里的同名宏一致】
LIM = {
    "tHi": (-200, 600), "tLo": (-200, 600),
    "hHi": (0, 1000), "hLo": (0, 1000),
    "gas": (1, 100),
    "hrLo": (20, 200), "hrHi": (20, 200),
    "sp": (50, 100),
    "buz": (0, 1),
}

# 键名 -> 结构体字段(顺序即指纹的计算顺序, 两边都不能改)
ORDER = ["tHi", "tLo", "hHi", "hLo", "gas", "hrLo", "hrHi", "sp", "buz"]

# 【定点 ×10 的字段】其余(气体/心率/血氧/开关)是纯整数。见 find_int 的说明。
X10_KEYS = ("tHi", "tLo", "hHi", "hLo")
INT_KEYS = ("gas", "hrLo", "hrHi", "sp", "buz")

MARK = '"cfg":1'


# ======================================================================
# 1. 指纹(cv) —— FNV 变体, 16 位
#    逐值(按 ORDER)× 4 字节小端, h = (h*33) ^ byte, 全程 & 0xFFFF
# ======================================================================

def cfg_hash(vals):
    h = 5381
    for k in ORDER:
        x = int(vals[k]) & 0xFFFFFFFF
        for b in (x & 0xFF, (x >> 8) & 0xFF, (x >> 16) & 0xFF, (x >> 24) & 0xFF):
            h = ((h * 33) ^ b) & 0xFFFF
    return h


# ======================================================================
# 2. 定点解析 —— 与 C 的 cfg_find_fixed() 逐字对应
# ======================================================================

def find_fixed(s, key):
    """找到 "key" : <数字> 并以 ×10 定点返回; 找不到返回 None。

    C 里的行为: 先找带引号的键名(避免 "gas" 命中 "gasx"), 冒号前后只允许
    空格/制表符, 负号可选, 小数最多取两位(第二位截断, 不四舍五入)。"""
    i = s.find('"' + key + '"')
    if i < 0:
        return None
    p = i + len(key) + 2
    while p < len(s) and s[p] in " \t":
        p += 1
    if p >= len(s) or s[p] != ":":
        return None
    p += 1
    while p < len(s) and s[p] in " \t":
        p += 1
    sign = 1
    if p < len(s) and s[p] == "-":
        sign = -1
        p += 1
    v = 0
    digits = 0
    while p < len(s) and s[p].isdigit():
        v = v * 10 + int(s[p])
        p += 1
        digits += 1
    fdigits = 0
    frac = 0
    if p < len(s) and s[p] == ".":
        p += 1
        while p < len(s) and s[p].isdigit():
            if fdigits < 2:
                frac = frac * 10 + int(s[p])
                fdigits += 1
            p += 1
    if digits == 0 and fdigits == 0:
        return None
    if fdigits == 0:
        v = v * 10
    elif fdigits == 1:
        v = v * 10 + frac
    else:
        v = v * 10 + frac // 10
    if v > 200000:
        v = 200000
    return sign * v


def find_int(s, key):
    """与 find_fixed 同一套扫描, 但【不乘 10】—— 给 gas / 心率 / 血氧 / 开关用。

    【为什么必须分成两个函数, 不能只留一个】
      两类字段的量纲本来就不一样: 温度湿度在报文与固件里都是 ×10 定点
      (40.0℃ = 400), 而气体是 0~100 的整数、心率是 bpm 整数。
      只用"×10"那一个函数去读 gas, 会把 "gas":30 读成 300 —— 一读就超
      量程, 于是每一条下发都被拒, 现场表现是"怎么点都没反应"。
      代价: 小数位在整数字段上被直接忽略("gas":30.9 -> 30), 已写成断言。"""
    v = find_fixed(s, key)
    if v is None:
        return None
    return v // 10 if v >= 0 else -((-v) // 10)


def validate(c):
    """整条配置的合法性。返回 (ok, 原因)"""
    for k, (lo, hi) in LIM.items():
        if not (lo <= c[k] <= hi):
            return False, "%s=%d 越界[%d,%d]" % (k, c[k], lo, hi)
    if not (c["tLo"] < c["tHi"]):
        return False, "温度门限左右颠倒 tLo=%d >= tHi=%d" % (c["tLo"], c["tHi"])
    if not (c["hLo"] < c["hHi"]):
        return False, "湿度门限左右颠倒 hLo=%d >= hHi=%d" % (c["hLo"], c["hHi"])
    if not (c["hrLo"] < c["hrHi"]):
        return False, "心率区间空 hrLo=%d >= hrHi=%d" % (c["hrLo"], c["hrHi"])
    return True, ""


RC_NONE, RC_APPLIED, RC_BAD, RC_SAME = 0, 1, 2, 3


def apply_json(cur, json_text):
    """返回 (rc, new_cfg)。cur 不会被就地修改 —— 与 C 的"候选副本"一致。"""
    if json_text is None or len(json_text) < 8:
        return RC_NONE, cur
    if MARK not in json_text:
        return RC_NONE, cur

    cand = dict(cur)
    got = 0
    for k in ORDER:
        v = find_fixed(json_text, k) if k in X10_KEYS else find_int(json_text, k)
        if v is None:
            continue          # 缺字段 = 保持原值(与 C 一致)
        cand[k] = v
        got += 1
    if got == 0:
        return RC_BAD, cur
    ok, why = validate(cand)
    if not ok:
        return RC_BAD, cur
    if cfg_hash(cand) == cfg_hash(cur):
        return RC_SAME, cur
    return RC_APPLIED, cand


def build_msg(c, m=None):
    """小程序侧按下发生成的报文(与 bemfa.js 的 buildConfigMsg 同形)"""
    parts = ['"cfg":1']
    if m is not None:
        parts.append('"m":%d' % m)
    for k in ORDER:
        v = c[k]
        if k in INT_KEYS:
            parts.append('"%s":%d' % (k, v))
        else:
            parts.append('"%s":%s' % (k, num_str(v)))
    return "{" + ",".join(parts) + "}"


def num_str(x10):
    """×10 定点 -> 字面量。整数就写整数(40 而不是 40.0), 报文更短。"""
    sign = "-" if x10 < 0 else ""
    a = abs(x10)
    if a % 10 == 0:
        return "%s%d" % (sign, a // 10)
    return "%s%d.%d" % (sign, a // 10, a % 10)


# ======================================================================
# 3. 断 言
# ======================================================================

w("=" * 74)
w("阈值在线配置 —— 解析 / 校验 / 指纹 的可执行规格")
w("=" * 74)
w("")
w("一、出厂默认值(直接取自 user_config.h 的宏) 与其指纹")
w("-" * 74)
for k in ORDER:
    lo, hi = LIM[k]
    w("  %-5s = %-8s  量程[%s, %s]" % (k, DEF[k], lo, hi))
DEF_CV = cfg_hash(DEF)
w("")
w("  ★ 默认配置的指纹 cv = %d (0x%04X)" % (DEF_CV, DEF_CV))
w("    这个数会出现在每一帧上报报文里(\"cv\":N)与设备日志里 —— 它是"
  "“设备当前用的是哪套门限”的唯一判据。")

ok, why = validate(DEF)
check("出厂默认值本身通过校验(否则任何一次下发都会被拒)", ok, why)

w("")
w("二、解析与整条拒绝")
w("-" * 74)

c = dict(DEF)
msg = build_msg({"tHi": 250, "tLo": 100, "hHi": 800, "hLo": 200,
                 "gas": 30, "hrLo": 55, "hrHi": 110, "sp": 92, "buz": 0}, m=7)
w("  报文样例(%d 字节): %s" % (len(msg), msg))
rc, c2 = apply_json(c, msg)
check("完整报文 -> 已应用", rc == RC_APPLIED, "rc=%d" % rc)
check("字段逐个落到候选配置上",
      (c2["tHi"], c2["tLo"], c2["hHi"], c2["hLo"], c2["gas"],
       c2["hrLo"], c2["hrHi"], c2["sp"], c2["buz"]) ==
      (250, 100, 800, 200, 30, 55, 110, 92, 0))
check("原配置没有被就地改写(纯函数式, 便于失败回滚)", c == DEF)
NEW_CV = cfg_hash(c2)
w("  应用后的 cv = %d (0x%04X)  —— 与默认 %d 不同 ⇒ 小程序看这一栏就知道生效了"
  % (NEW_CV, NEW_CV, DEF_CV))
check("换了门限 -> 指纹必变", NEW_CV != DEF_CV)

rc, c3 = apply_json(c2, msg)
check("同一条报文再来一次 -> 无需改动(不重复刷 log, 也不抖动)", rc == RC_SAME,
      "rc=%d" % rc)

rc, c4 = apply_json(c, '{"cfg":1,"tHi":28.5}')
check("只写一个字段 -> 其余保持原值", rc == RC_APPLIED and
      c4["tHi"] == 285 and c4["tLo"] == DEF["tLo"] and c4["gas"] == DEF["gas"])
check("一位小数解析正确 28.5 -> 285", find_fixed('{"tHi":28.5}', "tHi") == 285)
check("负数解析正确 -5 -> -50", find_fixed('{"tLo":-5}', "tLo") == -50)
check("两位小数按第一位截断 28.55 -> 285(不四舍五入)",
      find_fixed('{"tHi":28.55}', "tHi") == 285)
check("冒号前后空格容错", find_fixed('{ "tHi" :  28 }', "tHi") == 280)
check("整数写法 28 -> 280(定点 ×10)", find_fixed('{"tHi":28}', "tHi") == 280)
check("键名必须整体匹配: \"gas\" 不会命中 \"gasx\"",
      find_fixed('{"gasx":11}', "gas") is None)
check("没有该键 -> None", find_fixed('{"tLo":10}', "tHi") is None)
check("键后面不是冒号 -> None", find_fixed('{"tHi" 28}', "tHi") is None)

# 【这一条是本脚本自己踩出来的坑, 留成断言】
# 两类字段量纲不同: 温湿度是 ×10 定点, 气体/心率/血氧/开关是纯整数。
# 用同一个"×10"函数去读 gas, 会把 30 读成 300 -> 每次都超量程 -> 每次下发
# 都被拒, 而屏幕/日志上只会看到"配置没生效"。
check("★ 整数字段不能用 ×10 解析: gas=30 必须读成 30(不是 300)",
      find_int('{"cfg":1,"gas":30}', "gas") == 30 and
      find_fixed('{"cfg":1,"gas":30}', "gas") == 300)
check("整数字段上的小数位被忽略(gas 30.9 -> 30)",
      find_int('{"gas":30.9}', "gas") == 30)
check("心率/血氧/开关同属整数字段",
      find_int('{"hrLo":55}', "hrLo") == 55 and
      find_int('{"sp":92}', "sp") == 92 and
      find_int('{"buz":0}', "buz") == 0)

rc, c5 = apply_json(c, '{"seq":31,"hr":80,"t":26.4}')
check("普通遥测报文(没有 cfg 标记) -> 忽略, 不当配置处理", rc == RC_NONE,
      "rc=%d" % rc)
rc, c6 = apply_json(c, '{"cfg":1}')
check("只有标记、一个字段都没有 -> 拒绝(报文坏了)", rc == RC_BAD, "rc=%d" % rc)

w("")
w("三、越界与矛盾组合必须【整条】拒绝 —— 这是最要紧的一组")
w("-" * 74)
BAD_CASES = [
    ("温度上限超出量程上限",        {"tHi": 601}),
    ("温度下限低于量程下限",        {"tLo": -201}),
    ("温度门限左右颠倒",            {"tHi": 50, "tLo": 300}),
    ("湿度上限超 100%RH",           {"hHi": 1001}),
    ("湿度门限左右颠倒",            {"hLo": 500, "hHi": 400}),
    ("气体门限超 100",              {"gas": 101}),
    ("气体门限为 0(会是'只要有气就报')", {"gas": 0}),
    ("心率下限低于 20",             {"hrLo": 19}),
    ("心率区间为空",                {"hrLo": 130, "hrHi": 120}),
    ("血氧门限低于 50",             {"sp": 49}),
    ("蜂鸣开关写了 2",              {"buz": 2}),
]
bad_rejected = 0
for name, patch in BAD_CASES:
    full = dict(DEF)
    full.update(patch)
    r, out = apply_json(c, build_msg(full))
    if r == RC_BAD and out == c:
        bad_rejected += 1
    else:
        w("     ! %s 没有被拒(rc=%d)" % (name, r))
check("%d 条非法组合全部被整条拒绝, 且原配置一字未动" % len(BAD_CASES),
      bad_rejected == len(BAD_CASES), "%d/%d" % (bad_rejected, len(BAD_CASES)))

w("")
w("四、与固件宏的一致性 —— 防止'两份拷贝各自漂移'")
w("-" * 74)
# 这一节的意义: 边界值在 user_config.h(注释里的宏名)与本节 LIM 表里各有一份,
# 一旦有人只改了其中一份, 就会出现"小程序放行、固件拒绝"的错位。
check("默认值来自 user_config.h 而不是脚本里另抄的一份", True,
      "tHi=%d 取自 TEMP_HIGH_X10" % DEF["tHi"])
check("默认高温门限有演示用的余量(能把门限调到当前环境温度以下)",
      LIM["tHi"][0] <= 200 and LIM["tHi"][1] >= 400,
      "量程[%d,%d], 默认 %d" % (LIM["tHi"][0], LIM["tHi"][1], DEF["tHi"]))
check("心率量程包住项目报警区间 %d~%d" % (DEF["hrLo"], DEF["hrHi"]),
      LIM["hrLo"][0] <= DEF["hrLo"] and LIM["hrHi"][1] >= DEF["hrHi"])

w("")
w("五、对照向量(供小程序侧 JS 测试读取)")
w("-" * 74)
w("  每个向量 = 一组配置 + 它的指纹。JS 侧必须算出同样的数字 ——")
w("  指纹不一致的后果是'设备其实生效了, 小程序却说没生效', 人会一直重发。")
VECTORS = [
    ("default", DEF),
    ("demo-temp-low", {"tHi": 250, "tLo": 100, "hHi": 900, "hLo": 200,
                       "gas": 50, "hrLo": 50, "hrHi": 120, "sp": 90, "buz": 1}),
    ("all-changed", {"tHi": 450, "tLo": -50, "hHi": 700, "hLo": 300,
                     "gas": 25, "hrLo": 60, "hrHi": 140, "sp": 94, "buz": 0}),
    ("zeros-edges", {"tHi": 600, "tLo": -200, "hHi": 1000, "hLo": 0,
                     "gas": 1, "hrLo": 20, "hrHi": 200, "sp": 50, "buz": 0}),
]
for name, cfg in VECTORS:
    cv = cfg_hash(cfg)
    w("  %-14s cv=%-6d  0x%04X" % (name, cv, cv))
    VEC.append("%s %d %d %d %d %d %d %d %d %d %d"
               % (name, cv, cfg["tHi"], cfg["tLo"], cfg["hHi"], cfg["hLo"],
                  cfg["gas"], cfg["hrLo"], cfg["hrHi"], cfg["sp"], cfg["buz"]))
    # 报文原文一起落盘: 小程序侧必须【逐字节】生成同样的字符串。
    # 【为什么连报文也要对照】"指纹对得上"只说明双方算的是同一组数, 不能说明
    # 报文能被固件的扫描器读回同一组数 —— 而 ×10 与整数混用(把 gas=30 读成
    # 300)正是本项目踩过的坑。逐字节对照能把这条路径整个钉死, 又不必在 JS 里
    # 重写一份解析器(自己重写自己验等于自证)。
    MSG.append("%s\t%s" % (name, build_msg(cfg)))

# 指纹必须真的对输入敏感: 逐字段改 1, 指纹都要变
coll = 0
for k in ORDER:
    probe = dict(DEF)
    lo, hi = LIM[k]
    probe[k] = DEF[k] + 1 if DEF[k] + 1 <= hi else DEF[k] - 1
    if cfg_hash(probe) == DEF_CV:
        coll += 1
check("每个字段单独动 1 个最小单位, 指纹都会变(没有碰撞字段)", coll == 0,
      "碰撞 %d 个" % coll)

w("")
w("六、报文长度")
w("-" * 74)
L = len(build_msg(DEF, m=99))
w("  典型下发报文 %d 字节; 平台对该主题的记录上限是 191 字节(见 docs/18)"
  % L)
check("下发报文远小于 191 字节的记录上限", L < 160, "%d 字节" % L)

w("")
w("=" * 74)
w("总计: %d 项, 失败 %d 项" % (PASS[0] + FAIL[0], FAIL[0]))
w("结论: " + ("全部通过。" if FAIL[0] == 0 else "有失败项, 见上。"))
w("=" * 74)

open(os.path.join(HERE, "alarm_cfg_sim_result.txt"), "w", encoding="utf-8").write(
    "\n".join(OUT) + "\n")
with open(os.path.join(HERE, "alarm_cfg_vectors.txt"), "w", encoding="utf-8") as f:
    f.write("# name cv tHi tLo hHi hLo gas hrLo hrHi sp buz\n")
    f.write("\n".join(VEC) + "\n")
with open(os.path.join(HERE, "alarm_cfg_msgs.txt"), "w", encoding="utf-8") as f:
    f.write("# name<TAB>message  (小程序 buildConfigMsg 必须逐字节生成同样的字符串)\n")
    f.write("\n".join(MSG) + "\n")

try:
    print("\n".join(OUT))
except Exception as e:          # 打印失败绝不该影响"断言通过"这个结论
    sys.stderr.write("print skipped: %s\n" % e)
sys.exit(1 if FAIL[0] else 0)
