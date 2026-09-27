/* =========================================================================
 * PPG 心率/血氧处理 (简化算法, 非医疗级)
 * 算法思路与局限见 heart_rate.h 头部说明。
 *
 * 【2026-09-16 重写 —— 起因: 用户报"心率检测模块不稳定, 读数随机"】
 *   旧版有四个独立缺陷叠加, 数值验证(tools/ppg_hr_sim.py)全部复现并量化:
 *     ① 直流估计初值为 0 ⇒ 上电瞬间 5 万的直流被当成交流 ⇒ 峰值参考被顶到
 *        18262 ⇒ 峰阈值 9131, 而脉搏波只有约 600 LSB ⇒ 上电后根本测不到峰。
 *     ② 寻峰判据"上一拍在阈值上且本拍更低" ⇒ 下降沿每一步都算峰
 *        (40 秒判出 360 个峰, 真实 48 拍) ⇒ IBI 抖动 ⇒ 读数随机。
 *     ③ 没有绝对幅度下限 + 有效性永不清零 ⇒ 拿开手指后既编数又留旧值。
 *     ④ FIFO 溢出丢样本不进时间轴 ⇒ IBI 偏短 ⇒ 心率整体偏高。
 *
 * 【2026-09-17 追加一: 接触判定 + 直流阶跃吸附】
 *   "手指刚贴上"那一两秒的误测不是算法问题: 输入从环境光(几百)跳到手指
 *   反射光(几万), 高通把这一跳当成交流, 冲出幅度 36422 的假峰(真实脉搏波
 *   600), 把峰值参考顶死后要几十秒才爬回来。两条修法:
 *     ① bandpass() 里的直流阶跃吸附 —— 跳变超 PPG_DC_STEP_FRAC 直接吸附,
 *        从根上不让它变成假峰;
 *     ② begin_settle() 里把直流估计锚到当前样本 + 清掉上一次次测量的残余。
 *
 * 【2026-09-17 追加二: 从"持续测量"改成"一次测量"(本轮)】
 *   流程: 贴上 → 稳定 3 秒 → 采集 10 秒 → 出最终结果并冻结。
 *   四个状态(HR_State_t):
 *     IDLE      没手指          —— 不发布
 *     SETTLING  稳定期 3s       —— 算法照跑(预热)但不采集、不发布
 *     MEASURING 采集窗口 10s    —— 每拍累计 IBI 与 R, 屏上显示 pending
 *     DONE      窗口结束        —— 用整段的中位数算出最终值, 之后不再变化
 *
 *   为什么改成有限窗口:
 *     · "持续测量"时屏上的数每 2 秒变一次(其实每拍都在重算), 用户没法判断
 *       "这个数稳了没有" —— 这正是"读数随机"的观感来源之一;
 *     · 有限窗口可以用【整段的中位数】, 而不是最近 3 拍的: 10 秒在 60~100bpm
 *       下有 10~16 个 IBI, 一次伪影拖不走结果;
 *     · 窗口一到结果就冻结, 手指拿开就归零 —— "陈旧值"在结构上不存在了,
 *       所以 PPG_HR_STALE_MS 那条超时判据被删掉了(见 user_config.h)。
 *
 *   发布门控做在 HR_GetResult() 里, 保证"屏上"和"云端"看到的是同一个判断。
 *
 * 【2026-09-17 追加三: 重搏切迹被当成第二次搏动(本轮)】
 *   用户实测: pending 10s 正常倒数完, 10 秒后屏上仍是 HR:-- SpO2:--,
 *   行0 显示 "PPG: no pulse, retry"。器件在线(能走到 pending 证明接触判定
 *   正常), 所以既不是接线问题, 也不该是"信号太弱"。
 *
 *   排查方式与结论见 tools/ppg_nopulse_sim.py(三个候选原因逐个放进仿真):
 *     W1 稳定期运动脉冲顶高峰值参考     → 否(4Hz 低通把短脉冲削掉了)
 *     W2 采集期周期性伪影               → 否(同样够不到峰值参考)
 *     W3 重搏切迹被当成第二次搏动       → 是, 确定性复现
 *   机制: 一个心搏被数成两拍 → 窗口内 IBI 混进 300/520ms → 中位数锁死在
 *   半拍间隔上 → 之后每一次真搏动都成了离群值 → 连续 4 次离群就清空重来
 *   → 10 秒窗口在"3 拍 → 清空 → 3 拍"之间循环, 永远攒不够 → no pulse, retry。
 *   切迹 55% 时还会给错值(50bpm 报成 71bpm), 那比"没读数"更糟。
 *
 *   两条修法(都落在 on_peak 上, 参数见 user_config.h 的重搏切迹一节):
 *     ① 不应期 300→400ms —— 粗筛, 切迹落在主波后约 300ms, 直接关在门外;
 *     ② 切迹抑制 —— 细筛, 用【最近 5 拍已接受的 IBI 中位数】当参照, 间隔
 *        短于它的 PPG_NOTCH_MIN_RATIO 就不算一拍, 【并且不移动锚点】。
 *        不移动锚点很关键: 旧代码先把锚点移到假峰再判离群, 于是下一个间隔
 *        从假峰起算, 一个切迹会把后面整条链都带偏。
 *     ③ 逃生通道不再清空累计(见 user_config.h 的 PPG_IBI_RESET_STREAK 说明)。
 *   验证: 6 心率(40~150bpm) × 5 切迹档位(0~55%) 共 30 组, 修复版 0 组错值、
 *   0 组没测到(交付版为 2 组错值 + 4 组没测到); 无切迹的正常信号无误伤;
 *   弱信号下没有引入新的错值。
 *
 * 【2026-09-22 追加四: 加一个"未钳位"的直流读数, 只给上行报文用】
 *   起因: LED 电流从 0x24(7.2mA) 提到 0x4F(15.8mA) 后, 用户报"测 10 次 10 次
 *   都失败"(屏上行0 全是 "no pulse, retry")。要判断"是不是信号太强把 ADC 顶到
 *   满量程、波形被削平", 唯一的依据就是红外直流 d 的绝对值。
 *
 *   根因在【观测侧, 不在算法侧】: HR_GetIrDc() 返回 uint16_t, 走 clamp_u16
 *   压在 65535。屏上和【报文】原先共用它 —— 于是真实直流十几万被报成 65535:
 *   一个"看起来正常、且纹丝不动"的值。电流翻了一倍多而云端 d 不动,
 *   恰好会把"是不是饱和了"这个判断带反 —— 越接近/超过满量程, 报出来越是
 *   停在 65535。
 *
 *   修法: 新增 HR_GetIrDcRaw()(uint32_t, 不钳位), 只给 app_tasks.c 的 JSON
 *   使用; 屏上那行继续用钳位版 —— 一行 21 字符, d 从 5 位加长到 6 位会挤掉
 *   后面的 a/b, 而"截断得看起来正常"比不显示更糟。
 *
 *   验证: Keil 无头 0 Error / 0 Warning; Code +12B、RO +4B、RAM 不变
 *   (d/a/b 换掉 ts/tsv 后报文约 174B, 仍在巴法云 191B 记录上限内)。
 *   伴随改动见 app_tasks.c 的 build_data_json() 与 docs/37。
 *
 *   ⚠ 以上"追加四"的【根因判断是错的】, 见追加五。留在这里是因为它记下了
 *     当时的观察(d 纹丝不动), 而错在哪一步值得对照着看。
 *
 * 【2026-09-22 追加五: "10 次 10 次失败"的真因 —— 一个 4 倍的口径错误】
 *   追加四把病因归到"clamp_u16 把十几万压成 65535"。真因不是它:
 *   **PPG 样本在驱动层就已经是 `(v & 0x03FFFF) >> 2`(见 max30102.c),
 *     上限本来就是 65535** —— s_band_dc_ir 由这些样本算出, 永远超不过 65535,
 *   clamp_u16 什么都没挡掉。
 *
 *   真正搞错的是"58234 是什么量纲":
 *     实测 d = 58234 是【右移 2 位之后】的值 ⇒ 18 位原始值 = 58234 × 4
 *     = 232936, 占 18 位满量程 262143 的 **88.9%** —— 原来就已经贴着天花板。
 *   docs/36 选 0x4F 时把它当成"18 位原始值"去和 262143 比, 算出"只用了 49%,
 *   安全"。于是 ×2.19 倍电流 ⇒ 原始值约 51 万 ≫ 262143 ⇒
 *   **ADC 饱和, 波形被削成直线** ⇒ 带通输出≈0 ⇒ 一个峰都到不了阈值
 *   ⇒ s_amp_ref 恒为 0(a=0, b=0) ⇒ 每次都 "no pulse, retry"。这就是 10/10。
 *
 *   为什么这次改动【自己把问题引进来了】, 且当时没发现:
 *     · 饱和之后 d 停在 65535, 而 65535 看起来"很正常、还没到 262143";
 *     · a=0 又太容易被解释成"指腹没盖住窗口"(docs/15 第四节的老结论);
 *     · 追加四那次还在这个方向上加了一把——把 d 报成 uint32, 让人更相信
 *       "十几万很正常"。实际上 d 的物理上限就是 65535。
 *
 *   修法(本轮):
 *     ① user_config.h: PPG_LED_PA_* 0x4F → 0x20(6.4mA)。目标 d ≈ 79% 上限,
 *        按 d∝电流(58234/36 LSB = 1617.6 counts/LSB)反推得 32 LSB。
 *     ② 新增 PPG_ADC_SAT_LEVEL(65000): 屏上直接显示 "ADC SAT d:xxxxx",
 *        把"饱和"变成一个看得见的状态, 不再需要人去推。
 *     ③ 采集窗口 10→15 秒(见 user_config.h PPG_MEASURE_MS 的仿真依据)。
 *     ④ 显示优先级: PPG 调试行上移到 MPU6050 告警之前 —— 此前
 *        "! MPU6050 FAIL" 把 d/a/b 整场测量都盖住了, 这是这条故障拖了
 *        这么久才定位的直接原因。
 *
 *   验证: Keil 无头 0 Error / 0 Warning; 真机需烧录后看 d 是否回到 5 万级、
 *   a 是否回到 2 百级、b 是否开始涨。完整推导与自查见 docs/38。
 *
 * 内存: 两个 HR_WIN(200) 的 float 环形缓冲 = 800B × 2 = 1600B(静态区),
 *       窗口累计数组 (40×2 + 40×4) = 240B, 切迹抑制的滚动 IBI 参照
 *       (5×2) = 10B, 都不占用任务栈。
 *       用 float 而不是 uint16 存波形是因为缓冲里存的已经是"去掉直流与漂移
 *       之后的交流分量", 它是有正负的 —— 存成无符号会把波形削掉一半。
 * ========================================================================= */
