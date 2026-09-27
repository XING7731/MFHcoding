/* =========================================================================
 * bsp_delay.h —— TIM3 硬件微秒延时
 * 用途: DHT11 / DS18B20 / MAX30102 时序要求 us 级延时。
 * 关键点: 延时基于硬件定时器计数, 在 taskENTER_CRITICAL() 关闭中断后
 *         依然有效(HAL_Delay 依赖 SysTick 中断, 在临界区内会死锁)。
 * 前提: TIM3 预分频 72-1 → 计数频率 1MHz, 即 1 计数 = 1us。
 * ========================================================================= */
#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include "stm32f1xx_hal.h"

void bsp_delay_init(TIM_HandleTypeDef *htim);
void delay_us(uint16_t us);      /* 阻塞式, 最大 65535us */

#endif /* __BSP_DELAY_H */
