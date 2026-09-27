/* =========================================================================
 * alarm_cfg.c —— 报警门限的运行时配置(可由小程序在线下发)
 *
 * 设计说明与取舍见 alarm_cfg.h 的头部注释; 这里只写实现上的几个要点:
 *
 * ① 解析是"抠字段"而不是"完整 JSON 解析器"。
 *    设备侧只需要 9 个数字, 而报文里还会带 "m"(小程序自己的消息序号)之类
 *    与设备无关的字段。用 strstr 找带引号的键名 + 手写定点扫描, 代码不到
 *    60 行、不引入任何动态内存, 也不会因为报文里多一个字段就整条解析失败。
 *
 * ② 校验不过 = 整条拒绝, 绝不逐字段采用。
 *    门限之间有约束(高 > 低)。若逐字段采用, 一条只写了 tHi=25 的报文会与
 *    旧的 tLo=30 组合出"25 < 30" —— 报警判据当场变成永远为真。
 *    所以候选配置先在本地整体成型, 校验不过就整条丢掉。
 *
 * ③ 指纹(cv)在【候选通过校验之后】才算, 且输入不含 cv 自己。
 *    它是"两套门限是否相同"的唯一判据, 也是小程序"生效了没有"的答案。
 * ========================================================================= */

#include "alarm_cfg.h"
#include "user_config.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

/* 当前生效的门限。只有 WifiTask 会写(经 ApplyJson), 其余任务只经
 * AlarmCfg_Snapshot 读 —— 读的时候在临界区里整体拷一份。 */
static AlarmCfg_t s_cfg;
static uint16_t   s_def_cv     = 0;   /* 出厂默认配置的指纹(cv 相同即"就是默认值") */
static uint16_t   s_applied    = 0;   /* 成功下发的次数(诊断用) */

/* ---------------------------------------------------------------- 定点扫描 */

/* 在 s 里找 "key" : <数字>, 以 ×10 定点返回。
 *
 * 【为什么要带引号找键名】否则 "gas" 会命中 "gasx", "t" 会命中 "tHi"。
 * 【为什么自己抠数字而不用 strtof】microlib 下浮点输入会额外拉进一大块
 *   库代码; 而这 9 个字段的量纲是固定的(温度湿度 ×10, 其余整数), 手写
 *   整数扫描既省空间又不会引入浮点舍入 —— 两侧(固件/小程序)算出的定点值
 *   必须逐位一致, 否则指纹对不上。
 * 【小数位规则】最多取两位, 第二位【截断】不四舍五入: 28.55 → 285。
 *   截断而非舍入, 是为了让"小程序显示的门限"与"固件用的门限"永远一致:
 *   小程序自己也按 ×10 取整后下发, 两边不会各自舍入到不同的数。 */
static uint8_t cfg_find_x10(const char *s, const char *key, int32_t *out_x10)
{
    char        needle[12];
    const char *p;
    int32_t     v = 0, frac = 0;
    int32_t     sign = 1, digits = 0, fdigits = 0;

    if (s == 0 || key == 0 || out_x10 == 0) return 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(s, needle);
    if (p == 0) return 0;
    p += strlen(needle);

    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;                 /* 键名后面不是冒号 → 不是这个字段 */
    p++;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '-') { sign = -1; p++; }

    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; }

    if (*p == '.')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            if (fdigits < 2) { frac = frac * 10 + (*p - '0'); fdigits++; }
            p++;                             /* 多余的小数位直接跳过 */
        }
    }

    if (digits == 0 && fdigits == 0) return 0;      /* "abc" / "" → 视为没有 */

    if (fdigits == 0)      v = v * 10;              /* 整数写法 28 → 280 */
    else if (fdigits == 1) v = v * 10 + frac;       /* 28.5 → 285 */
    else                   v = v * 10 + (frac / 10);/* 28.55 → 285(截断) */

    if (v > 200000) v = 200000;                     /* 防溢出, 随后必被量程拒掉 */
    *out_x10 = sign * v;
    return 1;
}

/* 与上面同一套扫描, 但【不乘 10】—— 给气体/心率/血氧/开关这些整数字段用。
 *
 * 【为什么必须分开】两类字段量纲不同: 温湿度在报文与固件里都是 ×10 定点
 *   (40.0°C = 400), 而气体是 0~100 的整数、心率是 bpm 整数。若用同一个
 *   "×10"函数去读 gas, "gas":30 会被读成 300 → 每次都超量程 → 每一条下发
 *   都被拒, 而现场只能看到"怎么点都没反应"。
 *   (这个坑是 tools/alarm_cfg_sim.py 先踩到的, 已留成两侧的断言。) */
