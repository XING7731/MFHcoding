/* ============================================================================
 * app_tasks.c —— 六个 FreeRTOS 任务 + 任务间通信
 *
 * 任务划分、优先级、周期见 app_tasks.h 头部表格。
 * (2026-09-16 新增第 6 个: ImpactTask —— 摔倒判据专用 200Hz 采样。
 *  判据不能再挂在 Sensor_Task 的 100ms 周期上, 原因见 IMPACT_PERIOD_MS 处的说明。)
 * 通信机制:
 *   xQueueSensorRaw   Sensor_Task → Data_Processing_Task (长度 4, 队列)
 *   xQueueAlarmEvent  Data_Processing / WiFi_Task / EXTI(ISR) → Alarm_Task (长度 8)
 *   xSemSosUpload     EXTI(ISR) → WiFi_Task 的二进制信号量: 收到后立即抢发
 *   xMutexI2C         I2C 访问互斥。原先三器件共用 I2C1, 故一把锁即够;
 *                     现在 MPU6050 默认走 I2C2(见 MPU6050_ON_I2C2), 本锁仍
 *                     统一保护所有 I2C 访问 —— 多包一层只是把两条总线串行化,
 *                     功能安全(单锁顺序获取, 不可能死锁), 不必按总线拆锁。
 *   xMutexSnapshot    SystemData_t 快照读写互斥(DataProc/Display/WiFi 读, Alarm 写)
 *
 * 阻塞与实时性:
 *   所有等待都带超时且使用 vTaskDelay/队列阻塞, 不做忙等; 唯一会短时
 *   关闭中断的是 DHT11/DS18B20 的单总线时序段(约 4~6ms), 见各驱动注释。
 * ========================================================================== */
#include "app_tasks.h"
#include "user_config.h"
#include "bsp_delay.h"
#include "dht11.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "max30102.h"
#include "mq2.h"
#include "buzzer.h"
#include "oled_ssd1306.h"
#include "esp01s.h"
#include "mqtt_client.h"
#include "rtc_time.h"
#include "fall_detect.h"
#include "heart_rate.h"
#include "alarm_cfg.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>

/* ---- CubeMX 生成的外设句柄 ---- */
extern UART_HandleTypeDef huart1;   /* ESP01S */
extern UART_HandleTypeDef huart2;   /* 调试串口 */
extern I2C_HandleTypeDef  hi2c1;    /* OLED + MAX30102 (MPU6050 默认已移到 I2C2) */
extern ADC_HandleTypeDef  hadc1;    /* MQ2 */
extern TIM_HandleTypeDef  htim3;    /* 微秒延时 */

/* 全局错误处理(定义在 user/Src/main.c): 关中断 + 蜂鸣器长鸣 + 停机。
 * 本文件在 FreeRTOS 堆分配失败时用它来"响亮地失败", 而不是留个空指针
 * 让系统在后面某处莫名卡死。 */
extern void Error_Handler(void);

/* ---- 任务句柄 ---- */
TaskHandle_t hAlarmTask = NULL, hDataProcTask = NULL, hSensorTask = NULL;
TaskHandle_t hImpactTask = NULL;   /* 摔倒判据专用高速采样任务(200Hz), 见 ImpactTask */
TaskHandle_t hDisplayTask = NULL, hWifiTask = NULL;

/* ---- 通信对象 ---- */
QueueHandle_t     xQueueSensorRaw  = NULL;
QueueHandle_t     xQueueAlarmEvent = NULL;
SemaphoreHandle_t xSemSosUpload    = NULL;
SemaphoreHandle_t xMutexI2C        = NULL;
SemaphoreHandle_t xMutexSnapshot   = NULL;

/* ---- 全局快照(受 xMutexSnapshot 保护) ---- */
static SystemData_t s_snapshot;

/* ---- 报警去重(同一报警在冷却期内不重复投递) ---- */
#define ALARM_COOLDOWN_MS  30000UL
/* ---- duration_ms = 0 的事件(如"光照不足")不鸣蜂鸣器, 只在 OLED 保持 3s ---- */
#define DISPLAY_HOLD_MS    3000UL
static uint32_t s_last_alarm_tick[16];   /* 索引 = AlarmCode_t */

/* 立即上报的语义标志, 由 post_alarm() 置位、WiFi_Task 消费后清零:
 *   1 = 本次唤醒要发"数据报文"(如摔倒: 附带传感器数值与 status 文案)
 *   0 = 本次唤醒要发"SOS 事件报文"(按键触发: 只有时间与位置)
 * 两者都借同一个二进制信号量唤醒, 用本标志区分报文类型。 */
static volatile uint8_t s_immediate_report = 0;

/* ==========================================================================
 * 网络诊断状态 —— 给 OLED 用的"一句话病因"
 *
 * 【为什么需要它】
 *   AT 交互日志走 USART2(PA2/PA3), 但看它需要一根 USB-TTL 转串口线。
 *   而 OLED 是板子上现成的显示通道 —— 把"卡在哪一步"直接显示在屏幕上,
 *   不接电脑也能看到病因。排查时这一步往往比日志更关键, 因为它不需要
 *   任何额外器材。
 *
 * 【为什么不用互斥锁】
 *   写方(WifiTask)只写单个字节, 读方(DisplayTask)也只读单个字节, 在
 *   Cortex-M3 上是原子的; 显示偶尔晚一帧没有任何影响。RAM 已经很紧
 *   (见编译输出的 RW+ZI), 所以刻意省掉一个互斥量。
 *
 * 【为什么要分这么细】
 *   "连不上"这一个现象至少有 4 种完全不同的病因, 处置方式互不通用:
 *     AT 无应答    → 接线 / 波特率 / 供电 / EN 脚   (硬件层)
 *     热点连不上   → SSID 密码 / 频段 / 信号        (环境层)
 *     Broker 连不上→ 网络不通 / DNS / 防火墙        (网络层)
 *     Broker 拒绝  → 私钥 / 主题(看 CONNACK 码)     (账号层)
 *   屏幕上直接给出是哪一层, 就不会在错误的方向上白费功夫。
 * ========================================================================== */
typedef enum
{
    NET_DIAG_IDLE = 0,      /* 尚未开始联网 */
    NET_DIAG_AT_FAIL,       /* AT 无应答: 接线 / 波特率 / 供电 / EN 脚 */
    NET_DIAG_WIFI_FAIL,     /* 连不上热点: SSID 密码 / 频段 / 信号 */
    NET_DIAG_TCP_FAIL,      /* 连不上 Broker: 网络不通 / DNS / 防火墙 */
    NET_DIAG_MQTT_FAIL,     /* Broker 拒绝: 私钥 / 主题(arg = CONNACK 码) */
    NET_DIAG_SUB_FAIL,      /* 订阅失败: 数据照样能上云, 但控制台显示离线
                             * (arg = SUBACK 返回码; 0x80 = 主题不存在/无权限) */
    NET_DIAG_RETRY,         /* 退避等待中(arg = 本次等待秒数) */
    NET_DIAG_ONLINE         /* 已连上且上报成功(看 s_net_pub_count) */
} NetDiag_t;

static volatile uint8_t  s_net_diag      = NET_DIAG_IDLE;
static volatile uint8_t  s_net_diag_arg  = 0;   /* CONNACK 码 / 重试秒数 */
static volatile uint16_t s_net_pub_count = 0;   /* 累计"本机认为成功"的上报次数 */
/* 累计【回环确认成功】次数 —— 见 user_config.h 的"送达确认(回环)"说明。
 * 它才是"数据真的到了巴法云"的证据: 报文里带自增 seq, 发完在接收缓冲里
 * 找得到 "seq":N, 就说明这串字节确实经过了 Broker 又回推给了订阅者。
 * 判读: p(pub) 与 e(echo) 同步增长 = 正常;
 *       p 一直在涨而 e 恒为 0 = 发送是"假成功", 云端并没有收到数据。 */
static volatile uint16_t s_net_echo_count = 0;
static uint16_t          s_net_seq        = 0;   /* 报文自增序号(每发一条 +1) */

/* 把状态渲染成一行 21 字符以内的文案(OLED 一行 128px ≈ 21 个 6x8 字符)。
 * 【必须是纯 ASCII】armcc(AC5) 按 GBK 解析中文字面量会报警甚至吞引号,
 * 所以诊断文案一律英文短句 —— 详细说明放在注释里。 */
static void net_diag_line(char *out, size_t n)
{
    /* 【最优先提示】波特率被自适应改成过非配置值 → 说明模块实际不是配置的
     * 那个波特率。这条信息比其它任何诊断都值得先看到: 它解释了此前
     * "AT 完全没反应"的原因, 也提醒用户把 user_config.h 的 ESP01S_BAUD_RATE
     * 改成这个值, 免得每次上电都要重新扫一遍。 */
    if (ESP01S_GetBaud() != (uint32_t)ESP01S_BAUD_RATE)
    {
        snprintf(out, n, "NET: baud=%lu", (unsigned long)ESP01S_GetBaud());
        return;
    }

    switch ((NetDiag_t)s_net_diag)
    {
    case NET_DIAG_AT_FAIL:
        snprintf(out, n, "NET: AT no reply");                       break;
    case NET_DIAG_WIFI_FAIL:
        snprintf(out, n, "NET: WIFI fail");                         break;
    case NET_DIAG_TCP_FAIL:
        snprintf(out, n, "NET: TCP fail");                          break;
    case NET_DIAG_MQTT_FAIL:
        snprintf(out, n, "NET: MQTT rc=%02X", (unsigned)s_net_diag_arg); break;
    case NET_DIAG_SUB_FAIL:
        /* 订阅失败: 数据能上云, 但控制台会一直显示离线。
         * rc=80 基本就是"这个主题在 MQTT 控制台里不存在"(或名字写错)。 */
        snprintf(out, n, "NET: sub rc=%02X", (unsigned)s_net_diag_arg);  break;
    case NET_DIAG_RETRY:
        snprintf(out, n, "NET: retry %us", (unsigned)s_net_diag_arg);    break;
    case NET_DIAG_ONLINE:
        /* 【这一行是"数据到底有没有上去"的最终判据】
         *   p = 本机发送成功次数(AT+CIPSEND 拿到 SEND OK)
         *   e = 云端回环确认次数(收到自己刚发的报文)
         * 两者同步增长 → 数据确实到了巴法云, 去控制台看就行;
         * p 一直涨而 e 恒为 0 → 只是"假成功", 报文根本没到云端 ——
         * 最常见的原因是同一私钥被电脑上的调试客户端顶掉了(见 user_config.h
         * 的"送达确认(回环)"注释与 docs/13)。
         * 计数取模 1000 是为了压进一行 21 字符内(每 10s 才 +1, 3 位足够)。 */
#if NET_ECHO_CONFIRM_ENABLE
        if (s_net_pub_count >= 3 && s_net_echo_count == 0)
        {
            /* 已经发了 3 次以上却一次回环都没收到: 这是"假成功"的确证。 */
            snprintf(out, n, "NET: p=%u NO ECHO", (unsigned)(s_net_pub_count % 1000));
        }
        else
        {
            snprintf(out, n, "NET: OK p=%u e=%u",
                     (unsigned)(s_net_pub_count % 1000),
                     (unsigned)(s_net_echo_count % 1000));
        }
#else
        snprintf(out, n, "NET: OK pub=%u", (unsigned)s_net_pub_count);
#endif
        break;
    default:
        snprintf(out, n, "NET: ---");                               break;
    }
}

/* ---- 调试打印: 走 USART2(PA2/PA3, 115200-8N1) ----
 * 三个必须注意的点:
 *   1) 【曾经的缺陷】snprintf() 返回的是"假如缓冲区足够大、本应写入的长度",
 *      它可能大于缓冲区。若直接把 _n 交给 HAL_UART_Transmit(), 就会从
 *      112 字节的栈缓冲里越界往后读, 把栈上的其它数据当调试信息发出去。
 *      例如 DBG("[NET] published: %s", s_json) 时 s_json 最长 448 字节,
 *      必然触发。所以这里必须先把 _n 截断到 sizeof(_b)-1。
 *   2) 缓冲区 _b[112] 开在【调用者的任务栈】上 —— 估算任务栈时要把它算进去。
 *   3) HAL_UART_Transmit() 是阻塞发送(115200 下 112 字节约 10ms),
 *      不能在中断里调用, 也不要在高优先级任务里频繁调用。
 * 不需要调试输出时把 DBG_ENABLE 置 0, 编译期彻底去掉, 不占 Flash。 */
#define DBG_ENABLE 1

#if DBG_ENABLE
#define DBG(...) do {                                                              \
        char _b[112];                                                              \
        int _n = snprintf(_b, sizeof(_b), __VA_ARGS__);                             \
        if (_n > 0) {                                                              \
            if (_n > (int)sizeof(_b) - 1) { _n = (int)sizeof(_b) - 1; }             \
            HAL_UART_Transmit(&huart2, (uint8_t *)_b, (uint16_t)_n, 100);           \
        }                                                                          \
    } while (0)
#else
#define DBG(...) do { } while (0)
#endif

/* 任务栈水位报告开关: 1 = 每 10 秒从 USART2 打印各任务剩余栈, 便于实测调参;
 * 正式运行时可以置 0 关掉。见 APP_ReportStackUsage()。 */
#define STACK_REPORT_ENABLE 1

/* ==========================================================================
 * 通信对象创建 —— 在调度器启动前调用
 * ========================================================================== */
