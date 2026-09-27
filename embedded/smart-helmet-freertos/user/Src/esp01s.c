/* =========================================================================
 * ESP01S AT 指令驱动 (UART 环形缓冲 + 中断接收 + 自愈 + 全程可诊断)
 *
 * 【一、接收链路自愈(上一轮已修)】
 *   STM32 HAL 在串口出现帧错/噪声错/溢出(ORE)时, 会在 IRQHandler 里调用
 *   UART_EndRxTransfer(): 关掉 RXNE/PE/ERR 中断并把 RxState 复位为 READY,
 *   然后才回调 HAL_UART_ErrorCallback()。此后 HAL 不会自动恢复接收。
 *   若固件不接管这个回调, 接收就永久停摆 → 之后所有"等 OK / 等 >"必然超时
 *   → 上层认为发布失败 → 断开重连 → 连上后又撞上 → 再断。
 *   处置: ① main.c 实现 HAL_UART_ErrorCallback → ESP01S_OnError();
 *         ② 任务侧 ESP01S_RxWatchdog() 兜底补挂接收。
 *
 * 【二、本文件新增的"可诊断"能力 —— 这才是能定位"连不上云端"的关键】
 *   之前的固件只会告诉你 "connect failed step=3", 但那一步失败的原因可能
 *   是: 热点没开 / DNS 不解析 / 云端账号被拒 / 模块在重启…… 完全无法区分。
 *   现在:
 *     ① AT 收发全程经日志钩子原样打到 USART2(见 ESP01S_SetLogHook);
 *     ② 每次等待失败都记下"模块最后说了什么"(ESP01S_LastResp), 例如
 *        FAIL / ERROR / DNS Fail / no response, 上层直接打印;
 *     ③ 检测模块启动横幅 "ready", 累计 module_resets —— 这是判断
 *        【ESP01S 供电不足反复重启】最直接的证据;
 *     ④ 连不上时可用 ESP01S_DumpNetInfo() 一次性打出 IP / 当前 AP / 域名解析;
 *     ⑤ 域名连不上自动改用 IP 直连(手机热点 DNS 不可用是高频原因);
 *     ⑥ 已经连上目标 AP 就不再重发 CWJAP, 避免"自己把好连接拆了"。
 * ========================================================================= */
#include "esp01s.h"
#include "user_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

static UART_HandleTypeDef *s_uart = 0;
static volatile uint8_t    s_rxbyte;
static volatile uint16_t   s_head = 0;         /* ISR 写指针 */
static volatile uint16_t   s_tail = 0;         /* 任务读指针 */
static volatile uint8_t    s_buf[ESP_RXBUF_SIZE];
static ESP_State_t         s_state = ESP_STATE_RESET;
static ESP_Diag_t          s_diag;             /* 诊断计数, 见 esp01s.h */

/* ---- AT 日志 ---- */
static ESP_LogFn s_log = 0;
static uint16_t  s_logpos = 0;                 /* 已被日志消费到的缓冲位置 */
static char      s_line[ESP_LOG_LINE_MAX];     /* RX 行拼接缓冲 */
static uint16_t  s_linelen = 0;
static char      s_txbuf[ESP_LOG_LINE_MAX];    /* TX 报文可读化缓冲(与 s_line 分开, 避免互相破坏) */
static char      s_last_resp[ESP_LAST_RESP_MAX];
static uint8_t   s_ready_latch = 0;            /* 用于"ready 只在出现沿计一次" */

/* ==========================================================================
 * AT 日志
 * ========================================================================== */
void ESP01S_SetLogHook(ESP_LogFn fn)
{
    s_log = fn;
    if (fn == 0) { s_logpos = s_head; s_linelen = 0; }
}

void ESP01S_LogLine(const char *line)
{
    if (s_log) s_log(line);
}

static void log_flush(void)
{
    if (s_linelen == 0) return;
    s_line[s_linelen] = '\0';
    s_linelen = 0;
    if (s_log) s_log(s_line);
}

/* 把"上次之后新到的"接收字节以可读形式经钩子输出。
 * EX: '0x0D 0x0A' 作为行结束; 不可打印字节显示为 '.', 这样 MQTT 的二进制
 *     应答(CONNACK 等)也能在串口日志里看见轮廓。
 * 只在任务上下文调用(内部会做阻塞串口发送)。 */
