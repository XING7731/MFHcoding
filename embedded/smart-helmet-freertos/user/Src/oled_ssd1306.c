/* =========================================================================
 * SSD1306 (128x64, I2C) 驱动
 * 显存 1KB, 采用 SSD1306 水平寻址模式整页/按页写入。
 *
 * 总线时间说明: 128x64 整屏 1024 字节, 在 I2C 400kHz 下约 25~30ms 的阻塞
 * 传输 —— 因此必须持有 xMutexI2C。本工程刷新周期 500ms(DISPLAY_PERIOD_MS),
 * 单帧占 I2C1 带宽约 6%, 且 Display_Task 优先级最低(0), 不会拖慢 I2C1 上的
 * MAX30102 采样; 但若要再往下压(如 100ms), 需重新核算这条总线的时间预算。
 * ========================================================================= */
#include "oled_ssd1306.h"
#include "user_config.h"
#include <string.h>

/* 6x8 字库(5x7 点阵放在 6x8 单元中), 由 tools/gen_font.py 生成 */
extern const unsigned char F6x8[][6];

#if OLED_USE_CN_FONT
/* 中文 16x16 点阵需使用厂商字库(体积大), 自行拷入后在此声明。
 * 例如: extern const unsigned char HZ16x16[][32];  并实现取模索引 */
#endif

static I2C_HandleTypeDef *s_i2c = 0;
static uint8_t s_buf[OLED_PAGES][OLED_WIDTH];     /* 1KB 显存 */

/* 控制字节: 0x00 = 后续为命令, 0x40 = 后续为数据(SSD1306 I2C 约定) */
static uint8_t write_ctrl(uint8_t ctrl, const uint8_t *data, uint16_t len)
{
    if (s_i2c == 0) return 1;
    return (HAL_I2C_Mem_Write(s_i2c, OLED_I2C_ADDR7 << 1, ctrl,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, len,
                              OLED_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : 1;
}

/* 说明: 这里只保留 write_cmds(成批下发)。单条版本的 write_cmd 已删除 ——
 * 未使用的 static 函数会被 armcc 告警(#177-D declared but never referenced),
 * OLED_Init / OLED_RefreshPages 都走成批接口。 */
static uint8_t write_cmds(const uint8_t *cmds, uint16_t n)
{
    return write_ctrl(0x00, cmds, n);
}

uint8_t OLED_Init(I2C_HandleTypeDef *hi2c)
{
    static const uint8_t init_cmds[] = {
        0xAE,             /* 关闭显示 */
        0xD5, 0x80,       /* 时钟分频 */
        0xA8, 0x3F,       /* 多路复用比 = 64 (128x64) */
        0xD3, 0x00,       /* 显示偏移 0 */
        0x40,             /* 起始行 0 */
        0x8D, 0x14,       /* 使能内部电荷泵 */
        0x20, 0x00,       /* 存储模式 = 水平寻址 */
        0xA1,             /* 段重映射 */
        0xC8,             /* 行扫描方向反向 */
        0xDA, 0x12,       /* COM 引脚配置(128x64 常用值) */
        0x81, 0xCF,       /* 对比度 */
        0xD9, 0xF1,       /* 预充电周期 */
        0xDB, 0x30,       /* VCOMH 反压 */
        0xA4,             /* 显示内容跟随显存 */
        0xA6,             /* 正常显示(非反色) */
        0xAF              /* 开启显示 */
    };

    s_i2c = hi2c;
    memset(s_buf, 0, sizeof(s_buf));

    if (write_cmds(init_cmds, sizeof(init_cmds)) != 0) return 1;

    OLED_Clear();
    OLED_Refresh();
    return 0;
}

void OLED_Clear(void)
{
    memset(s_buf, 0, sizeof(s_buf));
}

void OLED_RefreshPages(uint8_t page_mask)
{
    uint8_t page;

    for (page = 0; page < OLED_PAGES; page++)
    {
        if ((page_mask & (1u << page)) == 0) continue;

        /* 设置页地址范围(0x22)与列范围(0x21), 然后一次写入该页 128 字节 */
        {
            uint8_t cmds[6];
            cmds[0] = 0x22; cmds[1] = page; cmds[2] = page;
            cmds[3] = 0x21; cmds[4] = 0x00; cmds[5] = 0x7F;
            if (write_cmds(cmds, sizeof(cmds)) != 0) return;
        }
        if (write_ctrl(0x40, s_buf[page], OLED_WIDTH) != 0) return;
    }
}

void OLED_Refresh(void)
{
    OLED_RefreshPages(0xFF);
}

/* 写一个 6x8 字符(ASCII 32~126) */
static void show_char(uint8_t x, uint8_t page, char c)
{
    uint8_t i;

    if (c < 32 || c > 126) c = ' ';
    if (page >= OLED_PAGES || x > (OLED_WIDTH - 6)) return;

    for (i = 0; i < 6; i++)
    {
        s_buf[page][x + i] = F6x8[(uint8_t)c - 32][i];
    }
}

void OLED_ShowString(uint8_t x, uint8_t page, const char *str)
{
    while (*str && x <= (OLED_WIDTH - 6))
    {
        show_char(x, page, *str++);
        x = (uint8_t)(x + 6);
    }
}

/* 按固定字符宽度显示, 不足部分补空格 —— 避免刷新时残留旧字符 */
void OLED_ShowStringPadded(uint8_t x, uint8_t page, const char *str, uint8_t width)
{
    uint8_t n = 0;

    while (*str && n < width && x <= (OLED_WIDTH - 6))
    {
        show_char(x, page, *str++);
        x = (uint8_t)(x + 6);
        n++;
    }
    while (n < width && x <= (OLED_WIDTH - 6))
    {
        show_char(x, page, ' ');
        x = (uint8_t)(x + 6);
        n++;
    }
}

#if OLED_USE_CN_FONT
/* 中文显示需要 16x16 点阵字库。此处只留接口, 不做任何假设实现:
 * 请把厂商例程中的中文点阵数组拷入, 并按你的取模方式实现索引。 */
void OLED_ShowCN(uint8_t x, uint8_t page, const char *cn_utf8)
{
    (void)x; (void)page; (void)cn_utf8;
    /* 未提供中文字库时, 退化为显示占位符 */
    OLED_ShowString(x, page, "[CN FONT MISSING]");
}
#endif