static uint8_t cfg_find_int(const char *s, const char *key, int32_t *out)
{
    int32_t x10;

    if (!cfg_find_x10(s, key, &x10)) return 0;
    *out = x10 / 10;          /* C 的整数除法向零截断, 与规格脚本一致 */
    return 1;
}

/* ---------------------------------------------------------------- 指纹 */

/* FNV 变体, 16 位: 逐值按 int32 小端拆 4 字节, h = (h*33) ^ byte。
 * 【逐字节而不是逐值】为了两端实现完全一致 —— 字节序是显式的, 不依赖任何
 *   编译选项; 而"把 9 个 int32 直接相加"这类做法会让 "tHi+1, tLo-1" 这种
 *   改动算出同一个指纹。 */
static uint16_t hash_step(uint16_t h, int32_t v)
{
    uint32_t x = (uint32_t)v;
    uint8_t  i;

    for (i = 0; i < 4; i++)
    {
        h = (uint16_t)(((uint32_t)h * 33u) ^ (uint32_t)(x & 0xFFu));
        x >>= 8;
    }
    return h;
}

uint16_t AlarmCfg_Hash(const AlarmCfg_t *c)
{
    uint16_t h = 5381u;

    if (c == 0) return 0;

    /* 顺序即规格: tHi,tLo,hHi,hLo,gas,hrLo,hrHi,sp,buz
     * (与 alarm_cfg.h 的 CFG_HASH_ORDER_DOC、Python 规格、JS 实现三处一致) */
    h = hash_step(h, (int32_t)c->temp_hi_x10);
    h = hash_step(h, (int32_t)c->temp_lo_x10);
    h = hash_step(h, (int32_t)c->humi_hi_x10);
    h = hash_step(h, (int32_t)c->humi_lo_x10);
    h = hash_step(h, (int32_t)c->gas_level);
    h = hash_step(h, (int32_t)c->hr_lo);
    h = hash_step(h, (int32_t)c->hr_hi);
    h = hash_step(h, (int32_t)c->spo2_min);
    h = hash_step(h, (int32_t)c->buz_enable);
    return h;
}

/* ---------------------------------------------------------------- 校验 */

#define CFG_IN_RANGE(v, lo, hi)  (((v) >= (lo)) && ((v) <= (hi)))

static uint8_t cfg_validate(const AlarmCfg_t *c, const char **why)
{
    /* 逐项量程 */
    if (!CFG_IN_RANGE(c->temp_hi_x10, CFG_TEMP_MIN_X10, CFG_TEMP_MAX_X10)) { *why = "tHi range";  return 0; }
    if (!CFG_IN_RANGE(c->temp_lo_x10, CFG_TEMP_MIN_X10, CFG_TEMP_MAX_X10)) { *why = "tLo range";  return 0; }
    if (!CFG_IN_RANGE(c->humi_hi_x10, CFG_HUMI_MIN_X10, CFG_HUMI_MAX_X10)) { *why = "hHi range";  return 0; }
    if (!CFG_IN_RANGE(c->humi_lo_x10, CFG_HUMI_MIN_X10, CFG_HUMI_MAX_X10)) { *why = "hLo range";  return 0; }
    if (!CFG_IN_RANGE(c->gas_level,   CFG_GAS_MIN,      CFG_GAS_MAX))      { *why = "gas range";  return 0; }
    if (!CFG_IN_RANGE(c->hr_lo,       CFG_HR_MIN,       CFG_HR_MAX))       { *why = "hrLo range"; return 0; }
    if (!CFG_IN_RANGE(c->hr_hi,       CFG_HR_MIN,       CFG_HR_MAX))       { *why = "hrHi range"; return 0; }
    if (!CFG_IN_RANGE(c->spo2_min,    CFG_SPO2_MIN,     CFG_SPO2_MAX))     { *why = "sp range";   return 0; }
    if (c->buz_enable > 1)                                                 { *why = "buz range";  return 0; }

    /* 【顺序约束 —— 这一组是"必须整条拒绝"的真正理由】
     * 高门限必须严格高于低门限: 相等或颠倒都会让判据同时"恒真"或"恒假"。 */
    if (c->temp_lo_x10 >= c->temp_hi_x10) { *why = "tLo>=tHi"; return 0; }
    if (c->humi_lo_x10 >= c->humi_hi_x10) { *why = "hLo>=hHi"; return 0; }
    if (c->hr_lo      >= c->hr_hi)        { *why = "hrLo>=hrHi"; return 0; }

    *why = "ok";
    return 1;
}