#include "heart_rate.h"
#include "user_config.h"
#include "app_events.h"   /* 提供 PPG_BATCH_MAX(批次上限) */
#include <math.h>
#include <string.h>

#define HR_WIN                 PPG_HR_WINDOW
#define SAMPLE_PERIOD_MS_PPG   (1000.0f / (float)PPG_SAMPLE_RATE_HZ)

/* 一阶 IIR 的系数: 由截止频率反解(alpha = 1 - e^(-2π·fc/fs))。
 * 在 HR_Init 里算一次存起来 —— 公式写成宏会让人以为它每次都要求指数,
 * 50Hz 下虽然无所谓, 但没有理由让读者去猜。 */

/* 重新捕获的间隔(样本数): 连续这么多点没有峰就认为阈值过高 */
#define REACQ_SAMPLES  ((PPG_PEAK_REACQ_MS * PPG_SAMPLE_RATE_HZ) / 1000)

/* 接触判定 / 稳定期 / 采集窗口(样本数), 见 user_config.h */
#define CONTACT_ON_SAMPLES   ((PPG_CONTACT_ON_MS  * PPG_SAMPLE_RATE_HZ) / 1000)
#define CONTACT_OFF_SAMPLES  ((PPG_CONTACT_OFF_MS * PPG_SAMPLE_RATE_HZ) / 1000)
#define SETTLE_SAMPLES       ((PPG_SETTLE_MS      * PPG_SAMPLE_RATE_HZ) / 1000)
#define MEASURE_SAMPLES      ((PPG_MEASURE_MS     * PPG_SAMPLE_RATE_HZ) / 1000)
#define WIN_MAX              PPG_WIN_MAX

/* 去抖计数器的上限, 防止长期贴着/长期拿开时无限累加后回绕归零 */
#define RUN_MAX  0xFFF0u

/* ---------------- 环形缓冲: 存放"带通之后"的交流分量 ---------------- */
static float    s_ac_ir[HR_WIN];
static float    s_ac_red[HR_WIN];
static uint16_t s_head = 0;
static uint16_t s_filled = 0;

/* ---------------- 滤波器状态 ---------------- */
static float    s_hp_a = 0.06f;           /* 高通系数(HR_Init 里按截止频率算) */
static float    s_lp_a = 0.39f;           /* 低通系数 */
static float    s_band_dc_ir = 0.0f;      /* 高通用的直流估计 */
static float    s_band_dc_red = 0.0f;
static float    s_band_lp_ir = 0.0f;      /* 低通输出(= 最终的交流分量) */
static float    s_band_lp_red = 0.0f;
static uint8_t  s_inited = 0;             /* 是否已用第一个样本初始化直流 */

