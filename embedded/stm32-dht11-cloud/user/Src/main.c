/* =========================================================
 * main.c 集成示例 —— STM32F103C8T6 + DHT11 + ESP8266 + 巴法云
 *
 * !!!!! 使用方法 !!!!!
 * 1. 用 CubeMX 生成 Keil 工程 (配置见 docs/02-CubeMX配置指南.md)
 * 2. 把 user/ 目录下的 5 组 .c/.h 文件加入工程
 * 3. 用本文件的内容 替换 CubeMX 生成的 Core/Src/main.c
 *    (保持 USER CODE 区域结构一致, 或者直接整个替换)
 * 4. 修改 user_config.h 里的 WiFi 和巴法云私钥
 * ========================================================= */
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#include "user_config.h"
#include "bsp_delay.h"
#include "dht11.h"
#include "esp8266.h"

/* CubeMX 生成工程中, 这三个句柄在 main.c/usart.c/tim.c 里定义.
 * 整文件替换后, 需要在 CubeMX 的 main.c USER CODE 区域 or 此处 extern:
 * 若 usart.c/tim.c 由 CubeMX 生成, 请把这里的 extern 保留即可 */
extern UART_HandleTypeDef huart1;   /* ESP8266 */
extern UART_HandleTypeDef huart2;   /* 调试串口 */
extern TIM_HandleTypeDef  htim3;    /* 微秒延时 */

/* -------- 简单的软定时 -------- */
static uint32_t sample_tick = 0;
static uint32_t heartbeat_tick = 0;

/* 调试打印 */
#define DBG(...)  do { char _b[128]; int _n = snprintf(_b, sizeof(_b), __VA_ARGS__); \
                       if (_n > 0) HAL_UART_Transmit(&huart2, (uint8_t*)_b, (uint16_t)_n, 200); } while (0)

/* 上电重连/断线重连 */
static void connect_cloud(void)
{
    DBG("[NET] connecting...\r\n");
    while (ESP8266_Connect() != 0)
    {
        DBG("[NET] connect failed, retry in 3s\r\n");
        HAL_Delay(3000);
    }
    DBG("[NET] bemfa connected, topic=%s\r\n", BEMFA_TOPIC);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();   /* CubeMX 生成: 见 docs 说明 */

    /* ↓↓↓ 这些 MX_xxx_Init 由 CubeMX 自动生成 ↓↓↓ */
    MX_GPIO_Init();
    MX_USART1_UART_Init();  /* ESP8266, 115200 */
    MX_USART2_UART_Init();  /* 调试口, 115200 */
    MX_TIM3_Init();         /* 72分频 -> 1us */
    /* ↑↑↑ 如果整文件替换main.c, 把CubeMX生成工程里的这几个函数体拷过来 ↑↑↑ */

    bsp_delay_init(&htim3);
    ESP8266_Init(&huart1);

    DBG("\r\n===== DHT11 + ESP8266 + Bemfa demo =====\r\n");
    connect_cloud();

    sample_tick = heartbeat_tick = HAL_GetTick();

    while (1)
    {
        ESP8266_Poll();

        /* ---- 周期采样并上传 ---- */
        if (HAL_GetTick() - sample_tick >= SAMPLE_PERIOD_MS)
        {
            sample_tick = HAL_GetTick();
            DHT11_Data_TypeDef d;
            if (DHT11_Read(&d) == 0)
            {
                char msg[32];
                float temp = d.temp_int + d.temp_dec * 0.1f;
                snprintf(msg, sizeof(msg), UPLOAD_FORMAT, temp, d.humi_int);
                DBG("[DHT11] temp=%.1f humi=%d -> %s\r\n",
                    temp, d.humi_int, msg);
                if (Bemfa_Publish(msg) != 0)
                {
                    DBG("[NET] publish failed, reconnect\r\n");
                    HAL_Delay(1000);
                    connect_cloud();
                }
            }
            else
            {
                DBG("[DHT11] read failed (check wiring/pull-up)\r\n");
            }
        }

        /* ---- 心跳 ---- */
        if (HAL_GetTick() - heartbeat_tick >= HEARTBEAT_PERIOD_MS)
        {
            heartbeat_tick = HAL_GetTick();
            if (Bemfa_Ping() != 0)
            {
                DBG("[NET] heartbeat failed, reconnect\r\n");
                connect_cloud();
            }
        }
    }
}

/* 说明: SystemClock_Config / MX_GPIO_Init / MX_USARTx_UART_Init / MX_TIM3_Init
 * 由 CubeMX 生成. 推荐做法:
 *   A. 不替换整个main.c, 只把本文件的 while(1) 逻辑和相关函数
 *      拷进 CubeMX 生成的 main.c 的 USER CODE 区域 (推荐);
 *   B. 或整文件替换, 并把生成工程 main.c 里的上述函数原样拷贝到本文件末尾. */
