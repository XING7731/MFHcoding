# -*- coding: utf-8 -*-
"""thresh_why.py —— 为什么小程序不告警？只读取数 + 逐项比对，给出判据。

【为什么需要这个工具】
   小程序的阈值预警判据在 bemfa.js 的 checkThresholds()：拿【本机保存的门限】
   去比【最新一帧报文】。不告警只有四种可能，肉眼分不出来：
     ① 这一帧各项本来就在门限内          -> 正常，不该告警
     ② 门限改了但没落本机（没点按钮）    -> 本机那份还是旧的
     ③ 某一项是 null（没读数）           -> 判据里 null 直接跳过
     ④ 取数失败 / 板子停了               -> 页面根本没拿到这一帧
   本工具把"云端那一帧"和"你要的门限"都摆出来，逐项算一遍，
   直接指出卡在哪一种。它【只读】，不建 MQTT 连接、不顶板子。

用法:
   python tools/thresh_why.py                       # 用出厂默认门限比对
   python tools/thresh_why.py tHi=20 gas=10         # 用指定门限比对
"""
import json
import os
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
UID = "d2f5cf227188d137f8171da8611a7614"
TOPIC = "temp001"
OUT = []

# 与小程序 bemfa.js 的 T_DEF 保持一致（改那边要同步改这里）
T_DEF = {"tHi": 40, "tLo": 10, "hHi": 90, "hLo": 20,
         "gas": 50, "hrLo": 50, "hrHi": 120, "sp": 90, "buz": 1}

# 与固件 alarm_cfg.h 的 CFG_* 量程一致
T_LIMIT = {"tHi": (-20, 60), "tLo": (-20, 60),
           "hHi": (0, 100), "hLo": (0, 100),
           "gas": (1, 100), "hrLo": (20, 200), "hrHi": (20, 200),
           "sp": (50, 100), "buz": (0, 1)}


def w(s):
    OUT.append(s)


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    return urllib.request.urlopen(req, timeout=15).read().decode("utf-8", "replace")


def parse_overrides(argv):
    """tHi=20 gas=10 -> {"tHi":20.0, "gas":10.0}"""
    t = dict(T_DEF)
    for a in argv:
        if "=" not in a:
            continue
        k, v = a.split("=", 1)
        k = k.strip()
        if k not in T_DEF:
            continue
        try:
            t[k] = float(v)
        except ValueError:
            pass
    return t


T = parse_overrides(sys.argv[1:])

now = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
w("=" * 72)
w("  取数时刻(本机): %s" % now)
w("  主题: %s  (MQTT 设备云 type=1)" % TOPIC)
w("=" * 72)

# ---------- 1. 取最新一帧 ----------
url = ("https://apis.bemfa.com/va/getmsg?uid=%s&topic=%s&type=1&num=1"
       % (UID, TOPIC))
w("")
w("[1] 取云端最新一帧")
try:
    raw = get(url)
except Exception as e:
    w("    !! 请求失败: %s" % e)
    w("       -> 这就是可能原因 ④ 的前半段（小程序取数失败同样会这样）。")
    raw = ""

t = None
cloud_unix = None
age = None
if raw:
    try:
        j = json.loads(raw)
        w("    code=%s message=%s" % (j.get("code"), j.get("message")))
        data = j.get("data")
        if isinstance(data, list) and data:
            msg = data[0].get("msg", "") or ""
            cloud_unix = data[0].get("unix") or data[0].get("timestamp")
            if cloud_unix:
                try:
                    cu = int(cloud_unix)
                    age = time.time() - cu
                    w("    云端时间 = %s   (距今 %.1f 秒)"
                      % (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(cu)), age))
                except Exception:
                    pass
            try:
                t = json.loads(msg)
            except Exception as e:
                w("    !! 报文不是 JSON: %s" % e)
                w("       原文: %s" % msg[:300])
        else:
            w("    !! 该主题下没有任何消息")
            w("       -> 可能原因 ④：板子从未成功上报，或主题名不对。")
    except Exception as e:
        w("    !! 解析失败: %s" % e)
        w("       原文: %s" % raw[:300])

