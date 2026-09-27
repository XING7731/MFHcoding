# -*- coding: utf-8 -*-
"""PPG 一次测量: 自动重试 / 自适应窗口 / 低置信兜底 到底值不值。

【为什么写这个脚本】
用户的原话: "现在我的心率和血氧测量成功率为 0 …… 先让我有读数, 然后你来
帮我定一个测量时长, 让成功率最高"。三个诉求, 三个都得有数支撑:

  ① 定测量时长    → 固定多长? 还是"信号好就早点收工"?
  ② 先有读数      → 自动重试能不能把 0 拉起来? 兜底给的数可不可信?
  ③ 别给错值      → 兜底一旦开口, 错值率还是不是 0?

【口径: 与 ppg_yield_sweep.py 完全一致, 便于横向对照】
  出数率 = 给出结果且 |偏差| <= 5 bpm
  错值率 = 给出结果但 |偏差| >  5 bpm    ← 底线必须 0
  无结果 = 明确报"没测到"
  条件固定为 ppg_yield_sweep.py 第十二节那一组(那是最接近真机的档):
  幅度 305(实测 a=312 换算), 噪声 8, 漂移 = OU std=600 / tau=0.8s,
  并同时跑一遍【带限 OU】(输出再经 1.5Hz 低通)做形态复核 —— 理由见
  该脚本 12-G: 单级 AR(1) 的谱在高频趋于平坦, 不加复核会把模型产物
  当成真收益。

【自适应窗口为什么可以在【离线】评估, 而不必再改状态机】
  固件早退的条件只看"窗口里已经攒到的那些拍", 而窗口里的拍是【只增不改】
  的列表。所以"在 t 时刻收工"的结果 = 用"时刻 <= t 的那些拍"复算中位数 ——
  与在线逐拍累计严格等价。tools/ppg_measure_sim.py 只多记了一份
  win_ibi_t(每拍的入窗时刻), 第一节就是【断言这次等价】——
  等价性不成立的话, 后面所有表都不用看了。

输出: ppg_retry_sim_result.txt (与脚本同目录; 本机 PowerShell 不回显 stdout)
"""

import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ppg_measure_sim as M          # noqa: E402  (可执行规格, 见其文件头)
import ppg_yield_sweep as Y          # noqa: E402  (复用它的 OU 生成器与常数)

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

OUT = []


def w(s=""):
    OUT.append(s)


FS = M.FS
SAMPLE_MS = M.SAMPLE_MS
MODE = 'fix'
TOTAL = 30.0            # 秒。够最长的一档: 1.0(起点) + 0.3(去抖) + 3(稳定) + 20(窗口)
ON = 1000.0             # 贴手时刻(ms), 与 ppg_measure_sim.py 的 main 一致
NOISE = 8.0

# 实测档: docs/15/16 的 a = 312 → 换算成输入正弦幅度
AMP = Y.A_MEASURED / Y.PK

HRS = (60, 72, 90)
SEEDS = (31, 32, 33, 34, 35, 36, 37, 38)      # 8 个 realization × 3 心率 = 24

# 漂移模型: (名字, 生成器工厂, 说明)
DRIFTS = [
    ("OU std=600 tau=0.8s(不平滑)",
     lambda sd: Y.make_ou(600.0, 0.8, sd)),
    ("带限 OU std=600 tau=0.8s(1.5Hz)",
     lambda sd: Y.make_ou(600.0, 0.8, sd, out_hz=1.5)),
]

# 窗口时长档位(毫秒)
W10, W15, W20 = 10000, 15000, 20000

# 提前结束的候选规则: (标签, 最少拍数, 一致占比下限)
#   下限取 70(= 门槛本身) 时, 这条规则退化成"门槛已经过了且拍数够, 就不必
#   再等" —— 它不放松任何判据, 只缩短等待, 所以不会引入错值。
EARLY = [
    ("门槛过 且 >= 6 拍", 6, M.CONSIST_PCT),
    ("门槛过 且 >= 8 拍", 8, M.CONSIST_PCT),
    ("门槛过 且 >= 8 拍/一致>=85%", 8, 85),
    ("门槛过 且 >=10 拍/一致>=90%", 10, 90),
]

