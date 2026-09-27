#ifndef __ALARM_CFG_H
#define __ALARM_CFG_H

#include "stm32f1xx_hal.h"

/* =========================================================================
 * alarm_cfg.h —— 报警门限的运行时配置(可由微信小程序在线下发)
 *
 * 【要解决的问题】
 *   需求里的门限(气体>50 / 温度>40 或 <10 / 心率越界)全是编译期宏, 想改一个
 *   数就得重新编译烧录。而现场真正需要的是"现在就调": 演示时环境只有 26℃,
 *   40℃ 的高温门限永远不可能触发, 蜂鸣器也就永远演示不出来。
 *
 * 【数据从哪来】
 *   小程序 → HTTP 接口(apis.bemfa.com/va/sendMessage) → 主题名/set
 *         → 巴法云把消息推给【订阅了该主题的所有设备】(含本设备)
 *         → 设备在 ESP01S 的 AT 接收缓冲里解析出这条 JSON
 *   设备侧不需要新开连接、不需要新主题, 用的就是上报时已经订阅的那条链路。
 *   详见 user_config.h 第十节。
 *
 * 【为什么只放 RAM, 不写 Flash】
 *   写 Flash 要先擦一整页(1KB), 擦除期间 CPU 取指会被总线停住约 20~40ms。
 *   本工程有一个 5ms 周期的撞击监视任务(摔倒判据的采样源), 被停这么一下
 *   就会漏掉撞击窗口 —— 用"阈值掉电保存"去换"摔倒可能漏报"不划算。
 *   代价是掉电后回到 user_config.h 的出厂默认值。
 *   补偿办法: 小程序每次打开/下发后都会比对设备回读的 cv(见下), 不一致就
 *   提示重发 —— 设备侧被设计成"随时可以被纠正", 而不是"必须自己记住"。
 *
 * 【cv: 配置指纹 —— 让"到底生效没有"变成一个可核对的数字】
 *   9 个门限值按固定顺序(见 CFG_HASH_ORDER)用 FNV 变体算出一个 16 位指纹,
 *   随每一帧遥测上报("cv":N), 也打在调试串口上。
 *   小程序用同一套算法算出"我发出去的那套门限的指纹", 与设备回读的对比:
 *       相等   → 设备当前用的就是你设的这套(不需要额外应答报文)
 *       不等   → 还没生效(设备离线 / 下发被拒 / 报文没送到)
 *   算法刻意选得极简(逐字节 h = (h*33) ^ byte, 全程 16 位), 为的是两侧
 *   实现不会因为"浮点/字节序/字符串编码"这些理由算出不同的数。
 *   默认配置(user_config.h 的出厂值)的指纹是 38972(0x983C)。
 * ========================================================================= */

/* 一次下发报文的解析长度上限。典型报文约 98 字节, 给一倍余量。
 * 【为什么要设上限】报文是从环形缓冲里逐字节抠出来的, 万一 '}' 一直没出现
 * (缓冲里混进了别的东西), 不能一直抠下去把栈写爆。 */
#define CFG_JSON_MAX        192

/* ---- 门限量程(越界即【整条】拒绝, 见 alarm_cfg.c 的说明) ----
 * 这些边界同时被小程序侧复制了一份(utils/bemfa.js 的 T_LIMIT),
 * 目的不是"防止乱填", 而是让小程序在下发前就能拦下自己知道会被拒的值,
 * 否则用户看到的现象是"点了没反应"。两侧的数值由
 * tools/alarm_cfg_sim.py 与 tools/wxmp_thresh_test.js 交叉核对。 */
#define CFG_TEMP_MIN_X10    (-200)   /* -20.0°C */
#define CFG_TEMP_MAX_X10    600      /*  60.0°C */
#define CFG_HUMI_MIN_X10    0        /*   0.0 %RH */
#define CFG_HUMI_MAX_X10    1000     /* 100.0 %RH */
#define CFG_GAS_MIN         1        /* 0 会造成"只要有气体就报警", 故下限取 1 */
#define CFG_GAS_MAX         100
#define CFG_HR_MIN          20
#define CFG_HR_MAX          200
#define CFG_SPO2_MIN        50
#define CFG_SPO2_MAX        100

