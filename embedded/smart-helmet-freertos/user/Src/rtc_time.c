/* =========================================================================
 * RTC 时间戳 (ISO8601 字符串生成)
 *
 * 见 rtc_time.h 的说明: 本硬件没有校时源与 RTC 备用电池,
 * 因此时间戳只能代表"上电初值 + 已运行时长", 属于近似时间。
 * 本模块把这一事实显式暴露给上层(time_valid 标志), 不做掩饰。
 * ========================================================================= */
#include "rtc_time.h"
#include "user_config.h"
#include <stdio.h>
#include <string.h>

extern RTC_HandleTypeDef hrtc;

/* 时间是否经过校准: 无校时通道 → 恒为 0。
 * 若后续增加了校时手段(如接 GPS 模块或上位机下发), 在此置 1。 */
static uint8_t s_time_valid = 0;

void RTC_Time_Init(void)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    /* 无 VBAT 电池: 上电后 RTC 时间不可信, 直接用配置初值设置。
     * 若你焊接了 VBAT 纽扣电池, 应改用后备寄存器标志位判断
     * "首次上电才设置", 避免每次重启都重置时间。 */
    /* 【易错点】STM32F1 用的是"老式 RTC"(RTC v1), 它的 RTC_TimeTypeDef
     * 只有 Hours / Minutes / Seconds 三个成员。
     * F4/F7/L4 系列才是 RTC v2, 多出 TimeFormat / DayLightSaving /
     * StoreOperation 三个字段。若照抄 F4 的例程, armcc 会报:
     *     #136: struct "<unnamed>" has no field "TimeFormat"
     *     #20:  identifier "RTC_HOURFORMAT12_AM" is undefined
     * 本文件此前就踩了这个坑, 现按 F1 的结构改写。 */
    t.Hours   = RTC_INIT_HOUR;
    t.Minutes = RTC_INIT_MIN;
    t.Seconds = RTC_INIT_SEC;
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

    d.WeekDay = RTC_WEEKDAY_MONDAY;
    d.Month   = RTC_INIT_MONTH;
    d.Date    = RTC_INIT_DAY;
    d.Year    = (uint8_t)(RTC_INIT_YEAR - 2000);   /* 年份字段为 00~99 */
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

    s_time_valid = 0;   /* 明确标记: 未经校准 */
}

/* 生成 "YYYY-MM-DDTHH:MM:SSZ" (UTC 约定, 本系统无时区处理) */
void RTC_Time_FormatISO8601(char *buf, uint16_t buf_len)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (buf == 0 || buf_len < 21) return;

    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);   /* 读日期前需先读时间 */

    snprintf(buf, buf_len, "20%02u-%02u-%02uT%02u:%02u:%02uZ",
             (unsigned)d.Year, (unsigned)d.Month, (unsigned)d.Date,
             (unsigned)t.Hours, (unsigned)t.Minutes, (unsigned)t.Seconds);
}

uint8_t RTC_Time_IsValid(void)
{
    return s_time_valid;
}
