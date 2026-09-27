#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f1xx_hal.h"

typedef struct
{
    uint8_t humi_int;    /* 湿度整数部分 */
    uint8_t humi_dec;    /* 湿度小数部分 */
    uint8_t temp_int;    /* 温度整数部分 */
    uint8_t temp_dec;    /* 温度小数部分 */
} DHT11_Data_TypeDef;

/* 读取成功返回 0, 失败返回 1 (校验和错误/无响应) */
uint8_t DHT11_Read(DHT11_Data_TypeDef *data);

#endif /* __DHT11_H */