/* ---------------- 时间轴 ---------------- */
static uint32_t s_idx = 0;                /* 虚拟样本序号(含 FIFO 丢掉的那部分) */
static uint32_t s_gap_total = 0;          /* 累计丢样本数, 供诊断 */

/* ---------------- 寻峰 ---------------- */
static float    s_v1 = 0.0f;              /* 上一拍的带通值 */
static float    s_v2 = 0.0f;              /* 上上拍 */
static float    s_amp_ref = 0.0f;         /* 峰值参考(双向自适应) */
static uint32_t s_last_peak_idx = 0;
static uint32_t s_since_peak = 0;
static uint8_t  s_settle = PPG_SETTLE_SAMPLES;

/* ---------------- 采集窗口的累计量(2026-09-17) ----------------
 * 每拍往里放一个 IBI、一个 R。只在 MEASURING 期间增长;
 * 进入 SETTLING 时会 clear_algo() 清空, 所以窗口起点必然是空的。
 * 窗口结束对整段取中位数 —— 这是"一次测量"能得到稳定值的关键。 */
static uint16_t s_win_ibi[WIN_MAX];
static uint8_t  s_win_ibi_n = 0;
static float    s_win_r[WIN_MAX];
static uint8_t  s_win_r_n = 0;
static uint8_t  s_reject_streak = 0;      /* 连续被判离群的拍数(逃生通道) */

/* ---------------- 切迹抑制用的滚动 IBI 参照(2026-09-17) ----------------
 * 只放【已经被接受】的最近 PPG_NOTCH_EST_BEATS 拍, 按时间顺序 FIFO 滚动。
 * 与 s_win_ibi 的分工: s_win_ibi 是"整个采集窗口"的累计(窗口结束才算一次);
 * 这个是"最近几拍"的滑动参照(每一拍都要用)。
 * 顺序必须保住(丢掉的是最旧那一拍), 所以取中位数时不能原地排序 ——
 * 见 rec_ibi_median()。 */
static uint16_t s_rec_ibi[PPG_NOTCH_EST_BEATS];
static uint8_t  s_rec_ibi_n = 0;

/* ---------------- 最终结果(窗口结束时算一次, 之后冻结) ----------------
 * 与"实时值"分开存是有意的: 采集期间屏上是 pending、上行是 null, 只有
 * DONE 才把这一组冻结值发出去 —— 中间态一律不外泄, 屏上和云端同一个出口。 */
static uint16_t s_fin_bpm = 0;
static uint8_t  s_fin_bpm_valid = 0;
static uint16_t s_fin_spo2 = 0;
static uint8_t  s_fin_spo2_valid = 0;
static float    s_fin_r = 0.0f;
static uint8_t  s_fin_beats = 0;          /* 窗口内实际攒到的合格拍数(诊断) */

/* ---------------- 血氧(实时) ---------------- */
static float    s_ratio_r = 0.0f;         /* 实时 R, 供标定时观察 */

/* ---------------- 接触判定 / 测量状态机 ----------------
 * 为什么要单独一个状态机: "手指贴上了"、"手指放稳了"、"采够 10 秒了"是三件
 * 时间尺度完全不同的事 —— 第一件是零延迟的物理事实, 第二件要等血流重新
 * 分布, 第三件是一个固定长度的时间窗。混在一起的结果就是每次贴上去的
 * 一两秒里屏上蹦出几个互相矛盾的数。 */
static HR_State_t s_state = HR_ST_IDLE;
static uint16_t   s_contact_run = 0;     /* 连续"有手指"的样本数(去抖) */
static uint16_t   s_nocontact_run = 0;   /* 连续"没手指"的样本数 */
static uint32_t   s_settle_left = 0;     /* 稳定期剩余样本数(倒计时) */
static uint32_t   s_win_left = 0;        /* 采集窗口剩余样本数(倒计时) */

/* 清空"算法结果"与"寻峰历史", 但【不动时间轴 s_idx 与累计丢样数 s_gap_total】
 * —— 前者是单调递增的虚拟时钟(清掉会让"距上次搏动多久"失去参照), 后者是
 * 全程累计的诊断量, 与某一次测量的开始/结束无关。 */
static void clear_algo(void)
{
    s_head = 0; s_filled = 0;

    s_v1 = s_v2 = 0.0f;
    s_amp_ref = 0.0f;
    s_last_peak_idx = 0;
    s_since_peak = 0;
    s_settle = PPG_SETTLE_SAMPLES;

    s_win_ibi_n = 0;
    s_win_r_n = 0;
    s_reject_streak = 0;
    s_rec_ibi_n = 0;          /* 滚动参照属于"某一次测量", 一并清掉 */
    s_win_left = 0;

    s_ratio_r = 0.0f;

    /* 最终结果一并清掉: 一次新测量开始时, 上一次的结果就该从屏上消失
     * (否则用户会看到"正在 settling"和"上一次的 72bpm"同时挂着)。 */
    s_fin_bpm = 0; s_fin_bpm_valid = 0;
    s_fin_spo2 = 0; s_fin_spo2_valid = 0;
    s_fin_r = 0.0f;
    s_fin_beats = 0;
}

/* 进入稳定期: 清算法 + 把直流估计直接锚到当前样本。
 *
 * 【为什么必须锚一下 —— 这是"贴手瞬间误测"最主要的一条】
 *   没有手指时, 高通里的直流估计会一路衰减到环境光量级(接近 0); 手指一贴
 *   上来, 输入从几百跳到几万, 高通输出的第一拍就是"几万减去接近 0" = 一个
 *   巨大的正尖峰, 远大于真实脉搏波(几百) —— 它必然越过阈值被当成一次搏动,
 *   于是第一个 IBI 完全是假的。把估计值直接设成当前样本, 这个阶跃就不存在。
 *   旧版那条"上电 109 秒测不到峰"是同一个坑的另一面(初值为 0), 这里补的是
 *   反复贴/拿时每次都踩的那一面。 */
static void begin_settle(uint16_t ir, uint16_t red)
{
    clear_algo();

    s_band_dc_ir  = (float)ir;
    s_band_dc_red = (float)red;
    s_band_lp_ir  = 0.0f;
    s_band_lp_red = 0.0f;
    s_inited = 1;

    s_settle_left = SETTLE_SAMPLES;
}

