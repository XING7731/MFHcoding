#ifndef __INIT_h__
#define __INIT_h__
#include "main.h"

#define RELAY_GPIO_PORT            	 	GPIOA																			//车位1指示灯
#define RELAY_GPIO_PIN                GPIO_PIN_5
#define RELAY_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0) 

#define RELAY1_GPIO_PORT            	 GPIOA																		//车位2指示灯
#define RELAY1_GPIO_PIN                GPIO_PIN_4
#define RELAY1_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0) 

#define RELAY2_GPIO_PORT            	 GPIOA																		//车位3指示灯
#define RELAY2_GPIO_PIN                GPIO_PIN_3
#define RELAY2_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0) 

#define BEEP_GPIO_PORT            	 GPIOA
#define BEEP_GPIO_PIN                GPIO_PIN_11
#define BEEP_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0) 

#define INFRA1_GPIO_PORT                  GPIOB
#define INFRA1_GPIO_PIN                   GPIO_PIN_11
#define INFRA1_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define INFRA2_GPIO_PORT                  GPIOB
#define INFRA2_GPIO_PIN                   GPIO_PIN_10
#define INFRA2_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define INFRA3_GPIO_PORT                  GPIOB
#define INFRA3_GPIO_PIN                   GPIO_PIN_1
#define INFRA3_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define RELAY(x)  	  do{ x ? \
												HAL_GPIO_WritePin(RELAY_GPIO_PORT, RELAY_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(RELAY_GPIO_PORT, RELAY_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)  
#define RELAY1(x)  	  do{ x ? \
												HAL_GPIO_WritePin(RELAY1_GPIO_PORT, RELAY1_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(RELAY1_GPIO_PORT, RELAY1_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)  
#define RELAY2(x)  	  do{ x ? \
												HAL_GPIO_WritePin(RELAY2_GPIO_PORT, RELAY2_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(RELAY2_GPIO_PORT, RELAY2_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0) 

#define BEEP(x)  	  do{ x ? \
												HAL_GPIO_WritePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(BEEP_GPIO_PORT, BEEP_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)  
#define INFRA1        HAL_GPIO_ReadPin(INFRA1_GPIO_PORT, INFRA1_GPIO_PIN)     /* 读取KEY0引脚 */
#define INFRA2        HAL_GPIO_ReadPin(INFRA2_GPIO_PORT, INFRA2_GPIO_PIN)     /* 读取KEY1引脚 */
#define INFRA3        HAL_GPIO_ReadPin(INFRA3_GPIO_PORT, INFRA3_GPIO_PIN)     /* 读取KEY2引脚 */
	
void MX_Hard_Init(void);

uint8_t Key_Scan(void) ;
void GPIO_EXTI_Init(void);
void BEEP_DOING(void);
void GPIO_RELAY_Init(void);	
void GPIO_INFRTA_Init(void);				
void BuzzerRingsNum(u8 num);
#endif





