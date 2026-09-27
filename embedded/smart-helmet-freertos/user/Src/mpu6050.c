/* =========================================================================
 * MPU6050 六轴传感器驱动 (只读原始寄存器, 不使用 DMP)
 *
 * 量程选择(关键):
 *   需求规定摔倒判据为 |a| > 2.5g。若按常见的 ±2g 配置, 冲击信号会被
 *   量程削顶(饱和在 2g), 2.5g 的判据永远不可能满足 —— 这是很多同类
 *   作品实测失败的原因。因此本驱动按 user_config.h 的 MPU6050_ACCEL_FS_G
 *   (默认 ±8g)配置, 保证 2.5g 判据有足够余量。
 *
 * 灵敏度(由数据手册量程表换算):
 *   ±2g  → 16384 LSB/g      ±4g  → 8192 LSB/g
 *   ±8g  →  4096 LSB/g      ±16g → 2048 LSB/g
 *   ±250°/s → 131 LSB/(°/s)   ±500°/s → 65.5 LSB/(°/s)
 *   ±1000°/s → 32.8           ±2000°/s → 16.4
 * 本驱动不做零偏/温漂标定, 角速度仅用于记录, 不参与摔倒判据。
 *
 * -------------------------------------------------------------------------
 * 【量程写回读验(本版新增) —— 为什么必须有】
 *
 * 起因: 实测发现静止时三轴合加速度报 3.9g(应为 1.0g), 且 3.95 ÷ 4 ≈ 1.0
 *       —— 恰好是 1g 的 4 倍(8g 档 4096 LSB/g ÷ 2g 档 16384 LSB/g = 4)。
 *       即: 配置写的是 ±8g, 芯片实际停在 ±2g 默认档, 而代码按 ±8g 解码。
 *
 * 原有的问题: 三个 reg_write() 的返回值全程未被检查, 所以"量程到底写进去
 *       没有"固件自己也不知道 —— 读数是错的, 但没有任何地方会报错。
 *
 * 本版做法: 写 → 读回 → 核对 → 不一致则重试;
 *       并且灵敏度系数按【实际读回值】决定, 不再按编译期配置"假设"。
 *       这样即使芯片锁在某一档, 报出的 g 值也是对的。
 *
 * 为什么"读数修对了"还不够: 量程本身决定了物理上限。若实际是 ±2g, 冲击
 *       被削顶在 2g, 而摔倒阈值是 2.5g —— 判据永远不可能触发。这一层靠
 *       软件补不回来, 只能报出来让人改配置(fatal 标志 → 屏幕告警)。
 *
 * -------------------------------------------------------------------------
 * 【静止自标定 —— 2026-09-16 新增, 取代"调阈值迁就错误读数"】
 *
 * 上一轮遇到的现象: 静止时三轴模值报 3.9g(应为 1.0g), 且 x 轴恒在 3.9。
 * 上一轮的应对是把判定阈值一并放大(2.5→5.0, 1.5→4.5), 让判据在"放大 4 倍
 * 的读数体系"里继续工作。那是权宜之计, 三个代价:
 *   ① 阈值不再对应需求文档的物理值, 验收/复现时无法自圆其说;
 *   ② 稳定窗口被压成 5.0~4.5(宽 0.5g), 摔后稍有晃动就反复重置计时;
 *   ③ 参数绑死在"这一批片子的错误"上, 换片量程正常的芯片读数又会不准。
 *
 * 本版改用静止自标定, 把读数直接修对。依据的物理事实只有一条:
 *
 *      静止时重力矢量长度恒为 1g  →  |a| ≡ 1g, 与姿态无关。
 *
 * 于是: 把静止期间【原始码值】的模值求平均, 得到的数【就是】真实的 LSB/g
 * —— 因为 码值模值 / 真实LSB/g = 1g, 所以 真实LSB/g = 码值模值。
 *
 * 这一步【完全不依赖任何寄存器读回】。芯片把 ACCEL_CONFIG 伪造成 ±8g 也没用,
 * 它改变不了"静止时模值对应 1g"。这是本方案与读写回校验的本质区别:
 *     读写回校验查的是 "寄存器说的, 和写进去的一致吗?"
 *     静止自标定查的是 "读数和物理定律一致吗?"
 * 克隆片能骗过前者, 骗不过后者。
 *
 * 【顺带回答一个现场疑问: 它测的是加速度还是位置?】
 *   这条判据本身就是答案。位置量在任何姿态下都不可能恒定给出同一个模值;
 *   而加速度计因始终叠着 1g 重力, 模值天然恒定(静止 1g / 失重→0 / 撞击数 g)。
 *   也正因为模值恒定, 摔倒判据只用 |a| 才有意义。
 *
 * 【标定失败怎么办】
 *   上电时帽子若正在被拿着晃动, "静止"前提不成立, 均值没有物理意义 ——
 *   整轮丢弃重来(轮间留出放手时间)。全部失败则回退到寄存器读回推算值
 *   (即上一版行为), 并把 cal_ok 置 0 → 屏幕显示 ! ACAL FAIL。
 *   宁可明确报"标定没做成", 也不假装读数是对的。
 * ========================================================================= */
