#ifndef _DS_1302_H
#define _DS_1302_H

#include "main.h"

#define DS1302_SCK_PORT            	 			 GPIOA
#define DS1302_SCK_PIN                		 GPIO_PIN_0
#define DS1302_SCK_CLK_ENABLE()      	 		 do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* 时钟使能 */

#define DS1302_RST_PORT              			 GPIOA
#define DS1302_RST_PIN               			 GPIO_PIN_2
#define DS1302_RST_CLK_ENABLE()       			 do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)      

#define DS1302_DATA_PORT            	 		 GPIOA
#define DS1302_DATA_PIN                		 GPIO_PIN_1
#define DS1302_DATA_CLK_ENABLE()      	 	 do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* 时钟使能 */


#define DS1302_SCK(x)    do{ x ? \
												HAL_GPIO_WritePin(DS1302_SCK_PORT, DS1302_SCK_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(DS1302_SCK_PORT, DS1302_SCK_PIN, GPIO_PIN_RESET); \
												}while(0) 

#define DS1302_RST(x)    do{ x ? \
												HAL_GPIO_WritePin(DS1302_RST_PORT, DS1302_RST_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(DS1302_RST_PORT, DS1302_RST_PIN, GPIO_PIN_RESET); \
												}while(0) 												
												
#define DS1302_DATA    	HAL_GPIO_ReadPin(DS1302_DATA_PORT, DS1302_DATA_PIN) 

#define RST_CLR	 HAL_GPIO_WritePin(GPIOA,GPIO_PIN_2,GPIO_PIN_RESET)			
#define RST_SET  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_2,GPIO_PIN_SET)

#define DAT_CLR  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_1,GPIO_PIN_RESET)
#define DAT_SET  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_1,GPIO_PIN_SET)
#define DAT_READ HAL_GPIO_ReadPin(DS1302_DATA_PORT,DS1302_DATA_PIN)

#define SCK_CLR   HAL_GPIO_WritePin(GPIOA,GPIO_PIN_0,GPIO_PIN_RESET)
#define SCK_SET   HAL_GPIO_WritePin(GPIOA,GPIO_PIN_0,GPIO_PIN_SET)

#define DataToBcd(x) ((x/10)*16+(x%10))//十进制转BCD码
#define BcdToData(x) ((x/16)*10+(x%16))  //the BCD code: the least 4bits can't excceed 9

#define WRITE_SEC_REG	        	0x80
#define WRITE_MIN_REG           0x82
#define WRITE_HOUR_REG          0x84
#define WRITE_DAY_REG           0x86
#define WRITE_MON_REG           0x88
#define WRITE_WEEK_REG          0x8a
#define WRITE_YEAR_REG          0x8c
#define READ_SEC_REG            0x81
#define READ_MIN_REG            0x83
#define READ_HOUR_REG           0x85
#define READ_DAY_REG            0x87
#define READ_MON_REG            0x89
#define READ_WEEK_REG           0x8b
#define READ_YEAR_REG           0x8d
#define WRITE_CONTROL_REG       0x8e
#define READ_CONTROL_REG        0x8f
#define WRITE_CHARGE_REG        0x90
#define READ_CHARGE_REG         0x91
#define WRITE_RAM_REG           0xc0
#define READ_RAM_REG            0xc1

typedef struct
{
	volatile uint8_t sec;//秒
	volatile uint8_t min;//分
	volatile uint8_t hour;//时
	volatile uint8_t day;//天
	volatile uint8_t mon;//月
	volatile uint8_t week;//星期
	volatile uint16_t year;//年
}DATE;


int DS1302_Init(const DATE * const date);
int DS1302_DateSet(const DATE * const date);
int DS1302_DateRead(DATE * const date);

extern DATE SysDate;

#endif

