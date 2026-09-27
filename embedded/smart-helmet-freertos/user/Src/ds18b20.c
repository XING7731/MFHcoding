/* =========================================================================
 * DS18B20 单总线温度传感器驱动 (HAL + FreeRTOS 版)
 *
 * 设计要点:
 *   1) 引脚固定配置为【开漏输出 + 外部 4.7K 上拉】:
 *      写 1 时引脚为高阻, 由上拉电阻拉高; 读 IDR 即可获得总线实际电平。
 *      因此读写过程中无需切换引脚方向, 时序更稳。
 *   2) 12 位分辨率的温度转换需要较长时间, 若用阻塞等待会挤占实时任务。
 *      这里采用【两阶段读取】: StartConvert() 只发命令后立即返回,
 *      由 Sensor_Task 在下一个周期(1s 后)再调 ReadTempX10() 取结果。
 *      本次不发配置字节, 使用器件出厂默认分辨率(12 位)。
 *   3) 时序段(约 5~6ms)同样用 taskENTER_CRITICAL() 保护; 内部只使用
 *      delay_us(), 不使用任何依赖中断的延时。
 * ========================================================================= */
#include "ds18b20.h"
#include "bsp_delay.h"
#include "user_config.h"
#include "FreeRTOS.h"
#include "task.h"

#define OW_LOW()      (DS18B20_PORT->BRR  = DS18B20_PIN)   /* 开漏拉低 */
#define OW_RELEASE()  (DS18B20_PORT->BSRR = DS18B20_PIN)   /* 高阻, 上拉 */
#define OW_READ()     ((DS18B20_PORT->IDR & DS18B20_PIN) ? 1 : 0)

#define OW_CMD_SKIP_ROM   0xCC
#define OW_CMD_CONVERT_T  0x44
#define OW_CMD_READ_SCR   0xBE

static void ow_delay_us(uint16_t us) { delay_us(us); }

/* 复位脉冲 + 存在脉冲检测: 0 = 器件有应答 */
static uint8_t ow_reset(void)
{
    uint8_t presence;

    OW_LOW();
    ow_delay_us(480);
    OW_RELEASE();
    ow_delay_us(70);
    presence = OW_READ();      /* 器件在 15~60us 内拉低总线 */
    ow_delay_us(400);          /* 等待总线释放 */

    return presence ? 1 : 0;
}

static void ow_write_bit(uint8_t bit)
{
    OW_LOW();
    ow_delay_us(2);
    if (bit) OW_RELEASE();
    ow_delay_us(60);
    OW_RELEASE();
    ow_delay_us(2);
}

static uint8_t ow_read_bit(void)
{
    uint8_t bit;

    OW_LOW();
    ow_delay_us(3);
    OW_RELEASE();
    ow_delay_us(10);
    bit = OW_READ();
    ow_delay_us(50);
    return bit;
}

static void ow_write_byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        ow_write_bit(byte & 0x01);
        byte >>= 1;            /* 低位在前 */
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t i, byte = 0;
    for (i = 0; i < 8; i++)
    {
        byte >>= 1;
        if (ow_read_bit()) byte |= 0x80;
    }
    return byte;
}

/* Dallas/Maxim CRC-8 (多项式 0x31 的反射形式 0x8C), 用于校验暂存器数据 */
static uint8_t ow_crc8(const uint8_t *d, uint8_t len)
{
    uint8_t crc = 0, i, j, inbyte, mix;

    for (i = 0; i < len; i++)
    {
        inbyte = d[i];
        for (j = 0; j < 8; j++)
        {
            mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

uint8_t DS18B20_StartConvert(void)
{
    uint8_t err;

    taskENTER_CRITICAL();
    err = ow_reset();
    if (!err)
    {
        ow_write_byte(OW_CMD_SKIP_ROM);    /* 单器件: 跳过 ROM 匹配 */
        ow_write_byte(OW_CMD_CONVERT_T);   /* 启动温度转换 */
    }
    taskEXIT_CRITICAL();

    return err;
}

uint8_t DS18B20_ReadTempX10(int16_t *temp_x10)
{
    uint8_t buf[9];
    uint8_t i, err;
    int16_t raw;

    taskENTER_CRITICAL();
    err = ow_reset();
    if (!err)
    {
        ow_write_byte(OW_CMD_SKIP_ROM);
        ow_write_byte(OW_CMD_READ_SCR);    /* 读暂存器 9 字节 */
        for (i = 0; i < 9; i++) buf[i] = ow_read_byte();
    }
    taskEXIT_CRITICAL();

    if (err) return 1;

    /* 第 9 字节是前 8 字节的 CRC 校验值 */
    if (ow_crc8(buf, 8) != buf[8]) return 1;

    raw = (int16_t)((buf[1] << 8) | buf[0]);

    /* DS18B20 原始值 = 温度(°C) × 16  →  0.1°C 单位 = 原始值 × 10 / 16 */
    *temp_x10 = (int16_t)(((int32_t)raw * 5) / 8);
    return 0;
}