#include "mpu6050.h"
#include "user_config.h"
#include <string.h>
#include <math.h>

#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_WHO_AM_I      0x75

static I2C_HandleTypeDef *s_i2c = 0;
static float s_accel_lsb_per_g = 4096.0f;   /* 当前换算系数, 初始化前为保守默认 ±8g */
static float s_lsb_reg         = 4096.0f;   /* 寄存器读回推算值, 标定失败时的回退值 */

/* 量程自检结果, 由 MPU6050_Init() 填写。文件作用域静态, 全 0 起始 —— 
 * "没初始化过"与"初始化正常"因此天然可区分。 */
static MPU6050_SelfCheck_t s_sc;

static uint8_t reg_write(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(s_i2c, MPU6050_I2C_ADDR7 << 1, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, 50) == HAL_OK) ? 0 : 1;
}

static uint8_t reg_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(s_i2c, MPU6050_I2C_ADDR7 << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, 50) == HAL_OK) ? 0 : 1;
}

/* 量程(g) → ACCEL_CONFIG bit4:3 的 AFS_SEL 编码 */
static uint8_t fs_g_to_afs(uint8_t g)
{
    switch (g)
    {
        case 2:  return 0x00;
        case 4:  return 0x08;
        case 8:  return 0x10;
        default: return 0x18;   /* ±16g */
    }
}

/* ACCEL_CONFIG 读回值 → 实际量程(g)。只取 AFS_SEL 两位, 忽略寄存器里
 * 其它位(HPF 等) —— 这样即使别的位有差异, 也能正确解出量程。 */
static uint8_t afs_to_fs_g(uint8_t cfg)
{
    switch ((cfg >> 3) & 0x03u)
    {
        case 0:  return 2;
        case 1:  return 4;
        case 2:  return 8;
        default: return 16;
    }
}

/* 量程(g) → 灵敏度 LSB/g (数据手册量程表) */
static float fs_g_to_lsb(uint8_t g)
{
    switch (g)
    {
        case 2:  return 16384.0f;
        case 4:  return 8192.0f;
        case 16: return 2048.0f;
        default: return 4096.0f;   /* ±8g */
    }
}

/* 写一个寄存器并读回核对; 不一致则重试。
 *
 * 返回: 最后一次能读到的寄存器值(读全失败则返回 0)。
 *   *retry_out: 连续不一致的次数; 0 表示一次写入就通过。
 *
 * 【为什么每次重试之间要隔 2ms】
 *   克隆片的寄存器写入偶发被丢掉, 隔一次总线空闲再写成功率更高;
 *   同时给器件内部寄存器更新留出时间, 避免"写进去了但立刻读回还是旧值"
 *   被误判为失败。 */
static uint8_t reg_write_verify(uint8_t reg, uint8_t val, uint8_t *retry_out)
{
    uint8_t rd  = 0;
    uint8_t got = 0;
    uint8_t i;

    for (i = 0; i < MPU6050_WRITE_RETRY_MAX; i++)
    {
        (void)reg_write(reg, val);
        HAL_Delay(2);

        if (reg_read(reg, &rd, 1) == 0)
        {
            got = rd;
            if (rd == val)
            {
                *retry_out = i;
                return rd;
            }
        }
    }

    *retry_out = MPU6050_WRITE_RETRY_MAX;
    return got;
}

/* ---- 读三轴原始码值(不做任何换算)。返回 0 = 成功 ----
 * 标定必须用原始码值: 一旦经过 s_accel_lsb_per_g 换算, 求出来的均值就
 * 不是 LSB/g 了("用要标定的系数去标定它自己", 循环论证)。 */
