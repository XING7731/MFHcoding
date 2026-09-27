#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h"

typedef struct
{
    uint8_t humi_int, humi_dec;
    uint8_t temp_int, temp_dec;
} DHT11_Data_TypeDef;

/* 读取成功返回 0, 失败返回 1
 * 注意: 内部包含约 4ms 的临界区(关闭中断)进行单总线时序采样,
 *       并包含 20ms 的 vTaskDelay —— 必须在任务上下文中调用,
 *       不可在中断服务函数或调度器启动前调用。 */
uint8_t DHT11_Read(DHT11_Data_TypeDef *data);

#endif /* __DHT11_H */
