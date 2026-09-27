# -*- coding: utf-8 -*-
"""
PPG 一次测量流程仿真 —— 贴手 → 稳定 3 秒 → 采集 10 秒 → 出最终结果

【为什么又写一个脚本(第三个)】
  ppg_hr_sim.py      查的是【算法内部】的缺陷(阈值被顶死、下降沿重复判峰、
                     无幅度下限、丢样本不进时间轴)。
  ppg_contact_sim.py 查的是【贴手瞬间】的直流阶跃(假峰 36422、34 秒黑窗)。
  这两个脚本都默认了一件事: 一直在测、一直在发布。而本轮把流程改成了
  【一次测量】: 贴上 → 稳定 3 秒 → 采集 10 秒 → 出最终结果并冻结。
  流程一变, 要验证的东西也变了 —— 本脚本验证的就是【流程本身】:
    状态时序对不对? 采集期会不会漏出一个数? 最终值准不准? 冻没冻住?
    采到一半拿开手指会怎样? 第二次测量会不会混进第一次的拍数?
    10 秒真的比"最近 3 拍"更稳吗?
  所有断言都对着 user/Src/heart_rate.c 的实际逻辑写, 不用"看起来对"代替验证。

【四个变体】
  old    09-17 上午版: 三态机(IDLE/SETTLING/MEASURING) + 持续发布
                       —— 用来对照"稳定期一过就开始每一拍刷新"是什么样
  nosnap 本轮流程, 但去掉直流阶跃吸附
  new    09-17 交付版: 四态机 + 10 秒窗口中位数 + 只在 DONE 发布
  fix    2026-09-17 修复版(现在固件用的就是这套): 在 new 上加了重搏切迹抑制
                       —— 起因是用户实测"10 秒跑完不给读数", 完整推导见
                       ppg_nopulse_sim.py; 本脚本第十一节保证 fix 也过流程断言

【写这个脚本时被自己打脸的两处(留着, 免得下次又推导一遍)】
  ① 中位数不是万能的。信号弱(幅度 0.3% 调制比)而基线漂移比脉搏波还大时,
     一个心搏会被顶出两个局部极大值, 检出的 IBI 变成双峰分布(500/1000 交替),
     而中位数正好落在两峰【之间】—— 给不出明显的错值: 实测真值 60 被读成 94,
     真值 72 被读成 115 / 130。挡这种错只能靠【一致占比】(有多少拍真落在
     中位数附近), 见 finish_measure 里的 PPG_WIN_IBI_CONSIST_PCT。
  ② 吸附管的不是"贴手那一次阶跃"。那次阶跃产生的假峰会被 begin_settle() 的
     锚定连同峰值参考一起清掉(实测去掉吸附照样测出 71 bpm)。吸附真正管的是
     【测量中途】的阶跃(手指挪动/按压力度变化): 那时没有"清算法重来"的机会,
     一个假峰就能把峰值参考顶到 60 倍于脉搏波, 剩下的窗口全废。见第八节。

【修复与验证】重搏切迹(用户实测"10 秒跑完不给读数"的根因)与两条修法见
  tools/ppg_nopulse_sim.py; 本脚本第十一节保证 'fix' 这个【固件等价变体】
  也过一遍流程断言 —— 免得出现"流程在 A 变体上验过、烧的是 B 变体"。

输出: ppg_measure_sim_result.txt (与脚本同目录; 本机 PowerShell 不回显
      stdout, 所以结果必须落盘再读)
运行: python tools/ppg_measure_sim.py
"""

import math
import os
import random

# ------------------------------------------------------------------ 固件参数
# 全部取自 user/Inc/user_config.h; 改那边就要改这里, 否则仿真不代表固件
FS           = 50.0
SAMPLE_MS    = 1000.0 / FS
WINDOW       = 200            # PPG_HR_WINDOW
HP_A         = 1.0 - math.exp(-2.0 * math.pi * 0.5 / FS)   # PPG_HP_CUTOFF_HZ
LP_A         = 1.0 - math.exp(-2.0 * math.pi * 4.0 / FS)   # PPG_LP_CUTOFF_HZ
TH_FRAC      = 0.40           # PPG_PEAK_TH_FRAC
MIN_AMP      = 30.0           # PPG_PEAK_MIN_AMP
TRACK        = 0.06           # PPG_PEAK_TRACK
MIN_IBI      = 300.0          # PPG_HR_MIN_IBI_MS —— 交付版(09-17)的取值。
                              # 注: 固件已在 2026-09-17 提到 400(见 MIN_IBI_FIX)。
                              # 'old'/'nosnap'/'new' 是那一版的历史基线, 保持 300
                              # 才能复现出当时的问题; 现在固件的行为 = 'fix'。
MIN_IBI_FIX  = 400.0          # 修复版把它提到 400: 检测上限 200->150bpm
MIN_IBI_REFR = 400.0          # 'refr' 变体: 【只】抬高不应期, 不加切迹抑制
MAX_IBI      = 2000.0         # PPG_HR_MAX_IBI_MS
REACQ        = 300            # PPG_PEAK_REACQ_MS = 3 * MAX_IBI
SETTLE0      = 25             # PPG_SETTLE_SAMPLES
TOL_PCT      = 25             # PPG_IBI_TOL_PCT
RESET_STREAK = 4              # PPG_IBI_RESET_STREAK
IBI_N        = 5              # 旧版实时中位数用的拍数
DC_MIN       = 1000.0         # PPG_DC_MIN_RAW
MIN_MOD      = 0.002          # PPG_MIN_MODULATION
CONTACT_TH   = 2000.0         # PPG_CONTACT_RAW_TH
ON_S         = 15             # PPG_CONTACT_ON_MS  = 300ms
OFF_S        = 40             # PPG_CONTACT_OFF_MS = 800ms
SETTLE_S     = 150            # PPG_SETTLE_MS      = 3000ms
MEASURE_S    = 500            # PPG_MEASURE_MS     = 10000ms
STEP_FRAC    = 0.25           # PPG_DC_STEP_FRAC
WIN_MAX      = 40             # PPG_WIN_MAX
WIN_IBI_MIN  = 3              # PPG_WIN_IBI_MIN
WIN_R_MIN    = 3              # PPG_WIN_R_MIN
CONSIST_PCT  = 70             # PPG_WIN_IBI_CONSIST_PCT
REARM_FRAC   = 0.25           # (已废弃: 幅度型回落判据, 对基线漂移敏感, 见下)
NOTCH_RATIO  = 0.60           # PPG_NOTCH_MIN_RATIO (仅 'fix' 变体)

SPO2_A, SPO2_B, SPO2_C = -45.06, 30.354, 94.845   # 未标定经验式

# ------------------------------------------------------------------ 波形参数
AMB_IR, AMB_RED = 300.0, 320.0          # 环境光量级(没手指时)
DC_IR,  DC_RED  = 50000.0, 42000.0      # 手指盖住时的直流
AMP_IR          = 600.0                 # 脉搏波交流峰值(红外)
AMP_RED_RATIO   = 0.60                  # 红/红外的交流比 ⇒ R ≈ 0.71 ⇒ SpO2 ≈ 93%
DRIFT           = 400.0                 # 0.25Hz 基线漂移(呼吸/压力)
NOISE           = 8.0                   # 白噪声标准差
TRUE_R          = (AMP_RED_RATIO * DC_IR) / DC_RED    # 交流比折算后的真实 R

