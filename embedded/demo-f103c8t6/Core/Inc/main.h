/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/*--------------------系统声明如无底层修改无需改动---------------------*/
/*----------------------------sys_start--------------------------------*/
#include "sys.h"
#include "iic.h"
#include "spi.h"
#include "adc.h"
#include "timer.h"
#include "usart.h"
#include "wdg.h"
#include "delay.h"
#include "init.h"
#include "algo.h"
#include "flash.h"
#include "keypad.h"
//#include "cJSON.h"
/*-----------------------------sys_end---------------------------------*/




/*-----------------------------标准库声明------------------------------*/
/*---------------------------C or C++_start----------------------------*/
#include "stdint.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include "math.h"
#include "float.h"
#include "stdarg.h"

/*----------------------------C or C++_end-----------------------------*/




/*----------------------------Hardware_start---------------------------*/
/*-----------------------根据硬件外设随时增加删改----------------------*/
//显示类




#include "SD1306.h"                                        //0.96led屏
//#include "ST7789C.h"																			 //1.3led屏
//#include "lcd1602b.h" 																	   //串口屏
/*----------------------------------------------------------------------*/
//通讯模块
//#include "esp32.h"                                       //WIFI
//#include "esp8266.h"                                      //WIFI
//#include "esp8266_al.h"
//#include "air780.h"                                       //蜂窝
//#include "BC26.h"                                         //NBIOT 
//#include "HC05.h"                                         //蓝牙
/*----------------------------------------------------------------------*/
//协议类,串口通讯
//#include "udp.h"                                            //上报
#include "rc522.h"                                          //RF_射频卡
//#include "as608.h"																					//指纹
//#include "ch2o.h"                                           //甲醛模块


/*----------------------------------------------------------------------*/
//ADC,模拟
//#include "raysile.h"                                      //紫外线
//#include "curme.h"                                        //电流*INA219取代,未启用*
//#include "volme.h"                                        //电压
//#include "watle.h"                                        //水位
//#include "noile.h"																				//噪声
//#include "dustle.h"																				//pm2.5
//#include "mqile.h"																				//MQ,系列，光敏，烟雾，空气，CO，C2H5OH
//#include "turble.h"																				//浑浊度
//#include "herle.h"                                        //
//#include "sudhumile.h"                                    //土壤湿度，雨量
//#include "phle.h"																					//酸碱度
//#include "presle.h"																				//压力模块
//#include "winddir.h"                                      //风向
//#include "windspe.h"                                      //风速
//#include "tcs3200.h"																		  //色彩识别
//#include "HC_SR04.h"																			//超声波
//#include "speter.h"																				//测转速
/*----------------------------------------------------------------------*/
//物理量
//#include "ds18b20.h"                                        //温度
//#include "bmp280.h"                                         //气压
//#include "mlx906.h"                                   			//红外温度
//#include "dht11.h"																					//湿度
//#include "ATGM336H.h"																				//定位
#include "ds1302.h"																					//RTC
//#include "adxl345.h"																				//加速度计
//#include "hx711.h"																					//称重模块
//#include "ina219.h"                                         //电流功率
//#include "max30102.h"                                        //心率血氧a
//#include "max30102_calculation.h" 													 //心率血氧b
#include "motors.h"																					//电机
//#include "MPU6050.h"																					//陀螺仪
//#include "JQ8900.h"																						//语音播放




















/*----------------------------Hardware_end-----------------------------*/





#define TRUE    										(1U)
#define FALSE   									  (0U)
#define ON                          (1U)
#define OFF                         (0U)
#define Hi                          (1U)
#define Lo                          (0U)  

#define  Regaddr      								 			0x02     


#define  English_Display              			 OFF  //测试
#define  TEST                         			 OFF  //测试
#define  WDT_RUN                      			 OFF  //看门狗



#define  Enable_UART1                        ON 
#define  Enable_UART2                        OFF 
#define  Enable_UART3                        OFF 

#define  Enable_TIM1                         OFF 
#define  Enable_TIM2                         ON 
#define  Enable_TIM3                         ON 
#define  Enable_TIM4                         ON 

#define  SPI_1                            	 OFF 	//
#define  SPI_2                            	 ON 	//
#define  SPI_3                            	 OFF 	//
#define  Hard_SPI                         	 ON 	//
#define  Soft_SPI                         	 OFF  //


#define  Ina219              					 			OFF  	//外设
#define  AT24CXX              				 		 	OFF  	//外设
#define  mlx906															OFF  	//外设
#define  bmp280															OFF  	//外设
#define  sd1306s														ON   //外设   //0.96寸LED
#define  adxl345														OFF   //外设  
#define  max30102												  	OFF   //外设 
#define  pcf8574												  	OFF    //外设  LED1602
#define  mpu6050												  	OFF   //外设  LED1602





void Error_Handler(void);
#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
