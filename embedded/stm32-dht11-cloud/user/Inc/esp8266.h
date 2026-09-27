#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f1xx_hal.h"

/* 初始化内部状态 (在 main 里初始化完 USART1 后调用) */
void ESP8266_Init(UART_HandleTypeDef *huart);

/* 轮询接收中断, 把数据收进环形缓冲区 (main循环中频繁调用) */
void ESP8266_Poll(void);

/* 连接流程: AT测试 -> 关回显 -> Station模式 -> 连WiFi -> TCP连巴法云 -> 订阅主题
 * 返回 0 成功, 1 失败 */
uint8_t ESP8266_Connect(void);

/* 向巴法云主题发布消息, 返回 0 成功 */
uint8_t Bemfa_Publish(const char *msg);

/* 向巴法云发送心跳 "ping\r\n", 返回 0 成功 */
uint8_t Bemfa_Ping(void);

#endif /* __ESP8266_H */
