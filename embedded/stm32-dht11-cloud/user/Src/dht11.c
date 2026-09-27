/* =========================================================
 * DHT11 单总线驱动 (HAL库版)
 * 引脚见 user_config.h, 默认 PA1
 * 时序依赖 TIM3 微秒延时 (bsp_delay)
 * 注意: DHT11 数据线需 4.7K~10K 上拉电阻到 VCC
 * ========================================================= */
#include "dht11.h"
#include "bsp_delay.h"
#include "user_config.h"

/* ---- 底层: F103 直接操作 CRL 寄存器切换输入/输出, 速度快且省事 ---- */
static void dht11_mode_output(void)
{
    /* PA1 -> 通用推挽输出 50MHz: CRL 第1个nibble = 0x3 */
    uint32_t crl = DHT11_PORT->CRL;
    crl &= ~(0xFUL << 4);
    crl |=  (0x3UL << 4);
    DHT11_PORT->CRL = crl;
}

static void dht11_mode_input(void)
{
    /* PA1 -> 上拉/下拉输入: CRL 第1个nibble = 0x8, 配合 ODR=1 上拉 */
    uint32_t crl = DHT11_PORT->CRL;
    crl &= ~(0xFUL << 4);
    crl |=  (0x8UL << 4);
    DHT11_PORT->CRL = crl;
    DHT11_PORT->ODR |= DHT11_PIN;   /* 上拉 */
}

#define DHT11_HIGH()  (DHT11_PORT->BSRR = DHT11_PIN)
#define DHT11_LOW()   (DHT11_PORT->BRR  = DHT11_PIN)
#define DHT11_READ()  ((DHT11_PORT->IDR & DHT11_PIN) ? 1 : 0)

/* 等待电平变为期望值, 超时返回 1 */
static uint8_t wait_level(uint8_t level, uint16_t timeout_us)
{
    while (timeout_us--)
    {
        if (DHT11_READ() == level) return 0;
        delay_us(1);
    }
    return 1;
}

uint8_t DHT11_Read(DHT11_Data_TypeDef *data)
{
    uint8_t buf[5] = {0};
    uint8_t i, j;

    /* 1. 主机起始信号: 拉低 >=18ms, 再拉高 20~40us */
    dht11_mode_output();
    DHT11_LOW();
    HAL_Delay(20);
    DHT11_HIGH();
    delay_us(30);
    dht11_mode_input();

    /* 2. 等待 DHT11 响应: 先拉低80us再拉高80us */
    if (wait_level(0, 100)) return 1;   /* 低电平开始 -> 有响应 */
    if (wait_level(1, 100)) return 1;
    if (wait_level(0, 100)) return 1;   /* 80us高电平结束 */

    /* 3. 连续读 40bit: 每bit先50us低电平, 后跟高电平 26~28us(0) / 70us(1) */
    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 8; j++)
        {
            if (wait_level(1, 100)) return 1;       /* 等待高电平开始 */
            delay_us(40);                            /* 40us后采样: 0已结束, 1还在 */
            buf[i] <<= 1;
            if (DHT11_READ()) buf[i] |= 1;
            if (wait_level(0, 100)) return 1;       /* 等待本bit结束 */
        }
    }

    /* 4. 校验和: 前4字节之和的低8位 == 第5字节 */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) return 1;

    data->humi_int = buf[0];
    data->humi_dec = buf[1];
    data->temp_int = buf[2];
    data->temp_dec = buf[3];
    return 0;
}
