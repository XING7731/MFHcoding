#ifndef __LCD1602B_H__
#define	__LCD1602B_H__
/* 包含头文件 ----------------------------------------------------------------*/
#include "main.h"

/* 类型定义 ------------------------------------------------------------------*/
/* 宏定义 --------------------------------------------------------------------*/
#define SIIC_SCL_RCC_CLK_ENABLE()         __HAL_RCC_GPIOC_CLK_ENABLE()
#define IICPCF8574_GPIO_SCL                 GPIO_PIN_13
#define IICPCF8574_GPIO_PORT                GPIOC

#define SIIC_SDA_RCC_CLK_ENABLE()         __HAL_RCC_GPIOC_CLK_ENABLE()
#define IICPCF8574_GPIO_SDA                 GPIO_PIN_14
#define IICPCF8574_GPIO_PORT                GPIOC


#define PCF8574_ADDR 	0X4e	//PCF8574地址(左移了一位) //0x7e(PCF8574AT)   0x4e(PCF8574T);0x40;0x27

//PCF8574各个IO的功能
#define RS_IO         	0		  //RS
#define RW_IO       		1   	//RW
#define CS_IO    				2    	//CS
#define BL_IO      			3    	//P3,BackLight
#define P4_IO      		  4    	//P4
#define P5_IO     		  5   	//P5
#define P6_IO     			6    	//P6
#define P7_IO    				7    	//P7


// LCD引脚定义（根据实际硬件连接修改）
#define LCD_RS_PIN 0  // P0: RS
#define LCD_RW_PIN 1  // P1: RW
#define LCD_E_PIN  2  // P2: E
// P4-P7: DB4-DB7

void LCD1602_Init(void);
void LCD_Write_Com(uint8_t cmd);
void LCD_Write_Data(uint8_t dat);
void LCD_Write_String(uint8_t x, uint8_t y, uint8_t *str);
void LCD1602_Clear(void);
void LCD_Move_Cursor(uint8_t x, uint8_t y, uint8_t blink);

uint8_t PCF8574_Init(uint8_t addr);
uint8_t PCF8574_ReadOneByte(void);
void PCF8574_WriteOneByte(uint8_t DataToWrite);
void PCF8574_WriteBit(uint8_t bit, uint8_t sta);
uint8_t PCF8574_ReadBit(uint8_t bit);
#endif 

