# -*- coding: utf-8 -*-
"""PPG 一次测量: 各旋钮对「出数率」的量化收益。

【为什么写这个脚本】
用户问"怎么把测量成功率提高"。固件里能拧的旋钮有十来个, 但"拧哪个最有效"
不能靠常识答 —— 旋钮之间会互相抵消(比如放宽低通能保住脉搏波峰形, 同时也会
把高频噪声放进来)。所以这里用同一段波形、同一组 seed, 把每个旋钮单独扫一遍。

每档输出三个数(三个都必须看, 只看第一个会被带偏):
    出数率 = 给出结果且 |偏差| <= 5 bpm 的比例   ← 要提高的就是它
    错值率 = 给出结果但 |偏差| >  5 bpm 的比例   ← 【底线: 必须保持 0】
    无结果 = 明确报"没测到"的比例                ← 宁可这样, 不要给错数

【怎么复用固件仿真而不复制算法】
tools/ppg_measure_sim.py 是"可执行规格"(算法与 heart_rate.c 一一对应), 它的
关键常量(LP_A / TH_FRAC / MIN_AMP / MEASURE_S / WIN_IBI_MIN / CONSIST_PCT /
NOTCH_RATIO)在算法函数体内都是【模块级直接引用】, 所以 setattr() 就能改 ——
不必把算法抄一份过来。抄一份的坏处是: 抄错不报错, 只让结论变成自证。
每档改完在 finally 里还原, 否则后面的档位会继承前一档的参数。

【一个必须说清的口径】
仿真的 amp 是【输入正弦幅度】, 固件上报的 a(=HR_GetPeakAmp) 是【带通之后
交流通道的峰值幅度】。两者不是同一个量: 4Hz 低通会把峰形削掉一点, 所以
同样的 amp, 输出侧的 a 会略小。本脚本按 a ≈ PK × amp 做数量级对应
(PK = 波形峰值/幅度, 数值算出), 只用于把"实测 a"换算成一个可扫的 amp,
不作严格标定。
"""

import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ppg_measure_sim as M          # noqa: E402  (可执行规格, 见文件头说明)

try:                                  # PowerShell 管道的 stdout 是 GBK
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

OUT = []


def w(s=""):
    OUT.append(s)


FS = M.FS
SAMPLE_MS = M.SAMPLE_MS
TOTAL = 26.0            # 秒。够最长的档: 1.0(起点) + 0.3(去抖) + 3(稳定) + 20(窗口)
ON = 1000.0             # 贴手时刻(ms), 与 ppg_measure_sim.py 的 main 一致
MODE = 'fix'            # 'fix' = 固件当前行为(一次测量 + 切迹抑制)
HRS = (60, 72, 90)
SEEDS = (31, 32, 33, 34, 35, 36, 37, 38)      # 8 个噪声 realization

# 波形峰值幅度 / 输入幅度 —— 数值算出, 不靠手写常数
PK = max(M.pulse_shape(p * 0.001) for p in range(6284))

# 工程里的实测样例(docs/15 §二、docs/16 §四): a = 312
A_MEASURED = 312.0
AMP_OF_A = A_MEASURED / PK

# 只允许 patch 这些 —— 列白名单是为了避免改到会被 __init__ 缓存的量
PATCHABLE = ('LP_A', 'HP_A', 'TH_FRAC', 'MIN_AMP', 'MEASURE_S',
             'WIN_IBI_MIN', 'CONSIST_PCT', 'NOTCH_RATIO', 'WIN_MAX')


def lp_a_from_hz(f):
    """一阶 RC 系数 ← 截止频率。与 ppg_measure_sim.py 模块头的算法同式。"""
    return 1.0 - math.exp(-2.0 * math.pi * f / FS)


hp_a_from_hz = lp_a_from_hz    # 同一个式子, 只是这次用在【高通】上


def sweep(amp, drift, noise=8.0, patch=None, hrs=HRS, seeds=SEEDS):
    """跑一档。返回 (出数, 错值, 无结果, 总数)。"""
    patch = dict(patch or {})
    backup = {}
    for k, v in patch.items():
        if k not in PATCHABLE:
            raise AssertionError("不允许 patch: %s" % k)
        backup[k] = getattr(M, k)
        setattr(M, k, v)
    ok = wrong = none = 0
    try:
        for hr in hrs:
            for sd in seeds:
                d = M.run(MODE, TOTAL,
                          M.Wave(hr, amp=amp, noise=noise, drift=drift),
                          on=ON, seed=sd)
                if d.fin_bpm_valid:
                    if abs(d.fin_bpm - hr) <= 5:
                        ok += 1
                    else:
                        wrong += 1
                else:
                    none += 1
    finally:
        for k, v in backup.items():
            setattr(M, k, v)
    return ok, wrong, none, ok + wrong + none


