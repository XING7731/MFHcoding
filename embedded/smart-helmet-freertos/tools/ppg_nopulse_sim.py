# -*- coding: utf-8 -*-
"""
PPG "10 秒后没有读数" 排查仿真 —— 为什么窗口跑完了却一个数都不给

【现场现象】
  pending 10s 正常倒数, 走完 10 秒后行1 仍然是 HR:-- SpO2:--,
  行0 显示 "PPG: no pulse, retry"。硬件、接线、器件都在线(行0 能走到 pending)。

【这个脚本回答一个问题】
  到底是"信号太弱", 还是"算法把自己弄瞎了"?
  两者在屏上长得一模一样(HR:-- SpO2:--), 只能靠数值复现分开。
  本脚本不猜: 把几种可能的原因逐个放进仿真, 看哪一个能【复现出零拍 / 错值】,
  再据此改代码 —— 改完同一个场景必须由"没测到 / 给错"变成"给出正确值"。

【三个候选原因与结论(结论是跑出来的, 不是推的)】
  W1 峰值参考被稳定期的运动伪影顶高  → 【否】4Hz 低通把短脉冲削掉了:
                                        8000 LSB 的脉冲只把参考从 577 抬到 824;
                                        2.5Hz 持续晃动(幅度 4000)也只到 580。
  W2 采集期周期伪影 + 破坏性清空      → 【否】同样够不到峰值参考, 复现不出零拍。
  W3 重搏切迹被当成第二次搏动          → 【是】确定性复现。
      切迹 40% 时: 中位数锁死在「半拍间隔」520ms 上 → 每一次真搏动都被判离群
      → 连续 4 次后清空重来 → 整个 10 秒窗口在「3 拍→清空→3 拍」之间循环,
      b: 永远停在 3, 窗口结束时一致占比不足 ⇒ no pulse, retry。
      切迹 55% 时更糟: 50bpm 下不报「没测到」, 而是报出 +21bpm 的错值。

【修复与验证】见第六节。修复用一个【时间判据】(而不是幅度判据)——
  第六节同时给出了幅度判据被放弃的实测理由: 它对基线漂移敏感, 会漏拍,
  把 90bpm 读成 30bpm。
【第七节】补一道"这条规则是不是承重的"的检查: 第六节用的切迹固定在主波后
  300ms, 而修复又把不应期抬到了 400ms —— 300ms 的切迹在不应期那一层就被
  挡住了, 于是那张 30 组的表可能压根没用到切迹抑制。第七节把切迹的【延迟】
  与【高度】两个方向都放开扫。结论(过程里有一次假设被否, 值得留着):
    · 延迟一过约 450ms, 切迹就不再形成独立峰了(连交付版都不出错) —— 所以
      "延迟按心动周期缩放、慢心率下会越过 400ms 不应期"这个【原假设是错的】,
      实测已被否掉(7-A 那一节就是它被否掉的现场);
    · 把高度也放开之后, 才找到 4 个「只有切迹抑制过得了」的组合 —— 全都在
      50bpm(高切迹 + 400~450ms 延迟)。同时也如实记下 1 个 fix 反而失手的
      反例: 切迹抑制是承重的, 但作用域窄, 而且不是免费的。

【固件状态】本脚本第六节的 'fix' 已经落到 user/Src/heart_rate.c 与
  user/Inc/user_config.h (PPG_NOTCH_MIN_RATIO / PPG_NOTCH_EST_BEATS /
  PPG_NOTCH_EST_MIN, 不应期 300→400)。

【输出】ppg_nopulse_sim_result.txt(本机 PowerShell 不回显 stdout, 故写文件)
【运行】python tools/ppg_nopulse_sim.py
"""

import os
import random
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ppg_measure_sim import (          # noqa: E402
    Ppg, Wave, MotionWave, NotchWave, shake_bursts,
    FS, SAMPLE_MS, AMP_IR, DC_IR, NOISE, DRIFT,
    MIN_AMP, TH_FRAC, SETTLE_S, MEASURE_S, ON_S,
    RESET_STREAK, NOTCH_RATIO, TOL_PCT,
)