static uint8_t read_raw_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t buf[6];

    if (reg_read(MPU_REG_ACCEL_XOUT_H, buf, 6)) return 1;

    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
    return 0;
}

/* 灵敏度 LSB/g → 量程(g): 把实测值反推成"芯片实际在哪一档"。
 * 阈值取相邻两档的算术中点 —— 留出 ±50% 的抖动余量, 标定值略有偏差也
 * 不会判到隔壁档去。 */
static uint8_t lsb_to_fs_g(float lsb)
{
    if (lsb >= 12288.0f) return 2;    /* 16384(±2g) 与 8192(±4g) 的中点 */
    if (lsb >= 6144.0f)  return 4;    /* 8192 与 4096(±8g) 的中点 */
    if (lsb >= 3072.0f)  return 8;    /* 4096 与 2048(±16g) 的中点 */
    return 16;
}

/* 静止自标定: 用"静止时 |a| ≡ 1g"反推真实灵敏度 LSB/g。
 *
 * 做法: 采 MPU6050_CAL_SAMPLES 个点, 每点读【原始】三轴码值并算模值;
 *       模值均值就是 LSB/g(因为此时模值在物理上对应 1g)。
 *       同时用"极差/均值"判断这段时间究竟静止没有 —— 动着的样本算出的
 *       均值没有物理意义, 必须整轮丢弃重来。
 *
 * 前置条件: 芯片已解除睡眠(PWR_MGMT_1), 且 ACCEL_CONFIG 已写过。
 * 结果: 写入 s_sc, 并据此设定 s_accel_lsb_per_g。 */
static void accel_self_calibrate(void)
{
    uint8_t round = 0;
    uint8_t ok    = 0;

    while (round < MPU6050_CAL_RETRY_MAX && !ok)
    {
        float   sum = 0.0f;
        float   mn  = 0.0f;
        float   mx  = 0.0f;
        uint8_t i;
        uint8_t n = 0;

        for (i = 0; i < MPU6050_CAL_SAMPLES; i++)
        {
            int16_t rx = 0, ry = 0, rz = 0;

            if (read_raw_accel(&rx, &ry, &rz) == 0)
            {
                /* 先转 float 再相乘: int16 的平方和会溢出 32 位 int
                 * (3 × 32767² ≈ 3.2e9 > 2.1e9)。 */
                float m = sqrtf((float)rx * (float)rx +
                                (float)ry * (float)ry +
                                (float)rz * (float)rz);

                if (n == 0) { mn = m; mx = m; }
                if (m < mn) { mn = m; }
                if (m > mx) { mx = m; }
                sum += m;
                n++;
            }
            HAL_Delay(MPU6050_CAL_INTERVAL_MS);
        }

        /* 至少拿到 3/4 的样本才作数 —— 否则这一次 I2C 本身就不稳,
         * 拿它算出来的均值不可信。 */
        if (n >= (MPU6050_CAL_SAMPLES * 3u / 4u))
        {
            float mean   = sum / (float)n;
            float spread = (mean > 1.0f) ? ((mx - mn) / mean * 100.0f) : 999.0f;

            s_sc.cal_mean_raw    = mean;
            s_sc.cal_spread_pct  = spread;

            /* 两条都满足才认:
             *   ① 真的没动(极差小)  ② 数值落在物理可能的档位区间内
             * 少了第②条, 一个坏传感器输出的乱数也可能"碰巧很稳定"。 */
            if (spread <= MPU6050_CAL_SPREAD_PCT &&
                mean   >= MPU6050_CAL_MIN_LSB &&
                mean   <= MPU6050_CAL_MAX_LSB)
            {
                s_sc.cal_lsb   = mean;
                s_sc.cal_retry = round;
                ok = 1;
            }
        }

        round++;

        /* 本轮没过 → 等一下再重来, 给"手离开帽子"留出时间 */
        if (!ok && round < MPU6050_CAL_RETRY_MAX)
        {
            HAL_Delay(MPU6050_CAL_RETRY_GAP_MS);
        }
    }

    s_sc.cal_ok = ok;

    if (ok)
    {
        /* 实测值即真实灵敏度 —— 直接用它, 不再管寄存器说了什么 */
        s_accel_lsb_per_g = s_sc.cal_lsb;
        s_sc.fs_cal       = lsb_to_fs_g(s_sc.cal_lsb);
    }
    else
    {
        /* 回退: 用寄存器读回推算值(即上一版行为), 并如实标记"未标定" */
        s_accel_lsb_per_g = s_sc.reg_lsb;
        s_sc.fs_cal       = 0;
    }
}

