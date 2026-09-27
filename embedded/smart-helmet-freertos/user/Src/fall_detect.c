/* =========================================================================
 * 摔倒/撞击检测状态机 (核心功能)
 * 判据与状态机设计见 fall_detect.h 头部说明。
 *
 * 【2026-09-16 本版的三个改动, 都是为了回答"怎么扔都不报警"】
 *   ① 新增自由落体入口(路径B)。原判据要求"100ms 内两次超阈", 落到软垫上
 *      常常只有一次尖峰, 于是永远凑不齐"两次" —— 掉下去了也不报警。
 *      "先失重、后落地"才是"掉下去"的本质特征, 且 0g 离 ±2g 的削顶边界
 *      最远, 是最不受量程限制的一处判据。
 *   ② 新增峰值保持。判据不触发时, 现场最需要知道的是"撞击到底有没有被
 *      采到": 峰值显示 2.0 = 采到了(量程内削顶), 一直 1.0 = 根本没采到,
 *      该往回查采样率与带宽, 而不是继续调阈值。
 *   ③ 【修掉路径B 自己的 bug】路径B 刚加进去时, "等落地"这一步用的仍是
 *      HIGH_G(1.8g), 于是它继承了它本该绕开的采样竞态: 撞击高于 1.8g 的
 *      时间只有 1~3ms, 主机 5ms 采一次, 仿真扫相位命中率仅 50% —— 看着像
 *      修好了, 其实还是一半漏报。改为 FALL_FREEFALL_LAND_G(0.9g) 后 100%。
 *      同时给条件3 的稳定带补上下沿 FALL_STABLE_MIN_G(0.8g), 否则失重中的
 *      0g 也满足"|a| < 1.5g", 会被当成"躺稳了"而误报。
 *
 * 注意: 本文件被驱动于【撞击监视任务】(200Hz), 不再挂在 Sensor_Task 的
 *       100ms 周期上 —— 详见 user_config.h 的 IMPACT_PERIOD_MS 说明。
 * ========================================================================= */
#include "fall_detect.h"
#include "user_config.h"
#include <math.h>

static FallState_t s_state = FALL_IDLE;
static uint32_t    s_t_impact = 0;      /* 首次冲击时刻 */
static uint32_t    s_t_stable = 0;      /* 稳定窗口起点 */
static uint32_t    s_t_lock   = 0;      /* 锁定起点 */
static uint32_t    s_confirm_enter = 0; /* 进入确认阶段时刻(用于防卡死) */
static float       s_mag = 0.0f;

#if FALL_FREEFALL_ENABLE
static uint32_t    s_t_ff   = 0;        /* 本次失重的起点(路径B) */
static uint8_t     s_ff_low = 0;        /* 上一采样点是否处于失重 */
#endif

static float       s_peak   = 0.0f;     /* 最近 FALL_PEAK_HOLD_MS 内的 |a| 峰值 */
static uint32_t    s_t_peak = 0;        /* 峰值最后一次被刷新的时刻 */

void FallDetect_Init(void)
{
    s_state = FALL_IDLE;
    s_t_impact = s_t_stable = s_t_lock = s_confirm_enter = 0;
    s_mag = 0.0f;
#if FALL_FREEFALL_ENABLE
    s_t_ff = 0;
    s_ff_low = 0;
#endif
    s_peak = 0.0f;
    s_t_peak = 0;
}

FallState_t FallDetect_GetState(void)  { return s_state; }
float       FallDetect_GetMagnitude(void) { return s_mag; }
float       FallDetect_GetPeak(void)   { return s_peak; }

