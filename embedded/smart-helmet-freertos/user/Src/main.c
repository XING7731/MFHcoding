/* ============================================================================
 * main.c —— 智能工地安全帽边缘感知系统 (STM32F103C8T6 + FreeRTOS)
 *
 * 【本文件的用法】
 *   方式 A(推荐): 用 CubeMX 生成工程后, 把本文件的 USER CODE 区域内容
 *                 拷到生成的 main.c 对应区域, 并按 docs/02 的表格核对参数。
 *   方式 B: 直接使用本文件替换生成的 main.c —— 此时请确认:
 *           - 晶振频率(本文件按 8MHz HSE × 9 = 72MHz 配置)
 *           - FreeRTOSConfig.h 中的 configMAX_PRIORITIES ≥ 4
 *             (任务优先级用到 3, 见 app_tasks.h)
 *           - configTOTAL_HEAP_SIZE 足够(建议 ≥ 8KB, 见 docs/01 资源预算)
 *
 * 【初始化顺序】(对应交付物第 2 项)
 *   HAL_Init()
 *     → SystemClock_Config()            72MHz, 否则 TIM3 的 1us 基准不成立
 *     → MX_GPIO_Init()                  引脚方向/上拉/EXTI
 *     → MX_USART1_UART_Init()           ESP01S (115200, 开中断接收)
 *     → MX_USART2_UART_Init()           调试串口 (115200)
 *     → MX_I2C1_Init()                  400kHz, OLED/MAX30102
 *     → MX_I2C2_Init()                  400kHz, MPU6050(仅当 MPU6050_ON_I2C2=1)
 *     → MX_ADC1_Init()                  MQ2
 *     → MX_TIM3_Init()                  1MHz 计数 → delay_us()
 *     → MX_RTC_Init()                   时间戳(无校时源, 见 rtc_time.h)
 *     → bsp_delay_init(&htim3)          微秒延时可用
 *     → 通信对象创建 / 外设驱动初始化
 *     → 创建任务 → vTaskStartScheduler() 调度器启动后 main() 不再返回
 * ========================================================================== */
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include "user_config.h"
#include "app_tasks.h"
#include "app_events.h"
#include "bsp_delay.h"
#include "buzzer.h"
#include "esp01s.h"
#include "mqtt_client.h"
#include "rtc_time.h"
#include "oled_ssd1306.h"
#include "mpu6050.h"
#include "max30102.h"
#include "mq2.h"

#include <stdio.h>
#include <string.h>

void Error_Handler(void);

/* ============================ 外设句柄 ============================ */
UART_HandleTypeDef huart1;   /* ESP01S */
UART_HandleTypeDef huart2;   /* 调试 */
I2C_HandleTypeDef  hi2c1;
ADC_HandleTypeDef  hadc1;
TIM_HandleTypeDef  htim3;
RTC_HandleTypeDef  hrtc;

/* ============================ 姿态传感器走哪条 I2C ============================
 * 【为什么要有这一层间接】
 *   MPU6050 驱动只用构造时传进去的句柄(见 mpu6050.c 的 s_i2c), 所以"换总线"
 *   在软件上只是换一个参数。把"哪个句柄"收敛到这两个宏, 好处是:
 *     ① 换总线只改 user_config.h 的一个宏, 不用在 main() 里到处找 &hi2c1;
 *     ② 驱动、任务、自检屏的判据全部自动跟着走 —— 尤其是自检屏: 若换了总线
 *        却仍去旧总线找 0x68, 屏幕会一直显示 M:--, 让人误以为模块坏了、
 *        接线错了, 反而多绕一圈(这正是本项目反复踩过的坑)。
 * ========================================================================== */
#if MPU6050_ON_I2C2
I2C_HandleTypeDef  hi2c2;                       /* MPU6050 专用总线 */
#define MPU6050_HI2C       (&hi2c2)
#else
#define MPU6050_HI2C       (&hi2c1)             /* 与 OLED/MAX30102 共用 */
#endif

/* ============================================================================
 * 调试输出小工具
 *
 * 【为什么定义放在句柄声明之后】这两个函数都用 huart2, 而 huart2 是上面的
 * 文件级变量 —— C 语言要求先声明后使用, 放到前面会报
 *   #20: identifier "huart2" is undefined
 * ========================================================================== */
#if NET_SELF_TEST
static void boot_msg(const char *s)
{
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 300);
}
#endif

/* AT 日志钩子 —— 把 ESP01S 的 AT 收发原样打到调试串口(USART2, 115200)
 *
 * 注册后, esp01s.c 会把每条发出去的 AT 命令和每一行模块应答交给本函数,
 * 于是在串口助手里能看到完整的交互过程, 例如:
 *     AT+CWJAP="0001","****"
 *     WIFI CONNECTED
 *     WIFI GOT IP
 *     OK
 *     AT+CIPSTART="TCP","bemfa.com",9501
 *     DNS Fail
 *     ERROR
 * 一眼就能看出卡在哪一步、模块说了什么 —— 这是定位"连不上云端"最有效的
 * 手段, 比任何猜测都可靠。
 *
 * 【两点注意】
 *  1) 钩子只在任务上下文被调用(esp01s.c 的中断路径绝不打印), 所以这里可以
 *     用阻塞发送; 代价是每行占用约 1~3ms。嫌日志吵就把 user_config.h 的
 *     NET_AT_LOG_ENABLE 置 0 —— 它在编译期决定要不要注册本钩子。
 *  2) USART2 同时被 app_tasks.c 的 DBG() 使用, 两者都只在任务里发送;
 *     不同任务并发打印时两行可能交错, 这是可接受的(不丢数据, 只是排版乱)。
 * ========================================================================== */