void HR_Init(void)
{
    const float twopi = 6.28318531f;
    const float fs = (float)PPG_SAMPLE_RATE_HZ;

    /* 带通的两个截止频率 → 一阶 IIR 系数。只算这一次。 */
    s_hp_a = 1.0f - expf(-twopi * PPG_HP_CUTOFF_HZ / fs);
    s_lp_a = 1.0f - expf(-twopi * PPG_LP_CUTOFF_HZ / fs);

    memset(s_ac_ir, 0, sizeof(s_ac_ir));
    memset(s_ac_red, 0, sizeof(s_ac_red));
    s_head = 0; s_filled = 0;

    s_band_dc_ir = s_band_dc_red = 0.0f;
    s_band_lp_ir = s_band_lp_red = 0.0f;
    s_inited = 0;

    s_idx = 0; s_gap_total = 0;

    s_v1 = s_v2 = 0.0f;
    s_amp_ref = 0.0f;
    s_last_peak_idx = 0;
    s_since_peak = 0;
    s_settle = PPG_SETTLE_SAMPLES;

    s_win_ibi_n = 0;
    s_win_r_n = 0;
    s_reject_streak = 0;
    s_rec_ibi_n = 0;
    s_win_left = 0;

    s_ratio_r = 0.0f;
    s_fin_bpm = 0; s_fin_bpm_valid = 0;
    s_fin_spo2 = 0; s_fin_spo2_valid = 0;
    s_fin_r = 0.0f;
    s_fin_beats = 0;

    s_state = HR_ST_IDLE;
    s_contact_run = 0;
    s_nocontact_run = 0;
    s_settle_left = 0;
}

/* PPG 原始量(诊断用, 见 heart_rate.h 的说明)。
 * 输入本来是 uint16, 值域天然 ≤ 65535; 仍做一次上限钳位, 免得浮点累加后
 * 转整数时溢出成"看起来正常"的小数字。 */
static uint16_t clamp_u16(float v)
{
    if (v <= 0.0f)     return 0;
    if (v >= 65535.0f) return 65535;
    return (uint16_t)(v + 0.5f);
}

uint16_t HR_GetIrDc(void)    { return clamp_u16(s_band_dc_ir); }
uint16_t HR_GetPeakAmp(void) { return clamp_u16(s_amp_ref); }

/* ---- 未钳位的红外直流: 【只给上行报文用】(2026-09-22 追加) ----
 * 【加它的理由, 在 2026-09-22 被改写了一次 —— 原理由本身是错的, 留下记录】
 *   原理由: "LED 电流提到 0x4F 后真实直流涨到十几万, 被 clamp_u16 压成 65535,
 *   屏上纹丝不动, 判断有没有顶到 ADC 满量程会被带反。"
 *   —— 这个理由**不成立**: PPG 样本在驱动层就已经是
 *   `(v & 0x03FFFF) >> 2` 的结果(见 max30102.c), 上限本来就是 65535,
 *   s_band_dc_ir 由这些样本算出, 永远不可能超过 65535, clamp_u16 这一层
 *   【什么也没挡掉】。真正把量纲搞错的是"58,234 是右移 2 位之后的数"这一点,
 *   而不是这里的钳位。见 docs/38 第一节。
 *
 *   那为什么还留着这个函数: 它给报文提供一个"驱动层将来若不再右移 2 位、
 *   恢复完整 18 位分辨率"时不会截断的出口。现在它的返回值与钳位版相同,
 *   属于结构性预留, 不承担排查职责 —— 所以判"是否顶到 ADC 上限"要用
 *   PPG_ADC_SAT_LEVEL 去比, 不要在这里找。
 *
 * 【上限 1000000 只是防溢出】18bit ADC 满量程 262143。 */
uint32_t HR_GetIrDcRaw(void)
{
    if (s_band_dc_ir <= 0.0f)       return 0;
    if (s_band_dc_ir >= 1000000.0f) return 1000000;
    return (uint32_t)(s_band_dc_ir + 0.5f);
}

/* ---- 测量状态(2026-09-17 新增) ---- */
HR_State_t HR_GetState(void) { return s_state; }

uint8_t HR_IsTouched(void) { return (s_state != HR_ST_IDLE) ? 1 : 0; }

/* 稳定期剩余毫秒, 供屏上做倒计时。
 * 用"向上取整"的秒: 剩 3000ms 显示 3, 剩 20ms 显示 1 —— 归零的那一刻
 * 刚好是"开始采集"的那一刻, 不会出现"显示 0 但还没开始"的尴尬。
 * 换算顺序先乘后除: SETTLE_SAMPLES(150) × 1000 = 150000, 在 uint32 内。 */
uint32_t HR_GetSettleRemainMs(void)
{
    if (s_state != HR_ST_SETTLING) return 0;
    return (s_settle_left * 1000UL) / (uint32_t)PPG_SAMPLE_RATE_HZ;
}

/* 采集窗口剩余毫秒(2026-09-17 新增)。
 * 屏上 "PPG: pending 7s" 用它。同样是向上取整: 窗口剩 20ms 时显示 1s,
 * 显示归零的那一刻正好是出最终结果的那一刻。 */
uint32_t HR_GetMeasureRemainMs(void)
{
    if (s_state != HR_ST_MEASURING) return 0;
    return (s_win_left * 1000UL) / (uint32_t)PPG_SAMPLE_RATE_HZ;
}

/* 窗口内已经攒到多少个【合格】IBI。
 * 用途: pending 期间看它有没有在涨 —— 一直不涨就说明这次测量注定拿不到
 * 结果, 不必干等满 10 秒; 测量失败后它还能告诉你"只攒到 2 拍"。 */
uint8_t HR_GetBeats(void)
{
    if (s_state == HR_ST_DONE) return s_fin_beats;
    return s_win_ibi_n;
}

/* 信号质量是否达标。
 * 【本版语义变了, 必须说清】旧版是"有光 + 调制深度足够", 现在改成
 * 【窗口结束时真的测出了心率】。理由: 报警的唯一目的是"提醒用户心率不
 * 正常", 而一个连心率都没测出来的窗口, 拿它去报警只会是噪声。
 * 所以: 只有 DONE 且最终心率有效时才可能返回 1。 */
uint8_t HR_IsSignalOk(void) { return HR_IsResultValid(); }

uint8_t HR_IsResultValid(void)
{
    if (s_state != HR_ST_DONE) return 0;
    return s_fin_bpm_valid;
}

/* 累计因 FIFO 溢出而丢掉的样本数。
 * 这个数非 0 就说明 I2C 读取跟不上器件产出 —— 是"心率偏高/抖动"的
 * 一个独立来源, 值得在排查行上直接看到。 */
uint32_t HR_GetDroppedSamples(void) { return s_gap_total; }

