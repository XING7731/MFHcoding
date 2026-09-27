#ifndef __RTC_TIME_H
#define __RTC_TIME_H

#include "stm32f1xx_hal.h"

/* ============================ 时间戳的诚实说明 ============================
 * 需求要求 JSON 中包含 "timestamp": "2026-09-11T10:00:00Z"。
 * 但本硬件清单中没有 RTC 电池(VBAT)与任何校时源(GPS/NTP 均未提供):
 *   - 无 NTP: 本固件不假设 ESP-AT 固件支持 SNTP 校时, 因此不做联网对时;
 *   - 无 VBAT: 掉电后 RTC 归零, 上电需重新设置时间。
 * 因此本驱动:
 *   1) 用 RTC 生成 ISO8601 格式字符串, 上电时用 user_config.h 中的
 *      RTC_INIT_* 作为初值(相当于"最近一次已知时间");
 *   2) 同时上报 uptime_ms;
 *   3) 快照中带 time_valid 标志, 明确区分"可信时间"与"仅单调递增的计时"。
 * 如需真实时间, 请接 VBAT 电池 + 增加校时通道(本固件不假装已具备)。
 * ======================================================================== */

void     RTC_Time_Init(void);
void     RTC_Time_FormatISO8601(char *buf, uint16_t buf_len);  /* 需 ≥21 字节 */
uint8_t  RTC_Time_IsValid(void);   /* 0 = 掉电后未校准(仅上电初值) */

#endif /* __RTC_TIME_H */