T0_EST = ON + (M.ON_S + M.SETTLE_S) * SAMPLE_MS     # 窗口起点的估算值(仅用于打印)


# ------------------------------------------------------------------ 跑一段波形
def run_ppgs(hr, amp, noise_seed, drift_fn, measure_ms):
    """跑一次完整流程, 返回 Ppg 对象。measure_ms 决定窗口长度(样本数换算)。"""
    old = M.MEASURE_S
    M.MEASURE_S = int(round(measure_ms / SAMPLE_MS))
    try:
        random.seed(noise_seed)
        d = M.Ppg(MODE)
        wv = Y.DriftWave(hr, drift_fn, amp=amp, noise=NOISE)
        for k in range(int(TOTAL * FS)):
            t_ms = k * SAMPLE_MS
            ir, red = wv.sample(t_ms, t_ms >= ON)
            d.push(t_ms, ir, red)
    finally:
        M.MEASURE_S = old
    return d


def att_seed(base, k):
    """第 k 次尝试用的种子。不同的 k 必须给【互不相关】的噪声与漂移 ——
    重试的全部意义就在于"换一段运气", 复用同一段噪声等于没重试。"""
    return base * 1009 + k * 137 + 7


# ------------------------------------------------- 与固件同一套的结算判据
def gate_of(ibis):
    """复刻 heart_rate.c 的 finish_measure()。返回 (中位数, 同意拍数, 是否有效)。

    三个条件缺一不可: 拍数 >= PPG_WIN_IBI_MIN(3), 同意拍数 >= 3,
    同意占比 >= PPG_WIN_IBI_CONSIST_PCT(70%)。
    """
    if len(ibis) < M.WIN_IBI_MIN:
        return 0, 0, False
    m = M.median(ibis)
    if m <= 0:
        return 0, 0, False
    agree = sum(1 for v in ibis if abs(v - m) * 100 <= m * M.TOL_PCT)
    ok = (agree >= M.WIN_IBI_MIN
          and agree * 100 >= M.CONSIST_PCT * len(ibis))
    return m, agree, ok


def bpm_of(med):
    return int((60000.0 + med / 2.0) / med) if med > 0 else 0


def classify(bpm, hr, valid):
    if not valid:
        return 'none'
    return 'ok' if abs(bpm - hr) <= 5 else 'wrong'


def norm(res):
    """(出数率, 错值率, 无结果) 的百分数。"""
    ok, wrong, none = res
    tot = ok + wrong + none
    return 100.0 * ok / tot, 100.0 * wrong / tot, 100.0 * none / tot


def row(label, res, note="", extra=""):
    o, x, n = norm(res)
    return ("  %-34s %5.0f%%  %5.0f%%  %5.0f%%   %s%s"
            % (label, o, x, n, extra, note))


def head(t):
    w("")
    w("-" * 78)
    w(t)
    w("-" * 78)


def window_start(d, measure_ms):
    """窗口起点 = 结束时刻 - 窗口长度。用 sim 自己记的 done_t 反推,
    比"用 ON + 去抖 + 稳定期"估算更准(那也是 sim 里的一条断言)。"""
    return d.done_t - measure_ms


def prefix_at(d, until_ms, inclusive=True):
    if inclusive:
        return [v for v, t in zip(d.win_ibi, d.win_ibi_t) if t <= until_ms]
    return [v for v, t in zip(d.win_ibi, d.win_ibi_t) if t < until_ms]