/* FIFO 溢出丢样本时由任务层通知: 把时间轴补回来, 并放弃跨缺口的那一拍。
 * 只在发送端"确实丢了数据"时调用。 */
void HR_NotifyDrop(uint8_t lost)
{
    if (lost == 0) return;
    s_idx += lost;
    s_gap_total += lost;
    s_last_peak_idx = 0;      /* 跨缺口的 IBI 不可信, 重新起算 */
    s_reject_streak = 0;
    s_v1 = s_v2 = 0.0f;
}

/* 带通: 先减掉缓慢的直流/漂移(高通), 再压掉高频噪声(低通)。
 * 只减直流是不够的 —— 呼吸(约 0.25Hz)和手指压力变化造成的漂移往往
 * 比脉搏波本身还大, 仿真里它会把 72bpm 读成 104.6bpm。 */
static float bandpass(float x, uint8_t ch)
{
    float hp;

    /* ---- 阶跃吸附(2026-09-17) ----
     * 输入相对当前直流估计跳变超过 PPG_DC_STEP_FRAC 时, 直接把估计吸附过去
     * —— 这一跳就不进入交流通道了。
     *
     * 【它管的是哪一段 —— 这一点被仿真纠正过, 别按直觉写】
     *   直觉上该由它挡的是"手指刚贴上"那一下: 输入从环境光(约 300)跳到反射
     *   光(约 50000), 高通会把这一跳当成交流, 冲出幅度 36422 的假峰(真实
     *   脉搏波只有 600), 把峰值参考顶到几万。
     *   但仿真(tools/ppg_measure_sim.py 第八节)说明: 那一次并不需要它。
     *   因为去抖 300ms 后 begin_settle() 会【清算法 + 把直流估计锚到当前样本】,
     *   之前冲出来的假峰、连同被顶高的峰值参考, 一起被丢弃 —— 实测去掉吸附,
     *   贴手场景照样能测出 71bpm。
     *
     *   它真正管的是【测量过程中】的阶跃: 手指挪动 / 按压力度变化, 直流突然
     *   跳一大截(实测按紧一点就是 +50%)。这一次没有"清算法重来"的机会 ——
     *   一个假峰就把峰值参考顶到脉搏波的 60 倍(36385 vs 600), 阈值随之抬到真
     *   搏动之上, 之后只能靠"连续 6 秒没峰就减半"的逃生通道往回爬, 而采集
     *   窗口只有 10 秒, 根本等不起: 仿真里那一次阶跃让窗口内的合格拍数从
     *   12 掉到 7。
     *
     * 为什么门槛取 25%: 真实 PPG 的交流/直流调制比只有 0.5%~5%, 而手指挪动 /
     *   压力变化造成的跳变是几十个百分点 —— 差一个数量级以上, 25% 谁也碰不到
     *   谁。所以它【只】在真的发生位移/压力阶跃时生效, 不会误伤脉搏波。 */
    if (ch == 0)
    {
        if (s_band_dc_ir > 1.0f &&
            fabsf(x - s_band_dc_ir) > PPG_DC_STEP_FRAC * s_band_dc_ir)
        {
            s_band_dc_ir = x;
        }
        s_band_dc_ir += s_hp_a * (x - s_band_dc_ir);
        hp = x - s_band_dc_ir;
        s_band_lp_ir += s_lp_a * (hp - s_band_lp_ir);
        return s_band_lp_ir;
    }

    if (s_band_dc_red > 1.0f &&
        fabsf(x - s_band_dc_red) > PPG_DC_STEP_FRAC * s_band_dc_red)
    {
        s_band_dc_red = x;
    }
    s_band_dc_red += s_hp_a * (x - s_band_dc_red);
    hp = x - s_band_dc_red;
    s_band_lp_red += s_lp_a * (hp - s_band_lp_red);
    return s_band_lp_red;
}

/* ---- 中位数(原地排序后取中间那个) ----
 * 中位数只用到"值的集合", 与顺序无关 —— 所以直接在累计数组上排序, 不再另
 * 开一份拷贝(省 160 字节, 这颗片子只有 20K RAM)。最多 40 个元素、每个窗口
 * 结束算一次(加上每拍一次的离群判据), 插入排序的代价可以忽略。
 * 偶数个元素时取靠后的那个(不做两者平均): 这里要的是"最可信的单拍间隔",
 * 平均值反而会被离群值拉偏。 */
static uint32_t median_u16(uint16_t *v, uint8_t n)
{
    uint8_t i, j;

    if (n == 0) return 0;
    for (i = 1; i < n; i++)
    {
        uint16_t key = v[i];
        j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; j--; }
        v[j] = key;
    }
    return (uint32_t)v[n / 2];
}

/* 滚动参照的中位数 —— 与 median_u16 的区别是【不破坏 s_rec_ibi 的时间顺序】。
 * 不能直接用 median_u16: 它是原地排序, 而滚动参照是按 FIFO 滚的, 顺序一乱
 * "丢掉最旧那一拍"就丢错了对象。n ≤ PPG_NOTCH_EST_BEATS(5), 拷一份到局部
 * 数组再插入排序, 只花 10 字节栈。取法(偶数个取靠后那个)与 median_u16
 * 一致, 保证与窗口内的中位数口径相同。 */
static uint32_t rec_ibi_median(void)
{
    uint16_t tmp[PPG_NOTCH_EST_BEATS];
    uint8_t  i, j, n = s_rec_ibi_n;

    if (n == 0) return 0;
    for (i = 0; i < n; i++) tmp[i] = s_rec_ibi[i];

    for (i = 1; i < n; i++)
    {
        uint16_t key = tmp[i];
        j = i;
        while (j > 0 && tmp[j - 1] > key) { tmp[j] = tmp[j - 1]; j--; }
        tmp[j] = key;
    }
    return (uint32_t)tmp[n / 2];
}

static float median_f(float *v, uint8_t n)
{
    uint8_t i, j;

    if (n == 0) return 0.0f;
    for (i = 1; i < n; i++)
    {
        float key = v[i];
        j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; j--; }
        v[j] = key;
    }
    return v[n / 2];
}

/* R → SpO2(按 SPO2_MODE 分档, 见 user_config.h)。
 * 单独抽成函数是因为它现在只在【窗口结束时】算一次: 采集期间只累计 R,
 * 不换算 —— 免得算出一堆中间值又得想办法不泄露出去。 */
