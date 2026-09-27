/* =========================================================================
 * app_events.h —— 任务间通信的数据结构与事件定义
 * 所有跨任务传递的数据结构集中在此, 便于统一维护队列长度与内存占用。
 * ========================================================================= */
#ifndef __APP_EVENTS_H
#define __APP_EVENTS_H

#include "stm32f1xx_hal.h"

/* ---------------- 报警事件码 ----------------
 * 【新增事件一律追加在末尾】它们会被当作 s_last_alarm_tick[] 的下标
 * (app_tasks.c, 长度 16), 插在中间会改变已有事件的编号 —— 排查时对着一份
 * 旧日志就会读错。当前用到 12 个, 仍在 16 以内。
 *
 * 【2026-09-22】ALARM_LIGHT_LOW 已随光照功能一并删除。它原来夹在
 * ALARM_TEMP_LOW 与 ALARM_SOS 之间 —— 直接拿掉会让 SOS 之后的每个编号
 * 都往前挪一位, 与旧日志对不上。这里【保留一个占位名】把编号固定住,
 * 语义已废弃、代码里不再产生这个事件。 */
typedef enum
{
    ALARM_NONE = 0,
    ALARM_FALL,            /* 摔倒/剧烈撞击 (核心功能) */
    ALARM_GAS,             /* MQ-2 气体/烟雾超标 */
    ALARM_HR_ABNORMAL,     /* 心率越界 */
    ALARM_SPO2_LOW,        /* 血氧偏低 */
    ALARM_TEMP_HIGH,       /* 高温 */
    ALARM_TEMP_LOW,        /* 低温 */
    ALARM_RESERVED_LIGHT,  /* 已废弃: 原 ALARM_LIGHT_LOW(光照不足), 仅占位保编号 */
    ALARM_SOS,             /* SOS 按键 (中断触发) */
    ALARM_WIFI_FAIL,       /* WiFi/上报失败 */
    ALARM_SENSOR_FAULT,    /* 传感器读取失败(容错降级提示) */
    ALARM_HUMI_HIGH,       /* 湿度过高(门限可由小程序下发) */
    ALARM_HUMI_LOW,        /* 湿度过低 */
    ALARM_CFG_SET          /* 阈值配置已更新 —— 只显示不鸣响(见 app_tasks.c) */
} AlarmCode_t;

/* ---------------- 报警事件 (Data_Processing/ISR/WiFi → Alarm_Task) ---------------- */
typedef struct
{
    AlarmCode_t code;
    uint32_t    duration_ms;     /* 蜂鸣器响铃时长 */
    uint8_t     urgent;          /* 1 = 需立即上报(WiFi_Task 抢发) */
    char        text_ascii[16];  /* OLED 显示用 ASCII 文案 */
    const char *text_cn;         /* OLED 显示用中文文案(需字库, 指常量字符串) */
} AlarmEvent_t;

/* ---------------- 原始采样 (Sensor_Task → Data_Processing_Task) ----------------
 * PPG 数据以"批"为单位传递: MAX30102 以 PPG_SAMPLE_RATE_HZ 采样,
 * Sensor_Task 每 100ms 取一次 FIFO, 因此一批包含若干样本。
 * 心率/血氧的窗口缓存与算法状态放在 Data_Processing_Task 内部。
 * --------------------------------------------------------------------------- */
#define PPG_BATCH_MAX   8

typedef struct
{
    /* MPU6050 */
    float    ax, ay, az;                 /* 加速度, 单位 g */
    float    gx, gy, gz;                 /* 角速度, 单位 °/s (原始寄存器换算, 未做零偏标定) */

    /* MAX30102 PPG */
    uint16_t ppg_red[PPG_BATCH_MAX];
    uint16_t ppg_ir[PPG_BATCH_MAX];
    uint8_t  ppg_n;
    /* 取这一批之前, FIFO 因溢出丢掉的样本数(0 = 没丢)。
     * 必须一路传到心率算法(HR_NotifyDrop), 否则"丢样本"在算法眼里等于
     * "没发生", 时间轴被压缩 ⇒ IBI 偏短 ⇒ 心率整体偏高。 */
    uint8_t  ppg_lost;

    /* DHT11 (温度+湿度) */
    uint8_t  dht_t_int, dht_t_dec;
    uint8_t  dht_h_int, dht_h_dec;

    /* DS18B20 (温度, 0.1°C 定点) */
    int16_t  ds_temp_x10;

    /* 模拟量 */
    uint16_t gas_level;                  /* 0~100 相对量程百分数 */

    /* 有效性标志 */
    uint8_t  mpu_ok;
    uint8_t  ppg_ok;
    uint8_t  dht_ok;
    uint8_t  ds_ok;
    uint8_t  gas_ok;

    uint32_t uptime_ms;
} SensorRaw_t;

/* ---------------- 系统快照 (Data_Processing → Display_Task / WiFi_Task) ----------------
 * 以长度 1 的队列 + xQueueOverwrite 传递: 始终只保留最新一帧, 不占用额外内存。
 * ------------------------------------------------------------------------------- */
typedef struct
{
    /* 处理后的有效数据 */
    uint16_t hr_bpm;         /* 0 = 无效 */
    uint16_t spo2_percent;   /* 0 = 无效 */
    /* 血氧是否已标定: 0 = 未标定估算(仅演示用), 1 = 用自己拟合的系数算出。
     * 单独一个字段而不是只靠 spo2_valid —— 否则拿到数据的人无法分辨
     * "94%" 是标定结果还是经验式估算, 而那两者的可信度差得很远。 */
    uint8_t  spo2_cal;
    /* PPG 测量状态(HR_State_t 的取值): 0 没手指 / 1 稳定中 / 2 采集中 /
     * 3 测量完成。
     * 【为什么要上行】一次测量有 13 秒(3 秒稳定 + 10 秒采集)里 hr 是 null。
     * 不带这个字段的话, 收数据的人无法区分"没放手"、"正在测"、"测了但没
     * 测到" —— 这三种情况的 hr 都是 null, 而它们对应完全不同的处置。 */
    uint8_t  ppg_st;
    int16_t  temp_x10;       /* 0.1°C */
    int16_t  humi_x10;       /* 0.1%RH */
    uint16_t gas_level;      /* 0~100 */
    float    ax, ay, az;

    /* 数据来源/有效性 */
    uint8_t  temp_src;       /* 0 = DHT11, 1 = DS18B20(降级替代) */
    uint8_t  temp_valid;
    uint8_t  hr_valid;
    uint8_t  spo2_valid;
    uint8_t  humi_valid;

    /* 当前报警状态 (由 Alarm_Task 写入) */
    AlarmCode_t alarm;
    char        status_ascii[16];
    const char *status_cn;

    uint32_t uptime_ms;
} SystemData_t;

/* ---------------- JSON / MQTT 缓冲区长度 ----------------
 * 数据报文最坏情况(时间戳 20 字符 + 状态文案最长 11 字符 + uptime 10 位)
 * 约 320 字符, 这里给 448 留足余量; MQTT 缓冲需再容纳主题名与报文头。 */
#define JSON_BUF_SIZE   448
#define MQTT_BUF_SIZE   640

#endif /* __APP_EVENTS_H */