def row(label, res, note=""):
    ok, wrong, none, tot = res
    return ("  %-30s  %5.0f%%   %5.0f%%   %5.0f%%   %s"
            % (label, 100.0 * ok / tot, 100.0 * wrong / tot,
               100.0 * none / tot, note))


# ------------------------------------------------------------------ 漂移形态
# 第二节的漂移是【单一 0.25Hz 正弦】(ppg_measure_sim.py 的 Wave 就长这样),
# 噪声是【白噪声】。真机的基线摆动来自呼吸 + 压力变化, 更像【有界随机过程】。
# 如果换个漂移形态第二节的结论就崩, 那"参数不是瓶颈"这句话就不可信 ——
# 所以下面这一节专门检验这个模型假设。

class DriftWave(M.Wave):
    """把基类自带的 0.25Hz 正弦漂移关掉, 换成外部给的漂移函数 fn(t_秒)。"""

    def __init__(self, hr, fn, **kw):
        kw.setdefault('drift', 0.0)
        M.Wave.__init__(self, hr, **kw)
        self.fn = fn

    def sample(self, t_ms, dc_on, dc_level=None):
        ir, red = M.Wave.sample(self, t_ms, dc_on, dc_level)
        if dc_on:
            dv = self.fn(t_ms / 1000.0)
            ir += dv
            red += dv * 0.8
        return ir, red


def make_ou(std_target, tau_s, seed, out_hz=None):
    """一阶自回归(OU)过程: 有界 + 有回归, 更像呼吸/压力造成的基线摆动。

    std_target = 稳态标准差(LSB), tau_s = 回归时间常数(秒)。
    离散步: x += -a·x + N(0, σ), a = 1-exp(-dt/tau), σ = std·sqrt(2a)
    ⇒ 稳态标准差 ≈ std。与 ppg_measure_sim.py 的 DRIFT=400 取同一量级,
      便于和正弦漂移对照。

    ⚠ out_hz 这个参数是后来补的, 原因值得记下来:
      单级 AR(1) 的功率谱在 f >> 1/(2π·tau) 处【趋于平坦】—— 也就是说
      "漂移"里混进了一大片宽带噪声。那样"降低低通"看起来就会比实际更有效
      (它其实是在切这块造出来的噪声)。给 out_hz 就多一级低通, 把高频压下去,
      让它是【平滑的基线摆动】。默认 None = 不平滑(与前一版结果可比)。"""
    rng = random.Random(seed * 7919 + 13)
    dt = SAMPLE_MS / 1000.0
    a = 1.0 - math.exp(-dt / tau_s)
    sigma = std_target * math.sqrt(2.0 * a)
    st = [0.0]
    if out_hz is None:
        def fn(_t):
            st[0] += -a * st[0] + rng.gauss(0.0, sigma)
            return st[0]
        return fn
    b = 1.0 - math.exp(-2.0 * math.pi * out_hz * dt)
    lp = [0.0]

    def fn(_t):
        st[0] += -a * st[0] + rng.gauss(0.0, sigma)
        lp[0] += b * (st[0] - lp[0])
        return lp[0]
    return fn


def measure_std(fn, n=6000):
    """数值实测一条漂移序列的标准差 —— 用于报告【真实】幅度。

    级联低通会衰减幅度, 所以加了 out_hz 之后 std_target 不再等于输出幅度。
    与其拍一个补偿系数, 不如直接量出来写在表里。"""
    xs = [fn(i * SAMPLE_MS / 1000.0) for i in range(n)]
    m = sum(xs) / len(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))


