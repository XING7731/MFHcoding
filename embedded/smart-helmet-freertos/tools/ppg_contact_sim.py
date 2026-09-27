# -*- coding: utf-8 -*-
"""
PPG 接触瞬态仿真 —— "贴手后 3 秒再开始监测"到底挡住了什么

【⚠ 版本对应关系 —— 先看这段再读结论】
  本脚本是 2026-09-17 上午写的, 对应的是【那一天上午版固件】: 三态机
  (IDLE/SETTLING/MEASURING) + 持续发布。当天下午流程改成了"一次测量"
  (贴手 → 稳定 3s → 采集 10s → 出结果并冻结), 四态机, 发布只在 DONE。
  所以:
    · 本脚本里 'new' 这个变体【已经不代表当前固件】, 它对应的是历史版本;
    · 当前固件的行为由 tools/ppg_measure_sim.py 验证(30 项断言);
    · 本脚本仍然有效的部分: 第一节(阶跃尖峰 36422 的存在与量级)与
      "接触锚定挡不住第一毫秒的阶跃"这个事实 —— 但注意, 下午的仿真又发现
      那一次假峰的危害会被 begin_settle() 的锚定清掉, 所以阶跃吸附真正管
      的是【测量中途】的阶跃, 见 ppg_measure_sim.py 第八节。
  保留本脚本是为了留下"怎么一步步查到这里"的完整链条, 不是为了当当前基线。

【为什么单独写一个脚本, 而不是并进 ppg_hr_sim.py】
  ppg_hr_sim.py 覆盖的是"手指一直在传感器上"时的算法缺陷(阈值被顶死、
  下降沿重复判峰、无幅度下限、丢样本不进时间轴)。它默认了一个前提: 波形
  从头到尾都有脉搏。
  但用户这一轮的反馈是"心率和血氧【还是】会误测" —— 说明剩下的问题不在那
  四条里。真正的嫌疑是【上电时没有手指, 过一会儿才放上去】这个瞬态, 而那
  个场景上一版根本没测过。本脚本就是补这个洞。

【要验证的机制(跑完发现它比"加个延时"严重得多)】
  上电时没有手指, 高通滤波器里的直流估计会一路衰减到环境光量级(几百)。
  手指一放上去, 输入从几百跳到几万 —— 这是一次【阶跃】。高通输出的第一拍
  就是"几万减去几百", 幅度是真实脉搏波的几十倍, 必然越过阈值被当成一次
  搏动, 并把自适应阈值里的"峰值参考"顶到几十倍高度。
  而峰值参考只能慢慢跟(每拍 6%)、外加"连续 6 秒没峰就减半"的逃生通道 ——
  换句话说, 贴手之后的很长一段时间里算法是【瞎的】: 要么长时间不出数,
  要么出的数是那次阶跃造出来的假拍算出来的。这就是"读数随机"的现场来源。

【四个变体跑同一段波形, 把两个修法的贡献分开】
  old    = 09-16 版(无接触判定, 直流只在首个样本锚定)   <- 现场烧的就是它
  anchor = 只加"接触确认后清算法 + 锚定直流"(去抖 300ms 后才生效)
  snap   = 再加"直流阶跃吸附"(不等确认, 当场生效)      <- 本轮补的关键一条
  new    = 再加"3 秒稳定期门控"(用户要求的那条)

输出: ppg_contact_sim_result.txt (本机 PowerShell 不回显 stdout, 故写文件)
运行: python tools/ppg_contact_sim.py
"""

import math
import random

