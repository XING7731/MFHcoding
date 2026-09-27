#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include "stm32f1xx_hal.h"

/* 基于 TIM3 的微秒级延时 (需在 CubeMX 中配置 TIM3: 72分频 -> 1us计数) */
void bsp_delay_init(TIM_HandleTypeDef *htim);
void delay_us(uint16_t us);

#endif /* __BSP_DELAY_H */
