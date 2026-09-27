/* =========================================================================
 * MAX30102 心率/血氧传感器驱动 (SpO2 模式, FIFO 批量读取)
 *
 * 本驱动边界(重要):
 *   只负责"把原始 PPG 数据完整、可靠地取出来"。
 *   心率/血氧换算属于算法层(见 heart_rate.c), 其中血氧换算系数必须标定
 *   ——本驱动不生成任何未经标定的生理数值。
 *
 * 寄存器地址与位域按 MAX30102 数据手册:
 *   0x00 INT_STATUS   0x02 INT_ENABLE   0x04 FIFO_WR_PTR  0x05 OVF_COUNTER
 *   0x06 FIFO_RD_PTR  0x07 FIFO_DATA    0x08 FIFO_CONFIG  0x09 MODE_CONFIG
 *   0x0A SPO2_CONFIG  0x0C LED1_PA(RED) 0x0D LED2_PA(IR)  0xFF PART_ID(0x15)
 * 说明: 具体寄存器值与位域请以你手上的数据手册版本为准, 本文件已标注
 *       每一处的含义, 便于逐条核对。
 *
 * 分辨率处理: FIFO 每通道 3 字节(18 位有效)。本驱动把有效位右移 2 位
 * 后存入 uint16_t —— 用于波形形态与 AC/DC 比值计算; 若你的算法需要
 * 完整 18 位分辨率, 请改用 uint32_t 缓冲(会显著增加 RAM 占用, 因为
 * 心率窗口需要 PPG_HR_WINDOW 点缓存)。
 * ========================================================================= */
#include "max30102.h"
#include "user_config.h"

#define MAX_REG_INT_STATUS    0x00
#define MAX_REG_INT_ENABLE    0x02
#define MAX_REG_FIFO_WR_PTR   0x04
#define MAX_REG_OVF_COUNTER   0x05
#define MAX_REG_FIFO_RD_PTR   0x06
#define MAX_REG_FIFO_DATA     0x07
#define MAX_REG_FIFO_CONFIG   0x08
#define MAX_REG_MODE_CONFIG   0x09
#define MAX_REG_SPO2_CONFIG   0x0A
#define MAX_REG_LED1_PA       0x0C   /* RED */
#define MAX_REG_LED2_PA       0x0D   /* IR  */
#define MAX_REG_PART_ID       0xFF

#define MAX_PART_ID_VALUE     0x15

/* ---- 两个"配置宏必须真的接进寄存器"的地方 ----
 * SPO2_CONFIG 的采样率位与 FIFO_CONFIG 的平均位, 以前是写死的 ——
 * 于是 user_config.h 里的 PPG_SAMPLE_RATE_HZ / PPG_FIFO_AVG 改了也不生效,
 * 而心率算法就按"20ms 一个样本"在算时间。配置宏与实际寄存器不一致,
 * 是这类问题里最难查的一种: 代码看起来完全正确。 */
static uint8_t spo2_sr_code(uint16_t hz)
{
    switch (hz)
    {
    case 50:   return 0;
    case 100:  return 1;
    case 200:  return 2;
    case 400:  return 3;
    case 800:  return 4;
    case 1000: return 5;
    case 1600: return 6;
    case 3200: return 7;
    default:   return 0;   /* 不认识的速率按 50Hz —— 与算法的时间假设保持一致 */
    }
}

static uint8_t fifo_avg_code(uint8_t avg)
{
    /* 器件编码是 log2: 000=不平均(1), 001=2, 010=4, 011=8, 100=16, 101=32 */
    uint8_t c = 0;
    while (avg > 1 && c < 5) { avg = (uint8_t)(avg >> 1); c++; }
    return c;
}

#define SPO2_ADC_RANGE_CODE   0x01   /* bit6:5 ADC 量程: 01 = 4096nA */
#define SPO2_LED_PW_CODE      0x03   /* bit1:0 LED 脉宽: 11 = 411us, 18bit */

static I2C_HandleTypeDef *s_i2c = 0;
static uint32_t s_samples_read = 0;   /* 已读取样本计数, 用于预热判断 */
static uint8_t  s_present = 0;        /* 1 = 初始化成功, 器件确实在 I2C 总线上 */
static uint8_t  s_init_code = 0xFF;   /* 最近一次 Init 的返回码(0xFF = 从未调用) */

static uint8_t reg_write(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(s_i2c, MAX30102_I2C_ADDR7 << 1, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, 50) == HAL_OK) ? 0 : 1;
}

static uint8_t reg_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(s_i2c, MAX30102_I2C_ADDR7 << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, 50) == HAL_OK) ? 0 : 1;
}