/* ---- 迟滞(回差)----
 * 【为什么必须有】门限附近的值一定在抖: 温度 25.0/25.1 来回跳, 气体 ADC
 * 本身就有几个字(LSB)的噪声。没有迟滞时, "报警 → 立刻恢复 → 下一轮又报警"
 * 会让蜂鸣器变成一串没完没了的啾啾声, 而现场其实什么都没变。
 * 规则(见 app_tasks.c 的 DataProcTask):
 *     值越过门限           → 置位, 开始报警
 *     值回落到 门限 - 迟滞  → 才解除
 * 迟滞只影响"解除", 不影响"触发" —— 触发永远按用户设的那个数。 */
#define ALARM_HYST_TEMP_X10   5      /* 0.5 °C  */
#define ALARM_HYST_HUMI_X10   20     /* 2.0 %RH */
#define ALARM_HYST_GAS        3
#define ALARM_HYST_HR         2      /* bpm */
#define ALARM_HYST_SPO2       1      /* % */

/* ---- 指纹覆盖的字段顺序(两侧都不能改) ----
 * tHi, tLo, hHi, hLo, gas, hrLo, hrHi, sp, buz —— 每个值按 int32 小端拆 4 字节 */
#define CFG_HASH_ORDER_DOC \
    "tHi,tLo,hHi,hLo,gas,hrLo,hrHi,sp,buz (int32 little-endian each)"

/* ---- 运行时生效的门限 ---- */
typedef struct
{
    int16_t  temp_hi_x10;    /* 高温门限, ×10(400 = 40.0°C) */
    int16_t  temp_lo_x10;    /* 低温门限, ×10 */
    int16_t  humi_hi_x10;    /* 高湿门限, ×10 */
    int16_t  humi_lo_x10;    /* 低湿门限, ×10 */
    uint16_t gas_level;      /* 气体门限, 0~100 相对量程 */
    uint16_t hr_lo;          /* 心率下限, bpm */
    uint16_t hr_hi;          /* 心率上限, bpm */
    uint16_t spo2_min;       /* 血氧下限, % */
    uint8_t  buz_enable;     /* 1 = 阈值报警鸣蜂鸣器; 0 = 只显示/只上报 */
    uint16_t cv;             /* 由上面 9 个值算出的指纹(不在指纹输入之内) */
} AlarmCfg_t;

/* ---- 下发报文的解析结果 ---- */
typedef enum
{
    CFG_RC_NONE = 0,     /* 报文里没有 "cfg":1 标记 → 不是配置报文, 忽略 */
    CFG_RC_APPLIED = 1,  /* 已应用 */
    CFG_RC_BAD = 2,      /* 报文或参数非法 → 整条拒绝, 原配置一字未动 */
    CFG_RC_SAME = 3      /* 与当前配置一致 → 不重复刷日志, 也不抖动 */
} AlarmCfgRc_t;

/* 初始化: 装载 user_config.h 里的出厂默认值并算出 cv。
 * 返回 1 = 出厂默认值本身越界, 已就地夹到量程内(调用方应打一行 WARN)。
 * 【为什么要把这件事报出来而不是自己打日志】DBG 宏只存在于 app_tasks.c,
 * 本模块不重复一套调试输出通道; 由调用方决定怎么记录。 */
uint8_t     AlarmCfg_Init(void);

/* 取一份快照。
 * 【为什么是"快照"而不是返回指针】一次判断要用到好几个门限(气体/温度/湿度/
 * 心率), 中间若被 WifiTask 换了配置, 就会出现"气体按新门限、温度按旧门限"
 * 的混合判断。调用方每个周期取一份, 整轮用同一份。 */
void        AlarmCfg_Snapshot(AlarmCfg_t *out);

uint16_t    AlarmCfg_Hash(const AlarmCfg_t *c);
/* 当前配置的指纹。单独给一个访问器是为了让上报路径不必拷整个结构体 ——
 * 16 位对齐读取在 Cortex-M3 上是原子的, 不会读到"半新半旧"的数。 */
uint16_t    AlarmCfg_CurrentCv(void);
uint8_t     AlarmCfg_IsDefault(void);
uint16_t    AlarmCfg_AppliedCount(void);
const char *AlarmCfg_RcText(uint8_t rc);

/* 解析并应用一条下发报文(必须是 NUL 结尾的字符串)。
 * 返回值见 AlarmCfgRc_t。纯函数式: 校验不过时一定会保持原配置。 */
uint8_t     AlarmCfg_ApplyJson(const char *json);

/* 把当前门限渲染成一行, 供调试串口打印(log 里要能一眼看出设备现在用的是哪套) */
void        AlarmCfg_FormatLine(char *out, size_t n);

#endif /* __ALARM_CFG_H */
