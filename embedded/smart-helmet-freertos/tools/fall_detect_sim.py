#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
摔倒检测数值验证 —— 纯 PC, 不需要硬件。

把"怎么扔都不报警"和"现在为什么能报警"在同一份脚本里跑出来。
复刻三样东西:
  1) 物理: 一次跌落过程中 |a| 的真实时间曲线(自由落体 → 撞击脉冲 → 躺稳)
  2) 器件: ±2g 削顶 → DLPF 二级低通 → ODR → 主机采样率
  3) 固件: fall_detect.c 的状态机 + mpu6050.c 的静止自标定

方法: 在 10kHz 的"真实时间轴"上生成信号, 依次施加器件的物理限制,
      再按主机采样率取点喂给状态机, 看最终有没有触发报警。

【本版新增的关键认识】
  路径B(自由落体)最初在"等撞击"那一步仍然用 HIGH_G=1.8g 当门限 —— 于是它
  继承了它本该绕开的那个采样竞态: 撞击脉冲在 1.8g 以上只停留 1~3ms, 而主机
  5ms 才采一次, 命中率只有 50%。仿真把这一条直接暴露成 FAIL。
  正确的语义是: 一旦确认了"持续失重", 剩下的问题只是"失重结束没有";
  而 0g 与静止 1.0g 之间差着整整一个 g, 用 0.9g 当"落地"门限, 跨越点落在
  极宽的平台上, 与采样相位无关 —— 命中率 100%。

用法: python tools/fall_detect_sim.py [输出文件] [--stdout]
      不给输出文件时, 默认写到 tools/fall_detect_sim_result.txt。
      为什么不直接打到屏幕: 中文 Windows 下这条链
      (python → PowerShell → 终端) 会把编码打乱成乱码, 而且本环境的
      PowerShell 工具根本不回显 stdout。让脚本自己以 UTF-8 写盘,
      再用读文件的方式取结果, 才不会拿到一堆问号。