#if NET_AT_LOG_ENABLE
static void net_at_log(const char *line)
{
    char b[128];
    int  n;

    n = snprintf(b, sizeof(b), "[AT] %s\r\n", line);
    if (n <= 0) return;
    if (n > (int)sizeof(b) - 1) n = (int)sizeof(b) - 1;

    (void)HAL_UART_Transmit(&huart2, (uint8_t *)b, (uint16_t)n, 300);
}
#endif /* NET_AT_LOG_ENABLE */

/* ---- 静态声明 ---- */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
#if MPU6050_ON_I2C2
static void MX_I2C2_Init(void);
#endif
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_RTC_Init(void);

/* ============================================================================
 * 上电 I2C 总线扫描 —— 回答"共用一对引脚, 到底谁在线"
 *
 * 【为什么需要这一步】
 *   OLED(0x3C) / MPU6050(0x68) / MAX30102(0x57) 三个器件都挂在
 *   PB6(SCL) + PB7(SDA) 上, 靠从机地址区分 —— 这是 I2C 的正常用法,
 *   不是"引脚重复/冲突": 总线同一时刻只和"被点名地址"的那一个器件通话。
 *
 *   但接线完成后, 肉眼无法确认三个器件是否都真的应答了。最常见的误判是:
 *        "屏幕亮着"  →  以为整条总线没问题
 *        →  其实心率芯片(0x57)根本没接上/没供电
 *        →  心率永远显示 "--", 又回头怀疑引脚冲突。
 *
 *   所以上电时扫一遍 0x08~0x77, 把"谁应答了"直接写到屏幕上。整机不需要
 *   USB-TTL, 复位后看头几秒的屏幕即可 —— 这是本轮排障要解决的问题。
 * ========================================================================== */
#if !NET_SELF_TEST

/* 调试串口输出(USART2, 115200) */
static void dbg_line(const char *s)
{
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 300);
}

#define I2C_SCAN_HIT_MAX  8
static uint8_t s_hit1[I2C_SCAN_HIT_MAX];   /* I2C1 上应答的 7bit 地址表 */
static uint8_t s_hit1_n = 0;
#if MPU6050_ON_I2C2
static uint8_t s_hit2[I2C_SCAN_HIT_MAX];   /* I2C2 上应答的 7bit 地址表 */
static uint8_t s_hit2_n = 0;
#endif

static uint8_t hit_has(const uint8_t *tbl, uint8_t cnt, uint8_t addr7)
{
    uint8_t i;
    uint8_t n = (cnt < I2C_SCAN_HIT_MAX) ? cnt : I2C_SCAN_HIT_MAX;

    for (i = 0; i < n; i++)
    {
        if (tbl[i] == addr7) return 1;
    }
    return 0;
}

/* I2C1 上有没有这个地址 —— OLED(0x3C) 与 MAX30102(0x57) 的判据 */
static uint8_t i2c_hit_has(uint8_t addr7) { return hit_has(s_hit1, s_hit1_n, addr7); }

#if MPU6050_ON_I2C2
/* I2C2 上有没有这个地址 —— 心率/姿态分开总线后, MPU6050(0x68) 的判据。
 * 注意: 判据必须跟着 MPU6050_ON_I2C2 走。若换了总线却仍去旧总线找 0x68,
 * 屏幕会一直显示 M:--, 让人误以为模块坏了或线接错了 —— 那正是本项目
 * 反复踩过的"误判来源"。 */
static uint8_t mpu_hit(uint8_t addr7) { return hit_has(s_hit2, s_hit2_n, addr7); }
#else
static uint8_t mpu_hit(uint8_t addr7) { return hit_has(s_hit1, s_hit1_n, addr7); }
#endif

/* 扫一条总线。逐个地址寻址, 应答(ACK)的被记下来; 不应答的每个约几毫秒
 * 超时, 整轮通常 < 400ms, 只在上电时做一次, 不影响运行。 */
static void scan_bus(I2C_HandleTypeDef *bus, uint8_t *tbl, uint8_t *cnt_out)
{
    uint8_t a;
    uint8_t n = 0;

    for (a = 0x08; a <= 0x77; a++)
    {
        if (HAL_I2C_IsDeviceReady(bus, (uint16_t)(a << 1), 1, 5) == HAL_OK)
        {
            if (n < I2C_SCAN_HIT_MAX) tbl[n] = a;
            n++;
        }
    }
    *cnt_out = n;
}

static void scan_log(const char *name, const uint8_t *tbl, uint8_t cnt)
{
    char    log[80];
    uint8_t i;
    uint8_t n = (cnt < I2C_SCAN_HIT_MAX) ? cnt : I2C_SCAN_HIT_MAX;
    int     k;

    k = snprintf(log, sizeof(log), "[BOOT] %s found %u:", name, (unsigned)cnt);
    for (i = 0; i < n && k < (int)sizeof(log) - 4; i++)
    {
        k += snprintf(log + k, sizeof(log) - (size_t)k, " %02X", tbl[i]);
    }
    (void)snprintf(log + k, sizeof(log) - (size_t)k, "\r\n");
    dbg_line(log);
}

static void I2C_BusScan(void)
{
    dbg_line("[BOOT] I2C bus scan\r\n");

    scan_bus(&hi2c1, s_hit1, &s_hit1_n);
    scan_log("I2C1", s_hit1, s_hit1_n);

#if MPU6050_ON_I2C2
    scan_bus(&hi2c2, s_hit2, &s_hit2_n);
    scan_log("I2C2", s_hit2, s_hit2_n);
#endif
}