static uint16_t spo2_from_r(float r, uint8_t *valid)
{
#if (SPO2_MODE != SPO2_MODE_OFF)
    float f;
  #if (SPO2_MODE == SPO2_MODE_CALIBRATED)
    /* 已标定: SpO2 = A - B × R, A/B 由自己拟合 */
    f = (float)SPO2_CAL_A - (float)SPO2_CAL_B * r;
  #else
    /* 未标定估算: 公开经验式(Maxim 参考设计的二次拟合)。
     * 数值不是医学结论, 报文里会带 spo2_cal=0 标明这一点。 */
    f = SPO2_UNCAL_A * r * r + SPO2_UNCAL_B * r + SPO2_UNCAL_C;
  #endif
    if (f < 0.0f)   f = 0.0f;
    if (f > 100.0f) f = 100.0f;
    *valid = 1;
    return (uint16_t)(f + 0.5f);
#else
    (void)r;
    *valid = 0;
    return 0;
#endif
}

/* 在一拍的时刻扫一遍滑动窗口, 计算 R = (AC_red/DC_red) / (AC_ir/DC_ir)。
 * 窗口里存的已经是带通之后的交流分量, 所以"交流有效值"直接用均方根,
 * 不需要再减均值 —— 基线漂移也不会被算进交流里(旧版正是把它算进去的)。 */
static void update_ratio(void)
{
    uint16_t i;
    float sq_i = 0.0f, sq_r = 0.0f;
    float ac_i, ac_r, r;

    if (s_filled < HR_WIN) return;                      /* 窗口未填满不计算 */

    if (s_band_dc_ir < PPG_DC_MIN_RAW || s_band_dc_red < PPG_DC_MIN_RAW)
    {
        s_ratio_r = 0.0f;                                /* 无有效血流信号 */
        return;
    }

    for (i = 0; i < HR_WIN; i++)
    {
        sq_i += s_ac_ir[i]  * s_ac_ir[i];
        sq_r += s_ac_red[i] * s_ac_red[i];
    }
    ac_i = sqrtf(sq_i / (float)HR_WIN);
    ac_r = sqrtf(sq_r / (float)HR_WIN);

    if (ac_i < 1.0f || (ac_i / s_band_dc_ir) < PPG_MIN_MODULATION)
    {
        s_ratio_r = 0.0f;                                /* 有光但没有脉搏波 */
        return;
    }

    r = (ac_r / s_band_dc_red) / (ac_i / s_band_dc_ir);
    /* R 也做一次一阶平滑 —— 单次算式抖一下就跳。这里平滑的是"每拍一个"的
     * R 采样, 窗口结束时还要对它们取中位数, 两层都对单点异常有抵抗力。 */
    s_ratio_r = (s_ratio_r == 0.0f) ? r : (0.75f * s_ratio_r + 0.25f * r);

    /* 只把【采集期】的 R 计入窗口: 稳定期里的 R 是在血流还没重新分布
     * 稳定的状态下算的, 正是要排除的东西。 */
    if (s_state == HR_ST_MEASURING && s_win_r_n < WIN_MAX)
    {
        s_win_r[s_win_r_n] = s_ratio_r;
        s_win_r_n++;
    }
}

static void on_peak(uint32_t idx, float amp)
{
    uint32_t dt;
    uint16_t period = (uint16_t)(SAMPLE_PERIOD_MS_PPG + 0.5f);

    /* 不应期: 生理上不可能比 PPG_HR_MIN_IBI_MS 更快, 更快的"峰"都是噪声。
     * 【2026-09-17】这个值从 300 提到 400ms —— 重搏切迹落在主波后约 300ms,
     * 300ms 的不应期正好把它放过去(见 user_config.h 的重搏切迹一节)。 */
    if (s_last_peak_idx != 0)
    {
        if ((idx - s_last_peak_idx) * period < PPG_HR_MIN_IBI_MS) return;
    }

    s_since_peak = 0;                    /* 有峰了, 重新捕获计时归零 */

    if (s_amp_ref <= 0.0f) s_amp_ref = amp;
    else                   s_amp_ref += PPG_PEAK_TRACK * (amp - s_amp_ref);

    if (s_last_peak_idx == 0)            /* 第一个峰只作锚点 */
    {
        s_last_peak_idx = idx;
        return;
    }

    dt = (idx - s_last_peak_idx);
    dt = dt * period;                    /* 换算成毫秒 */

    /* ---- 切迹抑制(2026-09-17): 明显短于已有 IBI 的间隔不算一拍 ----
     * 【顺序上最关键的一点】这一段必须在 s_last_peak_idx = idx 之前返回。
     * 旧代码是"先移动锚点, 再判离群", 于是被拒的这一拍已经把锚点带走了,
     * 下一个间隔从假峰起算 —— 一个切迹就能把后面整条链全带偏。
     *
     * 参照取【最近几拍的滚动中位数】而不是窗口内的上界:
     *   伪影的错误方式只有一种 —— 多出一个峰, 只会让间隔【变短】, 所以
     *   "上界"直觉上是最接近真实 IBI 的; 但上界没有自愈能力: 一次异常大的
     *   间隔(如 1640ms)会把它永久抬高, 之后所有真间隔(1000ms)都被判成
     *   "太短"而拒掉。滚动中位数会随新样本自己降回来, 所以选它。
     *
     * 为什么用【时间】而不是幅度(峰后必须回落到某条线):
     *   幅度判据对基线漂移敏感 —— 弱信号下漂移(实测 400)比脉搏波(150)还大,
     *   波形谷底被抬高, 该判据会漏拍, 实测把 90bpm 读成 30bpm(错值!);
     *   而 IBI 是纯时间量, 与漂移无关。详见 ppg_nopulse_sim.py 第六节的对照。
     *
     * 为什么前 PPG_NOTCH_EST_MIN 拍不判: 参照还没攒出来。这几拍由 400ms 的
     *   不应期兜底 —— 本项目的切迹就在 300ms 附近, 挡得住。 */
    if (s_rec_ibi_n >= PPG_NOTCH_EST_MIN)
    {
        uint32_t est = rec_ibi_median();
        if (est > 0 && (float)dt < PPG_NOTCH_MIN_RATIO * (float)est) return;
    }

    s_last_peak_idx = idx;

    if (dt < PPG_HR_MIN_IBI_MS || dt > PPG_HR_MAX_IBI_MS) return;

    /* 【只有采集期才累计】稳定期(DONE 之后也一样)只做预热/跟踪, 不进窗口:
     * 稳定期里指尖血容量还在重新分布, 那几拍测出来的间隔不完全是心搏间隔。 */
    if (s_state != HR_ST_MEASURING)
    {
        update_ratio();                  /* 实时 R 仍在更新, 供诊断 */
        return;
    }

    /* 离群剔除(+逃生通道)。与窗口内已有中位数差太多的间隔多半是噪声造的
     * 假拍, 但【连续】多次都对不上就说明心率是真的变了, 不能永远拒绝下去 ——
     * 仿真里没有这条通道时, 心率从 72 变成 110 后读数会永久卡在 72。
     * 这里的代价比旧版小: 旧版剔除的是"最近 3 拍", 现在窗口有 10 秒、
     * 十来拍, 偶尔剔掉一拍对中位数几乎没有影响。 */
    if (s_win_ibi_n >= 3)
    {
        uint32_t med = median_u16(s_win_ibi, s_win_ibi_n);
        if (med > 0 && ((dt > med) ? (dt - med) : (med - dt)) * 100
                        > med * PPG_IBI_TOL_PCT)
        {
            s_reject_streak++;
            if (s_reject_streak < PPG_IBI_RESET_STREAK) return;
            /* 连续这么多次对不上 → 接受这一拍(逃生通道)。
             * 【2026-09-17 改动】原来这里还要清空 s_win_ibi_n / s_win_r_n,
             * 现在不清了。理由: 10 秒窗口本来就只有十来拍, "清空"等于把已经
             * 攒到的可信样本一起扔掉 —— 切迹场景下正是这条把窗口永远钉在
             * 3 拍上(见文件头的追加三); 而中位数本来就不怕几个离群点, 让它们
             * 进累计并不会拖走结果。逃生通道的职责只是"别永久拒绝真变化",
             * 接受这一拍就够了。 */
        }
        s_reject_streak = 0;
    }

    if (s_win_ibi_n < WIN_MAX)
    {
        s_win_ibi[s_win_ibi_n] = (uint16_t)dt;
        s_win_ibi_n++;

        /* 滚动参照同步滚动(切迹抑制的输入)。
         * 只有【被接受】的拍才进参照 —— 参照代表的是"真实的搏动周期",
         * 用被拒的拍去更新它, 会把参照本身带偏。 */
        if (s_rec_ibi_n < PPG_NOTCH_EST_BEATS)
        {
            s_rec_ibi[s_rec_ibi_n] = (uint16_t)dt;
            s_rec_ibi_n++;
        }
        else
        {
            uint8_t k;
            for (k = 1; k < PPG_NOTCH_EST_BEATS; k++)
            {
                s_rec_ibi[k - 1] = s_rec_ibi[k];     /* FIFO: 丢掉最旧那一拍 */
            }
            s_rec_ibi[PPG_NOTCH_EST_BEATS - 1] = (uint16_t)dt;
        }
    }

    update_ratio();
}

