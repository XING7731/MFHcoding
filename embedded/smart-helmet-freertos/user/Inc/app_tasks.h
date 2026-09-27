#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "app_events.h"

/* ============================================================================
 * 任务划分与优先级 (需求指定)
 * ----------------------------------------------------------------------------
 * 任务                     优先级  周期/触发方式         说明
 * Alarm_Task              3(最高) 阻塞等报警队列         蜂鸣器 + OLED 报警
 * Impact_Task             3        5ms 周期(200Hz)       摔倒判据专用采样(2026-09-16 新增)
 * Data_Processing_Task    2        阻塞等采样队列         心率/血氧/温度/气体 处理与阈值判断
 * Sensor_Task             1        100ms 周期             读全部传感器(供显示与上报)
 * Display_Task            0        500ms 周期             刷 OLED
 * WiFi_Task               1        10s 周期 + SOS 事件    组 JSON 并经 MQTT 上报
 *
 * ⚠ 为什么摔倒判据要单独占一个 200Hz 任务(2026-09-16):
 *   判据原先寄生在 Data_Processing_Task 上, 而它由 Sensor_Task 的 100ms 周期
 *   驱动 ⇒ 加速度实际只被采到 10Hz。一次撞击只持续 5~20ms, 采不到的概率
 *   超过 80%; 且判据条件2 要求"100ms 内两次超阈", 采样周期恰好等于窗口宽度,
 *   这条条件等于要求撞击持续满 100ms —— 真实撞击远达不到。
 *   ⇒ 结果是"怎么扔都不报警", 而屏幕上一切正常。
 *   修法: 判据的样本必须由"快任务"提供, 与"给显示用的慢周期"彻底分开。
 *      判据要"快", 显示要"稳", 两者本来就不该共用一个周期。
 *
 * ⚠ 两个任务同为优先级 3(Alarm / Impact):
 *   FreeRTOS 允许同优先级并存, 二者极少同时就绪(Alarm 大部分时间阻塞在
 *   队列上), 不会相互饿死。Impact 必须高于 DataProc/Sensor/Display,
 *   否则一次 OLED 刷新(30~40ms)就能吃掉好几个撞击采样点。
 *
 * 关于优先级 0:
 *   FreeRTOS 空闲任务(Idle)优先级也是 0。Display_Task 与其同级时,
 *   在 configUSE_TIME_SLICING = 1 下会时间片轮转; 由于 Display_Task 每次都
 *   调用 vTaskDelay(), 不会饿死空闲任务。若要求所有业务任务严格高于空闲
 *   任务, 请整体上移一级(Display=1, Sensor=2, DataProc=3, Alarm=4), 保持
 *   相对顺序不变。
 *   注意: configMAX_PRIORITIES 必须 > 3(CubeMX 默认 7, 满足)。
 *
 * 关于 WiFi_Task 优先级:
 *   需求未指定。此处取 1(与 Sensor_Task 同级)。原因: ESP01S 的 AT 交互
 *   包含数十毫秒的阻塞等待(如 AT+CWJAP 最长等 20s), 让它低于
 *   Data_Processing / Alarm 才能保住报警实时性。
 * ========================================================================== */

extern TaskHandle_t hAlarmTask, hDataProcTask, hSensorTask, hDisplayTask, hWifiTask;
extern TaskHandle_t hImpactTask;   /* 摔倒判据专用 200Hz 采样任务 */

/* ---- 任务间通信对象 ---- */
extern QueueHandle_t     xQueueSensorRaw;   /* Sensor_Task → Data_Processing_Task (长度 4) */
extern QueueHandle_t     xQueueAlarmEvent;  /* 任意任务/ISR → Alarm_Task (长度 8) */
extern SemaphoreHandle_t xSemSosUpload;     /* EXTI(ISR) → WiFi_Task: 立即上报 SOS */
extern SemaphoreHandle_t xMutexI2C;         /* I2C1 总线互斥: 传感器读取 vs OLED 刷新 */
extern SemaphoreHandle_t xMutexSnapshot;    /* 系统快照互斥(见 app_tasks.c) */

/* ---- 初始化与任务创建 ---- */
void APP_CommInit(void);        /* 创建队列/信号量(调度器启动前调用) */
void APP_TasksCreate(void);     /* 创建 5 个任务(调度器启动前调用) */

/* ---- 联网自检模式的任务(仅当 user_config.h 的 NET_SELF_TEST = 1 时使用) ----
 * 只创建这一个任务: 反复执行"连 AP → 建 TCP → MQTT CONNECT → 发一条测试
 * 报文", 并把整个 AT 交互经日志钩子打到 USART2。
 * 目的: 排查联网问题时把范围从"整个系统"缩到"只有网络", 避免传感器/OLED
 *       的干扰, 也避免其它任务的日志混在一起看不清。
 * 【注意】该任务不使用 xQueueSensorRaw / xMutexSnapshot 等通信对象,
 *         因此自检模式下 main.c 不调用 APP_CommInit()。 */
void APP_NetSelfTestCreate(void);

/* ---- 任务栈余量实测(调度器启动后调用) ----
 * 打印各任务自启动以来的最小剩余栈(单位: 字), 用于判断栈给得够不够。
 * 由 Display_Task 每 10 秒调用一次, 见 app_tasks.c 的 STACK_REPORT_ENABLE。 */
void APP_ReportStackUsage(void);

/* ---- 供中断调用的报警投递(内部使用 FreeRTOS FromISR API) ---- */
void APP_PostAlarmFromISR(AlarmCode_t code, uint32_t duration_ms, uint8_t urgent);

#endif /* __APP_TASKS_H */