/* 把扫描结果写屏。说清: 每条总线扫到了哪些地址、各自对应哪对引脚、
 * 三个预期器件是否都在线。
 *
 * 【为什么把引脚也写出来】"把姿态传感器换到另一条总线"这种做法一旦硬件
 * 和软件不同步(线挪了固件没改, 或相反), 表现就是"某个器件读不到" ——
 * 而屏幕上一串十六进制地址是看不出问题在哪的。把 PB6/7 与 PB10/11 直接
 * 标在对应地址前面, 一眼就能和手上的接线对上。 */
static void I2C_ScanShowOnOled(void)
{
    char    l[24];
    uint8_t i;
    uint8_t n;
    int     k;

    OLED_Clear();
    OLED_ShowString(0, 0, "SMART HELMET");

    /* 第 1 行: I2C1(PB6/PB7) 上应答的地址 —— OLED 与 MAX30102 应在这里 */
    n = (s_hit1_n < I2C_SCAN_HIT_MAX) ? s_hit1_n : I2C_SCAN_HIT_MAX;
    k = snprintf(l, sizeof(l), "I2C1 PB6/7:");
    for (i = 0; i < n && k < (int)sizeof(l) - 4; i++)
    {
        k += snprintf(l + k, sizeof(l) - (size_t)k, " %02X", s_hit1[i]);
    }
    OLED_ShowString(0, 1, l);

    /* 第 2 行: I2C2(PB10/PB11) —— MPU6050 应在这里。
     * "没启用这条总线"与"启用了但一个器件都没扫到"必须分开写, 否则
     * 看到空白无法判断是配置问题还是接线问题。 */
#if MPU6050_ON_I2C2
    n = (s_hit2_n < I2C_SCAN_HIT_MAX) ? s_hit2_n : I2C_SCAN_HIT_MAX;
    if (n == 0)
    {
        (void)snprintf(l, sizeof(l), "I2C2 PB10/11: none");
    }
    else
    {
        k = snprintf(l, sizeof(l), "I2C2 PB10/11:");
        for (i = 0; i < n && k < (int)sizeof(l) - 4; i++)
        {
            k += snprintf(l + k, sizeof(l) - (size_t)k, " %02X", s_hit2[i]);
        }
    }
    OLED_ShowString(0, 2, l);
#else
    OLED_ShowString(0, 2, "I2C2: off");
#endif

    /* 第 3 行: O = OLED, M = MPU6050, X = MAX30102(心率)。
     * M 的判据跟随 MPU6050_ON_I2C2, 见 mpu_hit()。 */
    (void)snprintf(l, sizeof(l), "O:%s M:%s X:%s",
                   i2c_hit_has(OLED_I2C_ADDR7)    ? "OK" : "--",
                   mpu_hit(MPU6050_I2C_ADDR7)     ? "OK" : "--",
                   i2c_hit_has(MAX30102_I2C_ADDR7)? "OK" : "--");
    OLED_ShowString(0, 3, l);

    OLED_Refresh();
}

#endif /* !NET_SELF_TEST */

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    /* ---- 1) HAL 与时钟 ---- */
    HAL_Init();
    SystemClock_Config();
    /* 注意: TIM3 的 1us 基准依赖 72MHz 系统时钟; 若时钟配置不对,
     *       DHT11/DS18B20 的时序会整体错位。 */

    /* ---- 2) 外设初始化 ---- */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();
#if MPU6050_ON_I2C2
    MX_I2C2_Init();                  /* 姿态传感器专用总线, 见 user_config.h */
#endif
    MX_ADC1_Init();
    MX_TIM3_Init();
    MX_RTC_Init();

    /* ---- 3) 软件基础模块 ---- */
    bsp_delay_init(&htim3);          /* delay_us() 就绪 */
    Buzzer_Init();                   /* 上电静音 */
    ESP01S_Init(&huart1);            /* 启动 UART 中断接收 */

#if NET_AT_LOG_ENABLE
    /* 把 ESP01S 的 AT 收发原样导到调试串口。
     * 这是排查"连不上云端"的第一手证据, 调试阶段建议保持开启。 */
    ESP01S_SetLogHook(net_at_log);
#endif

    RTC_Time_Init();
    MQTT_Init();                     /* 仅初始化报文缓冲(见 mqtt_client.c) */

#if NET_SELF_TEST
    /* ======================================================================
     * 联网自检模式 (user_config.h 的 NET_SELF_TEST = 1)
     *
     * 只跑联网流程, 完全不碰传感器/OLED/报警/显示 —— 这样得到的串口日志
     * 是一条干净的"网络病历", 不会被别的任务的输出淹没, 也排除了
     * DHT11/DS18B20 周期性关中断对 ESP01S 接收的干扰。
     * 详细判读方法见 docs/08-巴法云连接诊断.md。
     * 排完问题把 NET_SELF_TEST 改回 0 即恢复完整功能。
     * ==================================================================== */
    boot_msg("\r\n[BOOT] NET_SELF_TEST = 1 (\xe4\xbb\x85\xe8\x81\x94\xe7\xbd\x91\xe8\x87\xaa\xe6\xa3\x80)\r\n");

    /* 【为什么自检模式也要点屏】
     * 串口日志虽然详细, 但看它需要一根 USB-TTL 线。而 OLED 是板子上现成的
     * 显示通道 —— 把"卡在哪一步 / 模块最后说了什么"直接写在屏幕上,
     * 手里没有线材也能读出病因。这是本次排障最关键的一处改动。 */
    if (OLED_Init(&hi2c1) == 0)
    {
        OLED_Clear();
        OLED_ShowString(0, 0, "NET SELF TEST");
        OLED_ShowString(0, 2, "starting...");
        OLED_Refresh();
    }

    APP_NetSelfTestCreate();