void APP_CommInit(void)
{
    /* 【为什么要检查返回值】
     * 队列和信号量都要从 FreeRTOS 堆(configTOTAL_HEAP_SIZE)里分配。
     * 如果堆不够, xQueueCreate 会返回 NULL 而不报错 —— 之后第一次
     * xQueueSend/xQueueReceive 触发 configASSERT 而停机, 现场只看到
     * "跑一下就死", 极难定位。这里逐个判空, 让失败点立刻显现。
     * 报错了就调大 Core/Inc/FreeRTOSConfig.h 里的 configTOTAL_HEAP_SIZE。 */
    xQueueSensorRaw  = xQueueCreate(4, sizeof(SensorRaw_t));
    xQueueAlarmEvent = xQueueCreate(8, sizeof(AlarmEvent_t));
    xSemSosUpload    = xSemaphoreCreateBinary();
    xMutexI2C        = xSemaphoreCreateMutex();
    xMutexSnapshot   = xSemaphoreCreateMutex();

    if (xQueueSensorRaw == NULL || xQueueAlarmEvent == NULL ||
        xSemSosUpload   == NULL || xMutexI2C == NULL ||
        xMutexSnapshot  == NULL)
    {
        Error_Handler();   /* FreeRTOS 堆不足 */
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(s_last_alarm_tick, 0, sizeof(s_last_alarm_tick));
    strcpy(s_snapshot.status_ascii, "normal");
    s_snapshot.status_cn = "\xe6\xad\xa3\xe5\xb8\xb8";
}

/* ==========================================================================
 * 报警投递
 * ========================================================================== */
static const char *alarm_text_ascii(AlarmCode_t code)
{
    switch (code)
    {
    case ALARM_FALL:         return "FALL ALARM";
    case ALARM_GAS:          return "GAS ALARM";
    case ALARM_HR_ABNORMAL:  return "HR ABNORMAL";
    case ALARM_SPO2_LOW:     return "SPO2 LOW";
    case ALARM_TEMP_HIGH:    return "TEMP HIGH";
    case ALARM_TEMP_LOW:     return "TEMP LOW";
    case ALARM_SOS:          return "SOS ALARM";
    case ALARM_WIFI_FAIL:    return "WIFI FAIL";
    case ALARM_SENSOR_FAULT: return "SENSOR ERR";
    case ALARM_HUMI_HIGH:    return "HUMI HIGH";
    case ALARM_HUMI_LOW:     return "HUMI LOW";
    case ALARM_CFG_SET:      return "CFG SET";
    default:                 return "WARNING";
    }
}

static const char *alarm_text_cn(AlarmCode_t code)
{
    switch (code)
    {
    case ALARM_FALL:         return "\xe6\x91\x94\xe5\x80\x92\xe6\x8a\xa5\xe8\xad\xa6";
    case ALARM_GAS:          return "\xe6\xb0\x94\xe4\xbd\x93\xe8\xb6\x85\xe6\xa0\x87";
    case ALARM_HR_ABNORMAL:  return "\xe5\xbf\x83\xe7\x8e\x87\xe5\xbc\x82\xe5\xb8\xb8";
    case ALARM_SPO2_LOW:     return "\xe8\xa1\x80\xe6\xb0\xa7\xe5\x81\x8f\xe4\xbd\x8e";
    case ALARM_TEMP_HIGH:    return "\xe9\xab\x98\xe6\xb8\xa9\xe7\x8e\xaf\xe5\xa2\x83";
    case ALARM_TEMP_LOW:     return "\xe4\xbd\x8e\xe6\xb8\xa9\xe7\x8e\xaf\xe5\xa2\x83";
    case ALARM_SOS:          return "SOS \xe7\xb4\xa7\xe6\x80\xa5";
    case ALARM_WIFI_FAIL:    return "\xe7\xbd\x91\xe7\xbb\x9c\xe5\xbc\x82\xe5\xb8\xb8";
    case ALARM_SENSOR_FAULT: return "\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8\xe6\x95\x85\xe9\x9a\x9c";
    case ALARM_HUMI_HIGH:    return "\xe6\xb9\xbf\xe5\xba\xa6\xe8\xbf\x87\xe9\xab\x98";
    case ALARM_HUMI_LOW:     return "\xe6\xb9\xbf\xe5\xba\xa6\xe8\xbf\x87\xe4\xbd\x8e";
    case ALARM_CFG_SET:      return "\xe9\x98\x88\xe5\x80\xbc\xe5\xb7\xb2\xe6\x9b\xb4\xe6\x96\xb0";
    default:                 return "\xe6\xb3\xa8\xe6\x84\x8f";
    }
}

/* ---- 哪些报警受"阈值报警蜂鸣"开关(buz)管辖 ----
 * 【为什么要有这个划分, 而不是一个总开关】
 *   蜂鸣器开关的用途是"白天调试嫌吵""锁在柜子里不想一直响"。但摔倒和 SOS
 *   是【安全功能】—— 关掉它等于把帽子最核心的报警静音。所以把这个开关的
 *   作用域明确限定在"数值越限"这一类报警上, 并在小程序界面上写清楚。
 *   网络故障报警也不受管: 它本身就是"你可能收不到数据了"的提示音。 */
static uint8_t alarm_is_threshold_kind(AlarmCode_t code)
{
    switch (code)
    {
    case ALARM_GAS:
    case ALARM_TEMP_HIGH:
    case ALARM_TEMP_LOW:
    case ALARM_HUMI_HIGH:
    case ALARM_HUMI_LOW:
    case ALARM_HR_ABNORMAL:
    case ALARM_SPO2_LOW:
        return 1;
    default:
        return 0;
    }
}

/* 当前"阈值报警蜂鸣"开关的状态。
 * 【为什么每次现取而不是维护一个镜像变量】镜像变量要在"配置被下发"的每条
 * 路径上同步, 漏一处就会变成"界面上关了、实际上还在响"。现取一次结构体拷贝
 * 只有几十字节, 而报警本身就是低频事件(还有 30s 冷却), 代价可以忽略。 */
static uint8_t buz_enabled_now(void)
{
    AlarmCfg_t c;
    AlarmCfg_Snapshot(&c);
    return c.buz_enable ? 1u : 0u;
}

/* duration_ms = 0 表示"只在 OLED 上提示, 不鸣蜂鸣器"
 * urgent = 1 表示 WiFi_Task 需要立即抢发(SOS / 网络异常 / 摔倒) */
static void post_alarm(AlarmCode_t code, uint32_t duration_ms, uint8_t urgent)
{
    AlarmEvent_t ev;
    uint32_t now = HAL_GetTick();

    if ((uint8_t)code < 16)
    {
        if (s_last_alarm_tick[code] != 0 &&
            (now - s_last_alarm_tick[code]) < ALARM_COOLDOWN_MS &&
            code != ALARM_SOS)          /* SOS 不受冷却限制 */
        {
            return;
        }
        s_last_alarm_tick[code] = now;
    }

    /* 蜂鸣开关: 只作用于"数值越限"类报警(见 alarm_is_threshold_kind)。
     * 关掉时把鸣响时长改成 0 —— 事件照样进队列, OLED 照常显示、云端照常收到,
     * 只是不吵人。这样"静音"与"不报警"是两件事, 不会因为关声音就把告警信息
     * 一起丢掉。 */
    if (duration_ms > 0 && alarm_is_threshold_kind(code) && !buz_enabled_now())
    {
        duration_ms = 0;
    }

    ev.code        = code;
    ev.duration_ms = duration_ms;
    ev.urgent      = urgent;
    ev.text_cn     = alarm_text_cn(code);
    strncpy(ev.text_ascii, alarm_text_ascii(code), sizeof(ev.text_ascii) - 1);
    ev.text_ascii[sizeof(ev.text_ascii) - 1] = '\0';

    (void)xQueueSend(xQueueAlarmEvent, &ev, pdMS_TO_TICKS(20));

    /* urgent 事件同时唤醒 WiFi_Task 立即上报。
     * 【补漏说明】此前只有 ISR 版(APP_PostAlarmFromISR)会 give 信号量,
     * 任务上下文调用的本函数不会 —— 于是"摔倒立即上报"实际退化成
     * "等下一个 10s 上报周期", 与需求不符。这里补上, 并用
     * s_immediate_report 标明本次要发的是数据报文而不是 SOS 事件报文。 */
    if (urgent)
    {
        s_immediate_report = 1;
        (void)xSemaphoreGive(xSemSosUpload);
    }
}

/* 供 EXTI 中断服务函数调用(见 main.c 的 HAL_GPIO_EXTI_Callback) */
void APP_PostAlarmFromISR(AlarmCode_t code, uint32_t duration_ms, uint8_t urgent)
{
    AlarmEvent_t ev;
    BaseType_t woken = pdFALSE;

    ev.code        = code;
    ev.duration_ms = duration_ms;
    ev.urgent      = urgent;
    ev.text_cn     = alarm_text_cn(code);
    strncpy(ev.text_ascii, alarm_text_ascii(code), sizeof(ev.text_ascii) - 1);
    ev.text_ascii[sizeof(ev.text_ascii) - 1] = '\0';

    /* 直接投递给 Alarm_Task(优先级 3, 因此 portYIELD_FROM_ISR 会立刻切过去) */
    (void)xQueueSendFromISR(xQueueAlarmEvent, &ev, &woken);

    /* 同时触发一次立即上报 */
    if (urgent)
    {
        (void)xSemaphoreGiveFromISR(xSemSosUpload, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

/* ==========================================================================
 * 任务 1: Sensor_Task (优先级 1, 周期 100ms) —— 读取全部传感器
 * ========================================================================== */
static void SensorTask(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    SensorRaw_t raw;
    MPU6050_Data_t mpu;
    MAX30102_Fifo_t fifo;
    DHT11_Data_TypeDef dht;
    uint16_t gas_level = 0;

    /* DHT11 每 2s 才允许读一次, 中间沿用上次结果 */
    static uint32_t dht_timer = 0;
    static uint8_t  dht_ok = 0, dht_ti = 0, dht_td = 0, dht_hi = 0, dht_hd = 0;

    /* DS18B20 两阶段: 阶段0 发转换命令, 阶段1 取结果 */
    static uint8_t  ds_phase = 0, ds_ok = 0;
    static uint32_t ds_timer = 0;
    static int16_t  ds_temp_x10 = 0;

    uint32_t dropped = 0;

    (void)argument;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        memset(&raw, 0, sizeof(raw));
        raw.uptime_ms = HAL_GetTick();

        /* ---------------- MPU6050 (I2C) ----------------
         * 默认挂在 I2C2 上(与 OLED 那条 I2C1 分开), 但这里仍持有 xMutexI2C:
         * 该锁统一保护所有 I2C 访问, 持有它不会出错, 只是把两条总线串行化。 */
        if (xSemaphoreTake(xMutexI2C, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            raw.mpu_ok = (MPU6050_Read(&mpu) == 0) ? 1 : 0;
            xSemaphoreGive(xMutexI2C);
        }
        if (raw.mpu_ok)
        {
            raw.ax = mpu.ax; raw.ay = mpu.ay; raw.az = mpu.az;
            raw.gx = mpu.gx; raw.gy = mpu.gy; raw.gz = mpu.gz;
        }

        /* ---------------- MAX30102 FIFO ---------------- */
        if (xSemaphoreTake(xMutexI2C, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            raw.ppg_ok = (MAX30102_ReadFifo(&fifo) == 0) ? 1 : 0;
            xSemaphoreGive(xMutexI2C);
        }
        if (raw.ppg_ok && fifo.n > 0)
        {
            raw.ppg_n = fifo.n;
            /* 溢出丢掉的样本数要一起带上去 —— 心率算法用它把时间轴补回来 */
            raw.ppg_lost = fifo.lost;
            memcpy(raw.ppg_red, fifo.red, fifo.n * sizeof(uint16_t));
            memcpy(raw.ppg_ir,  fifo.ir,  fifo.n * sizeof(uint16_t));
        }
        /* 上电预热阶段的数据不参与心率计算 */
        if (!MAX30102_IsWarmedUp()) raw.ppg_ok = 0;

        /* ---------------- DHT11 (每 2s) ---------------- */
        if ((HAL_GetTick() - dht_timer) >= DHT11_PERIOD_MS)
        {
            dht_timer = HAL_GetTick();
            if (DHT11_Read(&dht) == 0)
            {
                dht_ok = 1;
                dht_ti = dht.temp_int; dht_td = dht.temp_dec;
                dht_hi = dht.humi_int; dht_hd = dht.humi_dec;
            }
            else
            {
                dht_ok = 0;   /* 读取失败: 由 Data_Processing_Task 决定是否降级 */
            }
        }
        raw.dht_ok = dht_ok;
        raw.dht_t_int = dht_ti; raw.dht_t_dec = dht_td;
        raw.dht_h_int = dht_hi; raw.dht_h_dec = dht_hd;

        /* ---------------- DS18B20 (两阶段, 不阻塞等转换) ---------------- */
        if (ds_phase == 0)
        {
            if (DS18B20_StartConvert() == 0)
            {
                ds_phase = 1;
                ds_timer = HAL_GetTick();
            }
            else
            {
                ds_ok = 0;
            }
        }
        else if ((HAL_GetTick() - ds_timer) >= DS18B20_PERIOD_MS)
        {
            ds_phase = 0;
            if (DS18B20_ReadTempX10(&ds_temp_x10) == 0) ds_ok = 1;
            else                                        ds_ok = 0;
        }
        raw.ds_ok = ds_ok;
        raw.ds_temp_x10 = ds_temp_x10;

        /* ---------------- ADC: MQ-2 ---------------- */
        raw.gas_ok   = (MQ2_Read(&gas_level) == 0) ? 1 : 0;
        raw.gas_level = gas_level;

        /* ---------------- 投递给 Data_Processing_Task ---------------- */
        if (xQueueSend(xQueueSensorRaw, &raw, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            dropped++;    /* 处理不过来: 计数而不阻塞采样节奏 */
        }
    }
}

/* ==========================================================================
 * 任务 2: Data_Processing_Task (优先级 2) —— 滤波/判据/组快照/发报警事件
 * ========================================================================== */
static void DataProcTask(void *argument)
{
    SensorRaw_t raw;
    SystemData_t snap;
    uint16_t spo2 = 0;
    uint8_t  spo2_valid = 0;
    uint8_t  hr_valid = 0;
    uint8_t  temp_valid = 0;
    int16_t  temp_x10 = 0;
    int16_t  humi_x10 = 0;
    uint8_t  temp_src = 0;
    uint8_t  humi_valid = 0;
    static uint8_t fault_reported = 0;

    /* 门限快照 + 各门限的"已报警"锁存(迟滞用, 见下面第 4 节)。
     * 锁存用 static: 它描述的是"这一路现在是否处于报警态", 必须跨循环保持;
     * 若换成局部量, 每一轮都会从"未报警"重新开始, 迟滞就完全失效了。 */
    AlarmCfg_t cfg;
    static uint8_t lat_gas = 0, lat_t_hi = 0, lat_t_lo = 0;
    static uint8_t lat_h_hi = 0, lat_h_lo = 0, lat_hr = 0, lat_sp = 0;

    (void)argument;

    HR_Init();
    /* 注意: FallDetect_Init() 已移到 ImpactTask —— 判据的样本由那个任务提供,
     * 状态机初始化也应由同一个任务负责, 避免"谁先跑谁说了算"。 */

    for (;;)
    {
        if (xQueueReceive(xQueueSensorRaw, &raw, portMAX_DELAY) != pdTRUE) continue;

        /* 取一次门限快照: 本轮所有判断都用这一份(理由见下面第 4 节)。
         * 放在收数据之后而不是任务启动时——它必须是最新的, 否则小程序刚下发的
         * 门限要等下一次"重新进循环"才生效。 */
        AlarmCfg_Snapshot(&cfg);

        /* ---------------- 1) 摔倒/撞击检测: 已移出本任务 ----------------
         *
         * 【为什么搬走 —— 这是"怎么扔都不报警"的真正病根】
         *   本任务挂在 Sensor_Task 的 100ms 周期上(10Hz)。而一次撞击只持续
         *   5~20ms, 落在两个采样点之间的概率超过 80% —— 信号根本没被看到。
         *   更致命的是判据条件2 要求"100ms 内再次超阈", 采样周期恰好等于
         *   窗口宽度, 该条件等于要求撞击持续满 100ms, 真实撞击远达不到。
         *
         *   现在改由 ImpactTask 以 200Hz 驱动(见本文件 ImpactTask),
         *   配合 MPU 的 ODR=1kHz 与 DLPF=188Hz, 撞击尖峰才第一次被真正看见。
         *
         *   本任务仍保留 raw.ax/ay/az 供显示与上报使用 —— 判据与显示用的
         *   样本从此分开: 判据要"快", 显示要"稳", 两者本来就不该共用一个周期。
         * ------------------------------------------------------------------ */

        /* ---------------- 2) PPG → 心率/血氧 ---------------- */
        if (raw.ppg_ok)
        {
            /* 【顺序不能反】先告诉算法"刚才丢了 n 个样本", 再推这一批新样本。
             * 丢样本在算法眼里等于"没发生过", 不补时间轴的话 IBI 会系统性
             * 偏短 —— 仿真里真值 72bpm 会被读成 91.7bpm。 */
            if (raw.ppg_lost) HR_NotifyDrop(raw.ppg_lost);
            HR_PushSamples(raw.ppg_ir, raw.ppg_red, raw.ppg_n);
        }
        HR_GetResult(&snap.hr_bpm, &spo2, &spo2_valid);
        hr_valid = (snap.hr_bpm != 0) ? 1 : 0;

        /* ---------------- 3) 温度/湿度: DHT11 优先, 失败用 DS18B20 替代 ---- */
        if (raw.dht_ok)
        {
            temp_x10  = (int16_t)(raw.dht_t_int * 10 + raw.dht_t_dec);
            humi_x10  = (int16_t)(raw.dht_h_int * 10 + raw.dht_h_dec);
            temp_src  = 0;          /* DHT11 */
            temp_valid = 1;
            humi_valid = 1;
        }
        else if (raw.ds_ok)
        {
            /* 容错机制: DHT11 读取失败时用 DS18B20 温度代替,
             * 但湿度无替代来源 → humi_valid = 0, 上报时明确标记无效 */
            temp_x10  = raw.ds_temp_x10;
            temp_src  = 1;          /* DS18B20 */
            temp_valid = 1;
            humi_valid = 0;
            humi_x10   = 0;
        }
        else
        {
            temp_valid = 0;
            humi_valid = 0;
        }

        /* ---------------- 4) 阈值判断 → 报警事件 ----------------
         *
         * 【本节的每一个门限都来自 AlarmCfg, 不再是编译期宏】
         *   cfg 是本轮循环开头取的【一份快照】, 整节判断都只用它 ——
         *   若中途小程序正好下发了一套新门限, 也不会出现"气体按新门限、
         *   温度按旧门限"这种混合判断(那种组合可能自相矛盾, 例如温度上限
         *   被调低了、下限还是旧的)。
         *
         * 【迟滞(lat_xxx)为什么必须有】
         *   门限附近的值一定在抖(温度 25.0/25.1 来回跳, 气体 ADC 有几个 LSB
         *   的噪声)。没有迟滞时, 判据会在"报一次 → 下一刻不报 → 再下一刻又报"
         *   之间反复, 现场听到的是一串没完没了的啾啾声, 而环境其实什么都没变。
         *   规则: 越过门限就置位; 要解除必须回落到 门限-迟滞 以下。
         *   【注意迟滞只影响解除, 不影响触发】—— 触发永远严格按用户设的那个数,
         *   否则"我把门限设成 25 却 24.5 才报警"会让人怀疑设错了。
         *   各 lat_ 变量是 task 内的局部量, 不需要加锁。 */
        if (raw.gas_ok)
        {
            if (!lat_gas) { if (raw.gas_level > cfg.gas_level) lat_gas = 1; }
            else if (raw.gas_level + ALARM_HYST_GAS <= cfg.gas_level) lat_gas = 0;
            if (lat_gas) post_alarm(ALARM_GAS, 2000, 0);
        }
        else
        {
            lat_gas = 0;      /* 传感器读不到时不留陈旧状态 */
        }

        if (temp_valid)
        {
            /* 高低温互斥: 校验时已保证 tLo < tHi, 但两者之间仍有一段
             * "正常带"。各自独立判, 不写 else if —— 迟滞解除的那一刻,
             * 另一侧可能正好越限, 用 else if 会漏掉一轮。 */
            if (!lat_t_hi) { if (temp_x10 > cfg.temp_hi_x10) lat_t_hi = 1; }
            else if (temp_x10 + ALARM_HYST_TEMP_X10 <= cfg.temp_hi_x10) lat_t_hi = 0;

            if (!lat_t_lo) { if (temp_x10 < cfg.temp_lo_x10) lat_t_lo = 1; }
            else if (temp_x10 >= cfg.temp_lo_x10 + ALARM_HYST_TEMP_X10) lat_t_lo = 0;

            if (lat_t_hi) post_alarm(ALARM_TEMP_HIGH, 1500, 0);
            if (lat_t_lo) post_alarm(ALARM_TEMP_LOW, 1000, 0);
#if HW_FAN_PRESENT
            /* 有风扇时的散热控制应加在这里(本硬件无风扇, 故不实现) */
#else
            /* 硬件缺失: 无风扇驱动电路, 仅报警不做散热动作 */
#endif
        }
        else
        {
            lat_t_hi = 0;
            lat_t_lo = 0;
        }

        /* 湿度: 与温度同一套判断, 门限同样可由小程序下发。
         * 需求里没给湿度门限, 属本项目自定义(见 user_config.h 第五节)。 */
        if (humi_valid)
        {
            if (!lat_h_hi) { if (humi_x10 > cfg.humi_hi_x10) lat_h_hi = 1; }
            else if (humi_x10 + ALARM_HYST_HUMI_X10 <= cfg.humi_hi_x10) lat_h_hi = 0;

            if (!lat_h_lo) { if (humi_x10 < cfg.humi_lo_x10) lat_h_lo = 1; }
            else if (humi_x10 >= cfg.humi_lo_x10 + ALARM_HYST_HUMI_X10) lat_h_lo = 0;

            if (lat_h_hi) post_alarm(ALARM_HUMI_HIGH, 1500, 0);
            if (lat_h_lo) post_alarm(ALARM_HUMI_LOW, 1000, 0);
        }
        else
        {
            lat_h_hi = 0;
            lat_h_lo = 0;
        }

        /* 心率异常报警要【同时】满足"信号质量达标"。
         * 以前只看数值是否越界 —— 而读数不稳时, 一个噪声造成的 45bpm 就会
         * 触发"心率异常"。信号本身没达标时, 那个数不值得报警。
         * 【迟滞在这里尤其关键】心率读数本身有 ±1~2 bpm 的自然起伏(算法是
         * 10 秒窗口中位数, 仍会跳动), 门限上没迟滞就会反复触发。 */
        if (hr_valid && HR_IsSignalOk())
        {
            if (!lat_hr)
            {
                if (snap.hr_bpm < cfg.hr_lo || snap.hr_bpm > cfg.hr_hi) lat_hr = 1;
            }
            else if (snap.hr_bpm >= cfg.hr_lo + ALARM_HYST_HR &&
                     snap.hr_bpm <= cfg.hr_hi - ALARM_HYST_HR)
            {
                lat_hr = 0;
            }
            if (lat_hr) post_alarm(ALARM_HR_ABNORMAL, 1500, 0);
        }
        else
        {
            /* 信号没达标就不报警, 也顺手清掉锁存 —— 否则手指拿开后,
             * 这个锁存会挂在那里, 下次一有信号立刻补报一次旧状态。 */
            lat_hr = 0;
        }

        if (spo2_valid)
        {
            if (!lat_sp) { if (spo2 < cfg.spo2_min) lat_sp = 1; }
            else if (spo2 >= cfg.spo2_min + ALARM_HYST_SPO2) lat_sp = 0;
            if (lat_sp) post_alarm(ALARM_SPO2_LOW, 1500, 0);
        }
        else
        {
            lat_sp = 0;
        }

        /* 传感器集体失效时给出维护提示(只提示一次, 避免刷屏) */
        if (!raw.mpu_ok && !raw.dht_ok && !raw.ds_ok)
        {
            if (!fault_reported)
            {
                fault_reported = 1;
                post_alarm(ALARM_SENSOR_FAULT, 1000, 0);
            }
        }
        else
        {
            fault_reported = 0;
        }

        /* ---------------- 5) 组装快照并发布 ---------------- */
        snap.spo2_percent = spo2;
        snap.spo2_valid   = spo2_valid;
        snap.spo2_cal     = (SPO2_MODE == SPO2_MODE_CALIBRATED) ? 1 : 0;
        /* PPG 测量状态随快照一起发 —— 一次测量有 13 秒上行 hr 是 null,
         * 不带这个字段就分不清"没放手 / 正在测 / 测了没测到"(见 app_events.h)。 */
        snap.ppg_st       = (uint8_t)HR_GetState();
        snap.hr_valid     = hr_valid;
        snap.temp_valid   = temp_valid;
        snap.temp_x10     = temp_valid ? temp_x10 : 0;
        snap.temp_src     = temp_src;
        snap.humi_x10     = humi_x10;
        snap.humi_valid   = humi_valid;
        snap.gas_level    = raw.gas_level;
        snap.ax = raw.ax; snap.ay = raw.ay; snap.az = raw.az;
        snap.uptime_ms    = raw.uptime_ms;
        /* 报警状态字段由 Alarm_Task 维护, 这里不覆盖 */

        if (xSemaphoreTake(xMutexSnapshot, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            /* 只更新数据字段, 保留 Alarm_Task 写入的报警状态 */
            AlarmCode_t  keep_alarm = s_snapshot.alarm;
            char         keep_txt[16];
            const char  *keep_cn = s_snapshot.status_cn;

            strncpy(keep_txt, s_snapshot.status_ascii, sizeof(keep_txt) - 1);
            keep_txt[sizeof(keep_txt) - 1] = '\0';

            s_snapshot = snap;                     /* 覆盖数据字段 */

            s_snapshot.alarm = keep_alarm;
            memcpy(s_snapshot.status_ascii, keep_txt, sizeof(keep_txt));
            s_snapshot.status_cn = keep_cn;

            xSemaphoreGive(xMutexSnapshot);
        }
    }
}

/* ==========================================================================
 * 任务 3: Alarm_Task (优先级 3, 最高) —— 事件响应: 蜂鸣器 + OLED 状态
 *
 * 响应流程(与需求一致):
 *   收到事件 → 抢占当前报警 → 按 duration_ms 鸣响 →
 *   把报警状态写入快照(OLED 立即显示"摔倒报警"等) →
 *   到期后关蜂鸣器并恢复状态。
 * 鸣响方式按 urgent 分两种(2026-09-16 新增):
 *   urgent=1(摔倒/SOS) → 连续长鸣; 其余 → 200ms 断续提示音。
 *   摔倒报警因此是"一声不断的 3 秒长警", 而不是一串短促的啾啾声。
 * 为不丢失新事件, 鸣响过程按 50ms 切片等待队列, 一旦有新事件立刻抢占。
 * ========================================================================== */
static void AlarmTask(void *argument)
{
    AlarmEvent_t ev;
    AlarmEvent_t cur;
    uint8_t  active = 0;
    uint32_t elapsed = 0;
    uint32_t hold = 0;
    uint8_t  beep_phase = 0;

    (void)argument;

    Buzzer_Init();

    for (;;)
    {
        if (!active)
        {
            /* 无报警: 阻塞等待事件(不消耗 CPU) */
            if (xQueueReceive(xQueueAlarmEvent, &ev, portMAX_DELAY) == pdTRUE)
            {
                cur = ev;
                active = 1;
                elapsed = 0;
                beep_phase = 0;
                /* duration_ms = 0 表示只做显示提示(如光照不足), 不鸣响 */
                hold = (cur.duration_ms > 0) ? cur.duration_ms : DISPLAY_HOLD_MS;
                if (cur.duration_ms > 0) Buzzer_On();
                else                     Buzzer_Off();
                DBG("[ALARM] start code=%d dur=%lums\r\n", (int)cur.code,
                    (unsigned long)cur.duration_ms);

                if (xSemaphoreTake(xMutexSnapshot, pdMS_TO_TICKS(20)) == pdTRUE)
                {
                    s_snapshot.alarm  = cur.code;
                    s_snapshot.status_cn = cur.text_cn;
                    strncpy(s_snapshot.status_ascii, cur.text_ascii,
                            sizeof(s_snapshot.status_ascii) - 1);
                    s_snapshot.status_ascii[sizeof(s_snapshot.status_ascii) - 1] = '\0';
                    xSemaphoreGive(xMutexSnapshot);
                }
            }
        }
        else
        {
            /* 50ms 切片: 既能产生断续提示音, 又能被新事件抢占 */
            if (xQueueReceive(xQueueAlarmEvent, &ev, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                cur = ev;                 /* 新事件抢占: 重置计时 */
                hold = (cur.duration_ms > 0) ? cur.duration_ms : DISPLAY_HOLD_MS;
                elapsed = 0;
            }
            else
            {
                elapsed += 50;
            }

            /* 两种声音, 用耳朵就能区分事件性质(这是刻意的设计):
             *   urgent = 1(摔倒/SOS) → 【连续长鸣】, 听得出来"出事了";
             *   其余(气体/温度/心率…) → 200ms 响 / 200ms 停的断续提示音,
             *                            听得出来"提醒一下, 不用慌"。
             * 早期版本对摔倒也用了断续音, 3000ms 实际是 7 声"啾啾",
             * 与需求里"报警 3 秒"的语义不符(用户要的是一声不断的长警)。 */
            if (cur.duration_ms > 0)
            {
                if (cur.urgent)
                {
                    Buzzer_On();                    /* 连续长鸣, 不切换相位 */
                }
                else
                {
                    beep_phase = (uint8_t)((elapsed / 200) & 0x01);
                    if (beep_phase) Buzzer_On();
                    else            Buzzer_Off();
                }
            }
            else
            {
                Buzzer_Off();
            }

            if (elapsed >= hold)
            {
                Buzzer_Off();
                active = 0;

                if (xSemaphoreTake(xMutexSnapshot, pdMS_TO_TICKS(20)) == pdTRUE)
                {
                    s_snapshot.alarm = ALARM_NONE;
                    s_snapshot.status_cn = "\xe6\xad\xa3\xe5\xb8\xb8";
                    strcpy(s_snapshot.status_ascii, "normal");
                    xSemaphoreGive(xMutexSnapshot);
                }
            }
        }
    }
}

/* ==========================================================================
 * 任务 3.5(新增): ImpactTask (优先级 3, 周期 5ms = 200Hz) —— 摔倒判据专用采样
 *
 * 【为什么单独开一个任务, 而不是把 Sensor_Task 周期直接改成 5ms】
 *   Sensor_Task 一个周期里要读 MPU、MAX30102 FIFO、DHT11(位操作, 单次阻塞
 *   约 20ms)、DS18B20、两路 ADC, 再把整包样本投进队列。把它拉到 200Hz:
 *     · DHT11 那次 20ms 阻塞会直接吃掉 4 个采样点(每 2 秒发生一次);
 *     · 队列里传的是含 PPG 数组的大结构体, 200Hz 会把它撑爆并丢样本;
 *     · 显示与上报根本不需要 200Hz —— 那是纯浪费。
 *   判据真正需要的只有"加速度样本, 要快、要别丢", 那就只给它这一件事。
 *
 * 本任务职责极窄, 所以代码很短:
 *   读一帧 MPU6050 → 喂在线标定 → 喂摔倒状态机 → 该报警就报警。
 * 它不碰队列、不碰 OLED、不碰网络; 唯一共享资源是 I2C 总线锁。
 *
 * 优先级 3(与 AlarmTask 同级, 高于 Sensor/DataProc/Display):
 *   判据是安全功能, 不能被显示刷新(优先级 0)或数据处理压后。
 *
 * ⚠ I2C 锁等待超时给到 100ms(而不是别处的 20ms)是刻意的:
 *   一次 OLED 整屏刷新约 30~40ms, 期间本任务必然要等。宁可晚一点读到,
 *   也不要因为超时就【把样本丢掉】—— 丢样本正是本次要修的病。
 *   实测显示刷新每 500ms 一次, 对本任务的实际影响远小于 1 个采样点。
 * ========================================================================== */
static void ImpactTask(void *argument)
{
    TickType_t     last_wake = xTaskGetTickCount();
    MPU6050_Data_t d;
    uint8_t        rc;

    (void)argument;

    FallDetect_Init();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IMPACT_PERIOD_MS));

        rc = 1;
        if (xSemaphoreTake(xMutexI2C, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            rc = MPU6050_Read(&d);
            xSemaphoreGive(xMutexI2C);
        }
        if (rc != 0) continue;    /* 本点读失败就跳过, 绝不拿旧值凑数 ——
                                   * 拿旧值会让"一次撞击"被当成"两次撞击" */

        /* 静默期在线标定: 只喂原始码值, 由它自己判断"这一段够不够静止"。
         * 这样"上电时被拿在手里导致标定失败"的情形, 运行中放稳一两秒即自愈。 */
        MPU6050_OnlineCalFeed(d.raw_ax, d.raw_ay, d.raw_az);

        /* 判据。时刻用 HAL_GetTick(): 是【本采样发生的时刻】, 而不是样本被
         * 处理时的时刻 —— 两者在高负载下会差出好几毫秒, 而判据窗口只有 100ms。 */
        /* ⚠ 这里【故意不调用 DBG()】。DBG 会在调用者栈上开 112 字节缓冲并
         * 阻塞发送(115200 下发 60 字节约 5ms) —— 放在 5ms 周期的任务里,
         * 一次日志就要吃掉 1~2 个采样点, 而那正是摔倒判据最需要连续的区间。
         * 判据的现场信息改由 OLED 承担(第 6 行的 FALL:xx + 峰值 |a|),
         * 够用且零延迟代价。 */
        if (FallDetect_Update(d.ax, d.ay, d.az, HAL_GetTick()))
        {
            post_alarm(ALARM_FALL, FALL_ALARM_MS, 1);  /* 连续长鸣 3s + 立即上报 */
        }
    }
}

/* ==========================================================================
 * 任务 4: Display_Task (优先级 0, 周期 500ms) —— 刷新 OLED
 * 显示布局(6x8 字库, 每行 21 字符, 行号从 0 数):
 *   行0 PPG 测量状态(put finger on / settling in n s / pending n s / done)
 *   行1 心率/血氧        行2 温度/湿度      行3 气体/光照
 *   行4 三轴加速度       行5 状态(报警 / 传感器异常 / 摔倒瞬时态 / PPG 中间量)
 *   行6 运行时长 + |a| 峰值                  行7 联网诊断
 *
 * 【2026-09-17 布局调整: 去掉时间行】
 *   行0 原来显示 ISO8601 时间, 按用户要求去掉。理由也站得住 —— 设备侧
 *   RTC 没有备份电池(断电即失), 时间本来以云端为准, 上行报文里带的
 *   HH:MM:SS 才是权威值; 放在屏上属于"看着很全, 实际没人拿来用"。
 *   腾出来的行0 给"PPG 测量状态" —— 见下面 ppgline 的注释。
 * ========================================================================== */
static void x10_to_str(char *out, uint16_t out_len, int16_t v)
{
    uint16_t a = (v < 0) ? (uint16_t)(-v) : (uint16_t)v;

    if (v < 0) snprintf(out, out_len, "-%u.%u", (unsigned)(a / 10), (unsigned)(a % 10));
    else       snprintf(out, out_len, "%u.%u",  (unsigned)(a / 10), (unsigned)(a % 10));
}

/* 摔倒状态机的瞬时态 → 屏幕短标签(2026-09-16 新增)。
 * 只有 21 字符可用, 所以用缩写; 含义见 fall_detect.h 的状态迁移图。
 * 这张"状态→含义"对照是排查判据不报警时唯一需要的东西:
 *   FF  = 采到了失重, 在等撞击   → 撞击没被采到(查 DLPF/带宽/阈值)
 *   IMP = 采到一次撞击, 在等第二次 → 是单次磕碰, 或窗口太窄
 *   CFM = 两次都采到了, 在等"躺稳" → 摔后还在动(条件3 在重新计时) */
static const char *fall_state_tag(FallState_t st)
{
    switch (st)
    {
    case FALL_FREEFALL: return "FF";
    case FALL_IMPACT:   return "IMP";
    case FALL_CONFIRM:  return "CFM";
    case FALL_LOCKOUT:  return "LCK";
    default:            return "IDLE";
    }
}

static void DisplayTask(void *argument)
{
    SystemData_t s;
    char line[24];
    char ppgline[24];  /* 行0: PPG 测量状态(原时间行的位置) */
    char hrline[24], thline[24], gasline[24], aline[24], upline[24];
    char fallbuf[24];  /* 摔倒判据瞬时态行(FF/IMP/CFM), 见 fall_state_tag() */
#if HR_PPG_DEBUG_LINE
    char ppgbuf[24];   /* 只在 PPG 调试行开启时用; 跟着开关一起编掉, 避免"声明未引用"告警 */
#endif
    char tbuf[8], hbuf[8];
    char axb[8], ayb[8], azb[8];
    char magb[8];   /* 三轴模值 |a| 的文本, 与三轴同行显示 */
    uint32_t up;
#if STACK_REPORT_ENABLE
    uint8_t  stack_tick = 0;        /* 每 20 个周期(= 20 × DISPLAY_PERIOD_MS = 10 秒)打印一次栈水位 */
#endif

    (void)argument;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));

#if STACK_REPORT_ENABLE
        /* 任务栈余量实测: 放在优先级最低、周期最长的显示任务里, 不影响实时性 */
        if (++stack_tick >= 20)
        {
            stack_tick = 0;
            APP_ReportStackUsage();
        }
#endif

        if (xSemaphoreTake(xMutexSnapshot, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        s = s_snapshot;                       /* 拷贝一份, 尽快释放互斥锁 */
        xSemaphoreGive(xMutexSnapshot);

        /* ---- 行0: PPG 测量状态(2026-09-17 新增, 占原时间行的位置) ----
         *
         * 【为什么这一行值得占屏幕最顶上一行】
         *   用户原话是两个问题拼在一起: "心率血氧还是会误测" + "改成手放上去后
         *   3 秒再开始监测"。也就是说, 他会看到【贴上去的前十几秒是没有读数的】。
         *   如果这期间屏上只有 `HR:-- SpO2:--`, 那就和"模块坏了"长得一模一样 ——
         *   用户没法区分"请再等 5 秒"和"这次真的没测出来"。
         *   这一行就是把这几件事分开的:
         *     PPG: put finger on    → 还没检测到手指, 没数是【正常】的
         *     PPG: settling in 2s   → 检测到了, 稳定期还剩 2 秒, 没数也正常
         *     PPG: pending 7s       → 正在采集, 还剩 7 秒(用户要求显示 pending)
         *     PPG: done  R:0.72     → 15 秒采完了, 结果就是行1 上的值
         *                             (顺便把 R 常显出来, 血氧标定要记的就是它)
         *     PPG: no pulse, retry  → 采完了但没攒够有效拍数 —— 这是"这次没
         *                             测到", 不是"还在忙"。两者在屏上必须不
         *                             一样, 否则用户会一直干等下去。
         *   于是"行1 说有没有数 / 行0 说为什么没数", 两行合起来才是完整的
         *   现场判据。四个状态的定义与理由见 heart_rate.h / user_config.h。
         *
         * 【pending 为什么还带秒数】用户要求显示 pending, 这里连剩余秒数一起
         *   给: 一个光秃秃的 "pending" 不告诉人还要等多久, 而 15 秒不说话很
         *   容易被当成死机。向上取整, 所以显示归零的那一刻正好是出结果的那
         *   一刻, 不会出现"显示 0 却还没好"。
         *
         * 【R 值为什么放这里】血氧标定要的就是这个 R(见 docs/03 的标定方法),
         *   以前只能在调试串口里看。放在常显行上, 拿一台指夹血氧仪对照读数
         *   就能直接记点, 不必再接电脑。 */
        if (!MAX30102_IsPresent())
        {
            /* 芯片不在 I2C 总线上 —— 与"没贴手指"必须分开, 否则会一直往
             * 算法/佩戴上找原因(详见 docs/14)。 */
            strcpy(ppgline, "PPG: sensor NG");
        }
        else if (!MAX30102_IsWarmedUp())
        {
            strcpy(ppgline, "PPG: warming up");
        }
        else
        {
            switch (HR_GetState())
            {
            case HR_ST_SETTLING:
                /* 向上取整到秒: 剩 3000ms 显示 3, 剩 20ms 显示 1 —— 归零那一
                 * 刻正好是开始采集的那一刻。 */
                snprintf(ppgline, sizeof(ppgline), "PPG: settling in %us",
                         (unsigned)((HR_GetSettleRemainMs() + 999UL) / 1000UL));
                break;

            case HR_ST_MEASURING:
                snprintf(ppgline, sizeof(ppgline), "PPG: pending %us",
                         (unsigned)((HR_GetMeasureRemainMs() + 999UL) / 1000UL));
                break;

            case HR_ST_DONE:
                if (!HR_IsResultValid())
                {
                    /* 15 秒采完了, 但合格拍数不够(信号弱/没贴好)。
                     * 明确说"没测到, 重来一次", 而不是让用户对着 HR:-- 猜。
                     * 【必须先看行 5 的 d/a】它决定这次失败属于哪一类:
                     * 屏上有 "ADC SAT" 就先调 LED 电流, 别去怪佩戴(docs/38)。 */
                    strcpy(ppgline, "PPG: no pulse, retry");
                }
                else if (HR_GetRatioR() > 0.01f)
                {
                    /* R 用整数百分位拼出来, 不走 %f —— 省下浮点 printf 那一大坨
                     * 库代码(这个工程用的是精简 C 库)。 */
                    uint16_t r100 = (uint16_t)(HR_GetRatioR() * 100.0f + 0.5f);
                    snprintf(ppgline, sizeof(ppgline), "PPG: done  R:%u.%02u",
                             (unsigned)(r100 / 100U), (unsigned)(r100 % 100U));
                }
                else
                {
                    strcpy(ppgline, "PPG: done");
                }
                break;

            default:   /* HR_ST_IDLE */
                strcpy(ppgline, "PPG: put finger on");
                break;
            }
        }

        /* 心率/血氧行
         * 【六种状态必须分开, 否则"心率一直是 --"会让人白找原因】
         *   器件不在总线上      → SPO2:MAX30102 NG   (接错/没供电/地址不对)
         *   在线上但样本没进来  → PPG warming up     (FIFO 读不到数据, 会一直停在这)
         *   还没测完            → HR:-- SpO2:--      (行0 有倒计时, 等着就行)
         *   测完了但没攒够拍数  → HR:-- SpO2:--      (行0 是 "no pulse, retry")
         *   正常但血氧未标定    → HR:72 SpO2:94%~    (末尾 ~ 表示是估算值)
         *   正常且已标定        → HR:72 SpO2:98%
         * 前三种"没数"的原因完全不同, 而屏上 HR:-- 长得一样 ——
         * 所以必须靠行0 的状态文案来区分, 两者要一起看。
         *
         * 【为什么血氧要带一个 ~】SPO2_MODE=1 给的是公开经验式的估算值, 而
         * 标定过的值才谈得上可信。两者都是两位数出现在同一位置, 不标出来
         * 谁也不知道区别 —— 截图拿出去就成了"我们测到血氧 94%"。
         * 屏幕和报文里的 spo2_cal 必须说同一件事。 */
        if (s.hr_valid)
        {
            if (s.spo2_valid) snprintf(hrline, sizeof(hrline), "HR:%u SpO2:%u%%%s",
                                       (unsigned)s.hr_bpm, (unsigned)s.spo2_percent,
                                       s.spo2_cal ? "" : "~");
            else              snprintf(hrline, sizeof(hrline), "HR:%u SpO2:--",
                                       (unsigned)s.hr_bpm);
        }
        else if (!MAX30102_IsPresent())
        {
            /* 心率芯片压根不在 I2C 总线上(没接好/没供电/地址不对)。
             * 这与"没放手指 / 还没预热完"必须区分开, 否则会一直往算法上找原因。 */
            strcpy(hrline, "SPO2:MAX30102 NG");
        }
        else if (!MAX30102_IsWarmedUp())
        {
            /* 器件在线上, 但连"预热所需的样本"都还没攒够(100 个 ≈ 2 秒)。
             * 若这一行长时间不变, 说明 FIFO 里根本没有新样本 ———
             * 那是"读不到数据", 不是"没放手指"。 */
            strcpy(hrline, "PPG warming up");
        }
        else
        {
            strcpy(hrline, "HR:-- SpO2:--");
        }

        /* 温度/湿度行(DHT11 失败时温度来自 DS18B20, 湿度显示 --) */
        if (s.temp_valid) x10_to_str(tbuf, sizeof(tbuf), s.temp_x10);
        else              strcpy(tbuf, "--");
        if (s.humi_valid) x10_to_str(hbuf, sizeof(hbuf), s.humi_x10);
        else              strcpy(hbuf, "--");

        if (s.humi_valid) snprintf(thline, sizeof(thline), "T:%sC H:%s%%", tbuf, hbuf);
        else              snprintf(thline, sizeof(thline), "T:%sC H:--", tbuf);

        /* 气体行(光照段已随光照功能删除, 见 docs/39) */
        snprintf(gasline, sizeof(gasline), "GAS:%u",
                 (unsigned)s.gas_level);

        /* ---- 三轴加速度行 + 模值 |a| ----
         *
         * 【为什么要把 |a| 也显示出来 —— 它是"换算对不对"唯一的现场凭据】
         *   静止时 |a| 恒为 1.0g, 无论帽子朝哪个方向 —— 这是加速度计的固有
         *   性质(重力矢量长度不变), 也正是在判据能只用模值的原因。
         *   把 |a| 跟在三轴后面, 现场一眼可判换算是否正确:
         *       静止 |a| ≈ 1.0  → 换算正确, 按物理阈值判读即可
         *       静止 |a| ≈ 3.9  → 换算系数错(量程没生效), 见 docs/22 / docs/25
         *
         *   顺带: 它也是"这是个加速度计, 不是位置传感器"最直接的自证 ——
         *   位置量不可能在任意姿态下都给出同一个固定模值。
         *   (实现上取 FallDetect_GetMagnitude(), 即判据真正使用的那个值,
         *    避免"显示用的模值"和"判定用的模值"各算各的。) */
        x10_to_str(axb, sizeof(axb), (int16_t)(s.ax * 10.0f));
        x10_to_str(ayb, sizeof(ayb), (int16_t)(s.ay * 10.0f));
        x10_to_str(azb, sizeof(azb), (int16_t)(s.az * 10.0f));
        x10_to_str(magb, sizeof(magb), (int16_t)(FallDetect_GetMagnitude() * 10.0f));
        snprintf(aline, sizeof(aline), "A:%s %s %s|%s", axb, ayb, azb, magb);

        /* 运行时长行 + 最近 3 秒的 |a| 峰值(2026-09-16 新增)
         *
         * 峰值是判断"撞击到底有没有被采到"的【唯一现场凭据】, 而这一条
         * 恰恰是排查摔倒误/漏报时最先要分清的事 ——
         *     峰值能到 2.0 → 芯片看到了撞击(±2g 量程内削顶在 2.0)
         *                    若此时仍不报警, 问题在状态机/阈值;
         *     峰值一直是 1.0 → 撞击根本没进来, 该往回查采样率与 DLPF 带宽,
         *                    这种时候再怎么调阈值都是白费。
         * UP 保留是有用的: 报警后来不及看屏, 事后一眼能核对"这期间重启过没有"。 */
        up = s.uptime_ms / 1000;
        x10_to_str(magb, sizeof(magb), (int16_t)(FallDetect_GetPeak() * 10.0f));
        snprintf(upline, sizeof(upline), "UP:%02u:%02u:%02u P%s",
                 (unsigned)(up / 3600), (unsigned)((up / 60) % 60),
                 (unsigned)(up % 60), magb);

        /* ---- 写显存(受 I2C 互斥锁保护) ---- */
        if (xSemaphoreTake(xMutexI2C, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            OLED_Clear();

            OLED_ShowStringPadded(0, 0, ppgline, 21);         /* PPG 测量状态(原时间行) */
            OLED_ShowStringPadded(0, 1, hrline, 21);          /* 心率/血氧 */
            OLED_ShowStringPadded(0, 2, thline, 21);          /* 温度/湿度 */
            OLED_ShowStringPadded(0, 3, gasline, 21);         /* 气体 */
            OLED_ShowStringPadded(0, 4, aline, 21);           /* 三轴加速度 */

            if (s.alarm != ALARM_NONE)
            {
#if OLED_USE_CN_FONT
                OLED_ShowCN(0, 5, s.status_cn);               /* 中文报警文案 */
#else
                OLED_ShowStringPadded(0, 5, s.status_ascii, 21);
#endif
            }
#if HR_PPG_DEBUG_LINE
            /* ---- PPG 调试行【抢占】MPU6050 异常告警(2026-09-22 调整) ----
             * 【为什么把优先级倒过来】
             *   原来的顺序是"MPU6050 异常告警优先, PPG 调试行让位"。后果在
             *   2026-09-22 的现场照片上直接看到了: 屏上第 6 行是
             *   "! MPU6050 FAIL", 于是 **d/a/b 那一行整场测量都没有出现过**,
             *   用户只能看到行 0 的 "PPG: no pulse, retry" —— 而那一行恰好
             *   是"没采到光 / 有光没搏动 / 峰检出但间隔全被拒"三种病因的
             *   共同出口。一个【持续存在】的告警把唯一能分开病因的诊断行
             *   永久顶掉了, 这是显示优先级设计上的错。
             *
             * 【怎么调才两边都不丢】
             *   只在【正在测量或刚测完且没读出数】时抢占; IDLE 时这行仍归
             *   告警。理由: 告警是持续状态, 什么时候看都能看到; 而"这次为什么
             *   没测出来"只在测量那十几秒内可观测。心率一旦读出有效值,
             *   条件 `!s.hr_valid` 不成立, 这一行立刻交还给告警。
             *
             * 【ADC SAT 是怎么判的】d 由 max30102.c 的 `(v & 0x03FFFF) >> 2`
             *   得到, 上限 65535(不是 18 位 ADC 的 262143)。顶到它附近就说明
             *   光电流已经把头削平 —— 波形越强越测不出, 正是"加大 LED 电流后
             *   10 次 10 次失败"的现场。判据与推导见 docs/38 与 user_config.h。 */
            else if (!s.hr_valid && HR_GetState() != HR_ST_IDLE)
            {
                if ((float)HR_GetIrDc() >= PPG_ADC_SAT_LEVEL)
                {
                    /* d 顶上去了 —— 先说这一条, 因为它会推翻后面所有判读:
                     * 饱和时 a 必然是 0, 那不是"贴合不好", 是信号被削平了。 */
                    snprintf(ppgbuf, sizeof(ppgbuf), "ADC SAT d:%u",
                             (unsigned)HR_GetIrDc());
                }
                else if (HR_GetDroppedSamples() > 0)
                    snprintf(ppgbuf, sizeof(ppgbuf), "d:%u a:%u DRP",
                             (unsigned)HR_GetIrDc(), (unsigned)HR_GetPeakAmp());
                else
                    snprintf(ppgbuf, sizeof(ppgbuf), "d:%u a:%u b:%u",
                             (unsigned)HR_GetIrDc(), (unsigned)HR_GetPeakAmp(),
                             (unsigned)HR_GetBeats());
                OLED_ShowStringPadded(0, 5, ppgbuf, 21);
            }
#endif
#if MPU6050_FS_ALERT_LINE
            /* 姿态传感器异常告警 —— 排在 PPG 调试行【之前】。
             *
             * 理由: PPG 行只是排查辅助, 而这一条关系到安全功能是否还在工作。
             * 而屏幕其余各行(包括下面的 STATUS: normal)全都正常 —— 属于
             * "功能已失效却看不出来"的失效, 这一行是运行期间唯一的可见线索
             * (自检屏只显示 3.5 秒)。判定依据见 mpu6050.h / docs/22 / docs/26。
             *
             * 三种异常按"读数还能不能信"排序:
             *   init_rc != 0 → 芯片压根没读到, 全部姿态数据无效;
             *   fatal        → 实际量程 ≤ 摔倒阈值, 冲击削顶, 判据物理上
             *                  不可能触发(软件补不回来, 只能报警);
             *   !cal_ok      → 上电时没静止, 自标定没做成, 换算退回了寄存器
             *                  读回值 —— 读数可能仍被放大。此时看 A 行末尾的
             *                  |a|: 不等于 1.0 就说明确实还不准(重启即可重标)。 */
            else if (MPU6050_GetSelfCheck()->init_rc != 0)
            {
                OLED_ShowStringPadded(0, 5, "! MPU6050 FAIL", 21);
            }
            else if (MPU6050_GetSelfCheck()->fatal)
            {
                OLED_ShowStringPadded(0, 5, "! FALL DET DISABLED", 21);
            }
            /* 用 IsScaleTrusted() 而不是 cal_ok: 上电标定失败但静默期在线标定
             * 已经修回来时, 读数其实是对的, 不该再挂这条告警 ——
             * 告警必须"只在真的有问题时出现", 否则就成了狼来了。 */
            else if (!MPU6050_IsScaleTrusted())
            {
                OLED_ShowStringPadded(0, 5, "! ACAL FAIL", 21);
            }
#endif
            /* 摔倒判据的瞬时态(2026-09-16 新增) —— 只在 FF/IMP/CFM 三个
             * 【瞬时】态出现, 正常时这一行仍归 PPG 调试/STATUS 用。
             *
             * 为什么必须有: 判据不报警时, 需要回答的唯一问题是"状态机推进到
             * 哪一步了"。而这三个状态在屏上原本一模一样(都是 STATUS: normal),
             * 只能靠"再扔一次试试"来猜。现在可以直接看出来。
             * LOCKOUT(报过警后的 10s 锁定期)刻意不显示 —— 那不是异常,
             * 显示它只会把下面几行盖住 10 秒。 */
            else if (FallDetect_GetState() == FALL_FREEFALL ||
                     FallDetect_GetState() == FALL_IMPACT   ||
                     FallDetect_GetState() == FALL_CONFIRM)
            {
                /* 复用 magb: 此刻 aline 已经格式化完毕, 不会再读它 */
                x10_to_str(magb, sizeof(magb),
                           (int16_t)(FallDetect_GetMagnitude() * 10.0f));
                snprintf(fallbuf, sizeof(fallbuf), "FALL:%s %s",
                         fall_state_tag(FallDetect_GetState()), magb);
                OLED_ShowStringPadded(0, 5, fallbuf, 21);
            }
            /* PPG 调试行(HR_PPG_DEBUG_LINE=1)已上移到本文件上方 ——
             * 现在排在 MPU6050 异常告警【之前】, 并带 "ADC SAT" 分支。
             * 原地留白是为了不再出现两份判据慢慢走偏(同 HR_IsSignalOk 的
             * 教训)。d/a/b/DRP 的完整判读表见 docs/38 第二节。 */
            else
            {
                OLED_ShowStringPadded(0, 5, "STATUS: normal", 21);
            }

            OLED_ShowStringPadded(0, 6, upline, 21);          /* 运行时长 */

            /* 第 8 页(page 7)专供联网诊断: 一行说清"卡在哪一层"。
             * 这是不接电脑也能看到病因的关键 —— 见 net_diag_line() 的注释。 */
            net_diag_line(line, sizeof(line));
            OLED_ShowStringPadded(0, 7, line, 21);

            OLED_RefreshPages(0xFF);                          /* 整屏刷新 */
            xSemaphoreGive(xMutexI2C);
        }
    }
}

/* ==========================================================================
 * 任务 5: WiFi_Task (优先级 1) —— 连接 WiFi + MQTT 上报
 * 周期 10s(UPLOAD_PERIOD_MS); 收到信号量则立即上报(摔倒 → 数据报文; SOS 按键 → 事件报文)。
 *
 * 【本任务的重连策略 —— 针对"连上了却断了又连、连了又断"的修复】
 *   要点一: 区分"偶发发送失败"与"链路真的断了"。
 *           ESP01S 一次 CIPSEND 没等到 "SEND OK", 很可能只是链路瞬时拥塞,
 *           连接还在。此前只要 publish 返回非 0 就立刻 CIPCLOSE + 重连,
 *           把一次抖动放大成一次完整断连 —— 这是"反复掉线"的主要放大器。
 *           现在先原地重试 NET_PUB_RETRY_MAX 次, 且只有驱动层明确报出
 *           链路不可用(ESP01S_GetState != TCP_CONNECTED)时才放弃重试。
 *   要点二: 退避重连。失败后从 NET_RECONNECT_MIN_MS 起翻倍, 封顶
 *           NET_RECONNECT_MAX_MS, 避免信号差时形成密集重连。
 *   要点三: 蜂鸣器降噪。连续失败达 NET_ALARM_AFTER_FAILS 次才鸣响,
 *           不再"每失败一次就响 5 秒"。
 *   要点四: 每轮都调 ESP01S_Poll() + ESP01S_RxWatchdog(), 既同步模块侧
 *           的断连状态, 也兜底恢复 USART1 的接收链路(见 esp01s.c)。
 *   要点五: SOS 报文绝不丢弃 —— 发布失败则挂起, 重连成功后优先补发。
 * ========================================================================== */
static char s_json[JSON_BUF_SIZE];      /* 静态区, 不占任务栈 */
static char s_topic[48];

static uint16_t build_data_json(char *buf, uint16_t len, const SystemData_t *s,
                                const char *ts, uint8_t ts_valid)
{
    char tbuf[8], hbuf[8], axb[8], ayb[8], azb[8], hrb[8], spb[8];
    const char *hr_s, *spo2_s, *temp_s, *humi_s;

    /* 无效数据一律输出 JSON 的 null, 而不是编造 0 —— 上位机可以据此区分
     * "真实为 0" 与 "无数据"。 */
    if (s->hr_valid)   { snprintf(hrb, sizeof(hrb), "%u", (unsigned)s->hr_bpm);        hr_s = hrb; }
    else                 hr_s = "null";
    if (s->spo2_valid) { snprintf(spb, sizeof(spb), "%u", (unsigned)s->spo2_percent);  spo2_s = spb; }
    else                 spo2_s = "null";
    if (s->temp_valid) { x10_to_str(tbuf, sizeof(tbuf), s->temp_x10);                  temp_s = tbuf; }
    else                 temp_s = "null";
    if (s->humi_valid) { x10_to_str(hbuf, sizeof(hbuf), s->humi_x10);                  humi_s = hbuf; }
    else                 humi_s = "null";

    x10_to_str(axb, sizeof(axb), (int16_t)(s->ax * 10.0f));
    x10_to_str(ayb, sizeof(ayb), (int16_t)(s->ay * 10.0f));
    x10_to_str(azb, sizeof(azb), (int16_t)(s->az * 10.0f));

    /* 说明: 为避免 microlib 浮点格式化(占 Flash 且行为受限), 所有小数都以
     *      "整数×10"方式手工拼成字符串。 */
#if NET_JSON_COMPACT
    /* ---------------- 紧凑报文(默认) ----------------
     * 【为什么不能用上面那套完整键名】
     *   实测(2026-09-14): 巴法云该主题的记录字段只有 191 字节 —— 191 字节能
     *   写入, 192 字节起就【不再写进记录】。而 MQTT 转发不受此限, 所以会出现
     *   "订阅者收得到、设备回环确认也成功、唯独控制台永远没有数据"这种最难查
     *   的现象。完整键名报文约 250 字节, 必然被丢。
     *   紧凑键名把报文压到 ~150 字节, 见 user_config.h 的字段映射表。 */
    {
        /* ---- 2026-09-22 追加: 用 "ts"/"tsv" 换 "d"/"a"/"b" ----
         * 【起因】排查"心率测 10 次 10 次都失败"时, 云端只有 hr=null + ps,
         *   而能分开病因的三个原始量 d(红外直流)/a(脉搏波幅度)/b(合格拍数)
         *   只写在 OLED 上 —— 偏偏那一行又被 "! MPU6050 FAIL" 占着,
         *   于是"没采到光 / 有光没搏动 / 峰检出了但间隔全被拒"在云端长得
         *   一模一样, 只能靠人念屏上的数字。
         * 【为什么必须"换"而不是直接加】巴法云该主题的记录上限是 191 字节
         *   (见本节开头), 而现有报文已到约 171 字节。d/a/b 最坏要 28 字节,
         *   直接加会到 199 —— 超限的表现是"订阅者收得到、控制台永远没数据",
         *   正是本节开头记的那个最难查的坑。
         * 【拿哪两个换】ts(设备本地时间 HH:MM:SS, 17 字节) 与 tsv(该校时标记,
         *   8 字节)。理由: 板子无 VBAT 电池, RTC 每次上电回到编译初值,
         *   这个时间本来就与真实时间差几小时到几天; 屏上早已按用户要求不显示
         *   它; 云端的接收时间才是权威值, 小程序也【明确规定】不用它判在线
         *   (见小程序 utils/bemfa.js 的"数据新鲜度"一节与 index.js 的注释)。
         *   换掉后约 174 字节, 留 17 字节余量给 up 随时间从 6 位涨到 8 位。
         * 【d 为什么用 Raw 版】HR_GetIrDc() 是 uint16_t(钳在 65535) ——
         *   电流提到 0x4F 后真实直流十几万, 钳位后报上来永远是 65535,
         *   看起来"正常且纹丝不动", 判断"是否顶到 ADC 满量程"会被带反。
         *   详见 heart_rate.c 的 HR_GetIrDcRaw()。 */
        (void)ts;          /* 参数保留: 调用点与 RTC 取值不动, 缩小改动面 */
        (void)ts_valid;

        uint16_t n;

        /* seq 必须保留 "seq":N, 这个带尾逗号的形式 —— 回环确认靠它定位。 */
        n = (uint16_t)snprintf(buf, len,
            "{"
            "\"seq\":%u,"
            "\"hr\":%s,"
            "\"spo2\":%s,"
            /* sc = spo2_cal: 0 = 未标定估算(屏上带 ~), 1 = 已标定。
             * 只给一个数字而不说它是怎么来的, 收数据的人无法判断可信度。 */
            "\"sc\":%u,"
            /* ps = ppg_state: 0 没手指 / 1 稳定中 / 2 采集中 / 3 测量完成。
             * 这四种状态里 hr 都是 null, 只有它能把它们分开。 */
            "\"ps\":%u,"
            /* d / a / b = PPG 三个原始量, 一起读才能分清四种"没数":
             *   d≈0              → 压根没采到光
             *   d 正常, a≈0      → 有光但没有脉搏波(贴合/压力不对)
             *   d,a 正常, b=0    → 峰检出了, 但每个间隔都被判据拒掉
             *   d,a 正常, b>=1   → 攒到了拍, 差在"够不够 3 拍/一致占比"
             * b 是窗口内【合格】IBI 拍数, 与屏上同一口径。 */
            "\"d\":%lu,"
            "\"a\":%lu,"
            "\"b\":%u,"
            /* cv = 当前生效门限的指纹(见 alarm_cfg.h)。
             * 【为什么每帧都带】它是"我下发的那套门限到底生效没有"的唯一
             * 判据: 小程序用同一套算法算出自己那份的指纹, 与这里回读的比 ——
             * 相等就是生效了, 不需要设备再发一条应答报文(那会占用云端
             * "最后一条消息", 把遥测记录顶掉)。代价只有约 11 字节。 */
            "\"cv\":%u,"
            "\"t\":%s,"
            "\"h\":%s,"
            "\"gas\":%u,"
            "\"ax\":%s,"
            "\"ay\":%s,"
            "\"az\":%s,"
            "\"st\":\"%s\","
            "\"up\":%lu"
            "}",
            (unsigned)s_net_seq,
            hr_s,
            spo2_s,
            (unsigned)s->spo2_cal,
            (unsigned)s->ppg_st,
            (unsigned long)HR_GetIrDcRaw(),
            (unsigned long)HR_GetPeakAmp(),
            (unsigned)HR_GetBeats(),
            (unsigned)AlarmCfg_CurrentCv(),
            temp_s,
            humi_s,
            (unsigned)s->gas_level,
            axb, ayb, azb,
            s->status_ascii,
            (unsigned long)s->uptime_ms);

        /* ---- 云端记录上限保护(2026-09-22 追加) ----
         * 巴法云该主题的【记录】上限是 191 字节: 超过就不写入(只转发),
         * 表现为"订阅者收得到、设备回环也成功、控制台永远没有数据"。
         *
         * 【为什么必须有这道保护 —— 最坏组合恰好与摔倒报警重合】
         *   典型报文约 165 字节, 安全; 但各字段同时拉到最长会到 190 左右:
         *     st="fall_detected"(21B) + up 涨到 8 位 + ax/ay/az 都是 ±16g 的
         *     6 字符 + gas 满量程 + 温湿度是 3 位数 —— 这几项同时成立的
         *     时刻, 正是【报警那一帧】。也就是说: 不加保护的话,
         *     最该上传的一条反而最可能被云端丢掉, 而现象只是"控制台没数据",
         *     极其难查(同类事故见 docs/18)。
         *   注: 这个问题不是本次改动引入的 —— 改前最坏 199B 同样超,
         *   只是这次因为 d/a/b 的加入才把长度认真算了一遍
         *   (算它的工具 bemfa_compact_payload_check.py 此前漏了 sc/ps/cv,
         *   报出的长度一直偏小, 已一并修正并加了 --dry 模式)。
         *
         * 【与 enforce_payload_limit() 的分工 —— 两处阈值必须一致】
         *   本文件后面还有一个 enforce_payload_limit(): 它同样按
         *   NET_PAYLOAD_MAX 判断, 但降级结果是 {"seq":N,"ovf":1} ——
         *   一条几乎没内容的报文。对普通帧无所谓, 而【报警帧恰好是最长的
         *   一帧】: 摔倒时 st="fall_detected" + 三轴加速度都是 6 字符满量程,
         *   一旦落到那个兜底, 上云的只是一个"发生过溢出"的标记,
         *   而摔倒状态本身反而没传上去。
         *   所以这里先做一次【有内容】的降级: 只丢温湿度/气体,
         *   保住 st + 三轴 + 心率/血氧 + d/a/b + cv + up。
         *   阈值两边都用 NET_PAYLOAD_MAX(180, 比云端实测的 191 再留 11B 余量),
         *   这样这里降完(约 152B)就不会再触发那个兜底。
         *   小程序对缺字段本来就容错(缺哪个显示 "--"), 降级报文照样能看。 */
        if (n > NET_PAYLOAD_MAX)
        {
            n = (uint16_t)snprintf(buf, len,
                "{"
                "\"seq\":%u,"
                "\"hr\":%s,"
                "\"spo2\":%s,"
                "\"sc\":%u,"
                "\"ps\":%u,"
                "\"d\":%lu,"
                "\"a\":%lu,"
                "\"b\":%u,"
                "\"cv\":%u,"
                "\"ax\":%s,"
                "\"ay\":%s,"
                "\"az\":%s,"
                "\"st\":\"%s\","
                "\"up\":%lu"
                "}",
                (unsigned)s_net_seq,
                hr_s,
                spo2_s,
                (unsigned)s->spo2_cal,
                (unsigned)s->ppg_st,
                (unsigned long)HR_GetIrDcRaw(),
                (unsigned long)HR_GetPeakAmp(),
                (unsigned)HR_GetBeats(),
                (unsigned)AlarmCfg_CurrentCv(),
                axb, ayb, azb,
                s->status_ascii,
                (unsigned long)s->uptime_ms);
        }

        return n;
    }
#else
    /* ---------------- 完整报文(仅本机上位机消费时使用) ----------------
     * 字段最全, 但约 250 字节, 超过巴法云的记录上限(191B) ——
     * 用在云端控制台上会【看不到数据】。 */
    return (uint16_t)snprintf(buf, len,
        "{"
        "\"timestamp\":\"%s\","
        /* seq: 报文自增序号。放在时间戳之后、且紧跟逗号 —— 这样固件发完就能
         * 用 "seq":N, 这个模式在接收缓冲里精确定位到"自己刚发的那一条",
         * 从而判定它是真的经过了 Broker(见 user_config.h 的"送达确认(回环)")。
         * 用带尾逗号的模式是为了避免 "seq":12 误匹配到 "seq":123。 */
        "\"seq\":%u,"
        "\"heart_rate\":%s,"
        "\"spo2\":%s,"
        "\"temp\":%s,"
        "\"humidity\":%s,"
        "\"gas_level\":%u,"
        "\"accel_x\":%s,"
        "\"accel_y\":%s,"
        "\"accel_z\":%s,"
        "\"status\":\"%s\","
        "\"timestamp_valid\":%u,"
        "\"uptime_ms\":%lu,"
        "\"hr_valid\":%u,"
        "\"spo2_valid\":%u,"
        "\"spo2_cal\":%u,"
        /* ppg_state: 0 没手指 / 1 稳定中 / 2 采集中(pending) / 3 测量完成。
         * 这四种状态里 hr 都是 null, 只有它能把它们分开(见 app_events.h)。 */
        "\"ppg_state\":%u,"
        "\"temp_valid\":%u,"
        "\"humi_valid\":%u,"
        "\"temp_src\":\"%s\""
        "}",
        ts,
        (unsigned)s_net_seq,
        hr_s,
        spo2_s,
        temp_s,
        humi_s,
        (unsigned)s->gas_level,
        axb, ayb, azb,
        s->status_ascii,
        (unsigned)ts_valid,
        (unsigned long)s->uptime_ms,
        (unsigned)s->hr_valid,
        (unsigned)s->spo2_valid,
        (unsigned)s->spo2_cal,
        (unsigned)s->ppg_st,
        (unsigned)s->temp_valid,
        (unsigned)s->humi_valid,
        (s->temp_src == 0) ? "DHT11" : "DS18B20");
#endif /* NET_JSON_COMPACT */
}

static uint16_t build_sos_json(char *buf, uint16_t len, const char *ts)
{
    /* 坐标来源: 硬件缺失(无 GPS/北斗模块) → 使用配置的固定工点坐标。
     * 该字段不是实时定位结果, 论文/答辩中必须如实说明。 */
    return (uint16_t)snprintf(buf, len,
        "{"
        "\"event\":\"SOS\","
        "\"seq\":%u,"                /* 与数据报文同一套回环确认机制 */
        "\"location\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"location_source\":\"configured_fixed\""
        "}",
        (unsigned)s_net_seq, SOS_LOCATION_STR, ts);
}

/* ==========================================================================
 * 报文长度兜底 —— 保证发出去的 payload 一定能被云端【记录】
 *
 * 【背景(实测, 2026-09-14)】
 *   巴法云该主题的最后一条消息记录字段只有 191 字节; 超过时 MQTT 转发照常,
 *   但消息【不写进记录】—— 订阅者收得到、设备回环确认也成功, 控制台上却
 *   永远没有数据。这是本项目里最难查的一种"看起来一切正常"的故障。
 *
 *   已经用紧凑报文把长度压下来了, 这里再加一道保险: 万一将来又加了字段把
 *   报文顶超上限, 就退化成一条最小报文 —— 宁可少报几个字段, 也不能整条丢。
 *   触发时调试串口会留一行 WARN, 便于发现并去精简字段。
 * ========================================================================== */
static uint16_t enforce_payload_limit(char *buf, uint16_t len)
{
    uint16_t n = (uint16_t)strlen(buf);

    if (n <= NET_PAYLOAD_MAX)
    {
        return n;
    }

    DBG("[NET] WARN payload %uB > %uB limit -> send minimal\r\n",
        (unsigned)n, (unsigned)NET_PAYLOAD_MAX);

    /* 最小报文仍必须含 "seq":N, 这个带尾逗号的形式,
     * 否则 net_confirm_echo() 就找不到自己刚发的那一条了。 */
    n = (uint16_t)snprintf(buf, len, "{\"seq\":%u,\"ovf\":1}",
                           (unsigned)s_net_seq);
    return n;
}

/* ==========================================================================
 * 建链 —— 返回 0 = 成功, 否则给出"卡在哪一步"的编号
 *   1 = AT 无应答          → 模块波特率/接线/供电
 *   2 = AT 初始化失败      → 模块异常
 *   3 = 连 AP 失败         → 热点未开/非 2.4GHz/密码错
 *   4 = TCP 建链失败       → 域名解析(DNS)或网络不通
 *   5 = MQTT 被拒或无应答  → 账号(私钥)/主题/链路被中间设备掐断
 *
 * 【为什么要把返回值分这么细, 还要打印 ESP01S_LastResp()】
 *   以前只打印一句 "connect failed step=N", 知道卡在哪一步却不知道为什么
 *   (模块明明回了一句 "FAIL"/"DNS Fail", 但没人看得见)。现在每一步失败都
 *   把"模块最后说了什么"和 CONNACK 返回码一起打出来 —— 这是远程排查时
 *   唯一能拿到的一手证据。
 * ========================================================================== */
static uint8_t net_connect(void)
{
    uint8_t rc;

    DBG("[NET] 1/6 AT check...\r\n");
    rc = ESP01S_ATCheck();
    if (rc != 0)
    {
        /* 同步到 OLED: "AT no reply"。
         * 走到这一条几乎必然是硬件层问题 —— 接线 / 波特率 / 供电 / EN 脚,
         * 与热点、与云端账号都无关。看到它就不要再折腾云端配置了。 */
        s_net_diag = NET_DIAG_AT_FAIL;
        DBG("[NET] AT check failed code=%u resp=%s\r\n",
            (unsigned)rc, ESP01S_LastResp());
        return 1;
    }

    DBG("[NET] 2/6 join AP...\r\n");
    if (ESP01S_EnsureAP(WIFI_SSID, WIFI_PASSWORD) != 0)
    {
        s_net_diag = NET_DIAG_WIFI_FAIL;
        DBG("[NET] join AP failed resp=%s\r\n", ESP01S_LastResp());
        return 3;
    }

    DBG("[NET] 3/6 tcp connect %s:%u...\r\n",
        MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT);
#if NET_USE_IP_FALLBACK
    /* 先域名, 失败自动改用 IP 重试(手机热点 DNS 不可用时这一步救命) */
    rc = ESP01S_TcpConnectAuto(MQTT_BROKER_HOST, MQTT_BROKER_PORT,
                               MQTT_BROKER_IP_FALLBACK);
#else
    rc = ESP01S_TcpConnect(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
#endif
    if (rc != 0)
    {
        s_net_diag = NET_DIAG_TCP_FAIL;
        DBG("[NET] tcp connect failed resp=%s\r\n", ESP01S_LastResp());
        return 4;
    }

    DBG("[NET] 4/6 mqtt connect...\r\n");
    rc = MQTT_Connect();
    if (rc != 0)
    {
        /* 这一步的信息量最大:
         *   rc=2 → Broker 明确拒绝, 返回码直接指出原因(私钥错/未授权/协议不符)
         *   rc=3 → 完全没收到 CONNACK(链路被掐断), 与账号无关, 查网络
         * 把返回码一并送到 OLED: "MQTT rc=05" 就是"未授权(私钥/主题)"。 */
        s_net_diag     = NET_DIAG_MQTT_FAIL;
        s_net_diag_arg = MQTT_GetLastConnackRc();
        DBG("[NET] mqtt connect failed rc=%u connack=0x%02X (%s)\r\n",
            (unsigned)rc,
            (unsigned)MQTT_GetLastConnackRc(),
            MQTT_ConnackRcText(MQTT_GetLastConnackRc()));
        ESP01S_TcpClose();
        return 5;
    }

    /* ---- 5/6 订阅主题 —— 【这一步决定控制台显示"在线"还是"离线"】----
     * 巴法云控制台的在线判据是"该主题上有没有订阅者", 不是"有没有在发数据"。
     * 只发 PUBLISH 的设备: 数据照样上云(pubOnline=true)、控制台也能看到最新
     * 消息, 但设备图标永远是灰的 —— 这正是本项目此前遇到的现象。
     *
     * 【订阅失败不判为断线】: 上报功能完全不受影响, 只是控制台显示离线。
     * 所以这里 return 0 继续运行, 只把失败记进 s_net_diag/arg 供 OLED 与
     * 日志显示。一旦这样处理, 用户就不会因为"图标是灰的"而去做无谓的重连。 */
#if MQTT_SUBSCRIBE_ENABLE
    DBG("[NET] 5/6 subscribe %s...\r\n", MQTT_TOPIC_DATA);
    if (MQTT_Subscribe(MQTT_TOPIC_DATA) != 0)
    {
        s_net_diag     = NET_DIAG_SUB_FAIL;
        s_net_diag_arg = MQTT_GetLastSubackRc();
        DBG("[NET] subscribe failed suback=0x%02X (upload still works)\r\n",
            (unsigned)s_net_diag_arg);
        return 0;
    }
    DBG("[NET] 6/6 connected + subscribed: console shows ONLINE\r\n");
#else
    DBG("[NET] 5/6 subscribe skipped (MQTT_SUBSCRIBE_ENABLE=0)\r\n");
    DBG("[NET] 6/6 connected (console will show OFFLINE)\r\n");
#endif

    s_net_diag = NET_DIAG_ONLINE;
    return 0;
}

/* 记录一次失败: 按阈值决定是否报警, 返回"本次应等待的重连延迟",
 * 同时把退避值就地翻倍(封顶 NET_RECONNECT_MAX_MS)。
 * 抽成函数是为了让三处失败分支(建链/发布/心跳)共用同一套退避逻辑,
 * 避免各写一遍时把翻倍或封顶写错。 */
static uint32_t net_backoff(uint8_t *fail_streak, uint32_t *delay_ms)
{
    uint32_t wait = *delay_ms;
    uint32_t next;

    (*fail_streak)++;

    /* urgent=0: 本任务自己就是上报方, 不需要(也不应该)唤醒自己去发报文 */
    if (NET_ALARM_ON_FAIL && *fail_streak >= NET_ALARM_AFTER_FAILS)
    {
        post_alarm(ALARM_WIFI_FAIL, 5000, 0);
    }

    next = (*delay_ms) * 2;
    if (next > NET_RECONNECT_MAX_MS) next = NET_RECONNECT_MAX_MS;
    *delay_ms = next;

    /* 同步到 OLED: "retry Ns"。
     * 这行是给用户看的"心跳": 屏幕上的秒数一直在变, 说明程序活着、只是在等;
     * 如果长时间停在同一句不动, 那才是真卡住了。 */
    s_net_diag     = NET_DIAG_RETRY;
    s_net_diag_arg = (uint8_t)((wait + 999UL) / 1000UL);

    return wait;
}

/* ==========================================================================
 * 回环确认 —— 见 user_config.h 的"送达确认(回环)"一节
 *
 * 报文里带了 "seq":N, (N = 本条报文的序号), 这里就在接收缓冲里找这串字面量:
 *   找到   → 这串字节确实经过了巴法云 Broker, 又按订阅关系回推给了本设备
 *            (本设备自己订阅了上报用的同一个裸主题) ⇒ 【真送达】;
 *   找不到 → AT+CIPSEND 的 "SEND OK" 只是 ESP01S 的本地成功, 云端并没有
 *            收到数据 ⇒ 【假成功】。
 *
 * 【为什么这是唯一可靠的判据】
 *   QoS0 没有 PUBACK, 所以除了"等自己发的报文回环回来"之外, 设备侧没有任何
 *   办法确认云端收到了。而屏幕上的 pub 计数只反映 AT 层的成功, 恰恰是
 *   "数据没上去却一直显示 OK"的根源。
 *
 * 返回 1 = 已确认送达; 0 = 超时未见回环。
 * ========================================================================== */
static uint8_t net_confirm_echo(uint16_t seq)
{
#if NET_ECHO_CONFIRM_ENABLE
    char needle[24];

    /* 带尾逗号: 避免 "seq":12 误匹配到 "seq":123 */
    snprintf(needle, sizeof(needle), "\"seq\":%u,", (unsigned)seq);

    /* ESP01S_WaitContains 内部边等边刷 AT 日志, 不做忙等 */
    return (ESP01S_WaitContains(needle, (uint16_t)strlen(needle),
                                NET_ECHO_TIMEOUT_MS) == 0) ? 1 : 0;
#else
    (void)seq;
    return 0;
#endif
}

/* ==========================================================================
 * 阈值配置下行 —— 从"主题名/set"的推送里取小程序下发的门限
 *
 * 【消息怎么走到这里】
 *   小程序 → HTTP 接口(apis.bemfa.com/va/sendMessage) → 发布到 主题名/set
 *         → 巴法云把消息推给【订阅了该主题的所有设备】(含本设备)
 *         → 报文落进 ESP01S 的 AT 接收环形缓冲
 *   设备侧不需要新开连接、不需要新主题、不需要额外的订阅: 用的就是上报时
 *   已经订阅的那条链路(见 user_config.h 第十节)。
 *
 * 【为什么在缓冲里"找标记"而不是解析 MQTT 报文】
 *   接收缓冲里混着 AT 应答、CONNACK、SUBACK、以及自己上报的回环报文。
 *   与其实现一个完整的 MQTT 解包器(还要处理分片/剩余长度), 不如就按
 *   下发报文的字面量标记 "cfg":1 定位 —— 它足够独特, 且所有解析都交给
 *   AlarmCfg_ApplyJson(它只认 9 个键值, 报文里多什么字段都不影响)。
 *
 * 【为什么处理完要把整个接收缓冲清掉 —— 这是一个必须说清的取舍】
 *   ESP01S_RxFind 找到的永远是【第一个】匹配。若把配置报文留在缓冲里:
 *     · 每 10 秒都会再命中一次同一条(靠指纹判等兜住, 不会重复应用);
 *     · 更要命的是【更新的那条配置永远排不到】—— 它在这条旧配置后面,
 *       而查找从头开始, 每次都命中旧的。表现是"改了门限怎么都不生效,
 *       要等约 70 秒旧报文被新数据挤出缓冲后才突然生效"。
 *   清掉之后的行为是可预测的: 一次处理一条, 处理完就消费掉。
 *   代价: 若此刻缓冲里正好有一条 "CLOSED"(模块报的断连), 会被一起丢掉,
 *   断连要等到下一轮上报失败才发现(最多晚 10 秒)。两害相权, 取可预测的那个。
 *
 * 【已知限制(如实写明)】同一次缓冲里若有两条配置(用户连点两次下发),
 *   只会应用先到的那条, 后到的那条被清掉。小程序侧因此做成"下发后回读
 *   设备指纹确认, 不匹配就提示重发" —— 而不是靠设备这边猜哪条更新。
 * ========================================================================== */
static void net_poll_config(void)
{
    char     js[CFG_JSON_MAX];
    uint16_t pos;
    uint16_t n;
    uint8_t  rc;

    if (!ESP01S_RxFind("\"cfg\":1", 7, &pos)) return;   /* 没有配置报文 */

    /* 从 "cfg" 那个引号开始, 逐字节抠到 '}' 为止。
     * 用 RxPeek 而不是直接读缓冲: 越过"有效数据尾部"时会返回 0, 自然停下。 */
    for (n = 0; n < (uint16_t)(CFG_JSON_MAX - 1); n++)
    {
        uint8_t ch;

        if (!ESP01S_RxPeek((uint16_t)(pos + n), &ch)) break;
        js[n] = (char)ch;
        if (ch == '}') { n++; break; }
    }
    js[n] = '\0';

    /* 没读到结尾的 '}' —— 报文可能还在陆续到达(或太长被截断)。
     * 此时【不清缓冲也不动配置】, 下一轮再看; 宁可晚一轮, 不要拿半条报文
     * 去改门限。 */
    if (n < 8 || js[n - 1] != '}') return;

    rc = AlarmCfg_ApplyJson(js);

    DBG("[CFG] rx %uB -> %s\r\n", (unsigned)n, AlarmCfg_RcText(rc));

    if (rc == CFG_RC_APPLIED || rc == CFG_RC_SAME || rc == CFG_RC_BAD)
    {
        /* 处理过就消费掉(理由见函数头注释) */
        ESP01S_RxClear();
    }

    if (rc == CFG_RC_APPLIED)
    {
        char line[96];

        AlarmCfg_FormatLine(line, sizeof(line));
        DBG("[CFG] applied -> %s\r\n", line);

        /* 【为什么只在"当前没有报警"时才投这条显示事件】
         * AlarmTask 收到新事件会【抢占】正在响的那一条(换掉 cur、重置计时)。
         * "配置更新成功"只是给用户的确认, 没有任何理由去打断一次摔倒报警的
         * 长鸣。这里读 s_snapshot.alarm 没拿互斥量: 读到的只可能是"刚刚"
         * 或"仍然"的值, 两种结果都只是显示层的一次取舍, 不影响任何判据。 */
        if (s_snapshot.alarm == ALARM_NONE)
        {
            post_alarm(ALARM_CFG_SET, 0, 0);   /* duration=0: 只显示不鸣响 */
        }
    }
}

static void WifiTask(void *argument)
{
    uint8_t  connected = 0;
    uint32_t ping_timer = 0;
    uint8_t  fail_reason = 0;
    uint8_t  fail_streak = 0;            /* 连续失败次数, 成功一次即清零 */
    uint8_t  echo_fail_streak = 0;       /* 连续"发送成功但无回环"次数(见 net_confirm_echo) */
    uint32_t reconnect_delay = NET_RECONNECT_MIN_MS;
    uint32_t wait_ms = 0;
    uint8_t  sos_pending = 0;            /* SOS 报文发送失败 → 挂起, 重连后补发 */
    uint8_t  is_sos = 0;
    uint8_t  attempt = 0;
    uint8_t  pub_ok = 0;
    uint8_t  pub_step = 0;
    ESP_Diag_t diag;
    static ESP_Diag_t last_diag;         /* 仅用于"计数变化才打印", 避免刷屏 */
    SystemData_t s;
    char ts[24];
    uint8_t ts_valid;

    (void)argument;

    for (;;)
    {
        /* ---------------- ① 建链 ---------------- */
        if (!connected)
        {
            fail_reason = net_connect();
            if (fail_reason != 0)
            {
                ESP01S_RxWatchdog();      /* 兜底: 确认 USART1 接收仍然挂着 */
                ESP01S_GetDiag(&diag);

                /* 一行里给出所有排查需要的数字:
                 *   step  = 卡在哪一步(见 net_connect 注释)
                 *   resets= 模块重启次数(>0 且持续增长 = 供电不足, 先修电源)
                 *   ovf/rstrt/err = 本机 USART1 链路健康度
                 * 这三个串口计数若在涨, 说明问题在 MCU 本地串口侧(接线/波特率/
                 * 电源噪声), 而不是远端网络 —— 排查时先看这一行。 */
                DBG("[NET] connect failed step=%u streak=%u resets=%lu ovf=%lu rstrt=%lu err=%lu wait=%lums\r\n",
                    (unsigned)fail_reason, (unsigned)fail_streak,
                    (unsigned long)diag.module_resets,
                    (unsigned long)diag.rx_overflow,
                    (unsigned long)diag.rx_restart,
                    (unsigned long)diag.err_events,
                    (unsigned long)wait_ms);

                /* 卡在 TCP 或 MQTT 这两步时, 把模块自身的网络状态整块打出来
                 * (IP / 当前 AP / 信号强度 / 域名解析结果)。一次性看全,
                 * 免得来回猜。 */
                if (fail_reason >= 4)
                {
                    ESP01S_DumpNetInfo(MQTT_BROKER_HOST);
                }

                wait_ms = net_backoff(&fail_streak, &reconnect_delay);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }

            connected = 1;
            fail_streak = 0;
            reconnect_delay = NET_RECONNECT_MIN_MS;
            ping_timer = HAL_GetTick();
            DBG("[NET] connected\r\n");
        }

        /* SOS 补发: 上一次没送出去的紧急事件优先于常规数据 */
        if (sos_pending)
        {
            s_net_seq++;                       /* 补发也是一条新报文, 序号照增 */
            RTC_Time_FormatISO8601(ts, sizeof(ts));
            (void)build_sos_json(s_json, sizeof(s_json), ts);
            (void)enforce_payload_limit(s_json, sizeof(s_json));

            if (MQTT_Publish(MQTT_TOPIC_EVENT, s_json, (uint16_t)strlen(s_json)) == 0)
            {
                sos_pending = 0;
                /* 补发同样要确认送达: 紧急事件"发出去"并不等于"到了云端" */
                if (net_confirm_echo(s_net_seq)) s_net_echo_count++;
                DBG("[NET] SOS re-sent ok\r\n");
            }
            else if (ESP01S_GetState() != ESP_STATE_TCP_CONNECTED)
            {
                wait_ms = net_backoff(&fail_streak, &reconnect_delay);
                ESP01S_TcpClose();
                connected = 0;
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }
            /* 链路仍在但发送失败: 不清 sos_pending, 下一轮继续补发 */
        }

        /* ---------------- ② 攒报文 ---------------- */
        is_sos = 0;

        /* 等 10s 上报周期(UPLOAD_PERIOD_MS); 若信号量提前到达则立即上报。
         * 信号量有两个来源, 靠 s_immediate_report 区分报文类型:
         *   置位 → 摔倒等 urgent 事件, 发"数据报文"(带传感器数值与状态)
         *   未置位 → SOS 按键, 发"SOS 事件报文"(只有时间与位置) */
        if (xSemaphoreTake(xSemSosUpload, pdMS_TO_TICKS(UPLOAD_PERIOD_MS)) == pdTRUE)
        {
            if (s_immediate_report)
            {
                s_immediate_report = 0;
            }
            else
            {
                is_sos = 1;
            }
        }

        /* 每轮都做两件事:
         *   ① ESP01S_Poll()       —— 同步模块侧上报的断连(CLOSED / WIFI DISCONNECT)
         *   ② ESP01S_RxWatchdog() —— 兜底恢复 USART1 接收链路(见 esp01s.c)
         * 放在这里而不是只在超时分支, 是为了让 SOS 抢发路径也受同样保护。 */
        ESP01S_Poll();
        ESP01S_RxWatchdog();

        if (ESP01S_GetState() != ESP_STATE_TCP_CONNECTED)
        {
            /* 链路已不可用: 直接重连, 不要拿着一条死连接去发 */
            DBG("[NET] link down (module side)\r\n");
            wait_ms = net_backoff(&fail_streak, &reconnect_delay);
            ESP01S_TcpClose();
            connected = 0;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }

        /* 顺手看一眼有没有小程序下发的阈值配置。
         * 【为什么放在这里】这一点上既没有正在等待的回环确认、也还没开始发
         * 本轮报文 —— 此刻清接收缓冲不会破坏任何正在进行中的交互(见
         * net_poll_config 的说明)。 */
        net_poll_config();

        /* 报文序号: 必须在 build_*_json 之前自增, 才能让报文里的 seq
         * 与稍后用来在接收缓冲里查找的那串字面量完全一致。 */
        s_net_seq++;
        if (is_sos)
        {
            RTC_Time_FormatISO8601(ts, sizeof(ts));
            (void)build_sos_json(s_json, sizeof(s_json), ts);
            (void)enforce_payload_limit(s_json, sizeof(s_json));
            strncpy(s_topic, MQTT_TOPIC_EVENT, sizeof(s_topic) - 1);
            DBG("[NET] SOS event publish\r\n");
        }
        else
        {
            if (xSemaphoreTake(xMutexSnapshot, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                s = s_snapshot;
                xSemaphoreGive(xMutexSnapshot);
            }
            else
            {
                memset(&s, 0, sizeof(s));
                strcpy(s.status_ascii, "unknown");
            }

            RTC_Time_FormatISO8601(ts, sizeof(ts));
            ts_valid = RTC_Time_IsValid();
            (void)build_data_json(s_json, sizeof(s_json), &s, ts, ts_valid);
            (void)enforce_payload_limit(s_json, sizeof(s_json));
            strncpy(s_topic, MQTT_TOPIC_DATA, sizeof(s_topic) - 1);
        }
        s_topic[sizeof(s_topic) - 1] = '\0';

        /* ---------------- ③ 发送(带原地重试) ----------------
         * 关键: 只有驱动层明确判定"链路不可用"时才停止重试并重连;
         * 单纯的超时(多因链路瞬时拥塞)先重试, 别把抖动当成断线。 */
        pub_ok = 0;
        pub_step = 0;
        for (attempt = 0; attempt <= NET_PUB_RETRY_MAX; attempt++)
        {
            pub_step = MQTT_Publish(s_topic, s_json, (uint16_t)strlen(s_json));
            if (pub_step == 0) { pub_ok = 1; break; }

            if (ESP01S_GetState() != ESP_STATE_TCP_CONNECTED) break;

            vTaskDelay(pdMS_TO_TICKS(NET_PUB_RETRY_DELAY_MS));
        }

        if (!pub_ok)
        {
            if (is_sos) sos_pending = 1;   /* 紧急事件绝不丢弃, 重连后补发 */
            DBG("[NET] publish failed step=%u tries=%u\r\n",
                (unsigned)pub_step, (unsigned)(attempt + 1));

            wait_ms = net_backoff(&fail_streak, &reconnect_delay);
            ESP01S_TcpClose();
            connected = 0;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }

        s_net_pub_count++;
        /* 【这里不能无条件覆盖】: 若本轮建链时订阅失败(NET_DIAG_SUB_FAIL),
         * 控制台会一直显示离线 —— 这条信息比"上报成功"更值得留在屏上。
         * 否则用户看到 "OK pub=N" 会以为一切正常, 却在控制台怎么也找不到
         * 设备上线, 又回到"数据到底有没有上去"的猜谜里。 */
        if (s_net_diag != NET_DIAG_SUB_FAIL)
        {
            s_net_diag = NET_DIAG_ONLINE;
        }
        /* 把长度也打出来: 超过 NET_PAYLOAD_MAX(180B) 的报文在巴法云
         * 【会被转发但不被记录】, 控制台看不到 —— 长度本身就是一条证据。 */
        DBG("[NET] published %uB: %s\r\n",
            (unsigned)strlen(s_json), s_json);

        /* ---------------- ③之二 送达确认(回环) ----------------
         * 上面这一步只说明 AT 层"发成功了", 不代表云端收到了。
         * 这里等自己刚发的报文回环回来, 才敢说数据真的上了云。
         *   e 计数正常增长      → 一切正常, 数据在云端
         *   p 在涨而 e 恒为 0   → 假成功; 屏幕上会显示 "NET: p=N NO ECHO",
         *                          连续 NET_ECHO_FAIL_MAX 次后自动强制重连
         *                          (这就是"被别的客户端顶掉"的自愈动作) */
        if (net_confirm_echo(s_net_seq))
        {
            s_net_echo_count++;
            echo_fail_streak = 0;
            DBG("[NET] echo OK seq=%u e=%u\r\n",
                (unsigned)s_net_seq, (unsigned)s_net_echo_count);
        }
        else
        {
            echo_fail_streak++;
            DBG("[NET] WARN no echo seq=%u (%u/%u) -> data may NOT have reached cloud\r\n",
                (unsigned)s_net_seq, (unsigned)echo_fail_streak,
                (unsigned)NET_ECHO_FAIL_MAX);

            if (echo_fail_streak >= NET_ECHO_FAIL_MAX)
            {
                /* 连续多次"发送成功却没有任何回环" = 这条连接实际已经死了
                 * (半开连接; 最典型是同一私钥被电脑上的调试客户端顶掉)。
                 * 模块不会主动报 CLOSED, 所以只能靠这里发现并断开重连。 */
                DBG("[NET] link is HALF-OPEN (no echo x%u) -> force reconnect\r\n",
                    (unsigned)echo_fail_streak);
                echo_fail_streak = 0;
                wait_ms = net_backoff(&fail_streak, &reconnect_delay);
                ESP01S_TcpClose();
                connected = 0;
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }
        }

        /* ---------------- ④ 心跳 ----------------
         * keepalive 为 MQTT_KEEPALIVE_S, 这里留一半余量发 PINGREQ。
         * 每 10s 的上报本身也是流量, 但显式心跳能避免长时间静默被
         * 中间设备(手机热点 NAT 等)当作空闲连接回收。 */
        if ((HAL_GetTick() - ping_timer) >= ((MQTT_KEEPALIVE_S * 1000UL) / 2))
        {
            ping_timer = HAL_GetTick();
            if (MQTT_Ping() != 0)
            {
                DBG("[NET] ping failed\r\n");
                wait_ms = net_backoff(&fail_streak, &reconnect_delay);
                ESP01S_TcpClose();
                connected = 0;
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
                continue;
            }
        }

        /* ---------------- ⑤ 链路诊断 ----------------
         * 这几个计数只要在增长, 就说明问题在本地侧而不是远端网络, 排查时
         * 先看这一行:
         *   ovf/rstrt/err 在涨 → MCU 串口侧(接线/波特率/电源噪声, 或临界区挤掉接收)
         *   resets 在涨        → ESP01S 在反复重启 → 供电不足(独立 3.3V + 大电容)
         * 仅在数值变化时打印, 避免刷屏。 */
        ESP01S_GetDiag(&diag);
        if (diag.err_events    != last_diag.err_events ||
            diag.rx_overflow   != last_diag.rx_overflow ||
            diag.rx_restart    != last_diag.rx_restart  ||
            diag.module_resets != last_diag.module_resets)
        {
            DBG("[NET] diag: ovf=%lu rstrt=%lu err=%lu resets=%lu\r\n",
                (unsigned long)diag.rx_overflow,
                (unsigned long)diag.rx_restart,
                (unsigned long)diag.err_events,
                (unsigned long)diag.module_resets);
            last_diag = diag;
        }
    }
}

/* ==========================================================================
 * 任务创建 —— 在调度器启动前调用(统一在此创建, 避免与 CubeMX 里配置的
 * 任务重复。CubeMX 的 FreeRTOS 配置中请不要再勾选创建这些任务。)
 * ========================================================================== */
void APP_TasksCreate(void)
{
    /* 报警门限先就位, 再创建任务 —— 否则 DataProcTask 可能先跑一轮,
     * 用未初始化的门限做判断(那是全 0, 会让"值 > 门限"恒为真)。
     * 【为什么放在这里而不是 main.c】本文件是唯一有 DBG 宏的地方, 而这一步
     * 需要把"出厂默认值越界"这件事报出来(见 alarm_cfg.c)。 */
    {
        char line[96];

        if (AlarmCfg_Init())
        {
            DBG("[CFG] WARN factory defaults out of range -> clamped\r\n");
        }
        AlarmCfg_FormatLine(line, sizeof(line));
        DBG("[CFG] boot %s\r\n", line);
    }
    /* 栈深度单位为"字"(STM32 上 1 字 = 4 字节): 128 字 = 512B。
     * 【关键】这些栈不是 Keil 启动文件里的 Stack_Size, 而是全部从
     *         FreeRTOS 堆(configTOTAL_HEAP_SIZE)里切出来的 ——
     *         调大任何一项, 都必须同步确认堆还够用。
     * 每个 xTaskCreate 都判返回值: 堆不够时它返回非 pdPASS 并留下 NULL 句柄,
     * 若不管, 调度器起来后会"少一个任务", 表现为某功能完全不工作, 反而更难查。 */
    if (xTaskCreate(AlarmTask,   "Alarm",   192, NULL, 3, &hAlarmTask)    != pdPASS) Error_Handler();  /* 最高 */
    /* 撞击监视: 同样给 3 —— 它是安全功能的采样源, 不能被显示/数据处理压后。
     * 栈 160 字: 本任务【不调用 DBG()】(理由见 ImpactTask 内注释), 栈上
     * 最深的一层只是 HAL_I2C_Mem_Read 的 14 字节缓冲, 给 160 字很宽裕。 */
    if (xTaskCreate(ImpactTask,  "Impact",  160, NULL, 3, &hImpactTask)   != pdPASS) Error_Handler();
    if (xTaskCreate(DataProcTask,"DataProc",256, NULL, 2, &hDataProcTask) != pdPASS) Error_Handler();
    if (xTaskCreate(SensorTask,  "Sensor",  256, NULL, 1, &hSensorTask)   != pdPASS) Error_Handler();
    if (xTaskCreate(WifiTask,    "WiFi",    384, NULL, 1, &hWifiTask)     != pdPASS) Error_Handler();
    if (xTaskCreate(DisplayTask, "Display", 256, NULL, 0, &hDisplayTask)  != pdPASS) Error_Handler();  /* 最低 */
}

/* ==========================================================================
 * 任务栈水位报告
 *
 * 【为什么需要它】
 *   任务栈到底给多少, 凭空估算并不可靠。uxTaskGetStackHighWaterMark() 返回
 *   该任务自启动以来"剩余栈的最小值"(单位: 字), 直接反映了它实际用掉多少。
 *   经验判据: 剩余建议不低于总栈深的 25%; 越接近 0 越危险。
 *
 * 【怎么用】
 *   烧录后打开串口助手连 USART2(PA2/PA3, 115200-8N1), 每 10 秒会看到一行:
 *     [STACK] Alarm=63w DataProc=110w Sensor=98w WiFi=180w Display=121w
 *   哪一路偏小, 就到 APP_TasksCreate() 里把对应任务的栈调大,
 *   同时确认 Core/Inc/FreeRTOSConfig.h 的 configTOTAL_HEAP_SIZE 仍有余量。
 *
 * 【一个容易忽略的点】
 *   AlarmTask 之所以从 128 字(512B)提到 192 字(768B): 它虽然是"只等队列、
 *   响蜂鸣器"的轻任务, 但 DBG() 宏会在它自己的栈上放 112 字节缓冲,
 *   再叠加 snprintf 的内部占用, 512B 就显得勉强了。
 * ========================================================================== */
void APP_ReportStackUsage(void)
{
    DBG("[STACK] Alarm=%uw Impact=%uw DataProc=%uw Sensor=%uw WiFi=%uw Display=%uw\r\n",
        (unsigned)uxTaskGetStackHighWaterMark(hAlarmTask),
        (unsigned)uxTaskGetStackHighWaterMark(hImpactTask),
        (unsigned)uxTaskGetStackHighWaterMark(hDataProcTask),
        (unsigned)uxTaskGetStackHighWaterMark(hSensorTask),
        (unsigned)uxTaskGetStackHighWaterMark(hWifiTask),
        (unsigned)uxTaskGetStackHighWaterMark(hDisplayTask));
}

/* ==========================================================================
 * 联网自检模式 (仅当 user_config.h 的 NET_SELF_TEST = 1 时编译进来)
 *
 * 用途: 联网出问题时, 把排查范围从"整个系统"缩到"只有网络"。
 *   - 不启动 Sensor/DataProc/Alarm/Display 四个任务, 排除它们的干扰
 *     (尤其是 DHT11/DS18B20 会周期性关闭中断, 对 ESP01S 接收有影响);
 *   - 反复执行 "AT → 连 AP → 建 TCP → MQTT CONNECT → 发一条测试报文";
 *   - AT 交互经日志钩子原样打到 USART2(钩子在 main.c 注册);
 *   - 失败时自动打印模块自身的网络状态(IP / 当前 AP / 信号 / 域名解析)。
 *
 * 这样拿到的串口日志是一条干净的"网络病历", 判读方法见
 * docs/08-巴法云连接诊断.md。排完问题把 NET_SELF_TEST 改回 0 即恢复完整功能。
 * ========================================================================== */
#if NET_SELF_TEST

/* 把模块最后一句应答搬到 OLED: 过滤掉 \r \n 等不可见字符, 否则屏上会花屏。
 * 只保留可打印 ASCII, 其余折叠成一个空格。 */
static void oled_safe_copy(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    if (n == 0) return;
    if (src != 0)
    {
        while (*src != '\0' && i < (n - 1))
        {
            char c = *src++;
            if (c >= 0x20 && c < 0x7F)              dst[i++] = c;
            else if (i > 0 && dst[i - 1] != ' ')    dst[i++] = ' ';
        }
    }
    dst[i] = '\0';
}

static void NetSelfTestTask(void *argument)
{
    uint8_t    r;
    uint8_t    pub;
    char       ts[24];
    char       ol[22];          /* OLED 一行最多 21 字符(128px / 6px), 留 1 字节结尾 */
    ESP_Diag_t d;

    (void)argument;

    DBG("\r\n===== NET SELF TEST =====\r\n");
    DBG("SSID=%s  broker=%s:%u  topic=%s\r\n",
        WIFI_SSID, MQTT_BROKER_HOST, (unsigned)MQTT_BROKER_PORT, BEMFA_TOPIC);
    DBG("\xe6\x9c\xac\xe6\xa8\xa1\xe5\xbc\x8f\xe5\x8f\xaa\xe8\xb7\x91\xe8\x81\x94\xe7\xbd\x91\xe6\xb5\x81\xe7\xa8\x8b; \xe6\x81\xa2\xe5\xa4\x8d\xe6\xad\xa3\xe5\xb8\xb8\xe5\x8a\x9f\xe8\x83\xbd\xe8\xaf\xb7\xe6\x8a\x8a NET_SELF_TEST \xe6\x94\xb9\xe5\x9b\x9e 0\r\n");

    for (;;)
    {
        ESP01S_RxWatchdog();

        r = net_connect();
        if (r != 0)
        {
            DBG("[TEST] connect FAILED step=%u resp=%s\r\n",
                (unsigned)r, ESP01S_LastResp());
            ESP01S_DumpNetInfo(MQTT_BROKER_HOST);
        }
        else
        {
            RTC_Time_FormatISO8601(ts, sizeof(ts));
            (void)snprintf(s_json, sizeof(s_json),
                           "{\"event\":\"selftest\",\"timestamp\":\"%s\"}", ts);
            (void)enforce_payload_limit(s_json, sizeof(s_json));

            pub = MQTT_Publish(MQTT_TOPIC_DATA, s_json, (uint16_t)strlen(s_json));
            if (pub == 0)
            {
                /* 发布成功却看不到数据 → 问题不在链路, 而在主题/控制台(见 docs/08) */
                DBG("[TEST] publish OK (\xe4\xba\x91\xe7\xab\xaf\xe8\x8b\xa5\xe6\xb2\xa1\xe6\x98\xbe\xe7\xa4\xba, \xe6\x9f\xa5\xe4\xb8\xbb\xe9\xa2\x98\xe6\x98\xaf\xe5\x90\xa6\xe5\xbb\xba\xe5\x9c\xa8 MQTT \xe6\x8e\xa7\xe5\x88\xb6\xe5\x8f\xb0)\r\n");
            }
            else
            {
                DBG("[TEST] publish FAILED code=%u resp=%s\r\n",
                    (unsigned)pub, ESP01S_LastResp());
            }
        }

        ESP01S_GetDiag(&d);
        DBG("[TEST] diag: resets=%lu ovf=%lu rstrt=%lu err=%lu\r\n",
            (unsigned long)d.module_resets,
            (unsigned long)d.rx_overflow,
            (unsigned long)d.rx_restart,
            (unsigned long)d.err_events);

        /* ---- 把这一轮的结果同步到 OLED ----
         * 屏上这几行就是一份现场病历, 不接电脑也能读:
         *   NET SELF TEST          标题
         *   NET: <一句话病因>       见 net_diag_line()
         *   step=N                 卡在第几步: 1=AT 3=热点 4=TCP 5=MQTT, 0=通过
         *   <模块最后一句应答>      它到底说了什么 —— 信息量最大的一行
         *   rst=n ovf=n            模块重启次数 / 本机串口接收溢出
         *   err=n                  串口错误事件数
         *
         * 自检模式只跑这一个任务, 独占 I2C 总线, 因此不需要 xMutexI2C
         * (那个互斥量在自检模式下根本没被创建)。 */
        OLED_Clear();
        OLED_ShowString(0, 0, "NET SELF TEST");

        net_diag_line(ol, sizeof(ol));
        OLED_ShowString(0, 1, ol);

        snprintf(ol, sizeof(ol), "step=%u", (unsigned)r);
        OLED_ShowString(0, 2, ol);

        oled_safe_copy(ol, sizeof(ol), ESP01S_LastResp());
        OLED_ShowString(0, 3, ol);

        snprintf(ol, sizeof(ol), "rst=%lu ovf=%lu",
                 (unsigned long)d.module_resets, (unsigned long)d.rx_overflow);
        OLED_ShowString(0, 4, ol);

        snprintf(ol, sizeof(ol), "err=%lu", (unsigned long)d.err_events);
        OLED_ShowString(0, 5, ol);

        /* 当前生效波特率。若被自适应改成过别的值(说明模块实际不是 115200),
         * 这一行会把真实值显示出来 —— 隐蔽病因一眼可见。 */
        snprintf(ol, sizeof(ol), "baud=%lu", (unsigned long)ESP01S_GetBaud());
        OLED_ShowString(0, 6, ol);

        OLED_Refresh();

        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}

void APP_NetSelfTestCreate(void)
{
    /* 栈 384 字: 与 WifiTask 同量级 —— net_connect() 内部有 96 字节的 AT
     * 命令缓冲, 日志打印还会在栈上放 112 字节, 给少了会踩栈。 */
    if (xTaskCreate(NetSelfTestTask, "NetTest", 384, NULL, 1, NULL) != pdPASS)
    {
        Error_Handler();
    }
}

#endif /* NET_SELF_TEST */