# ------------------------------------------------------------------ 固件参数
# 全部取自 user/Inc/user_config.h; 改那边就要改这边, 否则仿真不代表固件
FS           = 50.0
SAMPLE_MS    = 1000.0 / FS
WINDOW       = 200            # PPG_HR_WINDOW
HP_A         = 1.0 - math.exp(-2.0 * math.pi * 0.5 / FS)   # PPG_HP_CUTOFF_HZ
LP_A         = 1.0 - math.exp(-2.0 * math.pi * 4.0 / FS)   # PPG_LP_CUTOFF_HZ
TH_FRAC      = 0.40           # PPG_PEAK_TH_FRAC
MIN_AMP      = 30.0           # PPG_PEAK_MIN_AMP
TRACK        = 0.06           # PPG_PEAK_TRACK
MIN_IBI      = 300.0          # PPG_HR_MIN_IBI_MS
MAX_IBI      = 2000.0         # PPG_HR_MAX_IBI_MS
REACQ        = 300            # PPG_PEAK_REACQ_MS = 3 * MAX_IBI
SETTLE0      = 25             # PPG_SETTLE_SAMPLES
TOL_PCT      = 25             # PPG_IBI_TOL_PCT
RESET_STREAK = 4              # PPG_IBI_RESET_STREAK
IBI_N        = 5              # PPG_IBI_MAX
STALE        = 250            # PPG_HR_STALE_MS = 5000ms
DC_MIN       = 1000.0         # PPG_DC_MIN_RAW
MIN_MOD      = 0.002          # PPG_MIN_MODULATION
CONTACT_TH   = 2000.0         # PPG_CONTACT_RAW_TH
ON_S         = 15             # PPG_CONTACT_ON_MS  = 300ms
OFF_S        = 40             # PPG_CONTACT_OFF_MS = 800ms
SETTLE_S     = 150            # PPG_SETTLE_MS      = 3000ms
STEP_FRAC    = 0.25           # PPG_DC_STEP_FRAC

SPO2_A, SPO2_B, SPO2_C = -45.06, 30.354, 94.845   # 未标定经验式

# ------------------------------------------------------------------ 波形参数
AMB_IR, AMB_RED = 300.0, 320.0          # 环境光量级(没手指时)
DC_IR,  DC_RED  = 50000.0, 42000.0      # 手指盖住时的直流
AMP_IR          = 600.0                 # 脉搏波交流峰值(红外)
AMP_RED         = 360.0                 # 取 360 ⇒ R ≈ 0.71 ⇒ SpO2 ≈ 93%
DRIFT           = 400.0                 # 0.25Hz 基线漂移(呼吸/压力)
NOISE           = 8.0                   # 白噪声标准差

MODES = {
    'old':    dict(contact=False, snap=False, gate=False),
    'anchor': dict(contact=True,  snap=False, gate=False),
    'snap':   dict(contact=True,  snap=True,  gate=False),
    'new':    dict(contact=True,  snap=True,  gate=True),
}
MODE_DESC = {
    'old':    "09-16 版: 无接触判定",
    'anchor': "接触确认后清算法+锚定直流(去抖 300ms 后生效)",
    'snap':   "再加直流阶跃吸附(不等确认, 当场生效)",
    'new':    "再加 3 秒稳定期门控(本次交付)",
}


def pulse_shape(phase):
    """一个像脉搏波的形状: 主波 + 二次谐波(纯正弦太理想化)"""
    return math.sin(phase) + 0.35 * math.sin(2.0 * phase + 0.6)


def gen(t_ms, hr_bpm, dc_on=True, dc_level=None, amp=None, noise=None):
    """生成 t_ms 时刻的 (ir, red)。
    dc_on=False 时只有环境光(没手指)。dc_level 可指定手指段的直流水平,
    用来模拟"手指数值不变但贴合/压力变了"造成的阶跃。
    amp / noise 可覆盖脉搏波幅度与噪声, 用来模拟末梢循环差/贴不紧的弱信号。"""
    t = t_ms / 1000.0
    a = AMP_IR if amp is None else amp
    nz = NOISE if noise is None else noise

    if not dc_on:
        return (AMB_IR + random.gauss(0, nz),
                AMB_RED + random.gauss(0, nz))

    dc_i = DC_IR if dc_level is None else dc_level
    dc_r = DC_RED * (dc_i / DC_IR)
    phase = 2.0 * math.pi * (hr_bpm / 60.0) * t
    p = pulse_shape(phase)
    dr = DRIFT * math.sin(2.0 * math.pi * 0.25 * t)
    return (dc_i + a * p + dr + random.gauss(0, nz),
            dc_r + a * (AMP_RED / AMP_IR) * p + 0.8 * dr + random.gauss(0, nz))


