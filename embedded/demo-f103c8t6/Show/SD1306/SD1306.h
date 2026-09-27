#ifndef __SD1306_H
#define __SD1306_H

#include "main.h"
#define OLED_ADDRESS	0x78 //通过调整0R电阻,屏可以0x78和0x7A两个地址 -- 默认0x78
#define OLED_cmd			0x00 
#define OLED_dat			0x40 
// LCD控制引脚定义
#define IICSD1306S_GPIO_PORT            	   GPIOC
#define IICSD1306S_GPIO_SDA                  GPIO_PIN_13
#define IICSD1306S_GPIO_PORT            	   GPIOC
#define IICSD1306S_GPIO_SCL                  GPIO_PIN_14

// 显示模式定义
#define NORMAL_DISPLAY  0
#define INVERSE_DISPLAY 1

// 字符大小定义
#define FONT_6X8        1
#define FONT_8X16       2

// 函数声明
void OLED_Init(void);
void WriteCmd(uint8_t I2C_Command);
void WriteDat(uint8_t I2C_Data);
void OLED_SetPos(uint8_t x, uint8_t y);
void OLED_Fill(uint8_t fill_Data);
void OLED_CLS(void);
void OLED_ON(void);
void OLED_OFF(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t TextSize, uint8_t mode);
void OLED_ShowStr(uint8_t x, uint8_t y, uint8_t ch[], uint8_t TextSize, uint8_t mode);
void OLED_ShowCN(uint8_t x, uint8_t y, uint8_t N, uint8_t mode);
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t N);
void OLED_ShowCentigrade(uint8_t x, uint8_t y);
void OLED_ShowTH(uint8_t x, uint8_t y, uint8_t DATA);
void OLED_ShowFormatStr(uint8_t x, uint8_t y,  const char* format, ...);
// 外部字体数据声明
//extern const uint8_t F6x8[][6];
//extern const uint8_t F8X16[];
//extern const uint8_t F16x16[];
//extern const uint8_t BMP2[][128*8]; // 假设最大128x64位图

#endif /* __OLED_H */





