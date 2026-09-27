#include "main.h"
#include "motors.h"

uint16_t count;
uint16_t PWM_Val = 5;

void MOTORS_FOOD_Init(void)
{
		GPIO_InitTypeDef GPIO_InitStruct;
		FOOD_GPIO_CLK_ENABLE();
		GPIO_InitStruct.Pin = FOOD_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(FOOD_GPIO_PORT, &GPIO_InitStruct);
}	



void PWM_Config()//PWM调节
{
	count++;
	if(count > 200)//20ms一个周期
		count = 0;
}