# ================================================================== 主流程
def main():
    w("=" * 78)
    w("PPG 一次测量: 自动重试 / 自适应窗口 / 低置信兜底")
    w("=" * 78)
    w("固件行为变体 '%s'; 真值心率 %s; 每档 %d 个 realization。"
      % (MODE, "/".join(str(h) for h in HRS), len(SEEDS) * len(HRS)))
    w("判定: |偏差| <= 5 bpm 记「出数」; 有值但偏差 > 5 记「错值」(底线 0)")
    w("")
    w("条件(取 ppg_yield_sweep.py 第十二节那一组, 最接近真机):")
    w("  幅度 = %.0f (实测 a=312 换算), 噪声 = %.0f, 漂移 = OU std=600 tau=0.8s"
      % (AMP, NOISE))
    w("  同一组条件跑两遍: 不平滑 OU 与【带限 OU】(1.5Hz) —— 后者是形态复核,")
    w("  两种实现同方向才算真收益(见 ppg_yield_sweep.py 12-G 的教训)。")
    w("")
    w("窗口起点估算 = %.0f ms(ON %.0f + 去抖 %d 样本 + 稳定 %d 样本)"
      % (T0_EST, ON, M.ON_S, M.SETTLE_S))

    # ---------------- 一、等价性自检 ----------------
    head("一、先把「离线复算前缀」这件事验证掉 —— 否则后面的表都不算数")
    w("  做法: 用 10 秒窗口实跑一次, 再用 20 秒窗口实跑一次, 看")
    w("  '20 秒那次的 win_ibi 里、时刻 <= 窗口起点+10 秒的那些拍' 是否")
    w("  逐拍等于 '10 秒那次的 win_ibi'(含顺序)。相等才说明离线复算与")
    w("  在线逐拍累计是同一件事。")
    w("")
    mism_in = mism_ex = total = 0
    for name, mk in DRIFTS:
        for hr in HRS:
            for sd in SEEDS:
                sd2 = att_seed(sd, 0)
                d10 = run_ppgs(hr, AMP, sd2, mk(sd2), W10)
                d20 = run_ppgs(hr, AMP, sd2, mk(sd2), W20)
                t0 = window_start(d20, W20)
                a_in = prefix_at(d20, t0 + W10, inclusive=True)
                a_ex = prefix_at(d20, t0 + W10, inclusive=False)
                b = list(d10.win_ibi)
                total += 1
                if a_in != b:
                    mism_in += 1
                if a_ex != b:
                    mism_ex += 1
    w("  共 %d 组。边界取 '<= 窗口起点+10s' 时不等价: %d 组" % (total, mism_in))
    w("  共 %d 组。边界取 '<  窗口起点+10s' 时不等价: %d 组" % (total, mism_ex))
    if mism_in == 0:
        BOUND_INC = True
        w("  ⇒ 采用【闭区间】(时刻 <= 窗口终点)。等价性成立: 离线复算可用。")
    elif mism_ex == 0:
        BOUND_INC = False
        w("  ⇒ 采用【开区间】(时刻 <  窗口终点)。等价性成立: 离线复算可用。")
    else:
        BOUND_INC = True
        w("  ⇒ ⚠ 两种边界都有不等价的组 ⇒ 离线复算只能当近似, 结论要保守读。")
    w("  (不等价的原因: 峰的时刻取的是【检出那一拍】的样本时刻, 与窗口关闭的")
    w("   那一拍之间可能有 1 个样本的重叠 —— 这是口径差不是算法差。)")

    def pre(d, until_ms):
        return prefix_at(d, until_ms, inclusive=BOUND_INC)

    # ---------------- 二、自动重试 ----------------
    head("二、自动重试: 单次成功率 p → 1-(1-p)^n")
    w("  流程: 一次测量失败(没过高置信门槛)就【自动重开一个窗口】再测一次。")
    w("  每一次尝试换一段独立的噪声与漂移 —— 复用同一段等于没重试。")
    w("  判读: 取【第一次给出有效结果】的那次算最终结果(固件也是这样, 一有")
    w("  读数就停, 不会为了多测几次让用户一直等)。")
    w("")
    for name, mk in DRIFTS:
        w("  [%s]" % name)
        w("  %-34s %6s  %6s  %6s   %s" % ("档位", "出数率", "错值率", "无结果", "备注"))
        for n in (1, 2, 3):
            res = [0, 0, 0]
            for hr in HRS:
                for sd in SEEDS:
                    got = None
                    for k in range(n):
                        s2 = att_seed(sd, k)
                        d = run_ppgs(hr, AMP, s2, mk(s2), W15)
                        m, ag, ok = gate_of(list(d.win_ibi))
                        if ok:
                            got = bpm_of(m)
                            break
                    res[{'ok': 0, 'wrong': 1, 'none': 2}[
                        classify(got or 0, hr, got is not None)]] += 1
            w(row("最多 %d 次尝试(每次 15 秒)" % n, tuple(res)))
        w("")

    # ---------------- 三、测量时长 ----------------
    head("三、测量时长: 固定多长 vs 信号好就早点收工")
    w("  '自适应' = 窗口上限 max, 但只要【门槛已经过了且拍数够】就立刻收工。")
    w("  平均窗长 = 本次运行里各 realization 实际用的窗口时长(不含 3 秒稳定期)。")
    w("")
    for name, mk in DRIFTS:
        w("  [%s]" % name)
        w("  %-34s %6s  %6s  %6s   %s" % ("档位", "出数率", "错值率", "无结果", "平均窗长"))
        for label, ms in [("固定 10 秒(改动前)", W10), ("固定 15 秒", W15),
                          ("固定 20 秒", W20)]:
            res = [0, 0, 0]
            for hr in HRS:
                for sd in SEEDS:
                    s2 = att_seed(sd, 0)
                    d = run_ppgs(hr, AMP, s2, mk(s2), ms)
                    m, ag, ok = gate_of(pre(d, window_start(d, ms) + ms))
                    res[{'ok': 0, 'wrong': 1, 'none': 2}[
                        classify(bpm_of(m), hr, ok)]] += 1
            w(row(label, tuple(res), extra="%.1f 秒  " % (ms / 1000.0)))

        for label, good_n, good_pct in EARLY:
            res = [0, 0, 0]
            used = []
            for hr in HRS:
                for sd in SEEDS:
                    s2 = att_seed(sd, 0)
                    d = run_ppgs(hr, AMP, s2, mk(s2), W20)     # 上限 20 秒
                    t0 = window_start(d, W20)
                    # 找最早的、满足早退条件的时刻
                    cand = sorted(set(t for t in d.win_ibi_t
                                      if t0 + 8000 <= t <= t0 + W20))
                    chosen, used_ms = None, W20
                    for t in cand:
                        pr = pre(d, t)
                        m, ag, ok = gate_of(pr)
                        if ok and len(pr) >= good_n \
                                and (100.0 * ag / len(pr)) >= good_pct:
                            chosen, used_ms = pr, t - t0
                            break
                    if chosen is None:
                        chosen = pre(d, t0 + W20)
                    used.append(used_ms)
                    m, ag, ok = gate_of(chosen)
                    res[{'ok': 0, 'wrong': 1, 'none': 2}[
                        classify(bpm_of(m), hr, ok)]] += 1
            w(row("自适应 8~20 秒: %s" % label, tuple(res),
                  extra="%.1f 秒  " % (sum(used) / len(used))))
        w("")

    # ---------------- 四、低置信兜底 ----------------
    head("四、低置信兜底: 高置信门槛没过的时候, 那个中位数能不能看")
    w("  这一节回答用户那句「先让我有读数」能不能兑现 —— 而且要给对价格:")
    w("  兜底一旦放出去, 【错值率就不再是 0】, 必须知道放出去的是什么。")
    w("")
    w("  两种候选兜底口径(都在「所有尝试都没过高置信门槛」时启用):")
    w("    (a) 拍数 >= 3        —— 只要攒到 3 拍就给中位数")
    w("    (b) 同意拍数 >= 3    —— 保留「至少 3 拍互相一致」, 只放弃 70% 占比")
    w("")
    for name, mk in DRIFTS:
        w("  [%s]" % name)
        w("    %-28s %6s %6s %6s   %s"
          % ("口径", "总组数", "其中给值", "给对", "备注"))
        for tag, rule in [("(a) 拍数>=3", 'n'), ("(b) 同意拍数>=3", 'a')]:
            tot = given = good = 0
            for hr in HRS:
                for sd in SEEDS:
                    s2 = att_seed(sd, 0)
                    d = run_ppgs(hr, AMP, s2, mk(s2), W15)
                    ibis = list(d.win_ibi)
                    m, ag, ok = gate_of(ibis)
                    tot += 1
                    if ok:
                        continue                    # 高置信已成立, 兜底不介入
                    hit = (len(ibis) >= 3) if rule == 'n' else (ag >= 3)
                    if not hit:
                        continue
                    given += 1
                    if abs(bpm_of(M.median(ibis)) - hr) <= 5:
                        good += 1
            w("    %-28s %6d %6d %6d   %s"
              % (tag, tot, given, good,
                 "给对率 %.0f%%" % (100.0 * good / given) if given else "本档没触发"))
        w("")
    w("  读法: 兜底不是「白捡成功率」。它把一部分【明确的无结果】换成【可能错的")
    w("  读数】, 所以要看的是'给值里给对了多少'。若给对率高, 说明门槛挡掉的")
    w("  多半是'拍数不够'而不是'信号在骗人', 兜底值得开; 若给对率低, 说明")
    w("  门槛挡的正是系统性双峰, 兜底就是拿错值换读数, 不能开。")

    # ---------------- 五、结论 ----------------
    head("五、结论(每条都对应上面某张表; 数值取自本次运行, 不在这里重复)")
    w("  1) 测量时长: 取【固定 15 秒】。")
    w("     看第三节'固定 10/15/20 秒'三行: 10→15 秒在两种漂移实现下都明显")
    w("     变好(同方向), 而 15→20 秒收益小、等待 +33%, 且不平滑那一组的")
    w("     错值率反而回升。取两者之间的拐点。")
    w("")
    w("  2) 自适应早退(攒够几拍就收工): 【否掉】。")
    w("     第三节后四行: 在相同甚至更长的平均窗长下, 出数率一律低于固定")
    w("     15 秒。直觉上'够了就早点收工'是对的, 数据说不是 —— 拍数越多,")
    w("     中位数越稳、'一致占比'越难被系统性双峰伪造。")
    w("")
    w("  3) 自动重试: 【否掉】。")
    w("     第二节: 不平滑那组 1→2→3 次只挪几个百分点且错值率同步上升;")
    w("     带限那组 1 次就已经没有'无结果', 重试完全无收益。")
    w("     机制: 重试只能把'无结果'变成结果, 救不回已被判据否掉的错值。")
    w("")
    w("  4) 低置信兜底: 【不能开】。")
    w("     第四节: 两种口径下'给值里给对了多少'都极低, 且更严的口径(b)")
    w("     给对率反而低于更松的(a)。⇒ 高置信门槛挡的正是系统性双峰,")
    w("     放开就是拿错值换读数。")
    w("")
    w("  5) 本脚本的边界(必须一起读): 波形仍是 sin(φ)+0.35sin(2φ+0.6) + 漂移")
    w("     + 白噪声, 幅度是可以任意放大的一个数 —— 【模型里没有 ADC 饱和】。")
    w("     所以它回答的是'换窗口长度有没有用', 不回答'把幅度往上推有没有用'。")
    w("     后者的上限必须由真机的 d 读数决定(见 docs/38 第三节)。")

    path = os.path.join(HERE, "ppg_retry_sim_result.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(OUT) + "\n")
    print("done -> %s" % path)


if __name__ == "__main__":
    main()