uint8_t MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t who = 0;
    uint8_t cfg_wr;
    uint8_t cfg_rd;
    uint8_t retry = 0;
    uint8_t fs_got;

    /* 每次重新初始化都清空上一次的结论, 避免残留的 fs_match/fatal 骗人 */
    memset(&s_sc, 0, sizeof(s_sc));
    s_accel_lsb_per_g = 4096.0f;
    s_lsb_reg         = 4096.0f;

    s_i2c = hi2c;
    HAL_Delay(50);                         /* 上电稳定等待 */

    (void)reg_write(MPU_REG_PWR_MGMT_1, 0x00);   /* 解除睡眠, 内部 8MHz 时钟 */
    HAL_Delay(10);

    if (reg_read(MPU_REG_WHO_AM_I, &who, 1)) { s_sc.init_rc = 1; return 1; }
    s_sc.who_am_i = who;
    if (who != 0x68) { s_sc.init_rc = 2; return 2; }   /* WHO_AM_I 固定值 */

    /* 采样率 = 1kHz / (1 + SMPLRT_DIV); 默认 0 → 1kHz(见 user_config.h 说明) */
    (void)reg_write(MPU_REG_SMPLRT_DIV, MPU6050_SAMPLE_DIV);
    /* DLPF: 默认 0x01(188Hz/1.9ms)。原为 0x03(44Hz/4.9ms) —— 那会把 5ms 级
     * 的撞击尖峰滤掉约 -14dB, 使 |a| 永远够不到摔倒阈值。详见 user_config.h。 */
    (void)reg_write(MPU_REG_CONFIG, MPU6050_DLPF_CFG);

    /* ---- 加速度量程: AFS_SEL = 0:±2g 1:±4g 2:±8g 3:±16g ----
     * 写 → 读回核对。读回值若与写入值不符, 说明该档位没能生效。 */
    cfg_wr = fs_g_to_afs(MPU6050_ACCEL_FS_G);

    cfg_rd = reg_write_verify(MPU_REG_ACCEL_CONFIG, cfg_wr, &retry);

    /* 【关键】灵敏度按实际读回值换算, 而不是按 cfg_wr。
     * 这样"芯片锁在别的档位"时读数依然正确, 把问题从"数值错但看不出来"
     * 降级为"数值对 + 一条明确的告警"。 */
    fs_got            = afs_to_fs_g(cfg_rd);
    s_lsb_reg         = fs_g_to_lsb(fs_got);
    s_accel_lsb_per_g = s_lsb_reg;      /* 暂用; 下面自标定若成功会覆盖它 */

    s_sc.accel_cfg_wr = cfg_wr;
    s_sc.accel_cfg_rd = cfg_rd;
    s_sc.fs_want      = (uint8_t)MPU6050_ACCEL_FS_G;
    s_sc.fs_got       = fs_got;
    s_sc.fs_match     = (cfg_rd == cfg_wr) ? 1 : 0;
    s_sc.retry        = retry;
    s_sc.reg_lsb      = s_lsb_reg;

    /* 角速度量程: FS_SEL = 0:±250 1:±500 2:±1000 3:±2000 °/s */
    (void)reg_write(MPU_REG_GYRO_CONFIG, 0x08);  /* ±500°/s → 65.5 LSB/(°/s) */

    /* ---- 静止自标定: 用物理定律反推真实灵敏度(见文件头说明) ----
     * 放在这里的两个理由:
     *   ① 必须在解除睡眠、ACCEL_CONFIG 写完之后 —— 标定读的就是加速度计,
     *      芯片还在睡眠或量程未定时, 标出来的系数没有意义;
     *   ② 必须在 FreeRTOS 启动之前 —— 标定要连续 HAL_Delay 约 1 秒,
     *      放调度器里会白白占着 CPU 影响其他任务启动。 */
#if MPU6050_AUTOCAL_ENABLE
    accel_self_calibrate();