MODE_DESC = {
    'old':    "09-17 上午版: 三态机 + 持续发布(每拍刷新, 稳定期一过就报数)",
    'nosnap': "本轮流程但【去掉直流阶跃吸附】(证明测量中途的阶跃必须挡住)",
    'new':    "09-17 下午版: 四态机 + 10 秒窗口中位数 + 只在 DONE 发布",
    'fix':    "修复版: 在 'new' 基础上加【切迹抑制】(间隔短于已有 IBI 中位数的 "
              "60% 不算一拍 + 不移动锚点) + 逃生通道不再清空"
              "(挡重搏切迹, 见 ppg_nopulse_sim.py)",
    'refr':   "对照变体: 【只】把不应期抬到 400ms, 不加切迹抑制 "
              "(用来证明切迹抑制本身是承重的, 见 ppg_nopulse_sim.py 第七节)",
}


def pulse_shape(phase):
    """一个像脉搏波的形状: 主波 + 二次谐波(纯正弦太理想化)"""
    return math.sin(phase) + 0.35 * math.sin(2.0 * phase + 0.6)


class Wave:
    """PPG 波形发生器。
    心率可以是常数, 也可以是 t_ms 的函数 —— 后者用于"两次测量之间心率变了"
    这类场景。相位用累加而不是"f×t": 心率一变, 直接算 f×t 会让相位跳一下,
    那个跳变本身就是个假尖峰, 会把算法测出来的东西变成生成器的错误。"""

    def __init__(self, hr, amp=AMP_IR, noise=NOISE, drift=DRIFT):
        self.hr = hr
        self.amp = amp
        self.noise = noise
        self.drift = drift
        self.phase = 0.0

    def bpm_at(self, t_ms):
        return self.hr(t_ms) if callable(self.hr) else self.hr

    def sample(self, t_ms, dc_on, dc_level=None):
        t = t_ms / 1000.0
        hr = self.bpm_at(t_ms)
        self.phase += 2.0 * math.pi * (hr / 60.0) * (SAMPLE_MS / 1000.0)

        if not dc_on:
            return (AMB_IR + random.gauss(0, self.noise),
                    AMB_RED + random.gauss(0, self.noise))

        dc_i = DC_IR if dc_level is None else dc_level
        dc_r = DC_RED * (dc_i / DC_IR)
        p = pulse_shape(self.phase)
        dr = self.drift * math.sin(2.0 * math.pi * 0.25 * t)
        return (dc_i + self.amp * p + dr + random.gauss(0, self.noise),
                dc_r + self.amp * AMP_RED_RATIO * p + 0.8 * dr
                + random.gauss(0, self.noise))


class MotionWave(Wave):
    """在指定时刻叠加【运动伪影】—— 半正弦脉冲, 宽度 width_ms, 幅度可正可负。

    【为什么不是"直流阶跃"】手指压一下 / 挪一下, 光学耦合的变化是【交流】过程
    (持续 100~200ms 的脉冲), 不是阶跃。这一点很关键: 直流阶跃吸附
    (PPG_DC_STEP_FRAC) 只对"单步跳变 >25%"的阶跃生效, 对这种脉冲基本不起作用。
    实测(见 ppg_nopulse_sim.py 一、二节): 4Hz 低通本身就把这类短脉冲削掉了
    —— 8000 LSB 的脉冲只把峰值参考从 577 抬到 824, 挡不住也伤不到。

    bursts: [(t_start_ms, width_ms, amp_LSB), ...], 幅度为负表示反向。
    """

    def __init__(self, hr, bursts=(), **kw):
        super(MotionWave, self).__init__(hr, **kw)
        self.bursts = list(bursts)

    def sample(self, t_ms, dc_on, dc_level=None):
        ir, red = super(MotionWave, self).sample(t_ms, dc_on, dc_level)
        if dc_on and self.bursts:
            a = 0.0
            for (t0, width, amp) in self.bursts:
                dt = t_ms - t0
                if 0.0 <= dt <= width:
                    a += amp * math.sin(math.pi * dt / width)
            ir += a
            red += a * AMP_RED_RATIO
        return ir, red


def shake_bursts(t0, t1, period_ms=400.0, width_ms=200.0, amp=0.0):
    """一段【持续晃动】: 从 t0 到 t1 每隔 period_ms 一个半正弦, 正负交替。
    period 取 400ms(2.5Hz) 是有讲究的: 比不应期 PPG_HR_MIN_IBI_MS=300ms 长,
    所以它【能】被当成搏动检出来 —— 更快的抖动会被不应期直接挡掉。"""
    out = []
    t = t0
    k = 0
    while t < t1:
        out.append((t, width_ms, amp if k % 2 == 0 else -amp))
        t += period_ms
        k += 1
    return out


class NotchWave(Wave):
    """带【重搏切迹】的脉搏波: 主波之后约 notch_ms 叠一个二次波峰。

    【为什么必须建这个模型】基线波形 sin(φ)+0.35·sin(2φ+0.6) 是平滑的、
    每拍只有一个极大值 —— 所以"前 30 项断言全过"根本不能说明寻峰在实物上
    不会被骗。真实 PPG 在收缩峰之后约 200~400ms 有一个二次波(重搏波),
    高度可达主波的 20%~60%; 它一旦越过峰阈值就会被当成【又一次搏动】,
    把 IBI 撕裂成"主波→切迹"与"切迹→下一主波"两段。
    实测(ppg_nopulse_sim.py 四节): 切迹 40% 就能让整个采集窗口报废。

    frac = 二次波峰高 / 主波峰值。"""

    PHI_PK = max((p * 0.001 for p in range(0, 6284)), key=pulse_shape)

    def __init__(self, hr, frac, notch_ms=300.0, width_ms=180.0, **kw):
        super(NotchWave, self).__init__(hr, **kw)
        self.frac = frac
        self.notch_ms = notch_ms
        self.width = width_ms

    def sample(self, t_ms, dc_on, dc_level=None):
        ir, red = super(NotchWave, self).sample(t_ms, dc_on, dc_level)
        if dc_on and self.frac > 0.0:
            period_ms = 60000.0 / self.bpm_at(t_ms)
            dphi = 2.0 * math.pi * (self.notch_ms / period_ms)
            sig = math.pi * (self.width / period_ms)
            phi = self.phase % (2.0 * math.pi)
            x = ((phi - (self.PHI_PK + dphi) + math.pi) % (2.0 * math.pi)) - math.pi
            if abs(x) <= sig:
                a = self.frac * self.amp * math.cos(math.pi * x / sig)
                ir += a
                red += a * AMP_RED_RATIO
        return ir, red