"""

import math
import os
import sys

# ------------------------------------------------------------------ 固件参数
FS_INTERNAL   = 10000.0    # 仿真内部时间分辨率 Hz(0.1ms)
#   为什么用 10kHz 而不是 1kHz: 撞击削顶形成的"平台"只有几毫秒宽, 1kHz 采样
#   会把平台上的点采少, 人为削弱撞击 —— 那是仿真误差, 不是器件行为。

CLIP_G        = 2.0        # 芯片实际量程 ±2g, 超过即削顶
HIGH_G        = 1.8        # FALL_ACCEL_HIGH_G(受 ±2g 上限约束)
LOW_G         = 1.5        # FALL_ACCEL_LOW_G
STABLE_MIN_G  = 0.8        # FALL_STABLE_MIN_G (稳定带下沿)
WINDOW_MS     = 100        # FALL_WINDOW_MS
STABLE_MS     = 1500       # FALL_STABLE_MS
FF_G          = 0.5        # FALL_FREEFALL_G
FF_MIN_MS     = 150        # FALL_FREEFALL_MIN_MS
FF_TIMEOUT_MS = 1500       # FALL_FREEFALL_TIMEOUT_MS
FF_LAND_G     = 0.9        # FALL_FREEFALL_LAND_G ("落地" = 失重结束)

IDLE, FF, IMPACT, CONFIRM, LOCKOUT = range(5)


# ------------------------------------------------------------------ 状态机
class Detector:
    """fall_detect.c 的逐行移植。

    use_freefall=False   复现"只有需求原判据"的行为(路径A)。
    ff_land_use_high=True 复现"路径B 但落地门限沿用 HIGH_G"的旧版本,
                          用来量化说明 50% 那个 FAIL 是怎么来的。
    """

    def __init__(self, use_freefall=True, ff_land_g=FF_LAND_G,
                 stable_min_g=STABLE_MIN_G, ff_land_use_high=False):
        self.use_ff = use_freefall
        self.ff_land = HIGH_G if ff_land_use_high else ff_land_g
        self.stable_min = stable_min_g
        self.st = IDLE
        self.t_imp = self.t_sta = self.t_cfm = self.t_ff = 0
        self.low = 0
        self.peak = 0.0
        self.triggered_at = None

    def update(self, mag, now):
        if mag >= self.peak:
            self.peak = mag
        trigger = False

        if self.st == IDLE:
            if self.use_ff:
                if mag < FF_G:
                    if not self.low:
                        self.low = 1
                        self.t_ff = now
                    elif now - self.t_ff >= FF_MIN_MS:
                        self.st = FF
                        return False
                else:
                    self.low = 0
            if mag > HIGH_G:
                self.st = IMPACT
                self.t_imp = now

        elif self.st == FF:
            # 落地 = 失重结束。门限只需要"明显高于 0g 平台", 不需要达到撞击量级。
            if mag > self.ff_land:
                self.st = CONFIRM
                self.t_sta = self.t_cfm = now
                self.low = 0
            elif now - self.t_ff > FF_TIMEOUT_MS:
                self.st = IDLE
                self.low = 0

        elif self.st == IMPACT:
            if now - self.t_imp <= WINDOW_MS:
                if mag > HIGH_G:
                    self.st = CONFIRM
                    self.t_sta = self.t_cfm = now
            else:
                self.st = IDLE
                if mag > HIGH_G:
                    self.st = IMPACT
                    self.t_imp = now

        elif self.st == CONFIRM:
            # 稳定带 [stable_min, LOW_G): 只认"被支撑着、躺着不动"的 1.0g,
            # 不认失重中的 0g —— 否则"擦到一下继续掉"会被当成躺稳。
            if self.stable_min <= mag < LOW_G:
                if now - self.t_sta >= STABLE_MS:
                    trigger = True
                    self.triggered_at = now
                    self.st = LOCKOUT
            else:
                # 【关键】回弹/中间值都只是"还没稳", 重置稳定窗口即可。
                # 早期版本在这里退回 IMPACT 重新找"第二次撞击", 结果回弹结束后
                # 窗口超时 → IDLE → 前面那次跌倒被遗忘, 稳定漏报。
                self.t_sta = now
            if self.st == CONFIRM and now - self.t_cfm > STABLE_MS * 3:
                self.st = IDLE

        elif self.st == LOCKOUT:
            pass
        return trigger


# ------------------------------------------------------------------ 器件模型
def biquad_lp(fc, fs):
    """二阶 Butterworth 低通系数(已归一化), 对应 MPU6050 的 DLPF。"""
    w0 = 2.0 * math.pi * fc / fs
    alpha = math.sin(w0) / (2.0 * (1.0 / math.sqrt(2.0)))
    b0 = (1.0 - math.cos(w0)) / 2.0
    b1 = 1.0 - math.cos(w0)
    a0 = 1.0 + alpha
    a1 = -2.0 * math.cos(w0)
    a2 = 1.0 - alpha
    return (b0 / a0, b1 / a0, b0 / a0, a1 / a0, a2 / a0)


def apply_lp(sig, fc, fs):
    b0, b1, b2, a1, a2 = biquad_lp(fc, fs)
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for x in sig:
        y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, x
        y2, y1 = y1, y
        out.append(y)
    return out


def impact_pulse(mag, t, t_land, width_ms, peak_g):
    """撞击脉冲: 半正弦, 峰值 peak_g(真实峰值, 通常远超量程), 宽度 width_ms。

    t / t_land / width_ms 单位统一为【毫秒】—— 混用秒与毫秒会让脉冲宽度差
    1000 倍, 结果是"仿真里根本没有撞击", 看起来像阈值写错了。
    """
    dt = (t - t_land) / width_ms
    if 0.0 <= dt <= 1.0:
        return peak_g * math.sin(math.pi * dt)
    return mag


# ------------------------------------------------------------------ 场景生成
def build_drop(drop_h, impact_width_ms, impact_peak_g, bounce,
               hold_ms=200.0, phase_ms=0.0):
    """生成"真实"|a|(g) 曲线, 采样率 FS_INTERNAL。

    phase_ms: 撞击时刻相对采样栅格的偏移。真实跌落不可能正好落在采样点上,
              扫这个量可以得到"检测命中率"(而不是"某一次恰好成/不成")。
    """
    g = 9.81
    t_fall_ms = math.sqrt(2.0 * drop_h / g) * 1000.0
    t_land = hold_ms + t_fall_ms + phase_ms
    total = t_land + 2600.0
    n = int(total * FS_INTERNAL / 1000.0)
    sig = []
    for i in range(n):
        t = i * 1000.0 / FS_INTERNAL
        if t < hold_ms:
            m = 1.0
        elif t < t_land:
            m = 0.02                                    # 自由落体: |a| ≈ 0
        else:
            m = 1.0                                     # 躺稳
            m = impact_pulse(m, t, t_land, impact_width_ms, impact_peak_g)
            if bounce:
                m = impact_pulse(m, t, t_land + 60.0, impact_width_ms, impact_peak_g)
        sig.append(m)
    return sig, t_land


def build_bump(width_ms=3.0, peak_g=30.0, t0=500.0):
    """走路撞门框: 单次撞击, 全程无失重。"""
    n = int(2000.0 * FS_INTERNAL / 1000.0)
    sig = []
    for i in range(n):
        t = i * 1000.0 / FS_INTERNAL
        sig.append(impact_pulse(1.0, t, t0, width_ms, peak_g))
    return sig, t0


def build_handling():
    """拿起→搬运→放下: 有 1.1~1.3g 的抬手/放手加速度, 但【全程不失重】。"""
    n = int(3000.0 * FS_INTERNAL / 1000.0)
    sig = [1.0] * n
    for t0, w, pk in ((400.0, 80.0, 1.30), (1200.0, 60.0, 1.15),
                      (2200.0, 70.0, 1.25)):
        for i in range(n):
            t = i * 1000.0 / FS_INTERNAL
            sig[i] = impact_pulse(sig[i], t, t0, w, pk)
    return sig, 0.0


def measure(sig, fc, odr_hz, host_hz, scale_err=1.0):
    """施加器件限制, 返回 [(时刻ms, 主机读到的|a|)]。

    scale_err: 换算系数错误引入的倍数(4.0 = 复现用户报的"静止 3.9g")。
    """
    clipped = [min(v, CLIP_G) for v in sig]            # 先削顶(ADC 饱和)
    filtered = apply_lp(clipped, fc, FS_INTERNAL)      # 再经 DLPF
    reg_period = FS_INTERNAL / odr_hz                  # 器件更新周期(内部点数)
    host_period = FS_INTERNAL / host_hz

    out = []
    k = 0
    reg_val = 0.0
    t_next = 0.0
    while True:
        idx = int(t_next)
        if idx >= len(filtered):
            break
        while k * reg_period <= idx and int(k * reg_period) < len(filtered):
            reg_val = filtered[int(k * reg_period)]
            k += 1
        out.append((t_next * 1000.0 / FS_INTERNAL, reg_val * scale_err))
        t_next += host_period
    return out


def run(samples, detector):
    for t_ms, mag in samples:
        if detector.update(mag, int(round(t_ms))):
            return True
    return False


def peak_of(samples):
    return max(m for _, m in samples)


# ------------------------------------------------------------------ 断言框架
PASS, FAIL = [], []


def check(name, got, want):
    ok = (got == want)
    (PASS if ok else FAIL).append(name)
    print("  [%s] %-42s → %s" % ("PASS" if ok else "FAIL", name,
                                 "报警" if got else "不报警"))


def main():
    print("=" * 76)
    print("摔倒检测数值验证   HIGH=%.1fg  LOW=%.1fg  稳定带下沿=%.1fg  落地门限=%.1fg"
          % (HIGH_G, LOW_G, STABLE_MIN_G, FF_LAND_G))
    print("=" * 76)

    # ================= 1) 采样率: 首要病根 =================
    # 只看"需求原判据"(路径A), 把采样率的影响单独隔离出来。
    # (如果直接拿完整检测器跑, 路径B 会把 10Hz 也救回来, 就看不到这个病根了
    #  —— 这本身也是一条结论, 见下面第三行断言。)
    print("\n[1] 采样率 —— 隔离测试: 只看需求原判据(路径A), 硬地 1m, DLPF=188Hz")
    sig, _ = build_drop(1.0, 3.0, 40.0, bounce=True)
    old = measure(sig, 188.0, 100.0, 10.0)       # 旧: Sensor_Task 100ms 周期
    new = measure(sig, 188.0, 1000.0, 200.0)     # 新: ImpactTask 5ms
    print("      旧(10Hz):  峰值 %.2fg, 共 %d 个样本" % (peak_of(old), len(old)))
    print("      新(200Hz): 峰值 %.2fg, 共 %d 个样本" % (peak_of(new), len(new)))
    check("路径A + 旧 10Hz 采样 → 漏报", run(old, Detector(use_freefall=False)), False)
    check("路径A + 新 200Hz 采样 → 报警", run(new, Detector(use_freefall=False)), True)
    check("补上路径B 后 10Hz 也能报(路径B 是主力)", run(old, Detector()), True)

    # ================= 2) DLPF 带宽 =================
    print("\n[2] DLPF 带宽 —— 同样隔离到路径A, 采样率统一 200Hz, 削顶平台 3ms")
    s44 = measure(sig, 44.0, 1000.0, 200.0)      # 旧: DLPF=0x03
    s188 = measure(sig, 188.0, 1000.0, 200.0)    # 新: DLPF=0x01
    print("      DLPF 44Hz (旧): 峰值 %.2fg" % peak_of(s44))
    print("      DLPF 188Hz(新): 峰值 %.2fg" % peak_of(s188))
    check("路径A + DLPF 44Hz 滤掉尖峰 → 漏报", run(s44, Detector(use_freefall=False)), False)
    check("路径A + DLPF 188Hz 保住尖峰 → 报警", run(s188, Detector(use_freefall=False)), True)
    check("补上路径B 后 DLPF 44Hz 也能报", run(s44, Detector()), True)

    # ================= 3) 自由落体路径 =================
    print("\n[3] 自由落体路径 —— 不回弹的单次撞击(软垫/地毯), 扫 40 个相位")
    print("      (旧判据要求\"100ms 内两次超阈\", 只横跨 1 个采样点时凑不齐)")
    phases = [i * 0.125 for i in range(40)]      # 0~4.875ms, 覆盖 5ms 采样周期
    n = len(phases)

    def hit_rate(make_det):
        h = 0
        for ph in phases:
            s, _ = build_drop(1.0, 4.0, 40.0, bounce=False, phase_ms=ph)
            sm = measure(s, 188.0, 1000.0, 200.0)
            if run(sm, make_det()):
                h += 1
        return h

    h_a = hit_rate(lambda: Detector(use_freefall=False))
    h_oldb = hit_rate(lambda: Detector(ff_land_use_high=True))   # 上一版: 落地仍用 1.8g
    h_b = hit_rate(lambda: Detector())
    print("      路径A(需求原判据)      : %2d/%d (%.0f%%)" % (h_a, n, 100.0 * h_a / n))
    print("      路径B 落地门限用 1.8g  : %2d/%d (%.0f%%)  ← 仍是采样竞态, 等于没修"
          % (h_oldb, n, 100.0 * h_oldb / n))
    print("      路径B 落地门限用 %.1fg   : %2d/%d (%.0f%%)"
          % (FF_LAND_G, h_b, n, 100.0 * h_b / n))
    check("原判据命中率 <100%(依赖相位运气)", h_a < n, True)
    check("落地门限沿用 HIGH_G 仍会漏报", h_oldb < n, True)
    check("自由落体路径命中率 =100%", h_b == n, True)

    print("\n      落地门限取值的敏感度(同一组 40 个相位):")
    for g in (0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8):
        h = hit_rate(lambda g=g: Detector(ff_land_g=g))
        mark = "  ← 采用" if abs(g - FF_LAND_G) < 1e-9 else ""
        print("        %.1fg : %2d/%d (%.0f%%)%s" % (g, h, n, 100.0 * h / n, mark))
    print("      0.6~1.0 都拿满分; 落地门限一旦往撞击量级(1.4g+)靠, 命中率立刻掉。")
    print("      取 0.9 而不是 0.6, 是为了给自由落体平台的纹波/噪声留一倍余量。")

    # ================= 4) 误报拦截 =================
    print("\n[4] 误报拦截 —— 不该报警的日常动作")
    pu, _ = build_drop(0.05, 3.0, 40.0, bounce=False)     # 拿起 5cm 再松手
    spu = measure(pu, 188.0, 1000.0, 200.0)
    print("      拿起放下 5cm: 失重 100ms < 门限 %dms" % FF_MIN_MS)
    check("拿起放下 5cm → 不报警", run(spu, Detector()), False)
    bp, _ = build_bump()
    sbp = measure(bp, 188.0, 1000.0, 200.0)
    check("走路磕碰(单次撞击, 无失重) → 不报警", run(sbp, Detector()), False)
    hd, _ = build_handling()
    shd = measure(hd, 188.0, 1000.0, 200.0)
    check("拿起搬运放下(抬手 1.3g, 不失重) → 不报警", run(shd, Detector()), False)

    # ================= 5) 标定 =================
    print("\n[5] 换算系数错误(复现用户报的\"无论怎么放都是 3.9\") —— 硬地 1m")
    bad = measure(sig, 188.0, 1000.0, 200.0, scale_err=4.0)
    st = [m for t, m in bad if t > 900.0]
    print("      静止读数 = %.2fg (真实应为 1.00g)" % (sum(st) / len(st)))
    check("换算错(×4)且未修正 → 漏报", run(bad, Detector()), False)

    raw_lsb, reg_lsb = 16384.0, 4096.0     # 真实(±2g) vs 驱动误判(±8g)
    win = [raw_lsb * (1.0 + 0.004 * math.sin(i)) for i in range(240)]
    mean = sum(win) / len(win)
    spread = (max(win) - min(win)) / mean * 100.0
    got_lsb = mean if spread <= 3.0 else reg_lsb
    print("      在线标定: 原始模值均值 %.0f LSB/g, 极差 %.1f%% (门限 3.0%%)"
          % (mean, spread))
    print("      修正后系数 %.0f LSB/g (原 %.0f, 与真值偏差 %.2f%%)"
          % (got_lsb, reg_lsb, abs(got_lsb - raw_lsb) / raw_lsb * 100.0))
    fixed = [(t, m * reg_lsb / got_lsb) for t, m in bad]
    st2 = [m for t, m in fixed if t > 900.0]
    print("      修正后静止读数 = %.2fg" % (sum(st2) / len(st2)))
    check("在线标定修正后 → 报警", run(fixed, Detector()), True)

    # ================= 6) 已知取舍 =================
    print("\n[6] 已知取舍(如实记录, 不作断言) —— 高处抛接")
    tc, _ = build_drop(0.30, 4.0, 20.0, bounce=False)
    stc = measure(tc, 188.0, 1000.0, 200.0)
    print("      把头盔向上抛 30cm 再用手接住: %s"
          % ("会报警" if run(stc, Detector()) else "不报警"))
    print("      说明: 失重 247ms 已过 150ms 门限, 接住后手不动约 1.0g 也满足稳定带。")
    print("      这一条在上一个版本(落地门限 1.8g)同样会报警 —— 不是本版引入的。")
    print("      真机若需排除, 最省事的做法是把 FF_MIN_MS 提到 250ms(≈31cm 起)。")

    # ================= 结论 =================
    print("\n" + "=" * 76)
    print("通过 %d 项, 失败 %d 项" % (len(PASS), len(FAIL)))
    if FAIL:
        for f in FAIL:
            print("   失败: " + f)
        return 1
    print("结论:")
    print("  · 三个病根可复现且修法有效: 采样率 10Hz→200Hz / DLPF 44→188Hz /")
    print("    标定时机(上电一次 → 运行期在线持续)")
    print("  · 路径B 必须把\"落地\"定义为\"失重结束\", 而不是\"又有一次 1.8g 撞击\" ——")
    print("    否则它继承了它本该绕开的采样竞态, 命中率只有一半")
    print("  · 稳定带加上 0.8g 下沿: 只认\"被支撑着躺着\"的 1.0g, 不认失重中的 0g")
    print("  · 失重时长门限(150ms)挡住了\"拿起放下\"\"走路磕碰\"\"拿起搬运\"三类误报")
    print("=" * 76)
    return 0


if __name__ == "__main__":
    argv = [a for a in sys.argv[1:] if a != "--stdout"]
    if "--stdout" in sys.argv:
        raise SystemExit(main())
    out = argv[0] if argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fall_detect_sim_result.txt")
    sys.stdout = open(out, "w", encoding="utf-8", newline="\n")
    rc = main()
    sys.stdout.close()
    raise SystemExit(rc)
