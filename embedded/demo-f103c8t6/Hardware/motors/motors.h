#ifndef __MOTORS_H__
#define __MOTORS_H__
#include "main.h"


typedef enum {
    MOTOR_STOP,
    MOTOR_CW,    // 正转
    MOTOR_CCW    // 反转
} MotorState_t;

#define FOOD_GPIO_PORT            	  GPIOB
#define FOOD_GPIO_PIN                 GPIO_PIN_8
#define FOOD_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define FOOD_Hi		HAL_GPIO_WritePin(FOOD_GPIO_PORT,FOOD_GPIO_PIN,GPIO_PIN_SET)
#define FOOD_Lo		HAL_GPIO_WritePin(FOOD_GPIO_PORT,FOOD_GPIO_PIN,GPIO_PIN_RESET)



#endif
extern uint16_t count;
extern uint16_t PWM_Val;

void MOTORS_FOOD_Init(void);

void TIM2_harder(void);
void PWM_Config();//PWM调节




