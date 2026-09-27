#ifndef __OLED_SSD1306_H
#define __OLED_SSD1306_H

#include "stm32f1xx_hal.h"

/* SSD1306 128x64, I2C 接口, 7bit 地址 0x3C (见 user_config.h)
 *
 * 显存: 1024 字节整屏缓冲(页地址 0~7, 每页 128 列)。
 * 刷新: OLED_Refresh() 整屏刷; OLED_RefreshPages(mask) 按页刷(省总线时间)。
 *
 * 字库说明:
 *   6x8  ASCII 字库由 tools/gen_font.py 生成, 内置于 oled_font.c(5x7 点阵
 *   放在 6x8 单元中), 覆盖 ASCII 32~126。
 *   中文需 16x16 点阵, 体积大且需厂商字库, 故通过 OLED_USE_CN_FONT 开关
 *   (见 user_config.h): 为 1 时需自行拷入厂商字库并实现 OLED_DrawCN()。
 * ========================================================================= */

#define OLED_WIDTH     128
#define OLED_HEIGHT    64
#define OLED_PAGES     8

uint8_t OLED_Init(I2C_HandleTypeDef *hi2c);              /* 0 = 成功 */
void    OLED_Clear(void);
void    OLED_Refresh(void);                              /* 刷新全部 8 页 */
void    OLED_RefreshPages(uint8_t page_mask);            /* bit0~bit7 对应页 0~7 */
void    OLED_ShowString(uint8_t x, uint8_t page, const char *str);
void    OLED_ShowStringPadded(uint8_t x, uint8_t page, const char *str, uint8_t width);
#if OLED_USE_CN_FONT
void    OLED_ShowCN(uint8_t x, uint8_t page, const char *cn_utf8);  /* 需厂商字库 */
#endif

#endif /* __OLED_SSD1306_H */