#else
    MQ2_Init(&hadc1);

    /* ---- 上电 I2C 自检屏 ----
     * 把两条总线各扫一遍并把结果写屏: 谁在线、挂在哪对引脚上, 一眼可见。
     * 默认 MPU6050 走 I2C2(PB10/PB11), OLED 与 MAX30102 留在 I2C1(PB6/PB7)
     * (由 user_config.h 的 MPU6050_ON_I2C2 决定, 置 0 则三者共用 I2C1)。
     * 屏幕停留约 3.5s 后交还给正常显示任务; 期间不启动调度器, 不影响运行。
     *
     * 注意: 这里仍只做"开跑前必须完成"的初始化。周期性的传感器探测依旧放在
     *       Sensor_Task 里, 避免上电时长时间阻塞。 */
    {
        uint8_t oled_ok;
        uint8_t max_rc;
        char    l[28];      /* 屏上每行用(21 字符足够, 留余量) */
        char    m[64];      /* 串口日志用(整句较长, 与 l 分开避免互相覆盖) */
        const MPU6050_SelfCheck_t *msc;
        const char *mark;

        oled_ok = (OLED_Init(&hi2c1) == 0) ? 1 : 0;

        if (oled_ok)
        {
            OLED_Clear();
            OLED_ShowString(0, 0, "SMART HELMET");
            OLED_ShowString(0, 2, "scanning I2C...");
            OLED_Refresh();
        }

        I2C_BusScan();               /* 两条总线都扫, 结果也会打到 USART2 */

        (void)MPU6050_Init(MPU6050_HI2C);
        /* 拿到的是"量程写回读验 + 静止自标定"的全部结论, 见 mpu6050.h。
         *
         * 【上电后约 1 秒内不要碰开发板】MPU6050_Init() 内部要做静止自标定
         * (单轮 190ms, 失败最多重试 4 轮)。此时板子必须静止 —— 标定靠的是
         * "静止时 |a| ≡ 1g"这条物理定律, 动着的样本算出的均值没有意义。
         * 标定失败会回退到寄存器换算(即旧行为), 屏上显示 ACAL FAIL。
         *
         * 【但失败不再是死局(2026-09-16 起)】静默期在线标定会在运行期间
         * 自动修回来: 把帽子放稳一两秒, 灵敏度就会被重新推出来, 无需重启。
         * 实测反馈的"无论怎么放都是 3.9", 就是上电时被拿在手里导致本轮
         * 标定失败 —— 有了在线标定, 这种情形会自愈, A 行 |a| 会自动回到 1.0。 */
        msc = MPU6050_GetSelfCheck();
        max_rc = MAX30102_Init(&hi2c1);

        /* 心率芯片的结论单独一行 —— 这一条最能省掉"心率一直是 -- 到底坏没坏"
         * 的来回猜测: 器件不在总线上 / 在但 ID 不对 / 一切正常, 三者分开写。 */
        if (!i2c_hit_has(MAX30102_I2C_ADDR7))
            (void)snprintf(l, sizeof(l), "MAX30102 NOT ON BUS");
        else if (max_rc != 0)
            (void)snprintf(l, sizeof(l), "MAX30102 ID ERR %u", (unsigned)max_rc);
        else
            (void)snprintf(l, sizeof(l), "MAX30102 OK");

        dbg_line("[BOOT] ");
        dbg_line(l);
        dbg_line("\r\n");

        /* ==================================================================
         * MPU6050 量程自检 —— 把"写进去的值"和"读回来的值"并排打出来
         *
         * 【为什么这一段必须存在】
         *   姿态传感器的量程决定了加速度量程上限, 而摔倒判据是
         *   "|a| > 2.5g"。若量程没写进去(克隆片常见), 芯片停在 ±2g:
         *     ① 读数被整体放大 4 倍 → 静止时显示约 4g 而不是 1.0g;
         *     ② 物理上限只有 2g, 2.5g 的判据永远不可能满足。
         *   两种后果都很隐蔽: 屏幕上没有任何异常字样, 功能却已经没了。
         *   光看代码无法判断量程有没有生效 —— 只有读回寄存器才知道。
         *   完整分析与处理分支见 docs/22-姿态量程写回读验.md。
         * ================================================================ */
        /* 串口日志统一用 m, 不动 l —— l 里还存着上面那行 MAX30102 结论,
         * 稍后要写到屏幕第 5 行(page 4)。 */
        if (msc->init_rc != 0)
        {
            (void)snprintf(m, sizeof(m), "[BOOT] MPU6050 init FAIL rc=%u\r\n",
                           (unsigned)msc->init_rc);
            dbg_line(m);
        }
        else
        {
            (void)snprintf(m, sizeof(m), "[BOOT] MPU WHO=0x%02X 1C wr=0x%02X rd=0x%02X retry=%u\r\n",
                           (unsigned)msc->who_am_i, (unsigned)msc->accel_cfg_wr,
                           (unsigned)msc->accel_cfg_rd, (unsigned)msc->retry);
            dbg_line(m);

            /* ---- 静止自标定结果: "读数可不可信"的最终依据 ----
             * 它完全不看寄存器 —— 即使上一行显示 1C rd=0x10(芯片声称 ±8g),
             * 这里也会用"静止时 |a| ≡ 1g"这条物理定律如实报出【实际】档位。
             * 这是克隆片唯一骗不过的一关: 它能伪造寄存器的值, 伪造不了物理。 */
            if (msc->cal_ok)
            {
                (void)snprintf(m, sizeof(m),
                               "[BOOT] accel self-cal OK: %u LSB/g (= %ug), "
                               "spread %u%%, round %u\r\n",
                               (unsigned)(msc->cal_lsb + 0.5f),
                               (unsigned)msc->fs_cal,
                               (unsigned)(msc->cal_spread_pct + 0.5f),
                               (unsigned)msc->cal_retry);
                dbg_line(m);

                if (msc->fs_cal != msc->fs_want)
                {
                    /* 这才是"克隆片"的铁证: 寄存器声称 8g, 物理实测 2g。
                     * 读数已按物理值换算, 所以 A 行静止时是 1.0 而非 3.9。 */
                    (void)snprintf(m, sizeof(m),
                                   "[BOOT] !! register claims %ug but physics says %ug "
                                   "-> keeping the physical scale\r\n",
                                   (unsigned)msc->fs_got, (unsigned)msc->fs_cal);
                    dbg_line(m);
                    (void)snprintf(m, sizeof(m),
                                   "[BOOT] -> actual FS is +-%ug; keep "
                                   "FALL_ACCEL_HIGH_G strictly below it (user_config.h)\r\n",
                                   (unsigned)msc->fs_cal);
                    dbg_line(m);
                }
            }
            else
            {
                (void)snprintf(m, sizeof(m),
                               "[BOOT] accel self-cal FAILED (spread %u%%), "
                               "fallback to register scale %u LSB/g\r\n",
                               (unsigned)(msc->cal_spread_pct + 0.5f),
                               (unsigned)(msc->reg_lsb + 0.5f));
                dbg_line(m);
                dbg_line("[BOOT] !! keep the helmet still at power-on, then reboot\r\n");
                dbg_line("[BOOT] !! readings may still be scaled; check |a| on OLED row A\r\n");
            }

            /* 结论以"解出的量程"为准(而非寄存器字节是否逐位相等):
             * 寄存器里 AFS_SEL 以外的位(HPF 等)有差异并不影响量程,
             * 那种情况下不该让人去改 MPU6050_ACCEL_FS_G —— 改了也没用。
             * 原始字节差异在上一行 "1C wr=.. rd=.." 里已经如实打出来了。 */
            if (msc->fs_got == msc->fs_want)
            {
                (void)snprintf(m, sizeof(m), "[BOOT] accel FS want %ug == got %ug, scale OK\r\n",
                               (unsigned)msc->fs_want, (unsigned)msc->fs_got);
                dbg_line(m);
            }
            else
            {
                /* 读回值与写入值不符: 档位没生效。
                 * 注意这时读数并不一定是错的 —— 驱动已按【读回值】重新
                 * 选了换算系数, 所以 g 值仍然可信, 只是"和配置想的不一样"。 */
                (void)snprintf(m, sizeof(m), "[BOOT] accel FS MISMATCH want %ug got %ug\r\n",
                               (unsigned)msc->fs_want, (unsigned)msc->fs_got);
                dbg_line(m);
                (void)snprintf(m, sizeof(m), "[BOOT] scale re-derived from readback -> set FS_G=%u\r\n",
                               (unsigned)msc->fs_got);
                dbg_line(m);
            }

            if (msc->fatal)
            {
                /* 量程 ≤ 摔倒阈值: 冲击被削顶, 判据在物理上无法触发。
                 * 软件补不回来, 只能明确告诉人去改哪三个宏。
                 *
                 * 注意阈值这里给的是 2.5/1.5 而不是当前配置的 5.0/4.5:
                 * 能走到这个分支说明读回值确实变低了(芯片真的换了档),
                 * 驱动已按读回值换了换算系数 → 读数回到【真实 g】, 这时
                 * 阈值就该用需求原值; 继续留 5.0/4.5 反而会因为物理量程
                 * 只有 2~4g 而永远达不到。 */
                dbg_line("[BOOT] !! actual FS <= fall threshold: fall detect CANNOT fire\r\n");
                dbg_line("[BOOT] !! fix: MPU6050_ACCEL_FS_G=2, "
                         "FALL_ACCEL_HIGH_G=2.5, FALL_ACCEL_LOW_G=1.5\r\n");
            }
        }

        if (oled_ok)
        {
            I2C_ScanShowOnOled();
            OLED_ShowString(0, 4, l);   /* MAX30102 结论(上面的串口日志用的 m, 未覆盖 l) */

            /* 第 5-7 页给 MPU6050 量程自检。l 从这一行起才被复用。
             * w = 写入 ACCEL_CONFIG 的值, r = 读回的值 —— 寄存器级证据,
             * 没有中间推断, 便于和串口日志对照。 */
            if (msc->init_rc != 0)
            {
                (void)snprintf(l, sizeof(l), "MPU6050 err rc=%u", (unsigned)msc->init_rc);
                OLED_ShowString(0, 5, l);
            }
            else
            {
                /* 行 5: 寄存器层证据 —— 写进去什么、读回来什么, 不做任何推断。
                 * 这一行是留给人对照数据手册用的原始事实。 */
                (void)snprintf(l, sizeof(l), "MPU 1C w%02X r%02X",
                               (unsigned)msc->accel_cfg_wr, (unsigned)msc->accel_cfg_rd);
                OLED_ShowString(0, 5, l);

                /* 行 6: 量程结论 —— 用【实测反推值 fs_used】, 不是寄存器声称值。
                 * "MPU FS 8g->2g !!" 读作: 配置要 8g, 实测只有 2g, 且致命。
                 * 克隆片能把寄存器读回也伪造成 8g, 只有物理实测是真的 ——
                 * 所以这一行左半是"想要什么", 右半是"物理上实际是什么"。
                 * (旧版这两处都用寄存器值, 于是克隆片在屏上全绿, 见 docs/25。) */
                mark = (msc->fs_used == msc->fs_want) ? "OK" : (msc->fatal ? "!!" : "!");
                (void)snprintf(l, sizeof(l), "MPU FS %ug->%ug %s",
                               (unsigned)msc->fs_want, (unsigned)msc->fs_used, mark);
                OLED_ShowString(0, 6, l);

                /* 行 7: 自标定详情 —— 支撑上一行结论的原始数据, 现场可自查。
                 *   "ACAL 16384 2g s3%" → 实测 16384 LSB/g, 即 ±2g 档, 静置抖动 3%
                 *   "ACAL FAIL s23%"    → 上电时没静止(抖动 23%), 已退回寄存器换算
                 * 判读: 只要这行是 OK 的, A 行静止时的 |a| 就应该是 1.0; 若 A 行
                 * 仍显示 3.9, 说明标定虽成功但读数换算另有问题, 要查代码而非传感器。 */
                if (msc->cal_ok)
                {
                    (void)snprintf(l, sizeof(l), "ACAL %u %ug s%u%%",
                                   (unsigned)(msc->cal_lsb + 0.5f),
                                   (unsigned)msc->fs_cal,
                                   (unsigned)(msc->cal_spread_pct + 0.5f));
                }
                else if (msc->cal_spread_pct >= 1.0f)
                {
                    (void)snprintf(l, sizeof(l), "ACAL FAIL s%u%%",
                                   (unsigned)(msc->cal_spread_pct + 0.5f));
                }
                else
                {
                    /* 连有效样本都没凑够(I2C 全程读失败), 抖动值无意义, 不显示 */
                    (void)snprintf(l, sizeof(l), "ACAL FAIL");
                }
                OLED_ShowString(0, 7, l);
            }

            OLED_Refresh();
            HAL_Delay(4500);         /* 留出抄读时间(比旧版多 1 秒: 多了一行自标定数据) */
            OLED_Clear();
            OLED_Refresh();
        }
    }

    /* ---- 4) 任务与通信对象 ---- */
    APP_CommInit();
    APP_TasksCreate();
