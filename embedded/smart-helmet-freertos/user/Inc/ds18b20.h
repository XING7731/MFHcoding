#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f1xx_hal.h"

/* 采用"两阶段读取"以避免阻塞:
 *   DS18B20_StartConvert() 发出温度转换命令后立即返回(约 1ms 临界区);
 *   DS18B20_ReadTempX10()  在转换时间之后再读取结果(约 6ms 临界区)。
 * 12 位分辨率的转换时间最长, 本驱动按配置的 DS18B20_RESOLUTION 处理,
 * 调用方需保证两次调用间隔大于该分辨率的转换时间(见 user_config.h)。
 * 返回: 0 = 成功, 1 = 失败(器件无应答) */

uint8_t DS18B20_StartConvert(void);
uint8_t DS18B20_ReadTempX10(int16_t *temp_x10);

#endif /* __DS18B20_H */