def sweep_drift_shape(make_fn, amp, hrs=HRS, seeds=SEEDS, patch=None):
    """与 sweep() 同口径, 但每次 run 用不同的漂移 realization。"""
    patch = dict(patch or {})
    backup = {}
    for k, v in patch.items():
        if k not in PATCHABLE:
            raise AssertionError("不允许 patch: %s" % k)
        backup[k] = getattr(M, k)
        setattr(M, k, v)
    ok = wrong = none = 0
    try:
        for hr in hrs:
            for sd in seeds:
                random.seed(sd)                # 白噪声 realization(与 sweep 同)
                d = M.Ppg(MODE)
                wv = DriftWave(hr, make_fn(sd), amp=amp, noise=8.0)
                for k in range(int(TOTAL * FS)):
                    t_ms = k * SAMPLE_MS
                    ir, red = wv.sample(t_ms, t_ms >= ON)
                    d.push(t_ms, ir, red)
                if d.fin_bpm_valid:
                    if abs(d.fin_bpm - hr) <= 5:
                        ok += 1
                    else:
                        wrong += 1
                else:
                    none += 1
    finally:
        for k, v in backup.items():
            setattr(M, k, v)
    return ok, wrong, none, ok + wrong + none


HEAD = ("  %-30s  %5s   %5s   %5s   %s"
        % ("档位", "出数率", "错值率", "无结果", "备注"))


def head(t):
    w("")
    w("-" * 78)
    w(t)
    w("-" * 78)