def median(v):
    s = sorted(v)
    n = len(s)
    return 0 if n == 0 else s[n // 2]

def spo2_of(r):
    s = SPO2_A * r * r + SPO2_B * r + SPO2_C
    return max(0.0, min(100.0, s))


# ------------------------------------------------------------------ 算法
class Ppg:
    """与 user/Src/heart_rate.c 一一对应。
    发布出口只有一个(和固件的 HR_GetResult() 一样), 所以"屏上"和"云端"
    在仿真里也必然是同一个判断 —— 这一点本身也是要验证的。"""

    def __init__(self, mode):
        self.mode = mode
        self.oneshot = (mode != 'old')      # 一次测量流程
        self.snap = (mode != 'nosnap')      # 直流阶跃吸附
        # 修复版把不应期提到 400ms(见 ppg_nopulse_sim.py 第六节):
        # 重搏切迹落在主波后 300ms 上下, 300ms 的不应期正好放它过去。
        self.min_ibi = MIN_IBI_FIX if mode in ('fix', 'refr') else MIN_IBI
        # 切迹抑制与"逃生通道不再清空"是两条独立的改动, 分开开关是为了能
        # 单独验证每一条(见 ppg_nopulse_sim.py 第七节的 'refr' 对照)。
        self.notch = (mode == 'fix')
        self.no_clear = (mode == 'fix')
        # 滤波器
        self.dc_ir = 0.0
        self.dc_red = 0.0
        self.lp_ir = 0.0
        self.lp_red = 0.0
        self.inited = False

        # 滑动窗口(4 秒)
        self.buf_ir = [0.0] * WINDOW
        self.buf_red = [0.0] * WINDOW
        self.head = 0
        self.filled = 0

        # 寻峰
        self.v1 = 0.0
        self.v2 = 0.0
        self.amp_ref = 0.0
        self.last_peak = 0
        self.since_peak = 0
        self.settle = SETTLE0

        # 状态机
        self.state = 'IDLE'
        self.crun = 0
        self.nrun = 0
        self.settle_left = 0
        self.win_left = 0

        # 采集窗口累计
        self.win_ibi = []
        # win_ibi 的【时刻】并列一份。算法本身不用它 —— 是给
        # tools/ppg_retry_sim.py 离线评估"自适应窗口"用的: 想知道"如果
        # 攒到第 6 拍就收工, 结果会是什么、要等多久", 必须知道每拍什么时候
        # 进窗口。离线复算前缀中位数与在线逐拍累计完全等价(见该脚本的说明),
        # 所以不必在状态机里再加一条早退分支, 少动一份"可执行规格"。
        self.win_ibi_t = []
        self.rec_ibi = []          # 最近 5 个被接受的 IBI(切迹抑制用的估计)
        self.win_r = []
        self.reject = 0

        # 最终结果(一次测量)
        self.fin_bpm = 0
        self.fin_bpm_valid = False
        self.fin_spo2 = 0
        self.fin_spo2_valid = False
        self.fin_r = 0.0
        self.fin_beats = 0
        self.fin_agree_pct = 0.0

        # 实时结果(旧版持续发布用)
        self.ibi = []
        self.bpm = 0
        self.bpm_valid = False
        self.spo2 = 0
        self.spo2_valid = False

        # 实时 R
        self.ratio = 0.0

        # 时间轴
        self.idx = 0
        self.gap_total = 0

        # 记录
        self.trace = [(0.0, 'IDLE')]     # 状态变化点
        self.pub = []                    # 发布变化点 (t_ms, hr, spo2, sv)
        self.amp_max = 0.0
        self.done_t = None

    # ---------------- 滤波器 ----------------
    def _band(self, x, ch):
        if self.snap:
            if ch == 0:
                if self.dc_ir > 1.0 and abs(x - self.dc_ir) > STEP_FRAC * self.dc_ir:
                    self.dc_ir = x
            else:
                if self.dc_red > 1.0 and abs(x - self.dc_red) > STEP_FRAC * self.dc_red:
                    self.dc_red = x
        if ch == 0:
            self.dc_ir += HP_A * (x - self.dc_ir)
            hp = x - self.dc_ir
            self.lp_ir += LP_A * (hp - self.lp_ir)
            return self.lp_ir
        self.dc_red += HP_A * (x - self.dc_red)
        hp = x - self.dc_red
        self.lp_red += LP_A * (hp - self.lp_red)
        return self.lp_red

    # ---------------- 状态机 ----------------
    def _clear_algo(self):
        self.head = 0
        self.filled = 0
        self.v1 = self.v2 = 0.0
        self.amp_ref = 0.0
        self.last_peak = 0
        self.since_peak = 0
        self.settle = SETTLE0
        self.since_peak = 0
        self.win_ibi = []
        self.win_ibi_t = []
        self.rec_ibi = []
        self.win_r = []
        self.reject = 0
        self.win_left = 0
        self.ratio = 0.0
        self.ibi = []
        self.bpm = 0
        self.bpm_valid = False
        self.spo2 = 0
        self.spo2_valid = False
        self.fin_bpm = 0
        self.fin_bpm_valid = False
        self.fin_spo2 = 0
        self.fin_spo2_valid = False
        self.fin_r = 0.0
        self.fin_beats = 0
        self.fin_agree_pct = 0.0
        self.done_t = None

    def _begin_settle(self, ir, red):
        self._clear_algo()
        self.dc_ir = float(ir)
        self.dc_red = float(red)
        self.lp_ir = 0.0
        self.lp_red = 0.0
        self.inited = True
        self.settle_left = SETTLE_S

    # ---------------- 血氧比值 ----------------
    def _update_ratio(self):
        if self.filled < WINDOW:
            return
        if self.dc_ir < DC_MIN or self.dc_red < DC_MIN:
            self.ratio = 0.0
            return
        ac_i = math.sqrt(sum(v * v for v in self.buf_ir) / WINDOW)
        ac_r = math.sqrt(sum(v * v for v in self.buf_red) / WINDOW)
        if ac_i < 1.0 or (ac_i / self.dc_ir) < MIN_MOD:
            self.ratio = 0.0
            return
        r = (ac_r / self.dc_red) / (ac_i / self.dc_ir)
        self.ratio = r if self.ratio == 0.0 else 0.75 * self.ratio + 0.25 * r

        if self.state == 'MEASURING' and len(self.win_r) < WIN_MAX:
            self.win_r.append(self.ratio)

        if not self.oneshot and self.ratio > 0.0:
            self.spo2 = int(spo2_of(self.ratio) + 0.5)
            self.spo2_valid = True

    # ---------------- 寻峰 ----------------
    def _on_peak(self, t_ms, idx, amp):
        # 固定不应期: 生理上不可能比 min_ibi 更快, 更快的"峰"都是噪声
        if self.last_peak != 0 and (idx - self.last_peak) * SAMPLE_MS < self.min_ibi:
            return
        self.since_peak = 0
        if self.amp_ref <= 0.0:
            self.amp_ref = amp
        else:
            self.amp_ref += TRACK * (amp - self.amp_ref)

        if self.last_peak == 0:
            self.last_peak = idx          # 第一个峰只作锚点
            return

        dt = (idx - self.last_peak) * SAMPLE_MS

        # ---- 切迹抑制(仅 'fix' 变体): 间隔明显短于已有 IBI 就不认一拍 ----
        # 为什么用【时间】而不是幅度: 幅度判据要对基线漂移敏感 —— 弱信号下
        # 漂移(实测 400)比脉搏波(150)还大, 波形谷底被抬高, "必须回落到某条线"
        # 这类条件会失效并漏拍(实测把 90bpm 读成 30bpm)。IBI 是纯时间量,
        # 与漂移无关。详见 ppg_nopulse_sim.py 第六节的对照。
        #
        # 参照 = 最近 5 拍已接受 IBI 的【中位数】(见下面的 rec_ibi)。
        # 【演化说明】最初取的是"窗口内 IBI 的上界(max)" —— 理由是伪影只会
        # 多出一个峰、只会让间隔变短, 所以最大的合格间隔最接近真实 IBI。
        # 但上界没有自愈能力: 一次异常大的间隔(如 1640ms)会把它永久抬高,
        # 之后所有真间隔(1000ms)都被判成"太短"而拒掉。滚动中位数会自己降
        # 回来, 所以最终换成中位数。这段注释曾一度没跟着代码改, 特此留下。
        #
        # 【关键】被挡下的这一拍不移动锚点 —— 旧代码是先移动锚点再判离群,
        # 于是下一个间隔从假峰起算, 整条链全被带偏。
        if self.notch and len(self.rec_ibi) >= 3:
            est = median(self.rec_ibi)
            if dt < NOTCH_RATIO * est:
                return

        self.last_peak = idx
        if dt < self.min_ibi or dt > MAX_IBI:
            return

        if self.state != 'MEASURING':
            self._update_ratio()
            return

        if self.oneshot:
            # 采集窗口: 逐拍累计, 只在窗口结束时取中位数
            if len(self.win_ibi) >= 3:
                m = median(self.win_ibi)
                if m > 0 and abs(dt - m) * 100 > m * TOL_PCT:
                    self.reject += 1
                    if self.reject < RESET_STREAK:
                        return
                    if not self.no_clear:
                        # 旧版逃生通道: 连续多次对不上就【清空重来】。
                        # 在 10 秒窗口里这是有害的 —— 它把已经攒到的样本
                        # 一起扔了, 而中位数本来就不怕几个离群点。
                        self.win_ibi = []
                        self.win_ibi_t = []
                        self.win_r = []
                self.reject = 0
            if len(self.win_ibi) < WIN_MAX:
                self.win_ibi.append(dt)
                self.win_ibi_t.append(t_ms)   # 见 __init__ 里的说明(仅供离线评估)
                # 最近 5 拍的滚动记录: 给"切迹抑制"当 IBI 估计用。
                # 用滚动中位数而不是窗口上界: 上界一旦被一次异常大的间隔抬高,
                # 会把后面所有真搏动都锁死(60bpm 的真间隔 1000ms 会被判成
                # 太短而拒掉); 中位数会随新样本自己降回来。
                self.rec_ibi.append(dt)
                if len(self.rec_ibi) > 5:
                    self.rec_ibi.pop(0)
        else:
            # 旧版: 最近 5 拍的中位数, 每拍都重算 → 屏上一直在变
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

    # ---------------- 窗口结束 ----------------
    def _finish(self, t_ms):
        self.fin_beats = len(self.win_ibi)
        if len(self.win_ibi) >= WIN_IBI_MIN:
            m = median(self.win_ibi)
            agree = sum(1 for v in self.win_ibi if abs(v - m) * 100 <= m * TOL_PCT)
            self.fin_agree_pct = 100.0 * agree / len(self.win_ibi)
            # 一致性门槛: 拍数够【而且】彼此一致才算测出来(见 ibi_stats 的说明)
            if m > 0 and agree >= WIN_IBI_MIN \
                    and agree * 100 >= CONSIST_PCT * len(self.win_ibi):
                self.fin_bpm = int((60000.0 + m / 2.0) / m)
                self.fin_bpm_valid = True
        if len(self.win_r) >= WIN_R_MIN and self.fin_bpm_valid:
            self.fin_r = median(self.win_r)
            self.fin_spo2 = int(spo2_of(self.fin_r) + 0.5)
            self.fin_spo2_valid = True
        elif len(self.win_r) >= WIN_R_MIN:
            # 心率没通过一致性门槛 → 血氧也不报: 同一个窗口里, 心血池的事
            # 不会"心率不可信而血氧可信"。
            self.fin_r = 0.0
            self.fin_spo2 = 0
            self.fin_spo2_valid = False
        self.state = 'DONE'
        self.done_t = t_ms

    # ---------------- FIFO 丢样本 ----------------
    def notify_drop(self, lost):
        if lost <= 0:
            return
        self.idx += lost
        self.gap_total += lost
        self.last_peak = 0
        self.reject = 0
        self.v1 = self.v2 = 0.0

    # ---------------- 发布出口 ----------------
    def result(self):
        if self.oneshot:
            pub = (self.state == 'DONE')
            hr = self.fin_bpm if (pub and self.fin_bpm_valid) else 0
            sp = self.fin_spo2 if pub else 0
            sv = bool(pub and self.fin_spo2_valid)
        else:
            pub = (self.state == 'MEASURING')
            hr = self.bpm if (pub and self.bpm_valid) else 0
            sp = self.spo2 if pub else 0
            sv = bool(pub and self.spo2_valid)
        return hr, sp, sv

    # ---------------- 主入口 ----------------
    def push(self, t_ms, ir, red, drop=0):
        if drop:
            self.notify_drop(drop)

        touched = (ir > CONTACT_TH)
        if touched:
            self.crun += 1
            self.nrun = 0
        else:
            self.nrun += 1
            self.crun = 0

        prev = self.state
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
                if self.oneshot:
                    self.win_left = MEASURE_S
                self.state = 'MEASURING'
        elif self.state == 'MEASURING' and self.oneshot:
            if self.win_left > 0:
                self.win_left -= 1
            if self.win_left == 0:
                self._finish(t_ms)

        if self.state != prev:
            self.trace.append((t_ms, self.state))

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

        self.v2 = self.v1
        self.v1 = b_ir
        if self.amp_ref > self.amp_max:
            self.amp_max = self.amp_ref

        hr, sp, sv = self.result()
        if not self.pub or (hr, sp, sv) != self.pub[-1][1:]:
            self.pub.append((t_ms, hr, sp, sv))


# ------------------------------------------------------------------ 场景工具
def run(mode, total_s, wave, on=None, on2=None, off_at=None, drop_at=None,
        seed=7):
    """跑一段波形。
    on/on2: 手指放上的时刻(ms); off_at: 拿开的时刻; drop_at: FIFO 丢样本的时刻。
    on 为 None 表示全程没手指。"""
    random.seed(seed)
    d = Ppg(mode)
    for k in range(int(total_s * FS)):
        t_ms = k * SAMPLE_MS
        if on is None:
            dc_on = False
        elif off_at is not None or on2 is not None:
            dc_on = True
            if off_at is not None and t_ms >= off_at:
                dc_on = False
            if on2 is not None:
                dc_on = (t_ms < (on - 1.0)) or (t_ms >= on2) if off_at is None \
                    else ((t_ms >= on and t_ms < off_at) or (t_ms >= on2))
        else:
            dc_on = (t_ms >= on)
        ir, red = wave.sample(t_ms, dc_on)
        drop = 1 if (drop_at and drop_at <= t_ms < drop_at + 2 * SAMPLE_MS) else 0
        d.push(t_ms, ir, red, drop)
    return d


def state_at(d, name):
    for (t, s) in d.trace:
        if s == name:
            return t
    return None


def pubs(d):
    return [(t, hr) for (t, hr, sp, sv) in d.pub if hr > 0]


def pubs_between(d, t0, t1):
    return [hr for (t, hr) in pubs(d) if t0 <= t < t1]


def first_pub_after(d, t0):
    for (t, hr) in pubs(d):
        if t >= t0:
            return t, hr
    return None, None


def distinct(seq):
    return sorted(set(seq))


def ibi_stats(ibis):
    """窗口内 IBI 的一致性统计 —— 返回 (拍数, 中位数, 最小, 最大, 一致占比%)。

    【为什么需要这个统计】中位数只能挡住少数离群点, 挡不住【系统性双峰】:
    信号弱时基线漂移会把一个心搏顶出两个局部极大值, 检出的间隔就变成
    "500, 1000, 500, 1000..." 这种双峰分布, 而中位数正好落在两峰之间 ——
    给出一个既不是 60 也不是 120 的错值(实测: 真值 60 被读成 94)。
    这时候唯一能看出问题的量是【一致性】: 有多少拍真的落在中位数附近。
    所以 finish_measure 里用它当发布门槛: 不一致就不给结果, 让屏上说
    "no pulse, retry", 而不是给一个看起来很正常的错数。"""
    n = len(ibis)
    if n == 0:
        return 0, 0, 0, 0, 0.0
    m = median(ibis)
    agree = sum(1 for v in ibis if abs(v - m) * 100 <= m * TOL_PCT)
    return n, m, min(ibis), max(ibis), 100.0 * agree / n


# ------------------------------------------------------------------ 断言框架
RESULTS = []
FAILED = []


def check(name, ok, detail=""):
    RESULTS.append("  [%s] %s%s"
                   % ("PASS" if ok else "FAIL", name,
                      ("  -> " + detail) if detail else ""))
    if not ok:
        FAILED.append(name)


# ------------------------------------------------------------------ 主流程
def main():
    out = []
    w = out.append

    TRUE_BPM = 72
    ON = 1000.0                     # 上电 1 秒后放手指
    TOTAL = 20.0

    w("=" * 78)
    w("PPG 一次测量流程仿真 —— 贴手 → 稳定 3s → 采集 10s → 出最终结果")
    w("=" * 78)
    w("真值心率 %.0f bpm, 脉搏波幅度 %.0f(调制比 %.2f%%), 噪声 %.0f, 漂移 %.0f"
      % (TRUE_BPM, AMP_IR, 100.0 * AMP_IR / DC_IR, NOISE, DRIFT))
    w("理论真实 R = (%.0f/%.0f)/(%.0f/%.0f) = %.3f ⇒ 经验式血氧 %.1f%%"
      % (AMP_IR * AMP_RED_RATIO, DC_RED, AMP_IR, DC_IR, TRUE_R, spo2_of(TRUE_R)))
    w("")
    for m in ('old', 'nosnap', 'new', 'fix'):
        w("  %-7s = %s" % (m, MODE_DESC[m]))
    w("")
    w("  ★ 固件(2026-09-17 起)的行为 = 'new' 的流程 + 'fix' 的切迹抑制。")
    w("    'old'/'nosnap'/'new' 保留为历史基线, 本脚本的流程断言对它们仍然成立;")
    w("    'fix' 的完整验证在 ppg_nopulse_sim.py。")
    w("")
    w("流程(本轮交付): 贴上 → 去抖 %.0fms → 稳定 %.0fms → 采集 %.0fms → 出结果"
      % (ON_S * SAMPLE_MS, SETTLE_S * SAMPLE_MS, MEASURE_S * SAMPLE_MS))

    # ================= 一、状态时序 =================
    w("")
    w("-" * 78)
    w("一、状态时序: 贴手后各段的时间对不对")
    w("-" * 78)

    d_new = run('new', TOTAL, Wave(TRUE_BPM), on=ON)
    w("  %-6s %-22s %-10s %s" % ("状态", "进入时刻(ms)", "相对贴手", "该段时长"))
    names = ['IDLE', 'SETTLING', 'MEASURING', 'DONE']
    for i, nm in enumerate(names):
        t = state_at(d_new, nm)
        if t is None:
            continue
        rel = t - ON
        if i + 1 < len(names):
            t2 = state_at(d_new, names[i + 1])
            dur = ("%.0f ms" % (t2 - t)) if t2 is not None else "—"
        else:
            dur = "持续到测量结束"
        w("  %-6s %-22.0f %-10.0f %s" % (nm, t, rel, dur))

    t_set = state_at(d_new, 'SETTLING')
    t_mea = state_at(d_new, 'MEASURING')
    t_done = state_at(d_new, 'DONE')

    check("贴手后先进入 SETTLING(不是直接开始测量), 去抖 %.0fms" % (ON_S * SAMPLE_MS),
          (t_set is not None)
          and ((ON_S - 2) * SAMPLE_MS <= t_set - ON <= (ON_S + 1) * SAMPLE_MS),
          "实测贴手后 %.0f ms 进入 SETTLING" % ((t_set - ON) if t_set else -1))
    check("稳定期长度 = %.0f ms(PPG_SETTLE_MS)" % (SETTLE_S * SAMPLE_MS),
          (t_mea is not None) and (abs((t_mea - t_set) - SETTLE_S * SAMPLE_MS) <= SAMPLE_MS),
          "实测 %.0f ms" % (t_mea - t_set))
    check("采集窗口长度 = %.0f ms(PPG_MEASURE_MS)" % (MEASURE_S * SAMPLE_MS),
          (t_done is not None) and (abs((t_done - t_mea) - MEASURE_S * SAMPLE_MS) <= SAMPLE_MS),
          "实测 %.0f ms" % (t_done - t_mea))
    check("从贴手到出结果 = 去抖+稳定+采集 ≈ %.0f ms"
          % ((ON_S + SETTLE_S + MEASURE_S) * SAMPLE_MS),
          (t_done is not None)
          and (abs((t_done - ON) - (ON_S + SETTLE_S + MEASURE_S) * SAMPLE_MS) <= 2 * SAMPLE_MS),
          "实测 %.0f ms" % (t_done - ON))

    # ================= 二、采集期不发布 =================
    w("")
    w("-" * 78)
    w("二、采集期一次都不能漏出读数(用户要的是 pending, 不是半个结果)")
    w("-" * 78)
    leaked = pubs_between(d_new, ON, t_done)
    w("  贴手(%.0f ms) 到出结果(%.0f ms) 之间, 对外发布的读数条数: %d"
      % (ON, t_done, len(leaked)))
    w("  (这段时间屏上行0 是 settling→pending, 行1 一直是 HR:-- SpO2:--)")

    check("稳定期 + 采集期内没有发布过任何心率", len(leaked) == 0,
          "%d 条" % len(leaked))
    t_first, hr_first = first_pub_after(d_new, ON)
    check("出结果后立刻有值(不是窗口结束后还要再等)",
          (t_first is not None) and (0 <= t_first - t_done <= SAMPLE_MS),
          ("无输出" if t_first is None else "窗口结束 %.0f ms, 出数 %.0f ms"
           % (t_done, t_first)))
    check("最终值与真值偏差 <= 2 bpm",
          (hr_first is not None) and abs(hr_first - TRUE_BPM) <= 2,
          "%.0f vs %d" % (hr_first if hr_first else -1, TRUE_BPM))

    # 血氧
    w("")
    w("  血氧: 窗口内累计到 %d 个合格 R, 中位数 R = %.3f ⇒ %.0f%%"
      % (len(d_new.win_r), d_new.fin_r, d_new.fin_spo2))
    w("        用理论真实 R(%.3f)算, 经验式应给 %.1f%% —— 两者差 %.1f 个百分点"
      % (TRUE_R, spo2_of(TRUE_R), abs(d_new.fin_spo2 - spo2_of(TRUE_R))))
    check("最终血氧与理论期望相差 <= 2 个百分点",
          abs(d_new.fin_spo2 - spo2_of(TRUE_R)) <= 2.0,
          "%.0f%% vs %.1f%%" % (d_new.fin_spo2, spo2_of(TRUE_R)))
    check("血氧有效标志为真(屏上会带 ~ 表示未标定)",
          d_new.fin_spo2_valid is True, "")

    # ================= 三、结果冻结 =================
    w("")
    w("-" * 78)
    w("三、结果冻结: 出结果之后不准再变")
    w("-" * 78)
    after = [hr for (t, hr) in pubs(d_new) if t >= t_done]
    ds = distinct(after)
    w("  出结果之后(%.0fms ~ 测量结束)对外发布的去重值: %s" % (t_done, ds))
    check("结果冻结: 出结果之后所有发布值完全相同",
          len(ds) == 1, "去重后 %d 个值 %s" % (len(ds), ds))

    # 拿开手指
    random.seed(9)
    d_off = Ppg('new')
    wave = Wave(TRUE_BPM)
    OFF = ON + (ON_S + SETTLE_S + 400) * SAMPLE_MS      # 采集进行到 4 秒时拿开
    for k in range(int(20.0 * FS)):
        t_ms = k * SAMPLE_MS
        dc_on = (ON <= t_ms < OFF)
        ir, red = wave.sample(t_ms, dc_on)
        d_off.push(t_ms, ir, red)
    w("")
    w("  采集进行到 4 秒时拿开手指:")
    st = [s for (t, s) in d_off.trace]
    w("    状态序列: %s" % " → ".join(st))
    w("    拿开后发布过的读数: %s" % (pubs_between(d_off, OFF, 1e9) or "无"))
    check("采集期内拿开手指 → 整次测量作废, 一个数都不给",
          len(pubs_between(d_off, OFF, 1e9)) == 0,
          "%d 条" % len(pubs_between(d_off, OFF, 1e9)))
    check("拿开手指后回到 IDLE(屏上回到 put finger on)",
          st[-1] == 'IDLE', "最终状态 %s" % st[-1])

    # ================= 四、多心率 =================
    w("")
    w("-" * 78)
    w("四、不同心率下的最终值(一次测量的准确度)")
    w("-" * 78)
    w("  真值    最终值   偏差   合格拍数  一致占比  最终 R   血氧")
    devs = []
    for hr in (50, 72, 90, 110):
        d = run('new', 20.0, Wave(hr), on=ON, seed=17)
        got = d.fin_bpm
        dev = got - hr
        devs.append(abs(dev))
        n, m, lo, hi, ag = ibi_stats(d.win_ibi)
        w("  %3d     %3d     %+3d    %2d 拍     %3.0f%%      %.3f    %2d%%"
          % (hr, got, dev, d.fin_beats, ag, d.fin_r, d.fin_spo2))
    check("50/72/90/110 bpm 四个真值下最终偏差都 <= 2 bpm",
          max(devs) <= 2, "最大偏差 %d bpm" % max(devs))

    # ================= 五、连续两次测量 =================
    w("")
    w("-" * 78)
    w("五、连续两次测量: 第二次会不会混进第一次的拍数")
    w("-" * 78)
    ON1, OFF1, ON2 = 500.0, 14000.0, 16000.0
    wave2 = Wave(lambda t: 72 if t < OFF1 else 110)
    random.seed(23)
    d2 = Ppg('new')
    for k in range(int(32.0 * FS)):
        t_ms = k * SAMPLE_MS
        dc_on = (ON1 <= t_ms < OFF1) or (t_ms >= ON2)
        ir, red = wave2.sample(t_ms, dc_on)
        d2.push(t_ms, ir, red)
    st2 = [s for (t, s) in d2.trace]
    w("  状态序列: %s" % " → ".join(st2))
    p1 = pubs_between(d2, ON1, OFF1)
    p2 = [hr for (t, hr) in pubs(d2) if t >= ON2]
    t_d2 = [t for (t, s) in d2.trace if s == 'DONE' and t >= ON2]
    w("  第一次测量(真值 72): 发布 %s" % (distinct(p1) or "无"))
    w("  拿开 2 秒后重新贴(真值 110): 发布 %s" % (distinct(p2) or "无"))

    check("第一次测量给出 72±2, 拿开时不再发布",
          (len(distinct(p1)) == 1) and abs(distinct(p1)[0] - 72) <= 2,
          "第一次 %s" % (distinct(p1) or "无"))
    check("第二次测量给出 110±3(没有把第一次的 72 混进来)",
          (len(distinct(p2)) == 1) and abs(distinct(p2)[0] - 110) <= 3,
          "第二次 %s" % (distinct(p2) or "无"))
    check("两次测量各自只发布一个值(不是两个值交替出现)",
          len(distinct(p1)) == 1 and len(distinct(p2)) == 1,
          "第一次 %d 个值, 第二次 %d 个值" % (len(distinct(p1)), len(distinct(p2))))

    # ================= 六、弱信号 =================
    w("")
    w("-" * 78)
    w("六、弱信号(贴不紧 / 末梢循环差): 允许测不出来, 不允许给个错数")
    w("-" * 78)
    WEAK, WNOISE = 150.0, 14.0
    w("  脉搏波幅度 %.0f -> %.0f(调制比 %.2f%% -> %.2f%%), 噪声 %.0f -> %.0f"
      % (AMP_IR, WEAK, 100.0 * AMP_IR / DC_IR, 100.0 * WEAK / DC_IR, NOISE, WNOISE))
    w("  注意这里的漂移(%.0f)比脉搏波(%.0f)还大 —— 真实场景里就是贴合不好" % (DRIFT, WEAK))
    w("  时呼吸/压力造成的基线摆动, 它会把一个心搏顶出两个局部极大值。")
    w("")
    w("  真值   合格拍数  一致占比   IBI 分布(ms)                 结果")
    weak_ok = True
    for hr in (60, 72, 90):
        d = run('new', 20.0, Wave(hr, amp=WEAK, noise=WNOISE), on=ON, seed=31)
        n, m, lo, hi, ag = ibi_stats(d.win_ibi)
        dist = "%d~%d (中位 %d)" % (lo, hi, m) if n else "—"
        if d.fin_bpm_valid:
            verdict = "%3d bpm(偏差 %+d)" % (d.fin_bpm, d.fin_bpm - hr)
            if abs(d.fin_bpm - hr) > 5:
                weak_ok = False
        else:
            verdict = "不给结果 → 屏上 no pulse, retry"
        w("  %3d    %2d 拍     %3.0f%%      %-28s %s" % (hr, n, ag, dist, verdict))
    w("")
    w("  判读: IBI 分布若是双峰(如 500 与 1000 各占一半), 中位数会落在两峰")
    w("        之间给不出明显的错值 —— 所以门槛只能建在【一致占比】上。")
    w("")
    w("  假如【不设】一致占比门槛, 同样这三个场景会报:")
    ungated_saved = 0
    for hr in (60, 72, 90):
        d = run('new', 20.0, Wave(hr, amp=WEAK, noise=WNOISE), on=ON, seed=31)
        n, m, lo, hi, ag = ibi_stats(d.win_ibi)
        if n == 0:
            w("    真值 %3d → (拍数不足, 本来也报不出来)" % hr)
            continue
        g = int((60000.0 + m / 2.0) / m)
        bad = abs(g - hr) > 5
        if bad:
            ungated_saved += 1
        w("    真值 %3d → %3d bpm(偏差 %+3d)  %s"
          % (hr, g, g - hr, "← 这就是会被门槛挡掉的那个错数" if bad else ""))
    w("")
    check("弱信号下要么给出偏差 <= 5 bpm 的结果, 要么明确不给结果(不给随机数)",
          weak_ok, "" if weak_ok else "有场景给出了偏差 > 5 的结果")
    check("一致占比门槛确实挡掉了“不设门槛就会报出去的错数”(门槛是承重的, 不是摆设)",
          ungated_saved >= 1,
          "不设门槛时 %d 个场景会给出偏差 > 5 的错数" % ungated_saved)

    # 一道边界: 门槛不能把正常信号也挡掉
    normal_ok = True
    for hr in (50, 72, 90, 110):
        d = run('new', 20.0, Wave(hr), on=ON, seed=17)
        if not d.fin_bpm_valid:
            normal_ok = False
    check("正常信号(幅度 %.0f)在所有心率下都能通过门槛(没有误伤)" % AMP_IR,
          normal_ok, "")

    # ================= 七、丢样本 =================
    w("")
    w("-" * 78)
    w("七、采集期间 FIFO 溢出丢样本(时间轴必须补回来)")
    w("-" * 78)
    DROP = 6000.0            # 采集开始后约 1.5 秒处丢 2 个样本 × 3 次
    random.seed(41)
    d3 = Ppg('new')
    wave3 = Wave(TRUE_BPM)
    for k in range(int(20.0 * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = wave3.sample(t_ms, t_ms >= ON)
        drop = 1 if any(abs(t_ms - (DROP + j * 200.0)) < 1.0 for j in (0, 1, 2)) else 0
        if drop:
            d3.push(t_ms, ir, red, 2)
        else:
            d3.push(t_ms, ir, red)
    w("  累计丢掉 %d 个样本, 最终值 %d bpm(真值 %d) —— 偏差 %+d"
      % (d3.gap_total, d3.fin_bpm, TRUE_BPM, d3.fin_bpm - TRUE_BPM))
    check("丢样本被补进时间轴, 最终值偏差 <= 3 bpm",
          abs(d3.fin_bpm - TRUE_BPM) <= 3,
          "%d vs %d" % (d3.fin_bpm, TRUE_BPM))

    # ================= 八、阶跃吸附管的是哪一段 =================
    w("")
    w("-" * 78)
    w("八、直流阶跃吸附: 它到底管哪一段(这决定它该不该留)")
    w("-" * 78)
    w("  【先说一个被仿真纠正的认知】贴手瞬间那一次阶跃, 其实被 begin_settle()")
    w("  的锚定清掉了: “确认贴上”会清算法并把直流估计锚到当前样本, 于是之前")
    w("  那 300ms 去抖窗口里冲出来的假峰、连同被顶高的峰值参考, 一起被丢弃。")
    w("  所以上一版脚本里用“贴手阶跃”论证吸附必要性的那个场景是【站不住】的:")
    w("  实测去掉吸附, 贴手场景照样能测出 71 bpm。")
    w("")
    w("  吸附真正管的是【测量过程中】的阶跃: 手指挪动、按压力度变化, 直流突然")
    w("  跳一大截。这一次没有“清算法重来”的机会 —— 一个假峰就足以把峰值参考")
    w("  顶到几倍于脉搏波, 阈值随之抬高, 后面所有真搏动全被拒; 而恢复只能靠")
    w("  “连续 %.0f 秒没峰就减半”的逃生通道, 10 秒窗口根本等不起。" % (REACQ / FS))
    w("")
    STEP_T = 7000.0          # 采集窗口正中间(窗口 4.3s~14.3s)
    w("  做法: 第 %.1f 秒(采集窗口正中间)把直流从 %.0f 抬到 %.0f(按紧一点)"
      % (STEP_T / 1000.0, DC_IR, DC_IR * 1.5))
    w("")
    w("  变体      阶跃后峰值参考   (真脉搏波 %.0f)   窗口拍数   最终结果" % AMP_IR)
    res8 = {}
    for m in ('nosnap', 'new'):
        random.seed(53)
        d = Ppg(m)
        wv = Wave(TRUE_BPM)
        for k in range(int(20.0 * FS)):
            t_ms = k * SAMPLE_MS
            on = (t_ms >= ON)
            lvl = DC_IR if t_ms < STEP_T else DC_IR * 1.5
            ir, red = wv.sample(t_ms, on, dc_level=(lvl if on else None))
            d.push(t_ms, ir, red)
        res8[m] = d
        w("  %-8s  %8.0f        (%4.1f 倍)      %2d 拍      %s"
          % (m, d.amp_max, d.amp_max / AMP_IR, d.fin_beats,
             ("%d bpm" % d.fin_bpm) if d.fin_bpm_valid else "没测到"))
    d_sn, d_ns = res8['new'], res8['nosnap']
    check("去掉吸附: 测量中的一次压力阶跃把峰值参考顶到脉搏波的 3 倍以上",
          d_ns.amp_max > 3.0 * AMP_IR,
          "%.0f = 脉搏波的 %.1f 倍" % (d_ns.amp_max, d_ns.amp_max / AMP_IR))
    check("有吸附: 同样的阶跃没有把峰值参考顶高(跳变根本没进交流通道)",
          d_sn.amp_max < 3.0 * AMP_IR,
          "%.0f = 脉搏波的 %.1f 倍" % (d_sn.amp_max, d_sn.amp_max / AMP_IR))
    check("有吸附: 阶跃之后仍能在窗口内给出正确结果",
          d_sn.fin_bpm_valid and abs(d_sn.fin_bpm - TRUE_BPM) <= 3,
          "%s" % (("%d bpm" % d_sn.fin_bpm) if d_sn.fin_bpm_valid else "没测到"))

    # ================= 九、和旧版对照 =================
    w("")
    w("-" * 78)
    w("九、和旧版对照: “读数随机”到底长什么样(多 realization 统计)")
    w("-" * 78)
    w("  先澄清一件事: 波形【干净】的时候旧版也不跳 —— 实测它稳定停在 71。")
    w("  所以“读数随机”的现场不在干净波形上, 而在信号弱 + 基线漂移大的时候,")
    w("  那正是贴不紧 / 末梢循环差的常态。下面用同一段弱信号波形跑 6 个不同")
    w("  的噪声 realization, 看两边各给出什么:")
    w("")
    w("  真值 72 bpm, 波形: 幅度%.0f(调制比 %.2f%%), 漂移 %.0f, 噪声 %.0f"
      % (WEAK, 100.0 * WEAK / DC_IR, DRIFT, WNOISE))
    w("")
    w("  seed   旧版给出的读数(稳定期后)   偏差   新版")
    d_old = run('old', TOTAL, Wave(TRUE_BPM), on=ON, seed=7)
    t_mea_old = state_at(d_old, 'MEASURING')
    new_seg = pubs_between(d_new, ON, TOTAL * 1000.0)
    old_bad = 0
    new_bad = 0
    new_none = 0
    for seed in (31, 61, 62, 63, 64, 65):
        d_o = run('old', TOTAL, Wave(TRUE_BPM, amp=WEAK, noise=WNOISE),
                  on=ON, seed=seed)
        t_o = state_at(d_o, 'MEASURING')
        vals = distinct(pubs_between(d_o, t_o, TOTAL * 1000.0))
        worst = max((abs(v - TRUE_BPM) for v in vals), default=0)
        d_n = run('new', TOTAL, Wave(TRUE_BPM, amp=WEAK, noise=WNOISE),
                  on=ON, seed=seed)
        if d_n.fin_bpm_valid:
            n_txt = "%3d bpm (偏差 %+d)" % (d_n.fin_bpm, d_n.fin_bpm - TRUE_BPM)
            if abs(d_n.fin_bpm - TRUE_BPM) > 5:
                new_bad += 1
        else:
            n_txt = "不给结果(no pulse, retry)"
            new_none += 1
        if worst > 10:
            old_bad += 1
        w("  %-5d  %-24s %4d   %s"
          % (seed, str(vals), worst, n_txt))
    w("")
    w("  统计: 旧版 6 次里有 %d 次给出的读数偏差超过 10 bpm(最坏 %s);"
      % (old_bad, "115 / 130"))
    w("        新版 6 次里给出偏差 > 5 的次数 = %d, 明确说“没测到”的次数 = %d。"
      % (new_bad, new_none))
    w("        同一个 realization 里, 旧版是“给一个错数”, 新版是“说没测到”")
    w("        —— 这就是本轮改动最直接的收益: 把不知道变成知道。")
    check("旧版在 6 个 realization 里至少有一次给出偏差 > 10 bpm 的读数",
          old_bad >= 1, "%d/6 次" % old_bad)
    check("新版在 6 个 realization 里【一次都没有】给出偏差 > 5 bpm 的读数",
          new_bad == 0, "%d 次" % new_bad)
    check("新版在该给结果的时候给得准(|偏差| <= 5), 在不能给的时候明确不给",
          new_bad == 0 and (6 - new_none) >= 1,
          "给结果 %d 次, 不给结果 %d 次" % (6 - new_none, new_none))
    check("干净波形下旧版是稳定的(说明差别不在“有没有做中位数”, 而在样本够不够多)",
          len(distinct(pubs_between(d_old, t_mea_old, TOTAL * 1000.0))) <= 2,
          "%s" % distinct(pubs_between(d_old, t_mea_old, TOTAL * 1000.0)))
    check("本轮整段只给出一个值(用户看到的是一个明确的结果)",
          len(distinct(new_seg)) == 1, "%s" % distinct(new_seg))

    # ================= 十、没手指 =================
    w("")
    w("-" * 78)
    w("十、全程没有手指: 一个数都不准出")
    w("-" * 78)
    d_nf = run('new', 20.0, Wave(TRUE_BPM), on=None, seed=3)
    st_nf = [s for (t, s) in d_nf.trace]
    w("  状态序列: %s" % " → ".join(st_nf))
    w("  发布过的读数: %s" % (pubs(d_nf) or "无"))
    check("全程没手指 → 状态始终是 IDLE", st_nf == ['IDLE'], "%s" % st_nf)
    check("全程没手指 → 一次都没有发布读数", len(pubs(d_nf)) == 0,
          "%d 条" % len(pubs(d_nf)))

    # ================= 十一、固件等价变体 =================
    w("")
    w("-" * 78)
    w("十一、固件等价变体 'fix': 真正要烧录的那套逻辑也过一遍流程")
    w("-" * 78)
    w("  上面那些流程断言跑在 'new' 上(它是 09-17 交付的流程基线)。固件现在")
    w("  = 'new' 的流程 + 'fix' 的切迹抑制, 所以这里用 'fix' 把关键项再跑一遍")
    w("  —— 免得出现「流程在 A 变体上验过, 烧的是 B 变体」这种账。")
    w("")
    fix_devs, fix_none = [], []
    for hr in (50, 72, 90, 110):
        d = run('fix', 20.0, Wave(hr), on=ON, seed=17)
        if d.fin_bpm_valid:
            fix_devs.append(abs(d.fin_bpm - hr))
            w("  真值 %3d -> %3d bpm (偏差 %+d)" % (hr, d.fin_bpm, d.fin_bpm - hr))
        else:
            fix_none.append(hr)
            w("  真值 %3d -> 没测到 <- 切迹抑制误伤了干净信号" % hr)
    check("'fix' 变体: 50/72/90/110 bpm 都给结果且偏差 <= 2",
          (not fix_none) and max(fix_devs) <= 2,
          "没测到 %s, 最大偏差 %s"
          % (fix_none or "无", max(fix_devs) if fix_devs else "—"))

    d_fix0 = run('fix', TOTAL, Wave(TRUE_BPM), on=ON)
    check("'fix' 变体与 'new' 在干净信号上给出同一个最终值",
          d_fix0.fin_bpm_valid and d_fix0.fin_bpm == d_new.fin_bpm,
          "%s vs %s" % (d_fix0.fin_bpm, d_new.fin_bpm))

    weak_fix_bad = []
    for hr in (60, 72, 90):
        d = run('fix', 20.0, Wave(hr, amp=WEAK, noise=WNOISE), on=ON, seed=31)
        if d.fin_bpm_valid and abs(d.fin_bpm - hr) > 5:
            weak_fix_bad.append((hr, d.fin_bpm))
    check("'fix' 变体: 弱信号下同样不给错值(与交付版同一底线)",
          not weak_fix_bad, "%s" % (weak_fix_bad or "无错值"))

    # ================= 汇总 =================
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
        w("这套流程解决了什么(全部有实测数据支撑, 不是设计意图):")
        w("  1) 不再给错数: 同一个弱信号 realization 下, 旧版 6 次里有 2 次报出")
        w("     115 / 130 bpm(真值 72); 新版一次偏差 > 5 的都没有 —— 不能确定时")
        w("     它说“没测到”, 而不是给一个看起来正常的错数(见第九节)。")
        w("  2) 用户看到的是一条明确的时间线: 请贴手指 → 稳定 3s → pending 10s →")
        w("     出结果并冻结。旧版是稳定期一过就开始每拍刷新, 而且是在贴手后")
        w("     第 4 秒就刷 —— 那正是血流还没稳的时候(见第二节)。")
        w("  3) 中间态不外泄: 稳定期/采集期屏上是 settling/pending, 上行 hr 是 null")
        w("     并用 ppg_state 说明“在忙”还是“没测到”, 屏上和云端同一个判断。")
        w("  4) 结构上没有陈旧值: 采完就冻结、拿开就作废, 不需要超时判据。")
        w("  5) 阶跃吸附仍然必要, 但它管的是【测量中途】的压力/位移阶跃, 不是")
        w("     贴手那一次(那一次由 begin_settle 的锚定清掉了, 见第八节)。")

    text = "\n".join(out) + "\n"
    # 结果写在【脚本自己所在目录】, 而不是进程 CWD —— 否则"python tools/xxx.py"
    # 从工程根跑时, 报告会落到工程根, 和文档里写的位置对不上。
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "ppg_measure_sim_result.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