static void log_rx_scan(void)
{
    uint8_t c;

    if (s_log == 0) { s_logpos = s_head; return; }

    while (s_logpos != s_head)
    {
        c = s_buf[s_logpos];
        s_logpos = (uint16_t)((s_logpos + 1) % ESP_RXBUF_SIZE);

        if (c == '\r')       continue;
        if (c == '\n')       { log_flush(); continue; }

        s_line[s_linelen++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        if (s_linelen >= (ESP_LOG_LINE_MAX - 1)) log_flush();
    }
}

static void log_tx(const char *cmd)
{
    char b[ESP_LOG_LINE_MAX];
    size_t n;

    if (s_log == 0) return;

    /* 去掉结尾的 \r\n 再打印, 日志更清爽 */
    n = strlen(cmd);
    while (n > 0 && (cmd[n - 1] == '\r' || cmd[n - 1] == '\n')) n--;
    if (n >= sizeof(b)) n = sizeof(b) - 1;
    memcpy(b, cmd, n);
    b[n] = '\0';

    s_log(b);
}

/* 打印即将通过 CIPSEND 发出去的二进制报文(可读化, 自动折行) */
static void log_tx_payload(const uint8_t *d, uint16_t len)
{
    uint16_t i, n = 0;

    if (s_log == 0) return;

    for (i = 0; i < len; i++)
    {
        uint8_t c = d[i];
        s_txbuf[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        if (n >= (ESP_LOG_LINE_MAX - 1))
        {
            s_txbuf[n] = '\0';
            s_log(s_txbuf);
            n = 0;
        }
    }
    if (n) { s_txbuf[n] = '\0'; s_log(s_txbuf); }
}

/* 记下"缓冲里最后那段可读文本", 供失败时上报。
 * 问题排查里最有用的就是这一句: 模块到底回答了 FAIL / ERROR / DNS Fail,
 * 还是根本没应答(no response)。 */
static void capture_resp(void)
{
    char tmp[ESP_LAST_RESP_MAX * 2];
    uint16_t pos = s_tail;
    uint16_t n = 0;
    uint16_t keep;

    if (s_head == s_tail)
    {
        strcpy(s_last_resp, "(no response)");
        return;
    }

    while (pos != s_head && n < (uint16_t)(sizeof(tmp) - 2))
    {
        uint8_t c = s_buf[pos];
        pos = (uint16_t)((pos + 1) % ESP_RXBUF_SIZE);

        if (c == '\r') continue;
        if (c == '\n') { if (n) tmp[n++] = '|'; continue; }   /* 换行用 | 代替, 压成一行 */
        tmp[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
    tmp[n] = '\0';

    /* 只保留末尾一段 —— 最近的应答比开头更重要 */
    keep = (uint16_t)(ESP_LAST_RESP_MAX - 1);
    if (n > keep)
    {
        memmove(tmp, tmp + (n - keep), keep);
        tmp[keep] = '\0';
    }
    strcpy(s_last_resp, tmp);
}

const char *ESP01S_LastResp(void)
{
    return (s_last_resp[0] != '\0') ? s_last_resp : "(none)";
}

/* ==========================================================================
 * 接收链路
 * ========================================================================== */
/* 重新武装"1 字节接收"。
 * 返回 0 = 成功。失败(例如 RxState 被占用)不在这里死等, 只记计数,
 * 交给 ESP01S_RxWatchdog() 在任务侧补挂。 */
static uint8_t arm_rx(void)
{
    if (s_uart == 0) return 1;
    return (HAL_UART_Receive_IT(s_uart, (uint8_t *)&s_rxbyte, 1) == HAL_OK) ? 0 : 1;
}

/* ==========================================================================
 * 波特率自适应 —— 专治"AT 完全无应答"
 *
 * 【为什么需要它】
 *   "AT 没有任何回应"最常见的三个原因里, **波特率不符**最容易被误判成
 *   "接线坏了" —— 因为除了逻辑分析仪, 肉眼根本看不出区别, 用户会一直在
 *   接线和供电上打转。
 *   ESP-01S 出厂 AT 固件默认 115200, 但模块若被刷过固件, 或执行过
 *   AT+UART_DEF/AT+UART_CUR 改过串口参数, 就可能是 9600/74880 等值。
 *   这里在 115200 完全无应答时自动扫一遍其它常见波特率:
 *     扫到了 → 把"模块真实波特率"打印出来并锁定, 问题当场解决;
 *     扫不到 → 也把"所有波特率都试过"这条信息暴露出来, 用户就可以
 *              放心地去查接线/供电/EN 脚, 不必再怀疑波特率。
 * ========================================================================== */
static const uint32_t s_baud_tab[] =
{
    9600, 74880, 57600, 38400, 19200, 230400, 460800
};
#define BAUD_TAB_N   (sizeof(s_baud_tab) / sizeof(s_baud_tab[0]))

/* 当前生效波特率(初值即 user_config.h 的配置值, 供诊断输出使用) */
static uint32_t s_cur_baud = ESP01S_BAUD_RATE;

/* 是否已经扫过波特率。扫过一次(无论成功失败)就不再重复 —— 否则每次
 * 重连都要白等 7 秒, 把重试周期拖得很长。重新上电即可再次扫描。 */
static uint8_t  s_baud_scan_done = 0;

uint32_t ESP01S_GetBaud(void)
{
    return s_cur_baud;
}

/* 切换 USART 波特率, 并【重新武装接收中断】。
 *
 * 【必须两件事一起做】HAL 不允许在已初始化状态下直接改 Init.BaudRate, 必须
 * 先 DeInit 再 Init; 而 Init 不会再打开接收中断 —— 忘了重新武装, 串口从此
 * 一个字节都收不到, 问题会从"波特率不对"升级成"彻底不通", 更难查。
 * 所以这里固定顺序: AbortReceive → DeInit → 改波特率 → Init → 清缓冲 → arm_rx。 */
static void uart_rebaud(uint32_t baud)
{
    if (s_uart == 0) return;

    (void)HAL_UART_AbortReceive(s_uart);
    (void)HAL_UART_DeInit(s_uart);
    s_uart->Init.BaudRate = baud;
    (void)HAL_UART_Init(s_uart);

    s_cur_baud = baud;

    ESP01S_RxClear();
    (void)arm_rx();
}

void ESP01S_Init(UART_HandleTypeDef *huart)
{
    s_uart = huart;
    s_head = s_tail = 0;
    s_logpos = 0;
    s_linelen = 0;
    s_ready_latch = 0;
    s_last_resp[0] = '\0';
    s_state = ESP_STATE_RESET;
    memset(&s_diag, 0, sizeof(s_diag));
    s_cur_baud = (huart != 0) ? huart->Init.BaudRate : ESP01S_BAUD_RATE;
    s_baud_scan_done = 0;
    (void)arm_rx();                                          /* 启动首次接收 */
}

/* ---- ISR 上下文: 单生产者写入 ---- */
/* 由 HAL_UART_RxCpltCallback 调用, 即 UART 每收到 1 字节触发一次。
 * 注意: 这里【不】做任何日志输出(阻塞串口发送不能在中断里做)。 */
void ESP01S_RxCplt(void)
{
    uint8_t byte = s_rxbyte;
    uint16_t next = (uint16_t)((s_head + 1) % ESP_RXBUF_SIZE);

    if (next != s_tail)          /* 缓冲满则丢弃最新字节(而不是覆盖未处理数据) */
    {
        s_buf[s_head] = byte;
        s_head = next;
    }
    else
    {
        s_diag.rx_overflow++;    /* 缓冲写满: 说明任务侧消费不及时或容量偏小 */
    }

    (void)arm_rx();              /* 重新武装接收(失败由看门狗兜底) */
}

/* ---- 线路错误回调(由 main.c 的 HAL_UART_ErrorCallback 转发) ---- */
void ESP01S_OnError(void)
{
    volatile uint32_t tmp;

    s_diag.err_events++;

    if (s_uart != 0)
    {
        /* STM32F1 清 PE/FE/NE/ORE 的标准做法: 读一次 SR 再读一次 DR。
         * 这里读走的是"出错那一帧"对应的数据寄存器内容, 属正常丢弃。 */
        tmp = s_uart->Instance->SR;
        tmp = s_uart->Instance->DR;
        (void)tmp;

        s_uart->ErrorCode = HAL_UART_ERROR_NONE;
    }

    (void)arm_rx();              /* 关键: HAL 已把接收结束掉, 必须重新武装 */
}

/* ---- 接收看门狗: 任务侧兜底 ---- */
void ESP01S_RxWatchdog(void)
{
    if (s_uart == 0) return;

    if (s_uart->RxState != HAL_UART_STATE_BUSY_RX)
    {
        s_diag.rx_restart++;
        (void)arm_rx();
    }
}

void ESP01S_GetDiag(ESP_Diag_t *out)
{
    if (out) *out = s_diag;
}

void ESP01S_RxClear(void)
{
    log_rx_scan();               /* 清之前先把还没打出去的字节打出去 */
    s_tail = s_head;
    s_logpos = s_head;
}

/* 在字节流中顺序匹配模式(支持二进制, 因此可识别 MQTT CONNACK)。
 * 找到时把模式首字节在缓冲中的下标写入 *pos。 */
uint8_t ESP01S_RxFind(const void *pattern, uint16_t len, uint16_t *pos)
{
    const uint8_t *p = (const uint8_t *)pattern;
    uint16_t it, k = 0, start = s_tail;

    if (len == 0) return 0;
    if (s_head == s_tail) return 0;

    for (it = s_tail; it != s_head; it = (uint16_t)((it + 1) % ESP_RXBUF_SIZE))
    {
        uint8_t c = s_buf[it];

        if (c == p[k])
        {
            if (k == 0) start = it;
            k++;
            if (k == len)
            {
                if (pos) *pos = start;
                return 1;
            }
        }
        else
        {
            /* 失配: 只有当本字节等于模式首字节时, 才认为新匹配从它开始 */
            k = 0;
            if (c == p[0]) { start = it; k = 1; }
        }
    }
    return 0;
}

uint8_t ESP01S_RxContains(const void *pattern, uint16_t len)
{
    return ESP01S_RxFind(pattern, len, 0);
}

uint8_t ESP01S_RxPeek(uint16_t pos, uint8_t *out)
{
    uint16_t p = (uint16_t)(pos % ESP_RXBUF_SIZE);

    if (p == s_head) return 0;               /* 还没收到 */
    if (s_head >= s_tail)
    {
        if (p < s_tail || p >= s_head) return 0;
    }
    else
    {
        if (p < s_tail && p >= s_head) return 0;
    }

    if (out) *out = s_buf[p];
    return 1;
}

uint8_t ESP01S_WaitContains(const void *pattern, uint16_t len, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();

    do
    {
        log_rx_scan();
        if (ESP01S_RxContains(pattern, len)) return 0;
        vTaskDelay(pdMS_TO_TICKS(10));      /* 让出 CPU, 不忙等 */
    } while ((HAL_GetTick() - t0) < timeout_ms);

    log_rx_scan();
    return 1;
}

uint8_t ESP01S_WaitOkOrFail(const char *fail_tok, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();

    do
    {
        log_rx_scan();
        if (ESP01S_RxContains("OK", 2)) return 0;
        if (ESP01S_RxContains("ERROR", 5)) return 1;
        if (ESP01S_RxContains("FAIL", 4)) return 1;
        if (fail_tok && ESP01S_RxContains(fail_tok, (uint16_t)strlen(fail_tok))) return 1;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while ((HAL_GetTick() - t0) < timeout_ms);

    log_rx_scan();
    return 2;
}

/* 等待 ">"(CIPSEND 的数据提示符), 同时识别模块给出的失败提示。
 * 返回 0 = 收到 '>';  1 = 模块明确报错(链路已不可用);
 *      2 = 超时;     3 = 模块回 "busy p..."(它正在处理数据, 稍后重试) */
static uint8_t wait_prompt(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();

    for (;;)
    {
        log_rx_scan();

        if (ESP01S_RxContains("busy", 4)) return 3;
        if (ESP01S_RxContains(">", 1))    return 0;

        if (ESP01S_RxContains("ERROR", 5) ||
            ESP01S_RxContains("link is not valid", 17) ||
            ESP01S_RxContains("CLOSED", 6))
        {
            return 1;                       /* 链路已断, 不必再等 */
        }

        if ((HAL_GetTick() - t0) >= timeout_ms) { log_rx_scan(); return 2; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ==========================================================================
 * 发送一条 AT 并等结果
 * ========================================================================== */
static uint8_t send_at(const char *cmd)
{
    size_t len = strlen(cmd);
    if (s_uart == 0) return 1;
    return (HAL_UART_Transmit(s_uart, (uint8_t *)cmd, (uint16_t)len, 1000) == HAL_OK) ? 0 : 1;
}

/* 统一的"发一条 AT + 等 OK"入口, 顺便把请求与失败原因都记下来。
 * 返回 0 = OK; 1 = 模块报错(ERROR/FAIL/fail_tok); 2 = 超时无应答。 */
static uint8_t at_expect(const char *cmd, uint32_t to, const char *fail_tok)
{
    uint8_t r;

    ESP01S_RxClear();            /* 先清(并把待打印的旧应答刷出去) */
    log_tx(cmd);
    send_at(cmd);
    r = ESP01S_WaitOkOrFail(fail_tok, to);
    if (r != 0) capture_resp();
    return r;
}

/* 查询"当前是否已经连上指定 AP"。
 * AT+CWJAP? 成功时回 "+CWJAP:\"ssid\",\"xx:xx..\",ch,rssi" 然后 OK;
 * 未连接时回 "No AP"。 */
static uint8_t wifi_joined(const char *ssid)
{
    if (at_expect("AT+CWJAP?\r\n", 3000, 0) != 0) return 0;
    return ESP01S_RxContains(ssid, (uint16_t)strlen(ssid)) ? 1 : 0;
}

/* ==========================================================================
 * 对外: 初始化 / 连 AP / 建 TCP
 * ========================================================================== */
uint8_t ESP01S_ATCheck(void)
{
    uint8_t i, r;

    /* ① 握手: 模块刚上电或刚复位时可能还没就绪, 单次 1s 容易误判失败,
     *    这里重试 3 次(每次间隔 300ms)。 */
    for (i = 0; i < 3; i++)
    {
        r = at_expect("AT\r\n", 1000, 0);
        if (r == 0) break;
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* AT 在当前波特率下完全无应答 → 扫一遍其它常见波特率(见 uart_rebaud 上方说明)。
     * 只扫一次: 扫过就不再重复, 免得每次重连都白等好几秒。
     * 【声明放在块首】armcc(AC5) 是 C90 风格, 函数级声明未使用会报
     * #177-D "declared but never referenced", 所以 b/found 只在真正用到
     * 它们的这个块里声明。
     * 这里刻意不写编号 ②, 以免与下面原有的 ②~⑤ 步骤编号冲突。 */
    if (i >= 3)
    {
        char    b[48];
        uint8_t found;

        if (s_baud_scan_done)
        {
            return 1;          /* 已经确认所有波特率都不通: 去查接线 / GND / EN / 供电 */
        }
        s_baud_scan_done = 1;

        ESP01S_LogLine("[AT] no reply, scanning other baud rates...");

        found = 0;
        for (i = 0; i < (uint8_t)BAUD_TAB_N; i++)
        {
            uart_rebaud(s_baud_tab[i]);
            if (at_expect("AT\r\n", 1000, 0) == 0) { found = 1; break; }
        }

        if (found == 0)
        {
            uart_rebaud(ESP01S_BAUD_RATE);   /* 恢复配置值, 行为可预期 */
            ESP01S_LogLine("[AT] no reply at ANY baud -> check TX/RX,GND,EN,power");
            return 1;
        }

        /* 扫到了 —— 把模块真实波特率打出来。这是排查时最有价值的一条信息:
         * 它同时解释了"为什么之前完全没反应", 也告诉用户该把
         * user_config.h / MX_USART1_UART_Init() 的波特率改成多少。 */
        snprintf(b, sizeof(b), "[AT] module baud rate = %lu",
                 (unsigned long)s_cur_baud);
        ESP01S_LogLine(b);
    }

    /* ② 关闭命令回显。两个好处:
     *    - 后续串口日志只剩模块的应答, 一眼就能看出它说了什么;
     *    - 串口上少一半流量, 减轻 115200 下的接收压力。 */
    (void)at_expect("ATE0\r\n", 1500, 0);

    /* ③ 站模式 */
    if (at_expect("AT+CWMODE=1\r\n", 2000, 0) != 0) return 2;

    /* ④ 关闭"自动连接 AP"。
     * 模块默认在掉线后自行反复重连上次的 AP, 这会与本驱动的重连流程
     * 抢串口、抢连接, 造成"断了又连、连了又断"的抖动。
     * 统一由固件决定何时重连。部分老固件无此指令, 忽略其结果。 */
    (void)at_expect("AT+CWAUTOCONN=0\r\n", 2000, 0);

    /* ⑤ 关闭 modem-sleep(省电)。
     * 省电模式下模块会周期性休眠, 休眠期间不响应 AT, 也可能把 TCP
     * 连接睡断 —— 是"连上后每隔一段时间就掉一次"的常见诱因。
     * 少数固件不接受该指令, 忽略结果即可。 */
    (void)at_expect("AT+SLEEP=0\r\n", 2000, 0);

    s_state = ESP_STATE_READY;
    return 0;
}

uint8_t ESP01S_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[96];
    uint8_t i;

    for (i = 0; i < 2; i++)
    {
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);

        if (at_expect(cmd, 25000, 0) == 0) return 0;

        /* 失败(超时或 FAIL)后不要立刻 CWQAP!
         * 有时模块其实已经连上了, 只是 "OK" 落在超时窗口之后才吐出来。
         * 此时盲目断开, 会把刚刚建好的连接拆掉, 人为制造"连上又断"。
         * 所以先问一次实际状态, 确认没连上再断开重来。 */
        if (wifi_joined(ssid)) return 0;

        (void)at_expect("AT+CWQAP\r\n", 3000, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return 1;
}

uint8_t ESP01S_EnsureAP(const char *ssid, const char *pwd)
{
    /* 已经连在目标 AP 上就什么都不做 —— 见 esp01s.h 里的说明,
     * 这是消除"链路一抖就重连、重连又把好连接拆掉"的关键一步。 */
    if (wifi_joined(ssid)) return 0;
    return ESP01S_JoinAP(ssid, pwd);
}

/* 单次 TCP 连接尝试 */
static uint8_t tcp_try(const char *host, uint16_t port)
{
    char cmd[96];

    (void)at_expect("AT+CIPMUX=0\r\n", 2000, 0);      /* 单连接 */

    /* 先关掉可能残留的旧连接: 否则 CIPSTART 会回 "ALREADY CONNECTED" 而被
     * 当成成功, 但那条链路可能早已半死 —— 之后每次收发都超时(假连接)。 */
    (void)at_expect("AT+CIPCLOSE\r\n", 2000, 0);

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", host, (unsigned)port);

    /* CIPSTART 成功时不同固件版本返回 "CONNECT\r\n\r\nOK" 或仅 "OK",
     * 用 at_expect 可同时兼容; 域名解析失败会回 "DNS Fail" + "ERROR"。 */
    if (at_expect(cmd, 20000, 0) != 0)
    {
        s_state = ESP_STATE_READY;
        return 1;
    }

    s_state = ESP_STATE_TCP_CONNECTED;
    return 0;
}

uint8_t ESP01S_TcpConnect(const char *host, uint16_t port)
{
    return tcp_try(host, port);
}

uint8_t ESP01S_TcpConnectAuto(const char *host, uint16_t port, const char *ip_fallback)
{
    char cmd[96];

    if (tcp_try(host, port) == 0) return 0;

    /* 走到这里说明按域名连失败了。最常见的原因是热点下发的 DNS 不解析
     * (手机热点尤其常见), 其次是域名解析被运营商劫持。
     * 先把模块自己的解析结果打出来 —— 有结果说明 DNS 通、问题在别处;
     * 没结果基本可以确定是 DNS。 */
    snprintf(cmd, sizeof(cmd), "AT+CIPDOMAIN=\"%s\"\r\n", host);
    (void)at_expect(cmd, 6000, 0);

    if (ip_fallback != 0 && ip_fallback[0] != '\0')
    {
        /* 日志字符串一律用 ASCII: armcc(AC5) 按 GBK 解析 UTF-8 中文字面量,
         * 会把结尾引号吞掉并报 #870-D invalid multibyte character sequence。
         * 中文只出现在注释里, 不放进字符串。 */
        ESP01S_LogLine("[AT] domain connect failed, retry by IP");
        if (tcp_try(ip_fallback, port) == 0) return 0;
    }

    return 1;
}

void ESP01S_TcpClose(void)
{
    (void)at_expect("AT+CIPCLOSE\r\n", 2000, 0);
    s_state = ESP_STATE_READY;
}

/* 返回码: 0 = 成功
 *         1 = 状态不对(未连接)
 *         2 = 未等到 '>' 或模块报错(链路不可用)
 *         3 = 串口发送失败
 *         4 = 模块回 SEND FAIL / ERROR
 *         5 = 等 SEND OK 超时
 * 上层(WifiTask)据此决定"原地重试"还是"断开重连":
 * 其中 5 多为链路拥塞/信号抖动, 重试往往就能成功, 不应立刻拆连接。 */
uint8_t ESP01S_TcpSend(const uint8_t *data, uint16_t len)
{
    char cmd[24];
    uint8_t r;
    uint8_t try_n;
    uint32_t t0;

    if (s_state != ESP_STATE_TCP_CONNECTED) return 1;

    /* ---- ① 请求发送许可, 等 '>' ---- */
    for (try_n = 0; try_n < 3; try_n++)
    {
        ESP01S_RxClear();
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", (unsigned)len);
        log_tx(cmd);
        send_at(cmd);

        r = wait_prompt(5000);
        if (r == 0) break;                                    /* 拿到 '>' */
        if (r == 3)
        {
            /* "busy p...": 模块正忙于处理刚收到的数据, 稍等再重新申请。 */
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (r == 1) { s_state = ESP_STATE_READY; return 2; }   /* 链路已断 */
        capture_resp();
        return 2;                                             /* 超时 */
    }
    if (try_n >= 3) { capture_resp(); return 2; }

    /* ---- ② 发正文 ---- */
    ESP01S_RxClear();
    log_tx_payload(data, len);
    if (HAL_UART_Transmit(s_uart, (uint8_t *)data, len, 2000) != HAL_OK) return 3;

    /* ---- ③ 等 "SEND OK" ---- */
    t0 = HAL_GetTick();
    for (;;)
    {
        log_rx_scan();
        if (ESP01S_RxContains("SEND OK", 7)) return 0;
        if (ESP01S_RxContains("SEND FAIL", 9)) { capture_resp(); return 4; }
        if (ESP01S_RxContains("ERROR", 5))     { capture_resp(); return 4; }
        if (ESP01S_RxContains("CLOSED", 6) ||
            ESP01S_RxContains("link is not valid", 17))
        {
            s_state = ESP_STATE_READY;
            return 2;
        }
        if ((HAL_GetTick() - t0) >= 8000) { capture_resp(); return 5; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ESP01S_Poll(void)
{
    /* 模块启动横幅: ESP-AT 每次上电/复位都会打印 "ready"。
     * 【这是判断"供电不足"最直接的证据】正常运行只应出现一次; 若计数持续
     * 增长, 说明 ESP01S 在反复重启 —— 优先查供电(独立 3.3V LDO + 100µF),
     * 而不是继续调代码。 */
    if (ESP01S_RxContains("ready", 5))
    {
        if (!s_ready_latch) { s_ready_latch = 1; s_diag.module_resets++; }
    }
    else
    {
        s_ready_latch = 0;
    }

    if (s_state != ESP_STATE_TCP_CONNECTED) return;

    /* 模块主动报错/断连时把状态回落, 交给 WiFi_Task 重连。
     * 覆盖常见的几种"链路已死"提示语。 */
    if (ESP01S_RxContains("CLOSED", 6) ||
        ESP01S_RxContains("WIFI DISCONNECT", 15) ||
        ESP01S_RxContains("link is not valid", 17))
    {
        s_state = ESP_STATE_READY;
    }
}

ESP_State_t ESP01S_GetState(void)
{
    return s_state;
}

/* ==========================================================================
 * 诊断: 一次性打出模块自身的网络状态
 * ========================================================================== */
void ESP01S_DumpNetInfo(const char *host)
{
    char cmd[96];

    if (s_log == 0) return;

    ESP01S_LogLine("---- module net info ----");

    /* 本机 IP: 全是 0.0.0.0 说明 DHCP 没拿到地址, TCP 必然连不上 */
    (void)at_expect("AT+CIFSR\r\n", 4000, 0);

    /* 当前 AP 与信号强度: "No AP" = 实际并没连上(即使代码认为已连) */
    (void)at_expect("AT+CWJAP?\r\n", 4000, 0);

    /* TCP 连接状态 */
    (void)at_expect("AT+CIPSTATUS\r\n", 4000, 0);

    /* 域名解析: 有 IP 说明 DNS 正常; 否则就是热点 DNS 的问题 */
    if (host != 0 && host[0] != '\0')
    {
        snprintf(cmd, sizeof(cmd), "AT+CIPDOMAIN=\"%s\"\r\n", host);
        (void)at_expect(cmd, 6000, 0);
    }

    ESP01S_LogLine("---- end of status ----");
}