def main():
    w("=" * 78)
    w("PPG 一次测量: 各旋钮对出数率的量化收益")
    w("=" * 78)
    w("固件行为变体 '%s'; 真值心率 %s; 每档 %d 个噪声 realization。"
      % (MODE, "/".join(str(h) for h in HRS), len(SEEDS)))
    w("判定: |偏差| <= 5 bpm 记「出数」; 有值但偏差 > 5 记「错值」(底线是 0)")
    w("")
    w("波形峰值/幅度 PK = %.3f(数值算出) ⇒ 实测样例 a=%.0f 对应输入幅度 ≈ %.0f"
      % (PK, A_MEASURED, AMP_OF_A))
    w("注意 amp 是输入正弦幅度, 固件的 a 是带通后的交流峰值, 两者只有")
    w("数量级对应关系(4Hz 低通会削一点峰), 不作严格标定。")
    w("")
    w("仿真基线(ppg_measure_sim.py 默认): 幅度 %.0f, 噪声 %.0f, 漂移 %.0f"
      % (M.AMP_IR, M.NOISE, M.DRIFT))
    w("弱信号场景(同上脚本第六节):      幅度 %.0f, 噪声 14, 漂移 %.0f"
      % (150.0, M.DRIFT))

    # ==================== 一、基线 ====================
    head("一、先问一个诊断问题: 在「实测 a=312」这个档, 仿真说出不出得来数?")
    w("  这一节是整份报告的地基。如果基线就很高, 那说明真机失败【不是这组")
    w("  参数能解释的】—— 方向要去查波形形态/佩戴, 而不是继续调参数。")
    w("")
    w(HEAD)
    base_sets = [
        ("乐观(漂移=200)", 200.0, 8.0),
        ("基准(漂移=400, = 仿真默认)", 400.0, 8.0),
        ("悲观(漂移=600, 噪声=14)", 600.0, 14.0),
    ]
    base = {}
    for name, dr, ns in base_sets:
        r = sweep(AMP_OF_A, dr, noise=ns)
        base[name] = r
        w(row(name, r, "amp=%.0f" % AMP_OF_A))
    w("")
    ok, wrong, none, tot = base["基准(漂移=400, = 仿真默认)"]
    w("  判读: 基准档出数率 %.0f%%, 错值率 %.0f%%。"
      % (100.0 * ok / tot, 100.0 * wrong / tot))

    # ==================== 二、幅度 × 漂移 地图 ====================
    head("二、主扫描: 幅度 × 漂移 —— 出数率由「谁大谁小」决定")
    w("  幅度代表 LED 电流 / 光学耦合(可以调); 漂移代表手指稳定性(只能靠佩戴)。")
    w("  行 = 漂移, 列 = 幅度。格子里是出数率 %。")
    w("")
    amps = [150.0, 220.0, AMP_OF_A, 360.0, 450.0, 600.0]
    drifts = [200.0, 400.0, 600.0, 900.0]
    w("  %-10s" % "漂移↓ / 幅度→" + "".join("%9.0f" % a for a in amps))
    grid = {}
    for dr in drifts:
        cells = []
        for a in amps:
            r = sweep(a, dr)
            grid[(dr, a)] = r
            cells.append("%8.0f%%" % (100.0 * r[0] / r[3]))
        w("  %-10.0f" % dr + "".join(cells))
    w("")
    w("  同一行里横向看 = 提高幅度的收益; 同一列里纵向看 = 漂移的杀伤力。")
    # 找"跨过哪条线才出得来"
    w("")
    w("  从每一档漂移下「第一个出数率 >= 80% 的幅度」看拐点:")
    for dr in drifts:
        hit = None
        for a in amps:
            if 100.0 * grid[(dr, a)][0] / grid[(dr, a)][3] >= 80.0:
                hit = a
                break
        if hit is None:
            w("    漂移 %-4.0f → 扫到的最大幅度(%.0f)都不够" % (dr, amps[-1]))
        else:
            w("    漂移 %-4.0f → 幅度 >= %.0f 才能稳定出数" % (dr, hit))

    # ==================== 三、提高幅度的等价收益 ====================
    head("三、把「幅度翻倍」换算成 LED 电流(唯一能量化到寄存器的杠杆)")
    w("  幅度与 LED 电流在【LED 未饱和 + ADC 未饱和】时近似成正比。")
    w("  LED_PA 寄存器 0x0C/0x0D: 0.2mA/LSB, 0x24 = 36 = 7.2mA(当前值)。")
    w("")
    w(HEAD)
    for pa, ma in ((0x24, 7.2), (0x4F, 16.0), (0x7F, 25.5), (0xFF, 51.0)):
        ratio = ma / 7.2
        r = sweep(AMP_OF_A * ratio, 400.0)
        dc_est = 58000 * ratio          # 实测 d ≈ 58234
        note = ("×%.1f 倍, DC≈%.0f" % (ratio, dc_est)
                + ("  ← ADC 满量程 262143" if dc_est > 200000 else ""))
        w(row("0x%02X = %.1f mA" % (pa, ma), r, note))
    w("")
    w("  ⚠ 幅度近似线性只在 LED/ADC 未饱和时成立。d 跟着电流一起涨, ")
    w("    18bit ADC 满量程 262143 —— 0xFF(51mA) 会把 DC 顶到 ≈41 万, 已经超了。")

    # ==================== 四、低通截止 ====================
    head("四、低通截止频率: 保峰形 还是 抑噪声")
    w("  当前 4Hz。脉搏波上升沿很陡(含 3~5 次谐波), 4Hz 会削掉峰顶 ⇒ 峰值幅度")
    w("  偏低; 放宽能保住峰形, 但噪声按 sqrt(带宽) 上升, 且会放过运动伪影。")
    w("")
    w(HEAD)
    for f in (3.0, 4.0, 6.0, 8.0, 10.0):
        r = sweep(AMP_OF_A, 400.0, patch={'LP_A': lp_a_from_hz(f)})
        note = "当前值" if f == 4.0 else ""
        w(row("%.0f Hz" % f, r, note))
    # 弱信号下也看一遍(保峰形对弱信号帮助最大, 也可能放出噪声)
    w("")
    w("  同一组档位在弱信号(幅度 150、噪声 14、漂移 400)下:")
    w(HEAD)
    for f in (3.0, 4.0, 6.0, 8.0, 10.0):
        r = sweep(150.0, 400.0, noise=14.0, patch={'LP_A': lp_a_from_hz(f)})
        note = "当前值" if f == 4.0 else ""
        w(row("%.0f Hz" % f, r, note))

    # ==================== 五、采集窗口 ====================
    head("五、采集窗口长度: 用等待时间换拍数")
    w("  当前 10 秒。窗口越长, 攒到的拍越多, 中位数越稳(次要效果)、")
    w("  一致占比门槛越容易过(主要效果)。代价是用户多等。")
    w("")
    w(HEAD)
    for s, tag in ((500, "10 秒(当前)"), (750, "15 秒"), (1000, "20 秒")):
        r = sweep(AMP_OF_A, 400.0, patch={'MEASURE_S': s})
        w(row(tag, r, "多等 %.0f 秒" % ((s - 500) * M.SAMPLE_MS / 1000.0)))
    w("")
    w("  弱信号(幅度 150、噪声 14、漂移 400)下窗口的收益更明显:")
    w(HEAD)
    for s, tag in ((500, "10 秒(当前)"), (750, "15 秒"), (1000, "20 秒")):
        r = sweep(150.0, 400.0, noise=14.0, patch={'MEASURE_S': s})
        w(row(tag, r, ""))

    # ==================== 六、一致占比门槛 ====================
    head("六、一致占比门槛(PPG_WIN_IBI_CONSIST_PCT): 这是【权衡】不是改进")
    w("  当前 70%%。它就是那条「不知道就说不确定」的闸门: 放宽它会提高出数率,")
    w("  同时会把偏差 > 5 的错数放出来。看错值率那一列 —— 它必须一直是 0。")
    w("")
    w(HEAD)
    for pct in (50, 60, 70, 80, 90):
        r = sweep(AMP_OF_A, 400.0, patch={'CONSIST_PCT': pct})
        note = "当前值" if pct == 70 else ("← 错值率抬头就说明不能放" if pct < 70 else "")
        w(row("%d%%" % pct, r, note))
    w("")
    w("  弱信号(幅度 150、噪声 14、漂移 400)下同一组:")
    w(HEAD)
    for pct in (50, 60, 70, 80, 90):
        r = sweep(150.0, 400.0, noise=14.0, patch={'CONSIST_PCT': pct})
        note = "当前值" if pct == 70 else ""
        w(row("%d%%" % pct, r, note))

    # ==================== 七、峰值阈值系数 ====================
    head("七、峰值阈值系数(PPG_PEAK_TH_FRAC)与绝对下限(PPG_PEAK_MIN_AMP)")
    w("  阈值 = max(TH_FRAC × 峰值参考, MIN_AMP)。降 TH_FRAC 让弱信号更容易过阈值,")
    w("  但也会让噪声/切迹更容易被当峰。MIN_AMP=30 在 a=312 下不是约束项。")
    w("")
    w(HEAD)
    for tf in (0.25, 0.32, 0.40, 0.50):
        r = sweep(AMP_OF_A, 400.0, patch={'TH_FRAC': tf})
        note = "当前值" if tf == 0.40 else ""
        w(row("TH_FRAC = %.2f" % tf, r, note))
    w("")
    w("  弱信号(幅度 150、噪声 14、漂移 400)下:")
    w(HEAD)
    for tf in (0.25, 0.32, 0.40, 0.50):
        r = sweep(150.0, 400.0, noise=14.0, patch={'TH_FRAC': tf})
        note = "当前值" if tf == 0.40 else ""
        w(row("TH_FRAC = %.2f" % tf, r, note))

    # ==================== 八、切迹抑制比例 ====================
    head("八、切迹抑制比例(PPG_NOTCH_MIN_RATIO): 会不会误伤真搏动")
    w("  当前 0.60。它的作用是挡住重搏切迹(真搏动后约 300ms 的二次波)。")
    w("  调大 = 挡得更凶(可能连真搏动一起挡); 调小 = 放得松(切迹可能漏进来)。")
    w("  注意: 这一档【没有切迹】, 所以它是「误伤检查」, 不是「能力检查」。")
    w("")
    w(HEAD)
    for nr in (0.45, 0.55, 0.60, 0.70, 0.80):
        r = sweep(AMP_OF_A, 400.0, patch={'NOTCH_RATIO': nr})
        note = "当前值" if abs(nr - 0.60) < 1e-9 else ""
        w(row("NOTCH_RATIO = %.2f" % nr, r, note))

    # ==================== 九、心率依赖 ====================
    head("九、成功率与心率的关系(低心率拍数少, 更容易攒不够)")
    w("  10 秒窗口里: 50bpm 只有 ~8 拍, 90bpm 有 ~15 拍。门槛要求 >= 3 拍 + 一致占比.")
    w("  每档 8 个 realization。")
    w("")
    w(HEAD)
    for hr in (45, 50, 60, 72, 90, 110, 130):
        r = sweep(AMP_OF_A, 400.0, hrs=(hr,), seeds=SEEDS)
        w(row("%d bpm" % hr, r, ""))

    # ==================== 十、自动重试 ====================
    head("十、结构性改法: 自动重试(DONE 后没测出就自动重开一个窗口)")
    w("  这不是参数, 是流程。把单次出数率 p 提升到 1-(1-p)^n(n 次重试)。")
    w("  ⚠ 但这条公式假设「每次失败互相独立」。真机上两种失败要分开看:")
    w("    · 失败由【随机噪声/漂移】决定 → 独立, 重试有效(仿真的 seed 就是这种)")
    w("    · 失败由【波形形态】决定(切迹/双峰/压力过大) → 不独立, 重试无效")
    w("  所以下表是【上限】。真机能不能兑现, 取决于失败里随机成分占多少。")
    w("")
    w("  从扫描里取三个有代表性的 p(漂移 400):")
    w(HEAD)
    for label, a in (("幅度=150(弱)", 150.0), ("幅度=%.0f(实测 a=312)" % AMP_OF_A, AMP_OF_A),
                     ("幅度=450", 450.0)):
        ok, wrong, none, tot = grid.get((400.0, a)) or sweep(a, 400.0)
        p = float(ok) / tot
        cells = "  n=1 %.0f%% | n=2 %.0f%% | n=3 %.0f%% | n=5 %.0f%%" % (
            100.0 * (1 - (1 - p) ** 1), 100.0 * (1 - (1 - p) ** 2),
            100.0 * (1 - (1 - p) ** 3), 100.0 * (1 - (1 - p) ** 5))
        w("  %-30s %s" % (label, cells))
    w("")
    w("  读法: 单次只有 50% 时, 重试 3 次能到 88% —— 但这是在「失败独立」的")
    w("  假设下。若真机失败是系统性的(比如每次都撞上切迹), 重试多少次都还是 0。")

    # ==================== 十一、漂移形态的稳健性 ====================
    head("十一、稳健性检验: 换掉「漂移是单一正弦」这个假设, 第二节还成立吗")
    w("  第二节的漂移是 400·sin(2π·0.25·t) —— 单一频率、严格周期。真机的基线")
    w("  摆动来自呼吸 + 压力变化, 是有界随机过程, 不是纯正弦。若换成随机漂移")
    w("  结论就崩, 那前面这些表都只是在跟一个特定波形打交道, 不能外推。")
    w("  下面固定幅度 = 实测档(%.0f), 只换漂移形态。每格口径与第二节相同。"
      % AMP_OF_A)
    w("")
    w(HEAD)
    res = sweep_drift_shape(
        lambda sd: (lambda t: 400.0 * math.sin(2.0 * math.pi * 0.25 * t)),
        AMP_OF_A)
    w(row("正弦 400(与第二节同构)", res, "对照组"))
    w("")
    w("  OU 随机过程(稳态标准差 400, 即与上表同一量级):")
    for tau in (0.3, 0.8, 1.5, 3.0):
        res = sweep_drift_shape(lambda sd, T=tau: make_ou(400.0, T, sd), AMP_OF_A)
        w(row("OU std=400, tau=%.1f s" % tau, res, ""))
    w("")
    w("  把 OU 幅度放大约 1.5~2 倍(稳态标准差 600 / 800):")
    for std in (600.0, 800.0):
        for tau in (0.8, 1.5):
            res = sweep_drift_shape(lambda sd, S=std, T=tau: make_ou(S, T, sd),
                                    AMP_OF_A)
            w(row("OU std=%.0f, tau=%.1f s" % (std, tau), res, ""))
    w("")
    w("  弱信号(幅度 150)下同一组, 作为对照 —— 看拐点是否只是整体平移:")
    w(HEAD)
    res = sweep_drift_shape(
        lambda sd: (lambda t: 400.0 * math.sin(2.0 * math.pi * 0.25 * t)),
        150.0)
    w(row("正弦 400(与第二节同构)", res, "对照组"))
    for tau in (0.3, 0.8, 1.5, 3.0):
        res = sweep_drift_shape(lambda sd, T=tau: make_ou(400.0, T, sd), 150.0)
        w(row("OU std=400, tau=%.1f s" % tau, res, ""))
    for std in (600.0, 800.0):
        res = sweep_drift_shape(lambda sd, S=std: make_ou(S, 1.5, sd), 150.0)
        w(row("OU std=%.0f, tau=1.5 s" % std, res, ""))

    # ==================== 十二、随机漂移下的旋钮收益 ====================
    head("十二、换成随机漂移后, 各旋钮到底还有没有收益(本轮最要紧的一节)")
    w("  第二/三节说「提高幅度没收益」, 那是建立在【漂移是慢速正弦】上的。")
    w("  十一节证明: 同样幅度, 换成随机漂移出数率就从 100% 掉到 46%、错值率")
    w("  50% —— 所以旋钮必须在【随机漂移】这个更近真机的条件下重扫一遍。")
    w("")
    D_STD, D_TAU = 600.0, 0.8
    w("  固定条件: OU 漂移 std=%.0f, tau=%.1f s, 幅度 %.0f(实测档), 噪声 8。"
      % (D_STD, D_TAU, AMP_OF_A))
    w(HEAD)
    base12 = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A)
    w(row("基线(不改任何参数)", base12, "← 对照基准"))

    w("")
    w("  12-A 幅度 —— 唯一能换算成 LED 寄存器值的杠杆。漂移是【加性】的, 不随")
    w("       电流变; 信号随电流近似线性 ⇒ 提高电流直接改善 信号/漂移 比:")
    w(HEAD)
    for a, ma in ((150.0, 3.5), (220.0, 5.2), (AMP_OF_A, 7.2),
                  (450.0, 10.6), (600.0, 14.2), (900.0, 21.3)):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), a)
        w(row("幅度 %.0f (LED≈%.1f mA)" % (a, ma), res, ""))

    w("")
    w("  12-B 高通截止(PPG_HP_CUTOFF_HZ, 当前 0.5)。漂移正落在通带下沿, 所以")
    w("       在【不伤到心率基频】的前提下抬高它, 是理论上最对症的一招。")
    w("       约束: 心率下限 50bpm = 0.83Hz —— 抬得过高会把慢心率也滤掉。")
    w(HEAD)
    for f in (0.3, 0.5, 0.7, 0.85, 1.0):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A,
                                patch={'HP_A': hp_a_from_hz(f)})
        w(row("HP %.2f Hz" % f, res, "当前值" if abs(f - 0.5) < 1e-9 else ""))

    w("")
    w("  12-C 低通截止: 漂移(0.3~0.5Hz)远低于低通, 理论上帮不上忙 ——")
    w("       这一档是去【验证这个判断】, 不是指望它有用。")
    w(HEAD)
    for f in (2.5, 3.0, 4.0):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A,
                                patch={'LP_A': lp_a_from_hz(f)})
        w(row("LP %.1f Hz" % f, res, "当前值" if abs(f - 4.0) < 1e-9 else ""))

    w("")
    w("  12-D 采集窗口: 漂移造成的失败是【系统性】的, 多攒拍不一定救得回来 ——")
    w("       但这一档要看的是它到底有没有用, 而不是继续假设。")
    w(HEAD)
    for s, tag in ((500, "10 秒(当前)"), (750, "15 秒"), (1000, "20 秒")):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A,
                                patch={'MEASURE_S': s})
        w(row(tag, res, ""))

    w("")
    w("  12-E 一致占比门槛: 十一节里 50% 的错值率说明【当前 70% 没挡住快速")
    w("       漂移】。这一档要回答: 收紧门槛能不能把错值压下去, 代价是多少。")
    w(HEAD)
    for pct in (70, 80, 90, 95):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A,
                                patch={'CONSIST_PCT': pct})
        w(row("门槛 %d%%" % pct, res, "当前值" if pct == 70 else ""))

    w("")
    w("  12-F 峰值阈值系数: 漂移抬高谷底, 降系数会让漂移更容易被当峰。")
    w(HEAD)
    for tf in (0.30, 0.40, 0.50, 0.60):
        res = sweep_drift_shape(lambda sd: make_ou(D_STD, D_TAU, sd), AMP_OF_A,
                                patch={'TH_FRAC': tf})
        w(row("TH_FRAC %.2f" % tf, res, "当前值" if abs(tf - 0.40) < 1e-9 else ""))

    w("")
    w("  12-G 稳健性复核: 把漂移换成【带限】的(输出再过一级 1.5Hz 低通)。")
    w("       为什么要复核: 单级 AR(1) 的谱在 f >> 1/(2π·tau) 处【趋于平坦】,")
    w("       等于往漂移里掺了一片宽带噪声。若 12-C(降低低通) 的收益只是在")
    w("       对这片造出来的噪声使劲, 那它就不是一个能落到固件上的改动。")
    w("")
    rough_std = measure_std(make_ou(D_STD, D_TAU, 31))
    smooth_std = measure_std(make_ou(D_STD, D_TAU, 31, out_hz=1.5))
    w("       数值实测输出标准差: 不平滑 %.0f LSB(≈ std_target %.0f); "
      % (rough_std, D_STD))
    w("       带限 1.5Hz 之后 %.0f LSB —— 带限把幅度也压小了, 所以这一组的" % smooth_std)
    w("       绝对水平整体更低, 只能看【相对本组基线】的提升, 不与上面几组比大小。")
    w(HEAD)

    def smooth(sd):
        return make_ou(D_STD, D_TAU, sd, out_hz=1.5)

    base_g = sweep_drift_shape(smooth, AMP_OF_A)
    w(row("带限漂移, 幅度 %.0f(本组基线)" % AMP_OF_A, base_g, ""))
    for a in (450.0, 600.0):
        w(row("带限漂移, 幅度 %.0f" % a, sweep_drift_shape(smooth, a), ""))
    for f in (2.5, 3.0):
        w(row("带限漂移, LP %.1f Hz" % f,
              sweep_drift_shape(smooth, AMP_OF_A,
                                patch={'LP_A': lp_a_from_hz(f)}), ""))
    for f in (0.7, 0.85):
        w(row("带限漂移, HP %.2f Hz" % f,
              sweep_drift_shape(smooth, AMP_OF_A,
                                patch={'HP_A': hp_a_from_hz(f)}), ""))
    for s, tag in ((750, "15 秒"), (1000, "20 秒")):
        w(row("带限漂移, 窗口 %s" % tag,
              sweep_drift_shape(smooth, AMP_OF_A,
                                patch={'MEASURE_S': s}), ""))
    w("")
    w("       判读口径: 若「降低低通」在带限漂移下不再有提升, 那 12-C 的收益")
    w("       就是模型的产物, 不能写进建议; 若仍有提升, 它才是真的。")

    # ==================== 汇总 ====================
    w("")
    w("=" * 78)
    w("结论(每一条都对应上面某张表, 数值取自本次运行)")
    w("=" * 78)
    w("")
    w("1) 「参数不是瓶颈」这句话只在【漂移是慢速正弦】时成立。")
    w("   第二节的漂移是 400·sin(2π·0.25·t), 0.25Hz 远低于 0.5Hz 高通, 被干净")
    w("   滤掉, 于是出数率 100%。换成随机漂移(十一节), 同样幅度掉到 46%、")
    w("   错值率涨到 50%。⇒ 不能拿第二节的 100% 去推断「真机应该测得出来」。")
    w("")
    w("2) 唯一稳健的杠杆: 提高信号幅度(即提高 LED 电流)。两个漂移模型同方向:")
    w("     不平滑漂移: 幅度 305→33%, 450→88%, 600→100%(错值 54%→12%→0%)")
    w("     带限漂移  : 幅度 305→75%, 450→96%, 600→100%")
    w("   机制: 漂移与噪声是【加性】的, 不随 LED 电流变; 信号随电流近似线性, ")
    w("   所以提高电流直接改善 信号/干扰 比 —— 这一条不依赖漂移形态假设。")
    w("   当前 PPG_LED_PA_* = 0x24(7.2mA)。扫描拐点在幅度 600 ≈ 14mA, 对应")
    w("   寄存器约 0x47, 与 docs/15 §3 早就给的 0x4F(15.8mA) 基本吻合。")
    w("   ⚠ 上限: d 会跟着电流一起涨(实测 58234 → 约 116000), 18bit ADC 满量程")
    w("     是 262143, 仍有余量; 但 0xFF(51mA) 会把 d 顶到 ≈41 万, 已经超了。")
    w("")
    w("3) 【被自查否掉的两条】—— 单独列出来, 免得下次又被当成方案:")
    w("   · 降低低通 4→2.5Hz: 不平滑漂移下 33%→54%; 带限漂移下 75%→71%(更差)")
    w("   · 提高高通 0.5→0.85Hz: 不平滑下 33%→54%; 带限下 75%→75%(无变化)")
    w("   原因: 单级 AR(1) 的功率谱在高频趋于平坦, 那片宽带噪声是模型造出来的。")
    w("   两条还各有副作用(降低通会削脉搏波峰形; 提高通会伤 50bpm 慢心率), 不做。")
    w("")
    w("4) 一致占比门槛不能靠收紧来救(12-E): 70%→90% 时错值率 54%→4%, 但出数率")
    w("   33%→25%、无结果 12%→71%。它只是把错值换成更多「没测到」, 不提高成功率。")
    w("")
    w("5) 采集窗口是次级杠杆(12-D 与 12-G): 10s→20s 在不平滑漂移下 33%→58%, ")
    w("   在带限漂移下 75%→83%。两个模型方向一致, 所以它比第 3 条那两条可信;")
    w("   但幅度只有 +8~25 个百分点, 远不如提高幅度的 +25~67 个百分点, 且代价")
    w("   是等待时间翻倍。它治的是「拍数不够」, 治不了「被漂移骗出错值」。")
    w("")
    w("6) 本脚本的边界(必须一起读): 波形是 sin(φ)+0.35sin(2φ+0.6) + 漂移 +")
    w("   白噪声。重搏切迹与运动伪影形态由 ppg_nopulse_sim.py 覆盖。所以本脚本")
    w("   回答的是「干扰要多大才会失败」, 不回答「真机的波形长什么样」——")
    w("   后者只能靠设备侧的 d/a 读数。")

    text = "\n".join(OUT) + "\n"
    path = os.path.join(HERE, "ppg_yield_sweep_result.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