/* ---------------------------------------------------------------- 接口 */

uint8_t AlarmCfg_Init(void)
{
    const char *why = "";
    uint8_t     clamped = 0;
    AlarmCfg_t *c   = &s_cfg;

    c->temp_hi_x10 = (int16_t)TEMP_HIGH_X10;
    c->temp_lo_x10 = (int16_t)TEMP_LOW_X10;
    c->humi_hi_x10 = (int16_t)HUMI_HIGH_X10;
    c->humi_lo_x10 = (int16_t)HUMI_LOW_X10;
    c->gas_level   = (uint16_t)GAS_ALARM_LEVEL;
    c->hr_lo       = (uint16_t)HR_MIN_BPM;
    c->hr_hi       = (uint16_t)HR_MAX_BPM;
    c->spo2_min    = (uint16_t)SPO2_MIN_PERCENT;
    c->buz_enable  = 1;

    /* 【为什么默认值也要夹一次】出厂宏是手改的, 完全可能被改成越界值
     * (比如有人把 TEMP_HIGH_X10 写成 5000)。那种情况下 Validate 会对
     * 【任何】候选配置返回失败 —— 现象是"配置怎么都发不进去", 而日志里
     * 只会看到 range, 很难联想到是出厂宏的问题。夹一次 + 让调用方打一行
     * WARN, 把这个问题在启动时就暴露出来。 */
    if (!cfg_validate(c, &why))
    {
        clamped = 1;
        if (c->temp_hi_x10 < CFG_TEMP_MIN_X10) c->temp_hi_x10 = CFG_TEMP_MIN_X10;
        if (c->temp_hi_x10 > CFG_TEMP_MAX_X10) c->temp_hi_x10 = CFG_TEMP_MAX_X10;
        if (c->temp_lo_x10 < CFG_TEMP_MIN_X10) c->temp_lo_x10 = CFG_TEMP_MIN_X10;
        if (c->temp_lo_x10 > CFG_TEMP_MAX_X10) c->temp_lo_x10 = CFG_TEMP_MAX_X10;
        if (c->temp_lo_x10 >= c->temp_hi_x10)  c->temp_lo_x10 = (int16_t)(c->temp_hi_x10 - 10);
        if (c->humi_hi_x10 < CFG_HUMI_MIN_X10) c->humi_hi_x10 = CFG_HUMI_MIN_X10;
        if (c->humi_hi_x10 > CFG_HUMI_MAX_X10) c->humi_hi_x10 = CFG_HUMI_MAX_X10;
        if (c->humi_lo_x10 < CFG_HUMI_MIN_X10) c->humi_lo_x10 = CFG_HUMI_MIN_X10;
        if (c->humi_lo_x10 >= c->humi_hi_x10)  c->humi_lo_x10 = 0;
        if (c->gas_level < CFG_GAS_MIN)        c->gas_level   = CFG_GAS_MIN;
        if (c->gas_level > CFG_GAS_MAX)        c->gas_level   = CFG_GAS_MAX;
        if (c->hr_lo < CFG_HR_MIN)             c->hr_lo       = CFG_HR_MIN;
        if (c->hr_hi > CFG_HR_MAX)             c->hr_hi       = CFG_HR_MAX;
        if (c->hr_lo >= c->hr_hi)              c->hr_lo       = (uint16_t)(CFG_HR_MIN);
        if (c->spo2_min < CFG_SPO2_MIN)        c->spo2_min    = CFG_SPO2_MIN;
        if (c->spo2_min > CFG_SPO2_MAX)        c->spo2_min    = CFG_SPO2_MAX;
        if (c->buz_enable > 1)                 c->buz_enable  = 1;
    }

    c->cv     = AlarmCfg_Hash(c);
    s_def_cv  = c->cv;
    s_applied = 0;
    return clamped;
}

void AlarmCfg_Snapshot(AlarmCfg_t *out)
{
    if (out == 0) return;

    taskENTER_CRITICAL();
    *out = s_cfg;                     /* 结构体整体拷贝, 不会读到"半套"门限 */
    taskEXIT_CRITICAL();
}

uint16_t AlarmCfg_CurrentCv(void)
{
    return s_cfg.cv;      /* 16 位对齐读取: Cortex-M3 上原子, 无需临界区 */
}

uint8_t AlarmCfg_IsDefault(void)
{
    return (s_cfg.cv == s_def_cv) ? 1 : 0;
}