def median(v):
    s = sorted(v)
    n = len(s)
    return 0 if n == 0 else s[n // 2]


# ------------------------------------------------------------------ 算法
class Ppg:
    def __init__(self, mode):
        cfg = MODES[mode]
        self.mode = mode
        self.has_contact = cfg['contact']
        self.has_snap = cfg['snap']
        self.has_gate = cfg['gate']

        self.dc_ir = 0.0
        self.dc_red = 0.0
        self.lp_ir = 0.0
        self.lp_red = 0.0
        self.inited = False

        self.buf_ir = [0.0] * WINDOW
        self.buf_red = [0.0] * WINDOW
        self.head = 0
        self.filled = 0

        self.v1 = 0.0
        self.v2 = 0.0
        self.amp_ref = 0.0
        self.last_peak = 0
        self.since_peak = 0
        self.settle = SETTLE0

        self.ibi = []
        self.reject = 0

        self.bpm = 0
        self.bpm_valid = False
        self.idx = 0

        self.ratio = 0.0
        self.spo2 = 0
        self.spo2_valid = False
        self.signal_ok = False

        self.crun = 0
        self.nrun = 0
        self.state = 'MEASURING' if not self.has_contact else 'IDLE'
        self.settle_left = 0

        # 记录
        self.peaks = []          # (t_ms, amp) 通过阈值与不应期的峰
        self.published = []      # (t_ms, bpm, spo2, spo2_valid)
        self.amp_max = 0.0
        self.gate_suppressed = 0

    # ---- 滤波器 ----
    def _band_step(self, x, ch):
        """阶跃吸附: 跳变超过 STEP_FRAC 就把直流估计吸附过去"""
        if not self.has_snap:
            return
        if ch == 0:
            if self.dc_ir > 1.0 and abs(x - self.dc_ir) > STEP_FRAC * self.dc_ir:
                self.dc_ir = x
        else:
            if self.dc_red > 1.0 and abs(x - self.dc_red) > STEP_FRAC * self.dc_red:
                self.dc_red = x

    def _band(self, x, ch):
        self._band_step(x, ch)
        if ch == 0:
            self.dc_ir += HP_A * (x - self.dc_ir)
            hp = x - self.dc_ir
            self.lp_ir += LP_A * (hp - self.lp_ir)
            return self.lp_ir
        self.dc_red += HP_A * (x - self.dc_red)
        hp = x - self.dc_red
        self.lp_red += LP_A * (hp - self.lp_red)
        return self.lp_red

    # ---- 状态机 ----
    def _clear_algo(self):
        self.head = 0
        self.filled = 0
        self.v1 = self.v2 = 0.0
        self.amp_ref = 0.0
        self.last_peak = 0
        self.since_peak = 0
        self.settle = SETTLE0
        self.ibi = []
        self.reject = 0
        self.bpm = 0
        self.bpm_valid = False
        self.ratio = 0.0
        self.spo2 = 0
        self.spo2_valid = False
        self.signal_ok = False

    def _begin_settle(self, ir, red):
        self._clear_algo()
        self.dc_ir = float(ir)
        self.dc_red = float(red)
        self.lp_ir = 0.0
        self.lp_red = 0.0
        self.inited = True
        self.settle_left = SETTLE_S

    # ---- 寻峰 ----
    def _on_peak(self, t_ms, idx, amp):
        if self.last_peak != 0:
            if (idx - self.last_peak) * SAMPLE_MS < MIN_IBI:
                return
        self.since_peak = 0
        self.peaks.append((t_ms, amp))

        if self.amp_ref <= 0.0:
            self.amp_ref = amp
        else:
            self.amp_ref += TRACK * (amp - self.amp_ref)

        if self.last_peak == 0:
            self.last_peak = idx
            return

        dt = (idx - self.last_peak) * SAMPLE_MS
        self.last_peak = idx
        if dt < MIN_IBI or dt > MAX_IBI:
            return

        if len(self.ibi) >= 3:
            m = median(self.ibi)
            if m > 0 and abs(dt - m) * 100 > m * TOL_PCT:
                self.reject += 1
                if self.reject < RESET_STREAK:
                    return
                self.ibi = []
            self.reject = 0

        self.ibi.append(dt)
        if len(self.ibi) > IBI_N:
            self.ibi.pop(0)

        if len(self.ibi) >= 3:
            m = median(self.ibi)
            if m > 0:
                self.bpm = int((60000.0 + m / 2.0) / m)
                self.bpm_valid = True

        self._update_ratio()

    def _update_ratio(self):
        self.signal_ok = False
        if self.filled < WINDOW:
            return
        if self.dc_ir < DC_MIN or self.dc_red < DC_MIN:
            self.ratio = 0.0
            self.spo2_valid = False
            return
        ac_i = math.sqrt(sum(v * v for v in self.buf_ir) / WINDOW)
        ac_r = math.sqrt(sum(v * v for v in self.buf_red) / WINDOW)
        if ac_i < 1.0 or (ac_i / self.dc_ir) < MIN_MOD:
            self.ratio = 0.0
            self.spo2_valid = False
            return
        self.signal_ok = True
        r = (ac_r / self.dc_red) / (ac_i / self.dc_ir)
        self.ratio = r if self.ratio == 0.0 else 0.75 * self.ratio + 0.25 * r
        s = SPO2_A * self.ratio * self.ratio + SPO2_B * self.ratio + SPO2_C
        s = max(0.0, min(100.0, s))
        self.spo2 = int(s + 0.5)
        self.spo2_valid = True

    # ---- 主入口 ----
    def push(self, t_ms, ir, red):
        touched = (ir > CONTACT_TH)

        if not self.has_contact:
            self.state = 'MEASURING'
        else:
            if touched:
                self.crun += 1
                self.nrun = 0
            else:
                self.nrun += 1
                self.crun = 0

            if self.state == 'IDLE':
                if self.crun >= ON_S:
                    self._begin_settle(ir, red)
                    self.state = 'SETTLING'
            elif self.nrun >= OFF_S:
                self._clear_algo()
                self.state = 'IDLE'
                self.settle_left = 0
                self.inited = False
            elif self.state == 'SETTLING':
                if self.settle_left > 0:
                    self.settle_left -= 1
                if self.settle_left == 0:
                    self.state = 'MEASURING'

        if not self.inited:
            self.dc_ir = float(ir)
            self.dc_red = float(red)
            self.inited = True

        b_ir = self._band(float(ir), 0)
        b_red = self._band(float(red), 1)

        self.buf_ir[self.head] = b_ir
        self.buf_red[self.head] = b_red
        self.head = (self.head + 1) % WINDOW
        if self.filled < WINDOW:
            self.filled += 1
        self.idx += 1

        if self.settle > 0:
            self.settle -= 1
        elif self.v1 > self.v2 and self.v1 >= b_ir and self.v1 > 0.0:
            need = TH_FRAC * self.amp_ref
            if need < MIN_AMP:
                need = MIN_AMP
            if self.v1 >= need:
                self._on_peak(t_ms, self.idx - 1, self.v1)

        self.since_peak += 1
        if self.since_peak > REACQ:
            self.amp_ref *= 0.5
            self.since_peak = 0

        if self.bpm_valid and (self.idx - self.last_peak) > STALE:
            self._clear_algo()

        self.v2 = self.v1
        self.v1 = b_ir

        if self.amp_ref > self.amp_max:
            self.amp_max = self.amp_ref

        # ---- 发布(门控) ----
        pub = True
        if self.has_gate:
            pub = (self.state == 'MEASURING')

        if self.bpm_valid and not pub:
            self.gate_suppressed += 1

        self.published.append((t_ms,
                               self.bpm if (pub and self.bpm_valid) else 0,
                               self.spo2 if pub else 0,
                               (pub and self.spo2_valid)))


# ------------------------------------------------------------------ 场景工具
def first_pub_after(d, t0):
    for (t, bpm, sp, sv) in d.published:
        if t >= t0 and bpm > 0:
            return t, bpm
    return None, None


def pubs_between(d, t0, t1):
    return [(t, bpm) for (t, bpm, sp, sv) in d.published if t0 <= t < t1 and bpm > 0]


def spikes_between(d, t0, t1):
    return [a for (t, a) in d.peaks if t0 <= t < t1]


# ------------------------------------------------------------------ 断言框架
RESULTS = []
FAILED = []


def check(name, ok, detail=""):
    RESULTS.append("  [%s] %s%s"
                   % ("PASS" if ok else "FAIL", name,
                      ("  -> " + detail) if detail else ""))
    if not ok:
        FAILED.append(name)


# ------------------------------------------------------------------ 场景 1
def scen_place_after_poweron(true_bpm=72, total_s=60.0, finger_ms=1000.0,
                            mode='new', seed=7):
    """上电时没手指, finger_ms 后手指放上"""
    random.seed(seed)
    d = Ppg(mode)
    for k in range(int(total_s * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = gen(t_ms, true_bpm, dc_on=(t_ms >= finger_ms))
        d.push(t_ms, ir, red)
    return d


def main():
    out = []
    w = out.append
    TRUE_BPM = 72
    FINGER_MS = 1000.0
    TOTAL_S = 60.0

    w("=" * 78)
    w("PPG 接触瞬态仿真 —— 贴手后 3 秒稳定期挡住了什么")
    w("=" * 78)
    w("场景一: 上电时没有手指(只有环境光), %.1f 秒后手指放上, 真值 %d bpm,"
      % (FINGER_MS / 1000.0, TRUE_BPM))
    w("        共观察 %.0f 秒" % TOTAL_S)
    w("波形  : 环境光 %.0f -> 手指直流 %.0f, 脉搏波峰值 %.0f, 漂移 %.0f, 噪声 %.0f"
      % (AMB_IR, DC_IR, AMP_IR, DRIFT, NOISE))
    w("手指盖住时 IR = %.0f, 拿开时 = %.0f, 相差 %.0f 倍 ——"
      % (DC_IR, AMB_IR, DC_IR / AMB_IR))
    w("        这就是「阶跃」的来头: 高通必须把这一跳当成交流才会有问题。")
    w("")
    for m in ('old', 'anchor', 'snap', 'new'):
        w("  %-7s = %s" % (m, MODE_DESC[m]))
    w("")

    runs = {}
    for m in ('old', 'anchor', 'snap', 'new'):
        runs[m] = scen_place_after_poweron(TRUE_BPM, TOTAL_S, FINGER_MS, m)

    # ---------- 一、阶跃本身 ----------
    w("-" * 78)
    w("一、贴手瞬间发生了什么(「读数随机」的物理来源)")
    w("-" * 78)
    w("  真实脉搏波经带通后的峰值约 %.0f。" % (AMP_IR * 0.9))
    w("")
    for m in ('old', 'anchor', 'snap', 'new'):
        d = runs[m]
        sp = spikes_between(d, FINGER_MS, FINGER_MS + 600.0)
        w("  %-7s 贴手后 600ms 内检出的峰: %-14s amp_max=%8.0f  阈值=%8.0f"
          % (m, (str([int(x) for x in sp]) if sp else "无"),
             d.amp_max, TH_FRAC * d.amp_max))

    spike_old = max(spikes_between(runs['old'], FINGER_MS, FINGER_MS + 600.0)
                    or [0.0])
    spike_anc = max(spikes_between(runs['anchor'], FINGER_MS, FINGER_MS + 600.0)
                    or [0.0])
    spike_snap = max(spikes_between(runs['snap'], FINGER_MS, FINGER_MS + 600.0)
                     or [0.0])

    check("旧做法: 贴手后 600ms 内出现远大于脉搏波的假峰(直流阶跃进交流通道)",
          spike_old > AMP_IR * 3.0,
          "假峰 %.0f = 脉搏波的 %.0f 倍" % (spike_old, spike_old / AMP_IR))
    check("旧做法: 假峰把峰值参考顶到 1e4 量级, 阈值抬到脉搏波之上",
          runs['old'].amp_max > 5000.0,
          "amp_max=%.0f, 阈值=%.0f, 脉搏波=%.0f"
          % (runs['old'].amp_max, TH_FRAC * runs['old'].amp_max, AMP_IR))
    check("「只加接触锚定」挡不住它 —— 去抖 300ms 后才生效, 阶跃在第 1 毫秒",
          spike_anc > AMP_IR * 3.0,
          "anchor 假峰 %.0f" % spike_anc)
    check("「直流阶跃吸附」当场生效, 假峰消失(回到脉搏波量级)",
          spike_snap <= AMP_IR * 3.0,
          "snap 假峰 %.0f, amp_max=%.0f" % (spike_snap, runs['snap'].amp_max))

    # ---------- 二、后果 ----------
    w("")
    w("-" * 78)
    w("二、后果: 贴手后多久才出得了数")
    w("-" * 78)
    for m in ('old', 'anchor', 'snap', 'new'):
        d = runs[m]
        t1, b1 = first_pub_after(d, FINGER_MS)
        if t1 is None:
            w("  %-7s %.0f 秒内没有发布过任何有效心率" % (m, TOTAL_S))
        else:
            w("  %-7s 首次发布: 贴手后 %6.0f ms, 值 %3d bpm (偏差 %+d)"
              % (m, t1 - FINGER_MS, b1, b1 - TRUE_BPM))

    t_old, b_old = first_pub_after(runs['old'], FINGER_MS)
    check("旧做法: 贴手后存在一段「黑窗」, 期间一个有效心率都出不来",
          (t_old is None) or (t_old - FINGER_MS > 4000.0),
          ("%.0f 秒内始终无输出" % TOTAL_S if t_old is None
           else "%.0f ms 才出数" % (t_old - FINGER_MS)))
    check("旧做法: 黑窗长到足以让用户认为「它测不出来 / 读数随机」",
          (t_old is None) or (t_old - FINGER_MS > 10000.0),
          ("%.0f 秒内始终无输出" % TOTAL_S if t_old is None
           else "黑窗 %.0f ms" % (t_old - FINGER_MS)))

    t_snap, b_snap = first_pub_after(runs['snap'], FINGER_MS)
    check("阶跃吸附后: 贴手后 4 秒内出数, 且偏差 <= 3 bpm",
          (t_snap is not None) and (t_snap - FINGER_MS <= 4000.0)
          and abs(b_snap - TRUE_BPM) <= 3,
          ("无输出" if t_snap is None
           else "%.0f ms, 值 %d" % (t_snap - FINGER_MS, b_snap)))

    # ---------- 三、稳定期门控 ----------
    w("")
    w("-" * 78)
    w("三、稳定期门控(用户要求的那条)")
    w("-" * 78)
    d_new = runs['new']
    settle_end = FINGER_MS + ON_S * SAMPLE_MS + SETTLE_S * SAMPLE_MS
    t_new, b_new = first_pub_after(d_new, FINGER_MS)
    cost = (t_new - t_snap) if (t_new and t_snap) else 0

    w("  稳定期从「确认贴上」起算: 去抖 %.0f ms + 稳定 %.0f ms = 贴手后 %.0f ms 结束"
      % (ON_S * SAMPLE_MS, SETTLE_S * SAMPLE_MS, settle_end - FINGER_MS))
    w("  对照(snap, 不加门控): 算法在贴手后 %.0f ms 就已给出有效值"
      % (t_snap - FINGER_MS if t_snap else -1))
    w("  加门控(new):          贴手后 %.0f ms 才对外发布"
      % (t_new - FINGER_MS if t_new else -1))
    w("  ⇒ 门控的真实代价 = %.0f ms; 门控期间算法内部算出有效值 %d 次。"
      % (cost, d_new.gate_suppressed))
    w("    在本例里代价几乎为零 —— 算法本来就要到那个时刻才攒够 3 拍 IBI。")
    w("    信号越弱这个代价越大, 但那时它挡住的东西也越多(见第六节)。")

    check("稳定期内一次都没有对外发布读数",
          len(pubs_between(d_new, FINGER_MS, settle_end)) == 0,
          "%d 条" % len(pubs_between(d_new, FINGER_MS, settle_end)))

    check("稳定期结束后 1 秒内就出数(3 秒等待里算法一直在预热, 不是白等)",
          (t_new is not None) and (0 <= t_new - settle_end <= 1000.0),
          ("无输出" if t_new is None
           else "稳定期结束于 %.0f ms, 出数于 %.0f ms, 滞后 %.0f ms"
                % (settle_end, t_new, t_new - settle_end)))
    check("首个发布值与真值偏差 <= 3 bpm",
          (b_new is not None) and abs(b_new - TRUE_BPM) <= 3,
          ("无输出" if b_new is None else "%d vs %d" % (b_new, TRUE_BPM)))

    # ---------- 四、反复贴/拿 ----------
    w("")
    w("-" * 78)
    w("四、反复贴 / 拿: 上一次次测量的残余会不会混进这一次")
    w("-" * 78)
    random.seed(11)
    d = Ppg('new')
    ON1, OFF1, ON2 = 0.0, 6000.0, 9000.0
    for k in range(int(22.0 * FS)):
        t_ms = k * SAMPLE_MS
        on = (t_ms < OFF1) or (t_ms >= ON2)
        ir, red = gen(t_ms, TRUE_BPM, dc_on=on)
        d.push(t_ms, ir, red)

    settle_end2 = ON2 + ON_S * SAMPLE_MS + SETTLE_S * SAMPLE_MS
    seg_late = pubs_between(d, OFF1 + OFF_S * SAMPLE_MS, ON2)
    seg_re = pubs_between(d, ON2, settle_end2)
    t_re, b_re = first_pub_after(d, ON2)

    w("  拿开手指后 %.0f ms(去抖)内仍会显示最后一个数, 之后立刻停 ——"
      % (OFF_S * SAMPLE_MS))
    w("  这是刻意的: 去抖太短的话, 手指稍微一挪就重启一次 3 秒稳定期。")
    check("拿开手指 1 秒内停止发布(不会一直挂着旧值)",
          len(seg_late) == 0, "%d 条" % len(seg_late))
    check("重新贴上后, 稳定期内不发布(不把上一段的拍数混进来)",
          len(seg_re) == 0, "%d 条" % len(seg_re))
    check("重新贴上后仍能正常出数(偏差 <= 3 bpm)",
          (t_re is not None) and abs(b_re - TRUE_BPM) <= 3,
          ("无输出" if t_re is None
           else "重贴后 %.0f ms, 值 %d" % (t_re - ON2, b_re)))

    # ---------- 五、测量中压力突变(向上阶跃) ----------
    w("")
    w("-" * 78)
    w("五、测量中贴合/压力突变: 会不会读出假数")
    w("-" * 78)
    w("  做法: 手指一直在, 但在第 8 秒把直流从 50000 抬到 58000(按紧一点),")
    w("        模拟一次向上阶跃。对照 old 与 new 的读数。")
    STEP_T = 8000.0
    res = {}
    for m in ('old', 'new'):
        random.seed(23)
        d = Ppg(m)
        for k in range(int(16.0 * FS)):
            t_ms = k * SAMPLE_MS
            lvl = DC_IR if t_ms < STEP_T else DC_IR * 1.16
            ir, red = gen(t_ms, TRUE_BPM, dc_on=True, dc_level=lvl)
            d.push(t_ms, ir, red)
        res[m] = d

    for m in ('old', 'new'):
        d = res[m]
        seg = pubs_between(d, STEP_T, STEP_T + 4000.0)
        vals = sorted(set(v for (t, v) in seg))
        worst = max(vals, key=lambda v: abs(v - TRUE_BPM)) if vals else None
        w("  %-7s 阶跃后 0~4s 的读数集合: %-22s amp_max=%.0f"
          % (m, (str(vals) if vals else "无"), d.amp_max))
        if worst is not None:
            w("          最大偏差 %+d bpm" % (worst - TRUE_BPM))

    d_o = res['old']
    seg_o = pubs_between(d_o, STEP_T, STEP_T + 4000.0)
    vals_o = sorted(set(v for (t, v) in seg_o))
    max_dev_o = max((abs(v - TRUE_BPM) for v in vals_o), default=0)
    d_n = res['new']
    seg_n = pubs_between(d_n, STEP_T, STEP_T + 4000.0)
    vals_n = sorted(set(v for (t, v) in seg_n))
    max_dev_n = max((abs(v - TRUE_BPM) for v in vals_n), default=0)

    check("阶跃吸附: 压力突变不再产生错误读数(偏差 <= 5 bpm)",
          max_dev_n <= 5, "最大偏差 %d bpm" % max_dev_n)

    # ---------- 六、弱信号: 头 3 秒的数会不会跳 ----------
    w("")
    w("-" * 78)
    w("六、弱信号(贴不紧 / 末梢循环差): 头 3 秒的数会不会跳")
    w("-" * 78)
    WEAK = 150.0
    WNOISE = 14.0
    w("  做法: 脉搏波幅度 %.0f -> %.0f(交流/直流 从 %.2f%% 降到 %.2f%%), 噪声 %.0f -> %.0f。"
      % (AMP_IR, WEAK, 100.0 * AMP_IR / DC_IR, 100.0 * WEAK / DC_IR, NOISE, WNOISE))
    w("  问题: 信号弱时算法要更久才攒够 3 拍, 而这段时间里它可能先锁上个错的。")
    w("")

    weak = {}
    for m in ('snap', 'new'):
        random.seed(31)
        d = Ppg(m)
        for k in range(int(16.0 * FS)):
            t_ms = k * SAMPLE_MS
            ir, red = gen(t_ms, TRUE_BPM, dc_on=(t_ms >= FINGER_MS),
                          amp=WEAK, noise=WNOISE)
            d.push(t_ms, ir, red)
        weak[m] = d

    for m in ('snap', 'new'):
        d = weak[m]
        early = sorted(set(v for (t, v) in pubs_between(d, FINGER_MS, settle_end)))
        late = sorted(set(v for (t, v) in
                          pubs_between(d, settle_end, settle_end + 3000.0)))
        w("  %-7s 稳定期[%.0f~%.0fms]内发布: %-16s | 稳定期后 3 秒内发布: %s"
          % (m, FINGER_MS, settle_end,
             (str(early) if early else "无"), (str(late) if late else "无")))

    d_w = weak['new']
    early_new = pubs_between(d_w, FINGER_MS, settle_end)
    t_w, b_w = first_pub_after(d_w, FINGER_MS)
    check("弱信号下稳定期内仍不发布(此时算法内部确实已经先给出过值, 见上行)",
          len(early_new) == 0, "%d 条" % len(early_new))
    check("弱信号下稳定期后能出数(门控没有把数永久挡住)",
          t_w is not None,
          ("无输出" if t_w is None
           else "贴手后 %.0f ms, 值 %d" % (t_w - FINGER_MS, b_w)))

    # ---------- 七、门控代价 vs 心率 ----------
    w("")
    w("-" * 78)
    w("七、门控代价 vs 心率 —— 3 秒够不够, 会不会拖慢")
    w("-" * 78)
    w("  算法至少要「1 个锚点峰 + 3 拍 IBI」才出得了数; 心率越快这个时间越短,")
    w("  而门控固定 3 秒 —— 所以代价只会出现在【心率偏快】的时候,")
    w("  反过来说: 心率偏慢时 3 秒甚至还不够算法自己攒够拍数, 门控等于零代价。")
    w("")
    w("  真值    snap 出数       new 出数        门控代价")
    rows = []
    for hr in (50, 72, 90, 110):
        row = {}
        for m in ('snap', 'new'):
            random.seed(41)
            d = Ppg(m)
            for k in range(int(16.0 * FS)):
                t_ms = k * SAMPLE_MS
                ir, red = gen(t_ms, hr, dc_on=(t_ms >= FINGER_MS))
                d.push(t_ms, ir, red)
            t1, b1 = first_pub_after(d, FINGER_MS)
            row[m] = ((t1 - FINGER_MS), b1) if t1 else (None, None)
        cost = (row['new'][0] - row['snap'][0]) if (row['new'][0] and row['snap'][0]) else 0
        rows.append((hr, row, cost))
        w("  %3d bpm %-14s %-14s %+5.0f ms"
          % (hr,
             ("%.0f ms" % row['snap'][0]) if row['snap'][0] else "无",
             ("%.0f ms" % row['new'][0]) if row['new'][0] else "无",
             cost))
    w("")
    w("  注: 门控固定 %.0f ms(含去抖), 心率 50 时算法自己就要 %.0f ms,"
      % ((ON_S + SETTLE_S) * SAMPLE_MS,
         rows[0][1]['snap'][0] if rows[0][1]['snap'][0] else 0))
    w("      所以门控在那时候是零代价的。")

    max_cost = max(r[2] for r in rows)
    check("门控代价在最坏情况下 <= 1.5 秒(上面每一行都在这个范围内)",
          max_cost <= 1500.0, "最大代价 %.0f ms" % max_cost)

    # ---------- 汇总 ----------
    w("")
    w("=" * 78)
    w("断言结果")
    w("=" * 78)
    for line in RESULTS:
        w(line)
    w("")
    w("总计: %d 项, 失败 %d 项" % (len(RESULTS), len(FAILED)))
    if FAILED:
        w("失败项:")
        for f in FAILED:
            w("  - " + f)
        w("")
        w("结论: 未全部通过 —— 先复核算法或断言, 不要直接烧录。")
    else:
        w("结论: 全部通过。")
        w("")
        w("三层修法各自的作用(按见效顺序):")
        w("  1) 直流阶跃吸附 —— 从根上不让阶跃变成假峰。这是本轮最关键的一条,")
        w("     没有它, 贴手后会有长达几十秒的「黑窗」, 那正是「读数随机」的来源。")
        w("  2) 接触锚定 + 清算法 —— 清掉上一次次测量的残余拍库, 不让两次混在一起。")
        w("  3) 3 秒稳定期门控 —— 用户要求的那条。它不改变算法结果, 改变的是")
        w("     「什么时候让用户看到」: 把贴手瞬间那些必然存在、但还不可信的中间态")
        w("     挡在门外, 屏上用一句倒计时代替一个会变的数。")

    text = "\n".join(out) + "\n"
    with open("ppg_contact_sim_result.txt", "w", encoding="utf-8") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
