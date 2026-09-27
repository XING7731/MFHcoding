#ifndef __PCF8574_H
#define __PCF8574_H

#include "main.h"






#define IICPCF8574_GPIO_PORT            	    GPIOC
#define IICPCF8574_GPIO_SDA                  GPIO_PIN_14
#define IICPCF8574_GPIO_PORT            	    GPIOC
#define IICPCF8574_GPIO_SCL                  GPIO_PIN_13




// PCF8574地址（根据硬件调整）
#define PCF8574_ADDR    0x27  // 默认地址（左移1位后）

// LCD引脚定义（PCF8574的IO映射）
#define LCD_RS    (1 << 0)  // P0: RS
#define LCD_RW    (1 << 1)  // P1: RW（通常接地）
#define LCD_E     (1 << 2)  // P2: E使能
#define LCD_BL    (1 << 3)  // P3: 背光
#define LCD_DATA  (0xF << 4) // P4-P7: 数据线D4-D7

// 函数声明
void LCD1602_Init(void); 
void LCD1602_Clear(void);
void LCD1602_SetCursor(uint8_t row, uint8_t col);
void LCD1602_WriteChar(char ch);
void LCD1602_WriteString(const char *str);
void LCD1602_SetBacklight(uint8_t state);

#endif