#endif /* NET_SELF_TEST */

    /* ---- 5) 启动调度器 (不再返回) ----
     * 用 vTaskStartScheduler() 而不是 osKernelStart(), 这样不依赖 CMSIS-RTOS
     * 包装层, 工程里启用/不启用 wrapper 都能编译。 */
    vTaskStartScheduler();

    while (1) { }
}

/* ============================================================================
 * 时钟配置: HSE 8MHz → PLL ×9 → 72MHz
 *
 * 【RTC 时钟源的自动降级 —— 这一条直接关系到板子上电能不能跑起来】
 *   理想情况: 用 LSE(外接 32.768kHz 晶振)驱动 RTC, 走时准。
 *   现实情况: 常见的 Blue Pill / 最小系统板【只焊了 8MHz 晶振, 没有 32.768kHz】。
 *   如果代码无条件要求 RCC_LSE_ON, HAL_RCC_OscConfig() 会因等不到 LSE 就绪
 *   而返回 HAL_ERROR, 于是 Error_Handler() 被调用 → 蜂鸣器长鸣、程序卡死,
 *   表现就是"上电没反应", 而且很难查。
 *
 *   因此这里做两级尝试:
 *     ① 先试 HSE + LSE; 成功 → RTC 用 LSE(精度高)
 *     ② 失败(LSE 起不来) → 关掉 LSE, 只配 HSE+PLL; RTC 改用 LSI(内部约 40kHz)
 *   注意 LSI 精度差(±百分之几十), 只适合生成"近似时间戳"; 本固件同时
 *   上报 uptime_ms 字段, 便于上位机判断时间是否可信(见 docs/03)。
 * ========================================================================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    uint32_t rtc_clock_source;

    /* ---- ① HSE 8MHz × 9 = 72MHz, 同时尝试启动 LSE ---- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) == HAL_OK)
    {
        rtc_clock_source = RCC_RTCCLKSOURCE_LSE;      /* 板上有 32.768kHz 晶振 */
    }
    else
    {
        /* ---- ② 板上没有 32.768kHz 晶振: 关掉 LSE 重来, RTC 改用 LSI ---- */
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
        if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        {
            Error_Handler();                          /* 连 HSE 都不行 → 真故障 */
        }
        rtc_clock_source = RCC_RTCCLKSOURCE_LSI;      /* 精度较差, 仅作近似时间戳 */
    }

    /* ---- AHB/APB 分频: SYSCLK 72MHz, APB1 36MHz, APB2 72MHz ---- */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* APB1 = 36MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* APB2 = 72MHz */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }

    /* ---- 把上面选定的 RTC 时钟源应用下去 ---- */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInit.RTCClockSelection = rtc_clock_source;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ============================ GPIO ============================ */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 上电默认电平: 蜂鸣器静音, 单总线空闲为高 */
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN,
                      BUZZER_ACTIVE_HIGH ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);

    /* PB5 蜂鸣器: 推挽输出 */
    GPIO_InitStruct.Pin = BUZZER_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &GPIO_InitStruct);

    /* PB12 DS18B20: 开漏输出(外部 4.7K 上拉), 驱动内部不切换方向 */
    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);

    /* PA1 DHT11: 先配为上拉输入(读取时驱动内部会切换到推挽输出) */
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);

    /* PA4 SOS 按键: 上拉输入 + 下降沿中断
     * 接法: 按键一端接 PA4, 另一端接 GND。平时内部上拉把 PA4 拉高,
     *       按下时接地 → 产生下降沿 → 触发 EXTI4 中断。
     * (另一种"一端接 3.3V"的接法需要改成 GPIO_PULLDOWN +
     *  GPIO_MODE_IT_RISING；本固件按上面的接 GND 方式配置。) */
    GPIO_InitStruct.Pin = SOS_KEY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SOS_KEY_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);   /* 低于 configMAX_SYSCALL_INTERRUPT_PRIORITY 才可调用 FromISR API */
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
}