if isinstance(t, dict):
    w("")
    w("    最新一帧: " + json.dumps(t, ensure_ascii=False))
    w("    seq=%s   st=%s   cv=%s" % (t.get("seq"), t.get("st"), t.get("cv")))

# ---------- 2. 逐项比对 ----------
w("")
w("[2] 逐项比对（小程序 checkThresholds 的同一套判据）")
w("    门限: " + json.dumps(T, ensure_ascii=False))
w("")

hits = []
skipped = []
ok_items = []

if not isinstance(t, dict):
    w("    （没有可比的报文）")
else:
    checks = [
        ("gas",  "气体",   t.get("gas"),  "gt", T["gas"]),
        ("tHi",  "温度",   t.get("t"),    "gt", T["tHi"]),
        ("tLo",  "温度",   t.get("t"),    "lt", T["tLo"]),
        ("hHi",  "湿度",   t.get("h"),    "gt", T["hHi"]),
        ("hLo",  "湿度",   t.get("h"),    "lt", T["hLo"]),
        ("hr",   "心率",   t.get("hr"),   "out", (T["hrLo"], T["hrHi"])),
        ("sp",   "血氧",   t.get("spo2"), "lt", T["sp"]),
    ]
    for key, name, val, op, lim in checks:
        if val is None:
            skipped.append((key, name))
            w("    %-4s %-3s  = null            → 跳过（判据里 null 不参与比较）"
              % (name, key))
            continue
        try:
            v = float(val)
        except (TypeError, ValueError):
            w("    %-4s %-3s  = %-15r → 不是数字，跳过" % (name, key, val))
            continue

        fired = False
        desc = ""
        if op == "gt":
            fired = v > lim
            desc = "%g > 门限 %g" % (v, lim)
        elif op == "lt":
            fired = v < lim
            desc = "%g < 门限 %g" % (v, lim)
        else:
            lo, hi = lim
            fired = (v < lo or v > hi)
            desc = "%g 超出 %g~%g" % (v, lo, hi)

        if fired:
            hits.append(name)
            w("    %-4s %-3s  = %-15g → ★ 越限：%s" % (name, key, v, desc))
        else:
            ok_items.append(name)
            w("    %-4s %-3s  = %-15g → 正常（%s）" % (name, key, v, desc))

# ---------- 3. 结论 ----------
w("")
w("[3] 结论")
if not isinstance(t, dict):
    w("    无法判定：没取到可解析的报文。先解决取数（板子在不在线 / 主题名）。")
else:
    if hits:
        w("    ✓ 按上面这组门限，应该有 %d 条告警：%s" % (len(hits), "、".join(hits)))
        w("      若小程序上【看不到】这条横幅，那问题不在判据，而在：")
        w("      · 小程序里保存的门限与这里用的不一致（可能改了没点按钮）")
        w("      · 页面没取到这一帧（取数失败 / 通道没起来）")
        w("      在「阈值设置」页核对「本机门限指纹」，并确认点过保存按钮。")
    else:
        w("    ✗ 按上面这组门限【没有任何一项越限】—— 所以不告警是正确的。")
        if ok_items:
            w("      已比且正常的项：%s" % "、".join(ok_items))
        if skipped:
            w("      因无读数被跳过的项：%s" % "、".join(n for _, n in skipped))
            w("      ⚠ 这些项【永远不会】告警，不管门限怎么改 —— 因为没有数字可比。")
            w("        要让它们能告警，得先让板子在这几项上出数。")
        w("")
        w("      想让某项现在就告警：把它的门限调到比当前读数更严一点，例如")
        w("        当前温度 %s、想触发高温告警 → tHi 设成 %s 左右"
          % (t.get("t"), (float(t["t"]) - 1) if t.get("t") is not None else "??"))
        w("      然后在「阈值设置」页改完点【仅存本机】即可（不用碰板子）。")

w("")
open(os.path.join(HERE, "thresh_why_result.txt"), "w",
     encoding="utf-8").write("\n".join(OUT) + "\n")
print("\n".join(OUT))