/* 采集窗口结束 → 用整段数据算出最终结果, 之后冻结不再变化。 */
static void finish_measure(void)
{
    uint32_t med;
    uint8_t  i, agree;

    s_fin_bpm = 0; s_fin_bpm_valid = 0;
    s_fin_spo2 = 0; s_fin_spo2_valid = 0;
    s_fin_r = 0.0f;
    s_fin_beats = s_win_ibi_n;

    /* ---- 心率: 窗口内所有合格 IBI 的中位数 + 一致性门槛 ----
     * 10 秒里每一次搏动都有发言权, 而中位数不会被其中一两次伪影拖走 ——
     * 这是"一次测量"相比旧版(只有最近 3 拍投票)最核心的差别。
     *
     * 【但中位数挡不住系统性双峰 —— 这是仿真查出来的, 必须配一致性判据】
     *   信号弱(调制比 0.3%)而基线漂移比脉搏波还大时, 一个心搏会被顶出两个
     *   局部极大值, 检出的间隔变成 "500, 1000, 500, 1000..." 的双峰分布,
     *   而中位数正好落在两峰之间 —— 给不出明显的错值。
     *   tools/ppg_measure_sim.py 实测(真值 72bpm, 6 个噪声 realization):
     *     只看中位数: 6 次里有 2 次报出 115 / 130 bpm;
     *     加一致性门槛: 一次偏差 > 5 的都没有, 不确定的 3 次明确不给结果。
     *   判据 = 与中位数相差不超过 PPG_IBI_TOL_PCT 的拍, 要占到
     *   PPG_WIN_IBI_CONSIST_PCT 以上, 且本身不少于 PPG_WIN_IBI_MIN 拍。
     *   (干净信号实测一致占比 100%, 所以这道门槛不会误伤正常测量。) */
    if (s_win_ibi_n >= PPG_WIN_IBI_MIN)
    {
        med = median_u16(s_win_ibi, s_win_ibi_n);
        agree = 0;
        for (i = 0; i < s_win_ibi_n; i++)
        {
            uint32_t dev = (s_win_ibi[i] > med) ? ((uint32_t)s_win_ibi[i] - med)
                                                : (med - (uint32_t)s_win_ibi[i]);
            if (dev * 100 <= med * PPG_IBI_TOL_PCT) agree++;
        }

        if (med > 0 && agree >= PPG_WIN_IBI_MIN &&
            (uint32_t)agree * 100 >=
            (uint32_t)PPG_WIN_IBI_CONSIST_PCT * s_win_ibi_n)
        {
            s_fin_bpm = (uint16_t)((60000UL + med / 2) / med);
            s_fin_bpm_valid = 1;
        }
    }

    /* ---- 血氧: 窗口内所有合格 R 的中位数 → 换算式 ----
     * 只在心率通过门槛时才算: 同一个窗口、同一段信号, 不会出现"心率不可信
     * 而血氧可信"(两者都受双峰伪影影响, 血氧还会被运动伪影直接带偏)。
     * 阈值 PPG_WIN_R_MIN 是另一道: 只有一两拍时 R 的随机误差可以让血氧差
     * 好几个百分点, 报出来比不报更糟。 */
    if (s_fin_bpm_valid && s_win_r_n >= PPG_WIN_R_MIN)
    {
        s_fin_r = median_f(s_win_r, s_win_r_n);
        s_fin_spo2 = spo2_from_r(s_fin_r, &s_fin_spo2_valid);
    }

    /* 信号可信 = 真的测出了心率: HR_IsSignalOk() 直接复用 HR_IsResultValid()
     * 的判据, 不再单独存一份, 免得两处判断慢慢走偏。 */

    s_state = HR_ST_DONE;
}