/* ============================ USART1 (ESP01S) ============================ */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = ESP01S_BAUD_RATE;   /* 可调: 见 user_config.h;
                                                * 驱动还会在无应答时自动扫其它波特率 */
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();

    /* USART1 中断优先级 = 5, 与 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 一致。
     * 【为什么保持 5 而不提到 4】把它提到 4(高于 FreeRTOS 临界区屏蔽线)确实能
     *   让 USART1 在 DHT11/DS18B20 的临界区内也能响应, 从而基本避免 ORE; 但那样
     *   串口中断会插进这两条单总线的微秒级时序中, 反过来把 DHT11 的位判读打乱。
     *   两者相权, 这里保持 5, 改用"出错即恢复"的方式从 ORE 中自愈 —— 见下面的
     *   HAL_UART_ErrorCallback() 与 esp01s.c 的 ESP01S_OnError()。 */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* ============================ USART2 (调试) ============================ */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

/* ============================ I2C1 (400kHz) ============================ */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;    /* 三器件(OLED/MPU6050/MAX30102)均支持 */
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

#if MPU6050_ON_I2C2
/* ============================ I2C2 (400kHz) ============================
 * 姿态传感器(MPU6050)专用总线, 与 OLED/MAX30102 所在的 I2C1 分开,
 * 使单个器件挂死时不会拖垮屏幕与心率。参数与 I2C1 完全一致, 两条总线
 * 都在 APB1(36MHz)上, 时序没有差别。
 *
 * 为什么是 PB10/PB11: STM32F103C8T6 的 I2C2 没有重映射选项, 只能是这两脚。
 * 引脚与时钟在 HAL_I2C_MspInit() 里配置。
 * ====================================================================== */
