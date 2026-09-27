#ifndef __ESP01S_H
#define __ESP01S_H

#include "stm32f1xx_hal.h"

/* =========================================================================
 * ESP01S (AT 固件) 驱动 —— UART 环形缓冲 + 中断接收 + 接收自愈 + 全程可诊断
 *
 * 使用的 AT 指令(ESP-AT 通用指令, 不依赖固件版本扩展功能):
 *   AT                                握手
 *   ATE0                              关闭命令回显(让日志只剩模块应答)
 *   AT+CWMODE=1                       站模式
 *   AT+CWAUTOCONN=0                   关闭模块自带的 AP 自动重连
 *   AT+SLEEP=0                        关闭 modem-sleep(省电休眠)
 *   AT+CWJAP="ssid","pwd"             连接 AP
 *   AT+CWJAP?                         查询当前 AP(判断"是否已连同一 AP")
 *   AT+CIFSR                          查询本机 IP
 *   AT+CIPDOMAIN="host"               查询域名解析结果
 *   AT+CIPMUX=0                       单连接模式
 *   AT+CIPSTART="TCP","host",port     建立 TCP 连接
 *   AT+CIPSEND=<len>                  进入发送模式, 收到 '>' 后发 len 字节
 *   AT+CIPCLOSE / AT+CIPSTATUS        关闭 / 查询
 * 注意: 出厂固件波特率可能是 115200 或 9600 —— 若 AT 握手失败先改波特率试。
 *
 * ---------------------------------------------------------------------------
 * 【为什么这个驱动要自带"诊断"能力】
 *   "连不上云端"这类问题的成因分布在四层(硬件供电 → 手机热点 → 云平台
 *   账号/主题 → 代码), 只靠一句 "connect failed" 无法定位。所以本驱动做三件事:
 *     ① 把所有 AT 收发原样导出(通过日志钩子打到 USART2), 失败时还能拿到
 *        模块最后说了什么 —— 是 "FAIL" / "ERROR" / "DNS Fail" 还是干脆没应答,
 *        一一可辨;
 *     ② 检测模块的启动横幅 "ready" —— 若它反复出现, 说明 ESP01S 在不停重启,
 *        基本可以断定是【供电不足】(发射瞬间 ~300mA 把 3.3V 拉垮), 而不是
 *        代码或网络问题;
 *     ③ 提供"只跑网络"的自检模式所需的状态查询接口(CIFSR / CWJAP? /
 *        CIPSTATUS / CIPDOMAIN)。
 * ========================================================================= */

/* 接收环形缓冲容量。
 * 【为什么是 1024】AT+CWJAP 阶段模块会连续吐出 "WIFI CONNECTED" /
 * "WIFI GOT IP" / "OK" 多段文本; 一旦写满, ESP01S_RxCplt() 会丢弃新字节,
 * 后续模式匹配就可能永远等不到 —— 表现为"连上了但随后必然掉线"。 */
#define ESP_RXBUF_SIZE      1024

/* AT 日志单行最大长度(超出则自动折行, 避免栈上大缓冲) */
#define ESP_LOG_LINE_MAX    80
/* 失败时保留的"模块最后应答"长度 */
#define ESP_LAST_RESP_MAX   64

typedef enum
{
    ESP_STATE_RESET = 0,
    ESP_STATE_READY,          /* AT 握手通过 */
    ESP_STATE_TCP_CONNECTED   /* TCP 已连上 Broker */
} ESP_State_t;

/* ---- 诊断计数: 用于判断"到底是网络断, 还是本地链路/供电出了问题" ---- */
typedef struct
{
    uint32_t rx_overflow;    /* 环形缓冲写满而丢弃字节的次数 */
    uint32_t rx_restart;     /* 接收链路异常后被重新武装的次数(正常应长期为 0) */
    uint32_t err_events;     /* 线路错误事件(帧错/噪声/溢出)次数 */
    uint32_t module_resets;  /* 检测到模块启动横幅 "ready" 的次数。
                              * 【它是最有价值的供电证据】ESP01S 正常上电只打印
                              * 一次; 若这个数在增长, 说明模块在运行中不断重启,
                              * 99% 是 3.3V 供电能力不足(峰值 300mA 拉垮电源),
                              * 而不是代码/网络问题 —— 必须先解决供电。 */
} ESP_Diag_t;

/* AT 收发日志钩子: 由 main.c 注册(把每一行打到 USART2)。
 * 传 NULL 可关闭。钩子只在任务上下文被调用, 不会在中断里执行。 */