static void push_one(uint16_t ir, uint16_t red)
{
    float b_ir, b_red, need;
    uint8_t touched = ((float)ir > PPG_CONTACT_RAW_TH) ? 1 : 0;

    /* ---------------- 接触判定 + 测量状态机 ----------------
     * 判据用原始 IR 值而不是滤波后的直流: 原始值零延迟, 手指一盖上立刻越界;
     * 滤波后的直流要等高通时间常数(0.5Hz ⇒ 约 0.3s)才跟上来。 */
    if (touched)
    {
        if (s_contact_run   < RUN_MAX) s_contact_run++;
        s_nocontact_run = 0;
    }
    else
    {
        if (s_nocontact_run < RUN_MAX) s_nocontact_run++;
        s_contact_run = 0;
    }

    if (s_state == HR_ST_IDLE)
    {
        /* 连续 CONTACT_ON_SAMPLES 个样本都判定为"有手指"才算真贴上。
         * 单点判定会被一次噪声/一次反光误触发 —— 而误触发的代价是白白清掉
         * 一次有效测量(回到 IDLE 再重来), 用 0.3 秒去抖换这个很划算。 */
        if (s_contact_run >= CONTACT_ON_SAMPLES)
        {
            begin_settle(ir, red);
            s_state = HR_ST_SETTLING;
        }
    }
    else if (s_nocontact_run >= CONTACT_OFF_SAMPLES)
    {
        /* 手指拿开 → 立刻作废并回到 IDLE, 状态机本身不再需要超时判据。
         * 【为什么不等超时】拿开之后采到的是环境和噪声, 让算法继续跑只会又
         * 编出数来(旧版 20 秒编了 838 次)。直接停掉, 屏上立刻回到 HR:--
         * —— 用户一眼能看出"我没贴, 所以没数", 而不是"怎么又变了个数"。
         * 采集窗口做一半被拿开时也走这条路: 这一次测量【整次作废】, 不给
         * 半个窗口的结果 —— 那正是"误测"的定义。 */
        clear_algo();
        s_state = HR_ST_IDLE;
        s_settle_left = 0;
        s_inited = 0;      /* 下次贴上时重新把直流估计锚到当时的样本 */
    }
    else if (s_state == HR_ST_SETTLING)
    {
        /* 稳定期倒计时。算法在下面照常跑(滤波预热/窗口填充/峰值参考跟踪),
         * 只是这一段既不采集也不发布 —— 所以 3 秒一到就能直接开始采集,
         * 不用再从零等滤波器收敛。 */
        if (s_settle_left > 0) s_settle_left--;
        if (s_settle_left == 0)
        {
            /* 稳定期结束 → 开始 10 秒采集窗口。
             * 窗口累计数组此时必然是空的: 它只在 MEASURING 期间增长, 而
             * 进入 SETTLING 时的 begin_settle() 已经清过一次(见 clear_algo)。 */
            s_win_left = MEASURE_SAMPLES;
            s_state = HR_ST_MEASURING;
        }
    }
    else if (s_state == HR_ST_MEASURING)
    {
        if (s_win_left > 0) s_win_left--;
        if (s_win_left == 0) finish_measure();
    }
    /* HR_ST_DONE: 结果已冻结 —— 不采集、不重算, 就等手指拿开 */

    /* 直流估计的初值必须取第一个样本 —— 否则滤波器一上电就把整个直流
     * 当成"交流", 产生巨大的启动瞬态, 把自适应阈值顶死再也回不来。
     * 这是旧版最致命的一个 bug(上电后 109 秒内测不到任何峰)。
     * (反复贴/拿的情况由 begin_settle() 负责重新锚定。) */
    if (!s_inited)
    {
        s_band_dc_ir  = (float)ir;
        s_band_dc_red = (float)red;
        s_inited = 1;
    }

    b_ir  = bandpass((float)ir,  0);
    b_red = bandpass((float)red, 1);

    s_ac_ir[s_head]  = b_ir;
    s_ac_red[s_head] = b_red;
    s_head = (uint16_t)((s_head + 1) % HR_WIN);
    if (s_filled < HR_WIN) s_filled++;
    s_idx++;

    /* ---- 寻峰: idx-1 必须比 idx-2 和 idx 都高(真正的局部极大值) ----
     * 旧判据是"上一拍在阈值之上且本拍更低", 它在下降沿的每一步都成立,
     * 一拍会触发很多次, 峰位随波形斜率漂移。 */
    if (s_settle > 0)
    {
        s_settle--;
    }
    else if (s_v1 > s_v2 && s_v1 >= b_ir && s_v1 > 0.0f)
    {
        /* 阈值取两个来源的较大者: 比例项让阈值随信号强弱自适应;
         * 绝对下限让"手指拿开、信号退化成噪声"时彻底停止寻峰。 */
        need = PPG_PEAK_TH_FRAC * s_amp_ref;
        if (need < PPG_PEAK_MIN_AMP) need = PPG_PEAK_MIN_AMP;
        if (s_v1 >= need) on_peak(s_idx - 1, s_v1);
    }

    /* ---- 重新捕获: 太久没有峰 → 把峰值参考降下来 ----
     * "自适应阈值"最典型的失效方式就是被一次异常大值顶高后回不来。
     * 宁可多给一条回得来的路, 也不要一个只会往上走的阈值。 */
    s_since_peak++;
    if (s_since_peak > (uint32_t)REACQ_SAMPLES)
    {
        s_amp_ref *= 0.5f;
        s_since_peak = 0;
    }

    s_v2 = s_v1;
    s_v1 = b_ir;
}

void HR_PushSamples(const uint16_t *ir, const uint16_t *red, uint8_t n)
{
    uint8_t i;

    if (ir == 0 || red == 0 || n == 0) return;
    if (n > PPG_BATCH_MAX) n = PPG_BATCH_MAX;

    for (i = 0; i < n; i++) push_one(ir[i], red[i]);
}

uint8_t HR_GetResult(uint16_t *bpm, uint16_t *spo2_percent, uint8_t *spo2_valid)
{
    /* 只有 DONE 才发布 —— 采集期间屏上是 "pending"、上行是 null。
     * 门控放在这里而不是显示层: 显示和上行走同一个出口, 就不会出现
     * "屏上还在 pending、云端已经收到一个数"这种情况(docs/30、docs/31)。 */
    uint8_t pub = (s_state == HR_ST_DONE) ? 1 : 0;

    if (bpm)          *bpm          = (pub && s_fin_bpm_valid) ? s_fin_bpm : 0;
    if (spo2_percent) *spo2_percent = pub ? s_fin_spo2 : 0;
    if (spo2_valid)   *spo2_valid   = (pub && s_fin_spo2_valid) ? 1 : 0;

    return (uint8_t)(pub && s_fin_bpm_valid);
}

float HR_GetRatioR(void)
{
    /* 采集完成后给的是【最终值】—— 血氧标定要记录的就是"这个最终血氧
     * 对应的那个 R", 中途的实时值没有意义。 */
    if (s_state == HR_ST_DONE) return s_fin_r;
    return s_ratio_r;
}
