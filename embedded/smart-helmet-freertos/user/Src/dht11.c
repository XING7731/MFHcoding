/* =========================================================================
 * DHT11 单总线驱动 (HAL + FreeRTOS 版)
 *
 * 关键实现说明(与"裸机版"的差异):
 *   1) 起始信号要求的 ≥18ms 低电平改用 vTaskDelay() 实现 ——
 *      这段时间让出 CPU 给其他任务, 比忙等更合理;
 *      此时数据线已被拉低, 其他任务不会干扰该引脚。
 *   2) 时序采样段(响应信号 + 40bit 数据, 实测约 2.5~4ms)整体放进
 *      taskENTER_CRITICAL() —— 否则 SysTick 或其他中断插入会导致
 *      位判定错误。代价是这几毫秒内中断被关闭, 会给系统带来约 4ms
 *      的调度抖动(报警响应因此最坏延后约 4ms, 可接受)。
 *   3) 临界区内绝对不能用 HAL_Delay()(依赖 SysTick 中断, 会死锁),
 *      只用 delay_us()(硬件计数器轮询)。
 * ========================================================================= */
#include "dht11.h"
#include "bsp_delay.h"
#include "user_config.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- PA1: 直接操作 CRL 寄存器切换输出/输入, 比调用 HAL_GPIO_Init 快得多 ---- */
static void dht11_mode_output(void)
{
    uint32_t crl = DHT11_PORT->CRL;
    crl &= ~(0xFUL << 4);          /* 清 PA1 的 4 位配置 */
    crl |=  (0x3UL << 4);          /* 0b0011: 通用推挽输出, 50MHz */
    DHT11_PORT->CRL = crl;
}

static void dht11_mode_input(void)
{
    uint32_t crl = DHT11_PORT->CRL;
    crl &= ~(0xFUL << 4);
    crl |=  (0x8UL << 4);          /* 0b1000: 上/下拉输入 */
    DHT11_PORT->CRL = crl;
    DHT11_PORT->ODR |= DHT11_PIN;  /* ODR=1 → 内部上拉(外部仍建议 4.7K 上拉) */
}

#define DHT_HIGH()   (DHT11_PORT->BSRR = DHT11_PIN)
#define DHT_LOW()    (DHT11_PORT->BRR  = DHT11_PIN)
#define DHT_READ()   ((DHT11_PORT->IDR & DHT11_PIN) ? 1 : 0)

/* 等待引脚到达指定电平; 超时返回 1 */
static uint8_t wait_level(uint8_t level, uint16_t timeout_us)
{
    while (timeout_us--)
    {
        if (DHT_READ() == level) return 0;
        delay_us(1);
    }
    return 1;
}

uint8_t DHT11_Read(DHT11_Data_TypeDef *data)
{
    uint8_t buf[5] = {0};
    uint8_t i, j;
    uint8_t err = 0;

    /* ---------- 阶段1: 主机起始信号 (可在临界区外, 允许任务切换) ---------- */
    dht11_mode_output();
    DHT_LOW();
    vTaskDelay(pdMS_TO_TICKS(20));   /* ≥18ms 低电平, 让出 CPU */
    DHT_HIGH();
    delay_us(30);                    /* 20~40us 高电平 */
    dht11_mode_input();

    /* ---------- 阶段2: 时序敏感段, 关中断采样 ---------- */
    taskENTER_CRITICAL();

    if (wait_level(0, 100)) err = 1;          /* DHT11 拉低 80us 响应 */
    if (!err && wait_level(1, 100)) err = 1;  /* 再拉高 80us */
    if (!err && wait_level(0, 100)) err = 1;  /* 进入数据位 */

    if (!err)
    {
        for (i = 0; i < 5 && !err; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (wait_level(1, 100)) { err = 1; break; }  /* 每 bit 以 50us 低电平开始 */
                delay_us(40);                                /* 40us 后采样: bit0 已结束, bit1 仍为高 */
                buf[i] <<= 1;
                if (DHT_READ()) buf[i] |= 1;
                if (wait_level(0, 100)) { err = 1; break; }  /* 等本 bit 结束 */
            }
        }
    }

    taskEXIT_CRITICAL();

    if (err) return 1;

    /* ---------- 阶段3: 校验和 (前4字节之和的低8位 == 第5字节) ---------- */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) return 1;

    data->humi_int = buf[0];
    data->humi_dec = buf[1];
    data->temp_int = buf[2];
    data->temp_dec = buf[3];
    return 0;
}