static void MX_I2C2_Init(void)
{
    hi2c2.Instance = I2C2;
    hi2c2.Init.ClockSpeed = 400000;    /* MPU6050 支持 400kHz 快速模式 */
    hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK) Error_Handler();
}
#endif /* MPU6050_ON_I2C2 */

/* ============================ ADC1 (MQ2) ============================
 * STM32F1 的 ADC 固定 12 位分辨率(该系列 HAL 无 Resolution 成员)。
 * 采用单通道轮询: 每次读之前用 HAL_ADC_ConfigChannel 切换通道。
 * (PT0603 光照通道已随光照功能删除, 见 docs/39)
 * ========================================================================== */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

    /* 默认通道: MQ-2 (ADC1_IN9 / PB1) */
    sConfig.Channel = ADC_CHANNEL_9;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

/* ============================ TIM3 (1MHz → 1us) ============================ */
static void MX_TIM3_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 72 - 1;      /* 72MHz / 72 = 1MHz, 1 计数 = 1us */
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFF;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) Error_Handler();

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) Error_Handler();
}

/* ============================ RTC ============================ */
static void MX_RTC_Init(void)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef DateToUpdate = {0};

    hrtc.Instance = RTC;
    /* 【易错点】STM32F1 的 RTC 只有一个预分频器 AsynchPrediv, 没有
     * SynchPrediv —— 那是 F4/F7 系列 RTC v2 的成员。照抄 F4 例程里的
     *   hrtc.Init.AsynchPrediv = 127;  hrtc.Init.SynchPrediv = 255;
     * 会直接报 "struct has no field SynchPrediv"。
     *
     * RTC_AUTO_1_SECOND 的含义: 让 HAL 自己按实际 RTC 时钟频率算出分频值,
     * 从而得到 1 秒时基。因为前面 SystemClock_Config() 可能选了 LSE(32768Hz)
     * 也可能因板上无晶振而回退到 LSI(约 40kHz), 用自动模式两种都正确,
     * 不用我们手算 (32768/(PRL+1)=1 或 40000/(PRL+1)=1)。 */
    hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) Error_Handler();

    /* 初值由 RTC_Time_Init() 设置(见 rtc_time.c), 此处仅占位避免未使用告警 */
    (void)sTime;
    (void)DateToUpdate;
}