typedef void (*ESP_LogFn)(const char *line);
void        ESP01S_SetLogHook(ESP_LogFn fn);
void        ESP01S_LogLine(const char *line);   /* 驱动内部需要主动写日志时用 */

/* ---- 基础 ---- */
void        ESP01S_Init(UART_HandleTypeDef *huart);
void        ESP01S_Poll(void);                       /* 任务中周期调用: 检测断连/模块重启 */
uint8_t     ESP01S_ATCheck(void);                    /* 0 = 成功; 1/2/3 = 失败在哪一步 */
uint8_t     ESP01S_JoinAP(const char *ssid, const char *pwd);
uint8_t     ESP01S_EnsureAP(const char *ssid, const char *pwd);
/* 已连到指定 AP 就直接返回 0, 不做无谓的重连 ——
 * 【为什么要这个】每次重连都无条件重发 AT+CWJAP, 会让模块先把已建立的
 * 连接拆掉再重连(耗时数秒, 期间 TCP 必然断)。链路一抖就"重连→拆掉正常
 * 连接→又连上→再抖", 现场看就是"断了又连、连了又断"。 */
uint8_t     ESP01S_TcpConnect(const char *host, uint16_t port);
uint8_t     ESP01S_TcpConnectAuto(const char *host, uint16_t port,
                                  const char *ip_fallback);
/* 先按域名连; 失败则把模块自己的域名解析结果(CIPDOMAIN)打出来, 再改用
 * ip_fallback 直连。手机热点下 DNS 不解析是"连不上云端"的高频原因。 */
void        ESP01S_TcpClose(void);
uint8_t     ESP01S_TcpSend(const uint8_t *data, uint16_t len);
ESP_State_t ESP01S_GetState(void);
uint32_t    ESP01S_GetBaud(void);   /* 当前生效波特率(自适应后可能与配置值不同) */

/* ---- 接收缓冲操作(供 ISR/任务使用) ---- */
void        ESP01S_RxCplt(void);                     /* 由 HAL_UART_RxCpltCallback 调用 */
void        ESP01S_RxClear(void);
uint8_t     ESP01S_RxContains(const void *pattern, uint16_t len);
uint8_t     ESP01S_RxFind(const void *pattern, uint16_t len, uint16_t *pos);
/* 在环形缓冲中查找模式; 找到时把该模式【首字节的下标】写入 *pos。
 * 用于解析 MQTT CONNACK 这类"固定头 + 可变返回码"的二进制应答。 */
uint8_t     ESP01S_RxPeek(uint16_t pos, uint8_t *out);
/* 读取缓冲中指定下标处的 1 字节(仅当该下标确实还在缓冲里)。 */
uint8_t     ESP01S_WaitContains(const void *pattern, uint16_t len, uint32_t timeout_ms);
/* 等待 "OK" 或 fail_tok(fail_tok 可为 NULL): 0 = 收到 OK, 1 = 失败, 2 = 超时 */
uint8_t     ESP01S_WaitOkOrFail(const char *fail_tok, uint32_t timeout_ms);

/* ---- 诊断 ---- */
const char *ESP01S_LastResp(void);
/* 最近一次等待失败时, 模块缓冲里最后那段可读文本。
 * 例如 "FAIL" / "ERROR" / "DNS Fail" / "(no response)"。
 * 上层把它打出来, 就能直接知道卡在哪一步、模块说了什么。 */
void        ESP01S_DumpNetInfo(const char *host);
/* 依次发 CIFSR / CWJAP? / CIPSTATUS / CIPDOMAIN, 结果经日志钩子输出。
 * 用于"连不上"时一次性看清模块自身的状态(WiFi 是否真的上了、IP 是多少、
 * 域名能不能解析)。 */

/* ---- 接收链路自愈 ----
 * ESP01S_OnError()      : 由 HAL_UART_ErrorCallback 调用。STM32 HAL 在
 *                         帧错/噪声错/溢出时会把"接收"整体结束掉(关掉
 *                         RXNE 中断), 若不在这里重新武装, 接收就永久停摆 ——
 *                         这是"连上之后必然掉线且再也收不到任何响应"的
 *                         经典成因, 属于必须处理的确定性缺陷。
 * ESP01S_RxWatchdog()   : 任务中周期调用。兜底检查"是否还挂着一个 1 字节
 *                         接收", 没有则重新武装, 并计入 rx_restart。 */
void        ESP01S_OnError(void);
void        ESP01S_RxWatchdog(void);
void        ESP01S_GetDiag(ESP_Diag_t *out);

#endif /* __ESP01S_H */