uint8_t MAX30102_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0;
    uint8_t spo2_cfg;
    uint8_t fifo_cfg;

    s_i2c = hi2c;
    s_samples_read = 0;
    s_present = 0;                    /* 先假设不在总线, 只有走完全流程才置 1 */

    /* 软复位并等待完成(手册建议复位后等待, 此处轮询状态寄存器) */
    if (reg_write(MAX_REG_MODE_CONFIG, 0x40)) { s_init_code = 1; return 1; }
    HAL_Delay(50);

    /* 读 PART_ID 是判断"器件到底在不在总线上"最直接的一步:
     * 若模块没接好/没供电/地址不对, 这里会因无 ACK 而失败。 */
    if (reg_read(MAX_REG_PART_ID, &id, 1)) { s_init_code = 2; return 2; }
    if (id != MAX_PART_ID_VALUE)           { s_init_code = 3; return 3; }

    /* 清中断状态、关闭中断输出(本驱动用轮询方式读 FIFO) */
    reg_write(MAX_REG_INT_ENABLE, 0x00);
    {
        uint8_t st = 0xFF;
        reg_read(MAX_REG_INT_STATUS, &st, 1);
    }

    /* FIFO 指针归零 */
    reg_write(MAX_REG_FIFO_WR_PTR, 0x00);
    reg_write(MAX_REG_OVF_COUNTER, 0x00);
    reg_write(MAX_REG_FIFO_RD_PTR, 0x00);

    /* FIFO_CONFIG: bit7:5 = 样本平均(由 PPG_FIFO_AVG 决定; 填 1 表示不平均),
     *              bit4   = FIFO 回卷使能(1 = 满后覆盖最旧数据, 避免卡死),
     *              bit3:0 = 几乎满阈值(0000 = 17 个样本)
     * ⚠ 打开平均会改变"每个样本代表的时间", 见 user_config.h 的说明。 */
    fifo_cfg = (uint8_t)((fifo_avg_code(PPG_FIFO_AVG) << 5) | (1 << 4) | 0x00);
    reg_write(MAX_REG_FIFO_CONFIG, fifo_cfg);

    /* SPO2_CONFIG: bit6:5 = ADC 量程, bit4:2 = 采样率, bit1:0 = LED 脉宽
     * 采样率取自 PPG_SAMPLE_RATE_HZ —— 心率算法把它当时间基准用,
     * 这两处必须是同一个数。 */
    spo2_cfg = (uint8_t)((SPO2_ADC_RANGE_CODE << 5) |
                         ((spo2_sr_code(PPG_SAMPLE_RATE_HZ) & 0x07) << 2) |
                          SPO2_LED_PW_CODE);
    reg_write(MAX_REG_SPO2_CONFIG, spo2_cfg);

    /* LED 电流: 步进 0.2mA/LSB(以数据手册为准)。数值由 PPG_LED_PA_* 决定 ——
     * 2026-09-21 从 0x24(7.2mA) 提到 0x4F(15.8mA), 依据(出数率扫描)写在
     * user_config.h 那两个宏上面, 这里不重复。 */
    reg_write(MAX_REG_LED1_PA, PPG_LED_PA_RED);
    reg_write(MAX_REG_LED2_PA, PPG_LED_PA_IR);

    /* 进入 SpO2 模式(红光 + 红外两路 LED) */
    reg_write(MAX_REG_MODE_CONFIG, 0x03);
    HAL_Delay(20);

    s_present   = 1;
    s_init_code = 0;
    return 0;
}

uint8_t MAX30102_IsPresent(void)
{
    return s_present;
}

uint8_t MAX30102_LastInitCode(void)
{
    return s_init_code;
}

uint8_t MAX30102_IsWarmedUp(void)
{
    return (s_samples_read >= PPG_WARMUP_SAMPLES) ? 1 : 0;
}

uint8_t MAX30102_ReadFifo(MAX30102_Fifo_t *f)
{
    uint8_t ptr[2];
    uint8_t raw[PPG_BATCH_MAX * 6];
    uint8_t count, i, n;
    uint8_t wr, rd;
    uint32_t v;

    f->n = 0;
    f->lost = 0;

    if (reg_read(MAX_REG_FIFO_WR_PTR, ptr, 2)) return 1;   /* 0x04 WR_PTR, 0x05 OVF */
    wr = ptr[0] & 0x1F;

    /* OVF_COUNTER: "写指针溢出"的次数 —— 非 0 就说明有样本被覆盖丢掉了。
     * 旧版读了这两个字节却只用第一个, 把这个信息丢了(见 max30102.h 的说明)。
     * 读完立刻清零, 让这个数表达"本轮新丢了多少"。 */
    f->lost = ptr[1] & 0x1F;
    if (f->lost) reg_write(MAX_REG_OVF_COUNTER, 0x00);

    if (reg_read(MAX_REG_FIFO_RD_PTR, &rd, 1)) return 2;
    rd &= 0x1F;

    count = (uint8_t)((wr - rd) & 0x1F);      /* FIFO 中待读样本数(0~31) */
    if (count == 0) return 0;                 /* 本轮暂无新样本, 非错误 */

    if (f->lost)
    {
        /* 溢出过 ⇒ FIFO 里最旧的那批样本已经被覆盖/过期。
         * 若只按 PPG_BATCH_MAX 读一部分, 剩下的旧样本会在下一轮才被推给
         * 算法, 于是"旧数据插在新数据后面", 时间轴彻底乱掉。
         * 正确做法: 把读指针直接推到"只留最新 n 个"的位置, 主动丢弃旧的。 */
        uint8_t keep = (count > PPG_BATCH_MAX) ? PPG_BATCH_MAX : count;
        uint8_t new_rd = (uint8_t)((wr - keep) & 0x1F);
        if (reg_write(MAX_REG_FIFO_RD_PTR, new_rd)) return 4;
        rd = new_rd;
        count = keep;
    }

    n = (count > PPG_BATCH_MAX) ? PPG_BATCH_MAX : count;

    if (reg_read(MAX_REG_FIFO_DATA, raw, (uint16_t)(n * 6))) return 3;

    for (i = 0; i < n; i++)
    {
        /* 每样本 6 字节: RED 3 字节 + IR 3 字节, 18 位有效 */
        v = ((uint32_t)raw[i * 6 + 0] << 16) |
            ((uint32_t)raw[i * 6 + 1] << 8)  |
             (uint32_t)raw[i * 6 + 2];
        f->red[i] = (uint16_t)((v & 0x03FFFF) >> 2);

        v = ((uint32_t)raw[i * 6 + 3] << 16) |
            ((uint32_t)raw[i * 6 + 4] << 8)  |
             (uint32_t)raw[i * 6 + 5];
        f->ir[i] = (uint16_t)((v & 0x03FFFF) >> 2);
    }

    f->n = n;
    s_samples_read += n;
    return 0;
}