uint8_t FallDetect_Update(float ax, float ay, float az, uint32_t now_ms)
{
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    uint8_t trigger = 0;

    s_mag = mag;

    /* ---- 峰值保持(与状态机无关, 每个样本都更新) ----
     * 刷新条件: 出现更高的值 → 立刻抬高并重新计时;
     * 否则等到超过 FALL_PEAK_HOLD_MS 没有更高值 → 退回到当前值。
     * 这样摔完 3 秒内屏幕上仍留着撞击峰值, 来得及抄读。 */
    if (mag >= s_peak)
    {
        s_peak   = mag;
        s_t_peak = now_ms;
    }
    else if ((uint32_t)(now_ms - s_t_peak) > FALL_PEAK_HOLD_MS)
    {
        s_peak   = mag;
        s_t_peak = now_ms;
    }

    switch (s_state)
    {
    /* ---------------- 静止/正常状态 ---------------- */
    case FALL_IDLE:
#if FALL_FREEFALL_ENABLE
        /* ---- 路径B 第 1 步: 失重检测 ----
         * 必须"持续"够久才算数: 单点跳到 0g 可能只是总线抖动或一次
         * 采样毛刺, 而真实下落至少持续几十毫秒(0.5m 落地约 320ms)。
         * 用"连续两个采样点都低于门限 + 累计时长"双重限定, 二者缺一不可。 */
        if (mag < FALL_FREEFALL_G)
        {
            if (!s_ff_low)
            {
                s_ff_low = 1;
                s_t_ff   = now_ms;
            }
            else if ((uint32_t)(now_ms - s_t_ff) >= FALL_FREEFALL_MIN_MS)
            {
                s_state = FALL_FREEFALL;   /* 确认在掉, 转去等撞击 */
                break;
            }
        }
        else
        {
            s_ff_low = 0;
        }
#endif
        if (mag > FALL_ACCEL_HIGH_G)          /* 条件1: |a| > 1.8g (受 ±2g 量程限制, 见 user_config.h 第五节) */
        {
            s_state    = FALL_IMPACT;
            s_t_impact = now_ms;
        }
        break;

#if FALL_FREEFALL_ENABLE
    /* ---------------- 已确认失重: 等撞击落地 ---------------- */
    case FALL_FREEFALL:
        if (mag > FALL_FREEFALL_LAND_G)
        {
            /* 路径B 第 2 步: 失重结束 ⇒ 落地 ⇒ 与路径A 汇合, 进入同一个确认流程。
             *
             * 【门限为什么不是 HIGH_G】这里曾经写的也是 FALL_ACCEL_HIGH_G,
             * 结果是路径B 仍然卡在它本该绕开的采样竞态上: 撞击脉冲高于 1.8g
             * 的时间只有 1~3ms, 主机 5ms 采一次, 仿真扫相位命中率仅 50%。
             * 确认过"持续失重"之后, 要问的只是"失重结束了没", 而 0g 与 1.0g
             * 之间差着整整一个 g, 过渡又宽又慢, 用 0.9g 去跨它必然跨得到。
             * 详见 user_config.h 的 FALL_FREEFALL_LAND_G 注释。
             *
             * 这里【不】要求"两次撞击" —— 一次干净的落地就应当算数。 */
            s_state         = FALL_CONFIRM;
            s_t_stable      = now_ms;
            s_confirm_enter = now_ms;
            s_ff_low        = 0;
        }
        else if ((uint32_t)(now_ms - s_t_ff) > FALL_FREEFALL_TIMEOUT_MS)
        {
            /* 失重之后迟迟没有落地(一直 0g 或一直小抖动) → 放弃本次 */
            s_state  = FALL_IDLE;
            s_ff_low = 0;
        }
        break;
#endif

    /* ---------------- 已检测到第一次冲击 ---------------- */
    case FALL_IMPACT:
        if ((now_ms - s_t_impact) <= FALL_WINDOW_MS)
        {
            if (mag > FALL_ACCEL_HIGH_G)      /* 条件2: 窗口内再次超阈值 */
            {
                s_state       = FALL_CONFIRM;
                s_t_stable    = now_ms;
                s_confirm_enter = now_ms;
            }
        }
        else
        {
            /* 窗口内没有第二次冲击 → 视为单次磕碰, 不报警 */
            s_state = FALL_IDLE;
            if (mag > FALL_ACCEL_HIGH_G)      /* 本样本本身可作为新的起点 */
            {
                s_state    = FALL_IMPACT;
                s_t_impact = now_ms;
            }
        }
        break;

    /* ---------------- 确认阶段: 观察是否进入稳定状态 ---------------- */
    case FALL_CONFIRM:
        if (mag >= FALL_STABLE_MIN_G && mag < FALL_ACCEL_LOW_G)
        {
            /* 条件3: 持续 FALL_STABLE_MS 都落在【稳定带 0.8g ~ 1.5g】 → 判定摔倒。
             *
             * 稳定带是"同时被支撑着 + 不动"的意思, 两个边界各挡掉一类错误:
             *   上沿 1.5g: 还在剧烈晃动/连续碰撞 ⇒ 不算稳
             *   下沿 0.8g: 还在失重(0g) ⇒ 也不算稳
             * 【下沿是 2026-09-16 补的】原来只写 < 1.5g, 而自由落体的 |a| ≈ 0g
             * 同样小于 1.5g —— 于是"掉到一半擦到桌沿又继续往下掉"会被判成
             * "躺稳了", 数满 1.5s 就误报警。加上下沿后, 只有真正被支撑着的
             * 那一段(静止时 |a| 恒为 1.0g, 不随姿态漂移)才计时。
             * 这也正是判据可以用模值的原因: 静止时模值恒定。 */
            if ((now_ms - s_t_stable) >= FALL_STABLE_MS)
            {
                trigger = 1;
                s_state = FALL_LOCKOUT;
                s_t_lock = now_ms;
            }
        }
        else
        {
            /* 两个边界之外都只是"还没稳", 稳定窗口重新计时。
             * (包括回弹、晃动、以及"又掉下去了"这三种情况。)
             *
             * 【这里曾经写错过, 而且是个真会漏报的错】早期版本在"又出现强冲击"
             * 时把状态退回 IMPACT 重新找"第二次撞击"。看着合理, 实际会漏报:
             *   头盔落地几乎必然回弹。回弹那一撞发生在进入 CONFIRM 之后 →
             *   状态被退回 IMPACT → 回弹结束后没有第二次撞击 → 窗口(100ms)超时
             *   → 状态回到 IDLE → 【前面那次跌倒被彻底遗忘, 不报警】。
             * 数值仿真里这个场景稳定复现, 现场表现就是"有时候报警有时候不报"。
             *
             * 正确语义: 一旦确认过撞击, 就只问一件事 —— "接下来稳没稳下来"。
             * 回弹只是"还没稳", 重置稳定窗口即可, 不该丢弃已经掌握的信息。
             * 连续碰撞因此也自然被覆盖(每次碰撞都重置计时)。
             * 兜底由下面的防卡死计时器负责(超时仍未稳定则放弃)。 */
            s_t_stable = now_ms;
        }

        /* 防卡死: 长时间无法进入稳定状态则放弃本次判定 */
        if (s_state == FALL_CONFIRM &&
            (now_ms - s_confirm_enter) > (FALL_STABLE_MS * 3))
        {
            s_state = FALL_IDLE;
        }
        break;

    /* ---------------- 报警锁定: 防止连续重复报警 ---------------- */
    case FALL_LOCKOUT:
        if ((now_ms - s_t_lock) >= FALL_LOCKOUT_MS)
        {
            s_state = FALL_IDLE;
        }
        break;

    default:
        s_state = FALL_IDLE;
        break;
    }

    return trigger;
}