/* ============================================================================
 * MSP: 外设时钟与引脚
 *
 * 分工说明(这是容易踩的重复定义坑):
 *   - HAL_MspInit()(全局 AFIO/PWR 时钟 + SWJ 重映射)由 CubeMX 生成的
 *     Core/Src/stm32f1xx_hal_msp.c 提供 —— 它里面还有
 *     __HAL_AFIO_REMAP_SWJ_NOJTAG()(关 JTAG 只留 SWD), 是有用的。
 *     所以本文件【不再】定义 HAL_MspInit, 否则两个 .c 符号重复 → 链接失败。
 *   - 下面这些"每个外设各自"的 MspInit 由本文件提供。
 * ========================================================================== */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin = GPIO_PIN_9;                  /* TX */
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;                 /* RX */
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
    else if (huart->Instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin = GPIO_PIN_2;                  /* TX */
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_3;                  /* RX */
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hi2c->Instance == I2C1)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C1_CLK_ENABLE();

        /* PB6 = SCL, PB7 = SDA: 复用开漏。外部建议各接 4.7K 上拉;
         * 若模块板上已有上拉电阻则无需重复添加。 */
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
#if MPU6050_ON_I2C2
    else if (hi2c->Instance == I2C2)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_I2C2_CLK_ENABLE();

        /* PB10 = SCL, PB11 = SDA: 复用开漏 + 上拉。
         * 【注意】I2C2 在 STM32F103C8T6 上【没有重映射选项】, 只能是这两个脚
         * (不像 I2C1 还能改到 PB8/PB9), 所以不用在这里处理 AFIO 重映射。
         * 外部仍需一组 4.7K 上拉到 3.3V; GY-521 类模块板载自带, 不必再加。 */
        GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
#endif /* MPU6050_ON_I2C2 */
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hadc->Instance == ADC1)
    {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        /* PB1 = ADC1_IN9 (MQ-2), 模拟输入。PB0 原为 PT0603 光照通道,
         * 已随光照功能删除(docs/39), 这里不再配置 */
        GPIO_InitStruct.Pin = GPIO_PIN_1;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc_handle)
{
    if (hrtc_handle->Instance == RTC)
    {
        __HAL_RCC_RTC_ENABLE();
    }
}

/* ============================================================================
 * 中断回调
 * ========================================================================== */

/* ESP01S 串口接收: 每个字节进环形缓冲 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        ESP01S_RxCplt();
    }
}

/* ESP01S 串口线路错误回调 —— 【这是"连上后反复断连"的关键修复点】
 *
 * 机理: DHT11/DS18B20 的单总线时序用 taskENTER_CRITICAL() 保护, 它通过
 *       BASEPRI 屏蔽"优先级数值 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)"
 *       的中断, 而 USART1 恰好配在 5 —— 于是这些几毫秒的临界区里,
 *       USART1 的 RXNE 中断被压住, 而 ESP01S 若正好在连续回数据
 *       (如 "WIFI CONNECTED"/"OK"/"SEND OK"), DR 寄存器只能存 1 字节,
 *       后面的字节到达就置位 ORE(溢出错误)。
 *
 *       STM32 HAL 一旦看到 ORE, 会走 UART_EndRxTransfer(): 关掉 RXNE/PE/ERR
 *       中断并把 RxState 复位, 然后才调用本回调。若固件不在这里重新武装
 *       接收, 串口接收就【永久停摆】—— 此后所有"等 OK / 等 >"全部超时,
 *       上层判定发布失败 → 断开重连 → 连上后过一会儿又撞上 → 再断,
 *       现场表现正是"断了又连、连了又断"。
 *
 * 处置: 转发给驱动层清错误标志并重新武装接收(见 esp01s.c 的 ESP01S_OnError)。
 *       任务侧另有 ESP01S_RxWatchdog() 兜底, 见 app_tasks.c 的 WifiTask。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        ESP01S_OnError();
    }
}

/* SOS 按键 (PA4 / EXTI4): 需求要求"立即进入 Alarm_Task 并发送紧急事件"。
 * 这里直接从 ISR 投递报警(Alarm_Task 优先级最高, portYIELD_FROM_ISR 会
 * 立即切换过去), 并触发 WiFi_Task 抢发 SOS 报文。 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin != SOS_KEY_PIN) return;

    /* 简单消抖: 200ms 内的重复触发忽略(ISR 内不做延时等待) */
    if ((now - last_tick) < 200 && last_tick != 0) return;
    last_tick = now;

    APP_PostAlarmFromISR(ALARM_SOS, 3000, 1);
}

/* ============================================================================
 * FreeRTOS 钩子函数
 * ========================================================================== */

/* 栈溢出钩子: FreeRTOSConfig.h 里 configCHECK_FOR_STACK_OVERFLOW = 2 时,
 * 内核会在每次任务切换时检查各任务栈末尾的魔数。一旦被踩坏就会调用本函数。
 *
 * 遇到这里卡住, 说明某个任务的栈给小了(或函数里有超大局部数组)。
 * 排查方法: 把参数 pcTaskName 打到调试串口/在 Watch 窗口看, 就能知道是哪个任务;
 *           然后在 APP_TasksCreate()(app_tasks.c) 里调大对应任务的栈深,
 *           同时确保 FreeRTOSConfig.h 的 configTOTAL_HEAP_SIZE 也同步调大。
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    __disable_irq();
    /* 蜂鸣器长鸣 —— 用声音提示"栈溢出", 不用接调试器也能发现 */
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN,
                      BUZZER_ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
    (void)pcTaskName;      /* 调试时在此打断点, 观察 pcTaskName 得知是哪个任务 */
    while (1) { }
}

/* Malloc 失败钩子: configUSE_MALLOC_FAILED_HOOK = 0 时不参与编译 */
#if (configUSE_MALLOC_FAILED_HOOK == 1)
void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN,
                      BUZZER_ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
    while (1) { }
}
#endif

/* ============================ 错误处理 ============================ */
void Error_Handler(void)
{
    __disable_irq();
    /* 蜂鸣器长鸣提示初始化失败(若 GPIO 已配置) */
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN,
                      BUZZER_ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