#else
    s_accel_lsb_per_g = s_sc.reg_lsb;
    s_sc.cal_ok       = 0;
    s_sc.fs_cal       = 0;
#endif

    /* 【实际量程该信哪个】两个来源的可信度不一样:
     *   标定成功 → fs_cal, 由实测灵敏度反推, 是物理证据, 克隆片伪造不了;
     *   标定失败 → fs_got, 来自寄存器读回, 只说明"寄存器是怎么声称的"。
     * fatal 优先依据前者, 因为它判定的是"冲击会不会被削顶"这个物理事实。 */
    s_sc.fs_used = s_sc.cal_ok ? s_sc.fs_cal : s_sc.fs_got;
    /* 量程 ≤ 摔倒阈值 → 冲击被削顶, |a| 永远够不到阈值, 判据静默失效。
     * 用浮点比较(而非截断成整数), 否则阈值 2.5g 会被当成 2g 而漏判。 */
    s_sc.fatal   = ((float)s_sc.fs_used <= FALL_ACCEL_HIGH_G) ? 1 : 0;

    s_sc.init_rc = 0;
    return 0;
}

uint8_t MPU6050_Read(MPU6050_Data_t *d)
{
    uint8_t buf[14];
    int16_t ax, ay, az, gx, gy, gz;

    if (reg_read(MPU_REG_ACCEL_XOUT_H, buf, 14)) return 1;

    ax = (int16_t)((buf[0]  << 8) | buf[1]);
    ay = (int16_t)((buf[2]  << 8) | buf[3]);
    az = (int16_t)((buf[4]  << 8) | buf[5]);
    /* buf[6..7] 为片内温度, 本项目的温度来源是 DHT11/DS18B20, 故不使用 */
    gx = (int16_t)((buf[8]  << 8) | buf[9]);
    gy = (int16_t)((buf[10] << 8) | buf[11]);
    gz = (int16_t)((buf[12] << 8) | buf[13]);

    /* 【加速度就在这里算出来的】原始码值 ÷ 灵敏度 = 多少 g。
     * 分母取自 MPU6050_Init() 的静止自标定结果, 并且运行期间会被静默期在线
     * 标定继续修正(见 MPU6050_OnlineCalFeed)。
     * 所以 d->ax/ay/az 的单位是 g, 是真实的加速度 —— 不是位移, 也不是角度。
     * 静止时三轴模值 sqrt(ax²+ay²+az²) 必为 1.0g, 现场据此自查换算对不对。 */
    d->ax = (float)ax / s_accel_lsb_per_g;
    d->ay = (float)ay / s_accel_lsb_per_g;
    d->az = (float)az / s_accel_lsb_per_g;

    /* 同一帧的原始码值一并带出, 供在线标定使用(避免再读一次总线) */
    d->raw_ax = ax;
    d->raw_ay = ay;
    d->raw_az = az;

    d->gx = (float)gx / 65.5f;
    d->gy = (float)gy / 65.5f;
    d->gz = (float)gz / 65.5f;

    return 0;
}

const MPU6050_SelfCheck_t *MPU6050_GetSelfCheck(void)
{
    return &s_sc;
}

uint8_t MPU6050_GetAccelFsG(void)
{
    return s_sc.fs_got;
}

float MPU6050_GetAccelLsbPerG(void)
{
    return s_accel_lsb_per_g;
}

uint8_t MPU6050_IsCalibrated(void)
{
    return s_sc.cal_ok;
}

