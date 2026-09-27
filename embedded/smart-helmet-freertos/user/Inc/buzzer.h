#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f1xx_hal.h"

/* 硬件: 需求清单只写了"蜂鸣器(GPIO 控制)", 未说明是有源还是被动式。
 * 本驱动按【有源蜂鸣器】实现: GPIO 给电平即响。
 *
 * 触发极性由 user_config.h 的 BUZZER_ACTIVE_HIGH 决定, 无需改动本文件:
 *   0 = 低电平触发 (S 脚拉低时鸣响)   ← 本工程实际硬件
 *   1 = 高电平触发
 *
 * 若实际是被动蜂鸣器(无源), 需要 PWM 驱动才能发声 —— 此时请把
 * buzzer.c 的 Buzzer_On 换成启动一个 TIM PWM 通道, 并注意
 * "响铃时长"由上层 Alarm_Task 控制, 本驱动只负责开关。
 * ========================================================================= */
void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);

#endif /* __BUZZER_H */
