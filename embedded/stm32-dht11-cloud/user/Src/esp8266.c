/* =========================================================
 * ESP8266-01S AT 指令驱动 + 巴法云(TCP创客云)协议封装
 *
 * 硬件: ESP8266 接 USART1 (PA9-TX / PA10-RX), 115200
 *       ESP8266 必须独立 3.3V 供电(>300mA), 不要从开发板取电!
 *
 * 巴法云 TCP 协议 (bemfa.com:8344):
 *   订阅: cmd=1&uid=私钥&topic=主题\r\n
 *   发布: cmd=2&uid=私钥&topic=主题&msg=消息\r\n
 *   心跳: ping\r\n  (30秒内至少一次, 否则服务器断开)
 * ========================================================= */
#include "esp8266.h"
#include "user_config.h"
#include <string.h>
#include <stdio.h>

#define RX_BUF_SIZE   1024
#define AT_TIMEOUT_MS 8000

static UART_HandleTypeDef *s_huart = 0;
static volatile uint8_t  rx_byte;               /* HAL 中断单字节接收 */
static volatile uint16_t rx_head = 0, rx_tail = 0;
static uint8_t rx_buf[RX_BUF_SIZE];

/* ---- 环形缓冲区 ---- */
static void buf_clear(void) { rx_tail = rx_head; }

static uint16_t buf_count(void)
{
    return (uint16_t)((rx_head + RX_BUF_SIZE - rx_tail) % RX_BUF_SIZE);
}

/* 在缓冲区中查找子串, 找到返回1 */
static uint8_t buf_find(const char *str)
{
    uint16_t n = buf_count();
    uint16_t len = (uint16_t)strlen(str);
    if (len == 0 || n < len) return 0;
    for (uint16_t i = 0; i + len <= n; i++)
    {
        uint16_t k;
        for (k = 0; k < len; k++)
        {
            if (rx_buf[(rx_tail + i + k) % RX_BUF_SIZE] != str[k]) break;
        }
        if (k == len) return 1;
    }
    return 0;
}

/* ---- HAL 接收中断回调: 单字节入环形缓冲 ---- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == s_huart)
    {
        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail)            /* 缓冲区满则丢弃 */
        {
            rx_buf[rx_head] = rx_byte;
            rx_head = next;
        }
        HAL_UART_Receive_IT(s_huart, (uint8_t *)&rx_byte, 1);
    }
}

void ESP8266_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    HAL_UART_Receive_IT(s_huart, (uint8_t *)&rx_byte, 1);
}

void ESP8266_Poll(void)
{
    /* 接收全靠中断, 这里预留扩展点(如处理下发的命令) */
}

/* ---- 发送 AT 指令并等待期望应答 ---- */
static uint8_t at_cmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    buf_clear();
    HAL_UART_Transmit(s_huart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000);
    while (HAL_GetTick() - start < timeout_ms)
    {
        if (buf_find(expect))   return 0;   /* 成功 */
        if (buf_find("ERROR"))  return 1;   /* 失败 */
    }
    return 1;
}

/* ---- 通过 AT+CIPSEND 发送一段原始 TCP 数据 ---- */
static uint8_t tcp_send(const char *data)
{
    char cmd[32];
    uint8_t payload[256];
    uint16_t len = (uint16_t)strlen(data);
    /* 巴法云协议要求 \r\n 结尾, 若数据里没有就补上 */
    snprintf((char *)payload, sizeof(payload), "%s%s", data,
             (len >= 2 && data[len-1] == '\n') ? "" : "\r\n");
    len = (uint16_t)strlen((char *)payload);

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", len);
    if (at_cmd(cmd, ">", 3000)) return 1;

    HAL_UART_Transmit(s_huart, payload, len, 1000);
    /* 等待发送完成回执 */
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 5000)
    {
        if (buf_find("SEND OK")) return 0;
        if (buf_find("SEND FAIL") || buf_find("CLOSED")) return 1;
    }
    return 1;
}

uint8_t ESP8266_Connect(void)
{
    char cmd[128];

    /* 1. 测试通信 */
    if (at_cmd("AT\r\n", "OK", 2000)) return 1;
    /* 2. 关闭回显, 方便匹配应答 */
    at_cmd("ATE0\r\n", "OK", 2000);
    /* 3. Station 模式 */
    if (at_cmd("AT+CWMODE=1\r\n", "OK", 3000)) return 1;
    /* 4. 连接 WiFi */
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n",
             WIFI_SSID, WIFI_PASSWORD);
    if (at_cmd(cmd, "WIFI GOT IP", 20000) &&
        at_cmd(cmd, "OK", 20000)) return 1;
    /* 5. 连接巴法云 TCP 服务器 (8344 端口) */
    if (at_cmd("AT+CIPSTART=\"TCP\",\"bemfa.com\",8344\r\n",
               "CONNECT", 15000)) return 1;
    /* 6. 订阅主题: cmd=1 */
    snprintf(cmd, sizeof(cmd),
             "cmd=1&uid=%s&topic=%s\r\n", BEMFA_UID, BEMFA_TOPIC);
    if (tcp_send(cmd)) return 1;

    return 0;
}

uint8_t Bemfa_Publish(const char *msg)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "cmd=2&uid=%s&topic=%s&msg=%s", BEMFA_UID, BEMFA_TOPIC, msg);
    return tcp_send(cmd);
}

uint8_t Bemfa_Ping(void)
{
    return tcp_send("ping");
}