/* =========================================================================
 * 静默期在线标定(2026-09-16 新增)
 *
 * 【要解决的问题】上电自标定要求"上电那一瞬间帽子静止"。现场更常见的是
 *   顺手拿着就上电 —— 那一轮标定必然失败, 读数继续被放大 4 倍(静止 3.9),
 *   而阈值是 1.8/1.5, 于是【判据静默失效: 怎么扔都不报警, 屏幕却一切正常】。
 *   实测反馈正是这一条("无论怎么放都是大约 3.9")。
 *
 * 【做法】把"用静止窗口反推灵敏度"这件事从"上电一次"改成"运行期持续进行"。
 *   每 MPU6050_ONLINE_CAL_WIN_MS 收一个窗口, 窗口内累积原始模值的
 *   计数/和/极差(O(1) 内存, 不用数组); 窗口结束时:
 *       ① 极差 ≤ SPREAD%       → 这段真的没动, 均值才有物理意义
 *       ② 均值 ∈ 物理合理区间  → 排除坏传感器输出的"稳定乱数"
 *   两条都满足才动换算系数。
 *
 * 【两条修正策略, 对应两类不同的问题】
 *   偏差 > SNAP 比例 → 直接替换。修的是"上电标定失败"这类量级错误
 *                      (4096 → 16384, 差 4 倍), 慢慢逼近要等好几秒才准。
 *   偏差 ≤ SNAP 比例 → 平滑跟随(1/4 步长)。修的是温漂、轻微倾斜之类慢变化。
 *
 * 【为什么不会把撞击当成 1g】撞击/晃动期间极差必然远超 3%, 第①条直接拦掉。
 * 【为什么要重算 fs_cal/fatal】在线值同样是"物理实测反推"的结论, 且窗口比
 *   上电那轮更长、判据更严 —— 它有权推翻上电时基于寄存器读回得到的结论。
 * ========================================================================= */
static float    s_oc_sum     = 0.0f;
static float    s_oc_mn      = 0.0f;
static float    s_oc_mx      = 0.0f;
static uint16_t s_oc_n       = 0;
static uint32_t s_oc_t0      = 0;
static uint8_t  s_oc_started = 0;

void MPU6050_OnlineCalFeed(int16_t raw_ax, int16_t raw_ay, int16_t raw_az)
{
#if MPU6050_ONLINE_CAL_ENABLE
    uint32_t now = HAL_GetTick();
    float    m;

    if (!s_oc_started)
    {
        s_oc_started = 1;
        s_oc_t0      = now;
    }

    m = sqrtf((float)raw_ax * (float)raw_ax +
              (float)raw_ay * (float)raw_ay +
              (float)raw_az * (float)raw_az);

    if (s_oc_n == 0) { s_oc_mn = m; s_oc_mx = m; }
    if (m < s_oc_mn) { s_oc_mn = m; }
    if (m > s_oc_mx) { s_oc_mx = m; }
    s_oc_sum += m;
    s_oc_n++;

    /* 窗口未满 → 继续累积 */
    if ((uint32_t)(now - s_oc_t0) < MPU6050_ONLINE_CAL_WIN_MS) return;

    /* 样本太少(如 I2C 大量失败)则本窗口作废, 不拿稀疏数据下结论 */
    if (s_oc_n >= 8u)
    {
        float mean   = s_oc_sum / (float)s_oc_n;
        float spread = (mean > 1.0f) ? ((s_oc_mx - s_oc_mn) / mean * 100.0f) : 999.0f;

        s_sc.online_spread = spread;

        if (spread <= MPU6050_ONLINE_CAL_SPREAD &&
            mean   >= MPU6050_CAL_MIN_LSB &&
            mean   <= MPU6050_CAL_MAX_LSB)
        {
            if (fabsf(mean - s_accel_lsb_per_g) >
                s_accel_lsb_per_g * MPU6050_ONLINE_CAL_SNAP)
            {
                s_accel_lsb_per_g = mean;                       /* 量级错了: 直接换 */
            }
            else
            {
                s_accel_lsb_per_g = s_accel_lsb_per_g * 0.75f + mean * 0.25f;
            }

            s_sc.online_lsb = s_accel_lsb_per_g;
            if (s_sc.online_ok < 255u) { s_sc.online_ok++; }

            s_sc.fs_cal  = lsb_to_fs_g(s_accel_lsb_per_g);
            s_sc.fs_used = s_sc.fs_cal;
            s_sc.fatal   = ((float)s_sc.fs_cal <= FALL_ACCEL_HIGH_G) ? 1 : 0;
        }
    }

    /* 开新窗口 */
    s_oc_sum = 0.0f;
    s_oc_mn  = 0.0f;
    s_oc_mx  = 0.0f;
    s_oc_n   = 0;
    s_oc_t0  = now;
#else
    (void)raw_ax; (void)raw_ay; (void)raw_az;
#endif
}

uint8_t MPU6050_IsScaleTrusted(void)
{
    /* 上电标定成功, 或在线标定至少修过一次 —— 两者都是"物理实测反推",
     * 读数是真实 g。任一成立, 阈值 1.8/1.5 才有意义。 */
    return (s_sc.cal_ok || s_sc.online_ok) ? 1 : 0;
}