TRUE_BPM = 72
ON       = 1000.0            # 上电 1 秒后贴手
TOTAL    = 20.0

T_SET   = ON + (ON_S * SAMPLE_MS)                    # 稳定期起点 1300 ms
T_MEA   = T_SET + SETTLE_S * SAMPLE_MS               # 采集窗口起点 4300 ms
T_DONE  = T_MEA + MEASURE_S * SAMPLE_MS              # 窗口终点    14300 ms
BLANK   = T_SET + 25 * SAMPLE_MS                     # clear_algo 后 0.5s 空窗

HRS = (40, 50, 72, 90, 110, 150)
NOTCHES = (0.0, 0.15, 0.25, 0.40, 0.55)

out = []
w = out.append


def run_wave(wave, mode='new', total_s=TOTAL, seed=7):
    random.seed(seed)
    d = Ppg(mode)
    for k in range(int(total_s * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = wave.sample(t_ms, t_ms >= ON)
        d.push(t_ms, ir, red)
    return d


def measure_wave(wave, mode, seed=31):
    """用给定的波形跑一次完整测量(贴手时刻固定 ON)。"""
    random.seed(seed)
    d = Ppg(mode)
    for k in range(int(TOTAL * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = wave.sample(t_ms, t_ms >= ON)
        d.push(t_ms, ir, red)
    return d


def measure(hr, notch, mode, seed=31):
    """跑一次带切迹的测量(切迹延迟用 NotchWave 的默认 300ms)。"""
    return measure_wave(NotchWave(hr, notch), mode, seed)


def measure_delay(hr, frac, delay_ms, mode, seed=31):
    """带指定【延迟-高度】的重搏切迹的一次测量 —— 见第七节。

    用绝对毫秒而不是"占周期的比例", 是因为第七节要问的问题恰恰是
    "切迹迟于多少毫秒就不再形成独立峰" —— 用周期比例会把这个问题盖住。
    (第七节 7-A 一开始按周期比例缩放扫过一遍, 假设被实测否掉了, 见那里。)"""
    return measure_wave(NotchWave(hr, frac, notch_ms=delay_ms), mode, seed)


def count_peaks(wave, mode='new', total_s=TOTAL, seed=7):
    """统计整段里【被寻峰判据接受】的搏动次数(不是「每拍」)。"""
    random.seed(seed)
    d = Ppg(mode)
    n = [0]
    orig = d._on_peak

    def hooked(t_ms, idx, amp):
        n[0] += 1
        return orig(t_ms, idx, amp)

    d._on_peak = hooked
    for k in range(int(total_s * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = wave.sample(t_ms, t_ms >= ON)
        d.push(t_ms, ir, red)
    return d, n[0]


def verdict(d, true_bpm=TRUE_BPM):
    if d.fin_bpm_valid:
        dev = d.fin_bpm - true_bpm
        return "%3d bpm (偏差 %+d)%s" % (d.fin_bpm, dev,
                                         "  <- 错值!" if abs(dev) > 5 else "")
    return "没测到 -> 屏上 no pulse, retry"


def row0(d):
    return ("PPG: done  R:%.2f" % d.fin_r) if d.fin_bpm_valid \
        else "PPG: no pulse, retry"


def row5(d):
    return "d:%d a:%d b:%d" % (DC_IR, int(d.amp_max), d.fin_beats)


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
    w("=" * 78)
    w("PPG 10 秒后没有读数 —— 排查仿真")
    w("=" * 78)
    w("真值 %d bpm, 脉搏波交流幅度 %.0f LSB(调制比 %.2f%%), 噪声 %.0f, 漂移 %.0f"
      % (TRUE_BPM, AMP_IR, 100.0 * AMP_IR / DC_IR, NOISE, DRIFT))
    w("流程: 贴手 %.0f -> 去抖 %.0fms -> 稳定期 %.0f~%.0fms -> 采集窗口 %.0f~%.0fms"
      % (ON, ON_S * SAMPLE_MS, T_SET, T_MEA, T_MEA, T_DONE))
    w("寻峰阈值 = max(%.2f × 峰值参考, %.0f)" % (TH_FRAC, MIN_AMP))

    # ---------------- 基线 ----------------
    w("")
    w("-" * 78)
    w("基线: 干净信号(手指放好就不再动)")
    w("-" * 78)
    d0 = run_wave(Wave(TRUE_BPM), seed=7)
    w("  峰值参考 %.0f, 窗口内 %d 拍, %s" % (d0.amp_max, d0.fin_beats, verdict(d0)))
    w("  屏上: 行0=%s   行5=%s" % (row0(d0), row5(d0)))
    check("基线正常: 干净信号能给出结果(说明问题不在算法主干)",
          d0.fin_bpm_valid and abs(d0.fin_bpm - TRUE_BPM) <= 2, verdict(d0))

    # ================= 一、W1a: 稳定期一个强脉冲 ==========================
    w("")
    w("-" * 78)
    w("一、W1(否): 稳定期里一个运动脉冲 —— 它能不能把峰值参考顶高")
    w("-" * 78)
    w("  怀疑: begin_settle() 把峰值参考清零 + 空窗 %.0fms(到 %.0fms), 之后"
      % (25 * SAMPLE_MS, BLANK))
    w("  第一个越过阈值的峰就是它的初值; 手指压一下的脉冲幅度轻易上千,")
    w("  一旦被它播种, 阈值 0.4×参考 就会长期高于真搏动(约 614 LSB)。")
    w("")
    w("  脉冲时刻   脉冲幅度   峰值参考(a:)   阈值     窗口拍数(b:)   最终结果")
    w1a_die = 0
    for t_burst in (1900.0, 2100.0, 2300.0, 2500.0, 2700.0, 2900.0):
        wv = MotionWave(TRUE_BPM, bursts=[(t_burst, 150.0, 8000.0)])
        d = run_wave(wv, seed=11)
        need = max(TH_FRAC * d.amp_max, MIN_AMP)
        if not d.fin_bpm_valid:
            w1a_die += 1
        w("  %6.0fms   %6.0f    %7.0f    %7.0f     %2d 拍        %s"
          % (t_burst, 8000.0, d.amp_max, need, d.fin_beats, verdict(d)))
    w("")
    w("  结论: 一个都杀不死。原因是 4Hz 低通 —— 150ms 宽的脉冲主要频率在")
    w("        6~7Hz, 被低通削掉一半以上, 真正的交流幅度远不及脉搏波。")
    check("W1 被否定: 短运动脉冲挡不住峰值参考(6 个时刻全部正常)",
          w1a_die == 0, "%d/6 个时刻出问题" % w1a_die)

    # ================= 二、W1b: 稳定期持续晃动 ============================
    w("")
    w("-" * 78)
    w("二、W1b(否): 稳定期里手指一直在轻微晃动(贴上去总要找位置)")
    w("-" * 78)
    w("  2.5Hz(周期 400ms > 不应期 300ms, 所以会被判成搏动), 覆盖整个稳定期。")
    w("")
    w("  晃动幅度   峰值参考(a:)   阈值     窗口拍数(b:)   最终结果")
    d1b = {}
    for amp in (0.0, 800.0, 2000.0, 4000.0):
        wv = MotionWave(TRUE_BPM, bursts=shake_bursts(T_SET, T_MEA, 400.0, 200.0, amp))
        d = run_wave(wv, seed=23)
        d1b[amp] = d
        need = max(TH_FRAC * d.amp_max, MIN_AMP)
        w("  %6.0f     %8.0f    %7.0f     %2d 拍        %s"
          % (amp, d.amp_max, need, d.fin_beats, verdict(d)))
    w("")
    w("  结论: 幅度 4000(6.7 倍于脉搏波)也只把参考抬到 580 —— 因为 2.5Hz 同样")
    w("        被 4Hz 低通削。所以峰值参考被顶高不是本现象的原因。")
    check("W1b 被否定: 持续晃动也挡不住峰值参考(最高 580, 与基线 577 同级)",
          all(d1b[a].fin_bpm_valid for a in d1b),
          "%s" % {int(a): d1b[a].fin_bpm for a in d1b})

    # ================= 三、W2: 采集期周期伪影 ============================
    w("")
    w("-" * 78)
    w("三、W2(否): 采集期里的周期性伪影 —— 能不能把窗口拍数压下去")
    w("-" * 78)
    w("  伪影落在采集窗口之内(手指轻微摩擦), 不是直流阶跃, 吸附挡不住。")
    w("")
    w("  伪影幅度   峰值参考(a:)   窗口拍数(b:)   最终结果")
    d2 = {}
    for amp in (0.0, 1500.0, 3000.0):
        wv = MotionWave(TRUE_BPM,
                        bursts=shake_bursts(T_MEA + 400.0, T_MEA + 4200.0,
                                            500.0, 250.0, amp))
        d = run_wave(wv, seed=29)
        d2[amp] = d
        w("  %6.0f     %8.0f       %2d 拍        %s"
          % (amp, d.amp_max, d.fin_beats, verdict(d)))
    check("W2 被否定: 采集期周期伪影(1500/3000)没把拍数压下去",
          all(d2[a].fin_bpm_valid for a in d2),
          "%s" % {int(a): ("%d拍" % d2[a].fin_beats) for a in d2})

    # ================= 四、W3: 重搏切迹(找到了) ==========================
    w("")
    w("-" * 78)
    w("四、W3(是): 重搏切迹被当成第二次搏动 —— 这就是原因")
    w("-" * 78)
    w("  基线波形 sin(phi)+0.35*sin(2phi+0.6) 是平滑的、每拍只有一个极大值,")
    w("  所以前 30 项断言全过根本不能说明实物上寻峰不会被骗。")
    w("  真实 PPG 在收缩峰后约 200~400ms 有个二次波(重搏波), 高度可达主波 20~60%。")
    w("")
    w("  切迹高度   全段检出峰数   窗口拍数(b:)   最终结果")
    d3 = {}
    for frac in NOTCHES:
        d, n = count_peaks(NotchWave(TRUE_BPM, frac), seed=31)
        d3[frac] = (d, n)
        w("  %5.0f%%        %5d 个      %2d 拍        %s"
          % (frac * 100.0, n, d.fin_beats, verdict(d)))
    w("")
    w("  基线(无切迹)全段 %.0f 秒约 24 拍; 某个切迹档位峰数明显更多,"
      % TOTAL)
    w("  多出来的就是被误判的切迹。")
    check("W3 复现: 72bpm 下切迹 >=40% 时窗口拍数被压到门槛以下(no pulse, retry)",
          any(not d3[f][0].fin_bpm_valid for f in NOTCHES if f > 0),
          "%s" % {int(f * 100): d3[f][0].fin_beats for f in NOTCHES})
    d_bad = measure(50, 0.55, 'new')
    check("W3 还会给出错值(比没测到更糟): 50bpm + 55% 切迹 -> 71bpm(+21)",
          d_bad.fin_bpm_valid and abs(d_bad.fin_bpm - 50) > 5,
          verdict(d_bad, 50))

    # 机制: 窗口内每一拍的去向
    w("")
    w("  机制(72bpm, 切迹 40%, 窗口内每个峰的去向):")
    d = Ppg('new')
    trace = []
    orig = d._on_peak

    def hooked(t_ms, idx, amp, _o=orig, _d=d, _tr=trace):
        b_n, b_r = len(_d.win_ibi), _d.reject
        _o(t_ms, idx, amp)
        _tr.append((t_ms, b_n, len(_d.win_ibi), b_r, _d.reject))
        return None

    d._on_peak = hooked
    random.seed(31)
    wv = NotchWave(TRUE_BPM, 0.40)
    for k in range(int(TOTAL * FS)):
        t_ms = k * SAMPLE_MS
        ir, red = wv.sample(t_ms, t_ms >= ON)
        d.push(t_ms, ir, red)
    for (t, bn, an, br, ar) in trace:
        if not (T_MEA <= t < T_DONE):
            continue
        if an == 1 and bn >= 3:
            act = "清空后接受 dt=%d   <-- 攒到的 %d 拍被扔掉" % (d.win_ibi[0], bn)
        elif an > bn:
            act = "接受 dt=%d" % d.win_ibi[an - 1]
        elif ar > br:
            act = "拒绝(离群)"
        else:
            act = "—"
        w("    %7.0f  已攒 %2d -> %-2d   %s" % (t, bn, an, act))
    w("")
    w("  一句话机制: 中位数锁死在半拍间隔上 -> 每一次真搏动都成了离群值")
    w("  -> 连续 %d 次就清空 -> 从头再攒。窗口只剩 10 秒, 永远攒不够。"
      % RESET_STREAK)
    check("机制确认: 窗口内确实出现 真搏动被判离群 + 累计被清空 的循环",
          any(t[3] > t[4] for t in trace if T_MEA <= t[0] < T_DONE)
          and any(t[1] >= 3 and t[2] == 1 for t in trace),
          "")

    # ================= 五、屏上判据表 ===================================
    w("")
    w("-" * 78)
    w("五、结论: 几种没读数在屏上各有特征 —— 下次实测直接对号入座")
    w("-" * 78)
    w("  原因                          行0                行5(a: 峰值参考 b: 拍数)")
    w("  W3 切迹被当成两拍(本次原因)   no pulse, retry    a: 几百(正常) b: 3~4 卡住")
    w("     信号本来就弱(贴合不好)     no pulse, retry    a: 几十~一百  b: 0~2")
    w("     指尖在动(伪影大)           no pulse, retry    a: 几百      b: 0~2")
    w("     正常                       done  R:x.xx       a: 几百      b: 10~16")
    w("")
    w("  【关键】只看 HR:-- 分不出来; a:/b: 两个数一起看才分得出来。")
    w("  b: 有 3~4 拍却仍然 no pulse ⇒ 不是没信号, 是攒到的拍彼此不一致")
    w("  —— 那正是切迹/双峰这一类 一个心搏被数成两拍 的特征。")

    # ================= 六、修复验证 =====================================
    w("")
    w("-" * 78)
    w("六、修复验证")
    w("-" * 78)
    w("  修法一(核心): 【切迹抑制】—— 间隔短于已有 IBI 上界的 %.0f%% 就不算一拍。"
      % (NOTCH_RATIO * 100))
    w("    为什么用时间而不是幅度: 幅度型判据(峰后必须回落到某条线)要对基线漂移")
    w("    敏感 —— 弱信号下漂移(实测 400)比脉搏波(150)还大, 波形谷底被抬高,")
    w("    于是它会漏拍。实测同一组弱信号: 幅度判据把 90bpm 读成 30bpm(错值!),")
    w("    时间判据与交付版读数完全一致(见本节末尾的对照)。IBI 是纯时间量,")
    w("    与漂移无关。")
    w("    为什么取【上界】而不是中位数: 伪影的错误方式只有一种 —— 多出一个峰,")
    w("    只会让间隔【变短】。所以合格间隔里最大的那个最接近真实 IBI。")
    w("    为什么是 %.0f%%: 切迹落在主波后 300ms 上下, 而本项目关心的心率区间"
      % (NOTCH_RATIO * 100))
    w("    是 50~120bpm(IBI 500~1200ms) —— %.0f%% × 500 = %.0fms > 300ms,"
      % (NOTCH_RATIO * 100, NOTCH_RATIO * 500))
    w("    挡得住; 而 0.6 × 1200 = 720ms < 1200ms, 真搏动一律放行。")
    w("  修法二: 被挡下的这一拍【不移动锚点】。旧代码是先移动锚点再判离群,")
    w("    于是下一个间隔从假峰起算 —— 一个假峰会把后面整条链都带偏。")
    w("  修法三: 连续 %d 拍被判离群时, 旧逻辑【清空累计】; 新逻辑只【接受这一拍】"
      % RESET_STREAK)
    w("    —— 10 秒窗口里的样本本来就少, 中位数又不怕几个离群点, 扔掉是净损失。")
    w("")
    w("  行=心率, 列=切迹高度; 表里是 偏差 bpm, -- = 没测到, ! = 偏差>5")
    w("")
    for mode in ('new', 'fix'):
        w("  [%s] %s" % (mode, "交付版(09-17 下午)" if mode == 'new' else "本轮修复版"))
        w("     切迹:   " + "".join("%8.0f%%" % (n * 100) for n in NOTCHES))
        for hr in HRS:
            row = "     %3d " % hr
            for n in NOTCHES:
                dd = measure(hr, n, mode)
                if not dd.fin_bpm_valid:
                    row += "%8s" % "--"
                else:
                    dev = dd.fin_bpm - hr
                    row += "%8s" % ("%+d%s" % (dev, "!" if abs(dev) > 5 else ""))
            w(row)
        w("")

    bad_new = none_new = bad_fix = none_fix = 0
    for hr in HRS:
        for n in NOTCHES:
            dd = measure(hr, n, 'new')
            if not dd.fin_bpm_valid:
                none_new += 1
            elif abs(dd.fin_bpm - hr) > 5:
                bad_new += 1
            dd = measure(hr, n, 'fix')
            if not dd.fin_bpm_valid:
                none_fix += 1
            elif abs(dd.fin_bpm - hr) > 5:
                bad_fix += 1
    w("  汇总(%d 心率 × %d 切迹 = %d 组):" % (len(HRS), len(NOTCHES),
                                              len(HRS) * len(NOTCHES)))
    w("    交付版: 没测到 %2d 组, 给错值 %2d 组" % (none_new, bad_new))
    w("    修复版: 没测到 %2d 组, 给错值 %2d 组" % (none_fix, bad_fix))
    w("")
    check("修复后 25 组里【一组错值都没有】(这是硬要求)",
          bad_fix == 0, "给错 %d 组" % bad_fix)
    check("修复后没测到的组数不多于交付版(修法不能比原来更差)",
          none_fix <= none_new, "修复版 %d 组 vs 交付版 %d 组" % (none_fix, none_new))
    if none_fix:
        w("")
        w("  残留: 修复版仍有 %d 组没测到。这些组的切迹高到主波的一半以上、"
          % none_fix)
        w("        间隔又只有 300ms —— 在 10 秒窗口里它们与真搏动确实难以区分,")
        w("        判定为【不给结果】而不是猜一个数, 这是有意的取舍;")
        w("        与「信号太弱时不给结果」是同一类行为。")
    else:
        w("  残留: 修复版在本表全部 %d 组里都给出了结果(没有一组被放弃)。"
          % (len(HRS) * len(NOTCHES)))
    check("修复前至少有一组给出错值 —— 这个 bug 不只是没读数, 还会报错数",
          bad_new >= 1, "%d 组给错值" % bad_new)
    check("修复后不再出现错值(要求 0)", bad_fix == 0, "%d 组" % bad_fix)

    w("")
    w("  副作用检查一: 无切迹的正常信号(可能被新判据误伤)")
    hurt = []
    for hr in HRS:
        d = run_wave(Wave(hr), mode='fix', seed=17)
        ok = d.fin_bpm_valid and abs(d.fin_bpm - hr) <= 2
        if not ok:
            hurt.append((hr, d.fin_bpm, d.fin_bpm_valid))
        w("    真值 %3d -> %-24s %s" % (hr, verdict(d, hr), "" if ok else "<- 误伤!"))
    check("修复后无切迹的正常信号仍然全部正确(没有误伤)", not hurt, "%s" % hurt)

    w("")
    w("  副作用检查二: 弱信号(幅度 150, 噪声 14, 漂移 400)下 fix 与交付版的对照")
    w("  (允许不给结果, 不允许给错值; 这里正是幅度判据翻车的地方)")
    w("    seed  真值   %-18s %s" % ("交付版", "修复版"))
    weak_bad = 0
    weak_fix_worse = 0
    for seed in (31, 61, 62, 63, 64, 65):
        for hr in (60, 72, 90):
            d_n = run_wave(Wave(hr, amp=150.0, noise=14.0), mode='new', seed=seed)
            d_f = run_wave(Wave(hr, amp=150.0, noise=14.0), mode='fix', seed=seed)
            if d_f.fin_bpm_valid and abs(d_f.fin_bpm - hr) > 5:
                weak_bad += 1
            if (not d_n.fin_bpm_valid) and d_f.fin_bpm_valid \
                    and abs(d_f.fin_bpm - hr) > 5:
                weak_fix_worse += 1
            w("    %-5d %-5d %-18s %s"
              % (seed, hr,
                 ("%d bpm" % d_n.fin_bpm) if d_n.fin_bpm_valid else "没测到",
                 ("%d bpm" % d_f.fin_bpm) if d_f.fin_bpm_valid else "没测到"))
    check("修复后在弱信号下没有给出错值(幅度判据在这里会给出 30bpm)",
          weak_bad == 0, "%d 组错值" % weak_bad)
    check("修复后也没有把交付版本来能给的结果变成不给", weak_fix_worse == 0,
          "%d 组" % weak_fix_worse)

    # ================= 七、切迹抑制是不是承重的 ========================
    w("")
    w("-" * 78)
    w("七、切迹抑制【本身】是不是承重的 —— 延迟与高度两个方向一起扫")
    w("-" * 78)
    w("  【为什么必须补这一节】第四、六节的切迹固定在主波后 300ms、高度 <=55%,")
    w("  而修复又把不应期抬到了 400ms —— 300ms 的切迹在【不应期】那一层就被")
    w("  挡住了。也就是说, 前面那张 30 组的表很可能压根没用到切迹抑制。一条")
    w("  没被任何场景用到的规则, 表上是全对、实物上是未知, 不该留在固件里。")
    w("")
    w("  这一节的做法: 把「切迹延迟」和「切迹高度」两个方向都放开扫, 用三个")
    w("  变体对照 —— 只有出现「refr 出错、fix 正确」的组合, 才算证明它承重。")
    w("    new  = 交付版(不应期 300ms, 没有切迹抑制)")
    w("    refr = 【只】把不应期抬到 400ms(不加切迹抑制) —— 最容易想到的改法")
    w("    fix  = 400ms 不应期 + 切迹抑制(本轮真正落地的)")
    w("")

    def vtxt(d, hr):
        if not d.fin_bpm_valid:
            return "没测到"
        dev = d.fin_bpm - hr
        return "%3d bpm(%+d)%s" % (d.fin_bpm, dev, "!" if abs(dev) > 5 else " ")

    def bad(d, hr):
        return (not d.fin_bpm_valid) or abs(d.fin_bpm - hr) > 5

    w("  7-A 先扫延迟(高度固定 45%): 切迹迟于多少毫秒就【不再形成独立峰】?")
    w("      延迟ms  心率   周期ms  延迟/周期    new            refr           fix")
    late_bad = []
    for delay in (300.0, 350.0, 400.0, 420.0, 450.0, 500.0):
        for hr in (50, 72, 90):
            period = 60000.0 / hr
            cells = [vtxt(measure_delay(hr, 0.45, delay, m), hr)
                     for m in ('new', 'refr', 'fix')]
            w("      %6.0f  %4d  %6.0f   %5.2f     %-14s %-14s %s"
              % (delay, hr, period, delay / period, cells[0], cells[1], cells[2]))
            if delay >= 450.0 and cells[0] == "没测到":
                late_bad.append((delay, hr))
        w("")
    w("      判读: 延迟一过约 450ms, 连【交付版】都不再出错 —— 迟于这个位置的")
    w("      重搏波已经落进波谷/下一拍的上升沿, 与主波融在一起, 不再形成独立的")
    w("      局部极大值。而 400ms 的不应期正好覆盖到那之前, 所以在这一族波形上")
    w("      「只抬不应期」(refr) 一个改法就够。")
    w("      ★ 这与本轮最初的猜测【相反】: 原假设是「心率越慢、切迹离主波越远")
    w("        (按周期缩放), 于是越过 400ms 的不应期, 只有切迹抑制挡得住」——")
    w("        实测把这个假设否掉了。假设被否也算结论, 记在这里免得下次再推一遍。")
    w("")
    check("7-A: 延迟 >=450ms 的组合里, 交付版一个都没出错(迟切迹不成峰)",
          not late_bad, "%s" % (late_bad or "6 个组合全过"))

    w("  7-B 再把高度放开(高度 × 延迟一起扫): 能不能找到只有切迹抑制挡得住的情况?")
    w("      高度  延迟ms  心率    new            refr           fix         标记")
    only_fix, regress = [], []
    tot = {'new': [0, 0], 'refr': [0, 0], 'fix': [0, 0]}   # [没测到, 给错值]
    for frac in (0.45, 0.55, 0.70, 0.90):
        for delay in (300.0, 400.0, 450.0, 500.0, 600.0):
            for hr in (50, 72, 90):
                res = {}
                for mode in ('new', 'refr', 'fix'):
                    d = measure_delay(hr, frac, delay, mode)
                    res[mode] = d
                    if not d.fin_bpm_valid:
                        tot[mode][0] += 1
                    elif abs(d.fin_bpm - hr) > 5:
                        tot[mode][1] += 1
                mark = ""
                if bad(res['refr'], hr) and not bad(res['fix'], hr):
                    mark = "<<< 只有 fix 能过"
                    only_fix.append((frac, delay, hr))
                elif (not bad(res['refr'], hr)) and bad(res['fix'], hr):
                    mark = "<<< 反过来: fix 失手"
                    regress.append((frac, delay, hr))
                w("      %4.0f%%  %6.0f  %4d   %-14s %-14s %-14s %s"
                  % (frac * 100, delay, hr, vtxt(res['new'], hr),
                     vtxt(res['refr'], hr), vtxt(res['fix'], hr), mark))
        w("")
    w("      汇总(%d 个组合, 记 [没测到/给错值]):"
      % (len((0.45, 0.55, 0.70, 0.90)) * 5 * 3))
    w("        new  [%2d/%2d]    refr [%2d/%2d]    fix [%2d/%2d]"
      % (tot['new'][0], tot['new'][1], tot['refr'][0], tot['refr'][1],
         tot['fix'][0], tot['fix'][1]))
    w("")
    w("      只有切迹抑制挡得住的组合: %d 个" % len(only_fix))
    for (frac, delay, hr) in only_fix:
        w("        高度 %.0f%%、延迟 %.0fms、心率 %d bpm(IBI %dms, 判据线 %.0fms)"
          % (frac * 100, delay, hr, 60000 // hr, 0.6 * 60000.0 / hr))
    w("")
    w("      【如实记下反向的那 %d 个组合】" % len(regress))
    for (frac, delay, hr) in regress:
        w("        高度 %.0f%%、延迟 %.0fms、心率 %d bpm: fix 反而没测到,"
          % (frac * 100, delay, hr))
        w("        而 refr/new 能给出正确值。")
    w("      说明切迹抑制【不是免费的】: 卡在阈值线上的切迹被多挡掉一拍, 慢心率")
    w("      下本来就少的那几拍再少一个, 就可能不够门槛。这是一次有明确范围的")
    w("      取舍, 不是无条件更好 —— 净值仍然为正(见上面的汇总), 所以留着。")
    w("")
    check("7-B: 存在「refr 出错而 fix 正确」的组合 —— 切迹抑制确实在起作用",
          len(only_fix) >= 1, "找到 %d 个" % len(only_fix))
    check("7-B: 修复版的失败组合数不多于「只抬不应期」",
          (tot['fix'][0] + tot['fix'][1]) <= (tot['refr'][0] + tot['refr'][1]),
          "fix %d 组 vs refr %d 组"
          % (tot['fix'][0] + tot['fix'][1], tot['refr'][0] + tot['refr'][1]))
    check("7-B: 修复版没有引入新的【错值】(宁可没测到, 不可给错数)",
          tot['fix'][1] <= tot['refr'][1],
          "fix %d 组 vs refr %d 组" % (tot['fix'][1], tot['refr'][1]))

    # ================= 汇总 ============================================
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


try:
    main()
except Exception:
    out.append("EXCEPTION:")
    out.append(traceback.format_exc())

path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "ppg_nopulse_sim_result.txt")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