uint16_t AlarmCfg_AppliedCount(void)
{
    return s_applied;
}

const char *AlarmCfg_RcText(uint8_t rc)
{
    switch ((AlarmCfgRc_t)rc)
    {
    case CFG_RC_NONE:    return "not-a-config";
    case CFG_RC_APPLIED: return "applied";
    case CFG_RC_BAD:     return "rejected";
    case CFG_RC_SAME:    return "already-same";
    default:             return "?";
    }
}

uint8_t AlarmCfg_ApplyJson(const char *json)
{
    AlarmCfg_t  cand;
    const char *why = "";
    int32_t     v;
    uint8_t     got = 0;

    if (json == 0) return CFG_RC_NONE;

    /* 必须是配置报文: 靠一个字面量标记定位(与小程序、规格脚本三处一致)。
     * 上报报文、SOS 报文都会经过这里, 没有这个标记就立刻返回。 */
    if (strstr(json, "\"cfg\":1") == 0) return CFG_RC_NONE;

    /* 以【当前配置】为基底: 报文里没写的字段保持原值(便于"只调一个门限") */
    AlarmCfg_Snapshot(&cand);

    if (cfg_find_x10(json, "tHi", &v)) { cand.temp_hi_x10 = (int16_t)v; got++; }
    if (cfg_find_x10(json, "tLo", &v)) { cand.temp_lo_x10 = (int16_t)v; got++; }
    if (cfg_find_x10(json, "hHi", &v)) { cand.humi_hi_x10 = (int16_t)v; got++; }
    if (cfg_find_x10(json, "hLo", &v)) { cand.humi_lo_x10 = (int16_t)v; got++; }
    if (cfg_find_int(json, "gas",  &v)) { cand.gas_level   = (uint16_t)v; got++; }
    if (cfg_find_int(json, "hrLo", &v)) { cand.hr_lo       = (uint16_t)v; got++; }
    if (cfg_find_int(json, "hrHi", &v)) { cand.hr_hi       = (uint16_t)v; got++; }
    if (cfg_find_int(json, "sp",   &v)) { cand.spo2_min    = (uint16_t)v; got++; }
    if (cfg_find_int(json, "buz",  &v)) { cand.buz_enable  = (uint8_t)((v != 0) ? 1 : 0); got++; }

    /* 有标记却一个字段都没解析出来 → 报文坏了(例如被截断), 不能当"全默认"用 */
    if (got == 0) return CFG_RC_BAD;

    if (!cfg_validate(&cand, &why)) return CFG_RC_BAD;

    cand.cv = AlarmCfg_Hash(&cand);
    if (cand.cv == s_cfg.cv) return CFG_RC_SAME;    /* 同一个指纹: 不重复刷日志 */

    /* 提交(结构体整体替换, 见 Snapshot 的说明) */
    taskENTER_CRITICAL();
    s_cfg = cand;
    taskEXIT_CRITICAL();

    s_applied++;
    return CFG_RC_APPLIED;
}

void AlarmCfg_FormatLine(char *out, size_t n)
{
    AlarmCfg_t  c;
    char        a[10], b[10], d[10], e[10];

    if (out == 0 || n == 0) return;

    AlarmCfg_Snapshot(&c);

    /* ×10 定点手工渲染: 本工程不用 %f(见 app_tasks.c 的说明), 这里照旧 */
#define CFG_X10_STR(dst, val) do {                                   \
        int32_t _a = (val);  const char *_s = (_a < 0) ? "-" : "";    \
        if (_a < 0) _a = -_a;                                        \
        snprintf((dst), sizeof(dst), "%s%ld.%ld", _s,                \
                 (long)(_a / 10), (long)(_a % 10));                  \
    } while (0)

    CFG_X10_STR(a, c.temp_hi_x10);
    CFG_X10_STR(b, c.temp_lo_x10);
    CFG_X10_STR(d, c.humi_hi_x10);
    CFG_X10_STR(e, c.humi_lo_x10);
#undef CFG_X10_STR

    snprintf(out, n,
             "cv=%u %s t=%s/%s h=%s/%s gas=%u hr=%u-%u sp=%u buz=%u",
             (unsigned)c.cv,
             (c.cv == s_def_cv) ? "DEF" : "CUS",
             a, b, d, e,
             (unsigned)c.gas_level,
             (unsigned)c.hr_lo, (unsigned)c.hr_hi,
             (unsigned)c.spo2_min,
             (unsigned)c.buz_enable);
}
