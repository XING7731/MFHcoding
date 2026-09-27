#include "main.h"
#include "init.h"
//extern TCS3200_HandleTypeDef tcs;  // 声明外部变量

extern uint16_t speter_flag;

// TCS3200 OUT引脚中断服务函数
//void EXTI9_5_IRQHandler(void) {
//    if (__HAL_GPIO_EXTI_GET_IT(SPET_OUT_PIN) != RESET) {	
//					speter_flag++;	
//          if(speter_flag>0xffff)speter_flag=0; 			
//        __HAL_GPIO_EXTI_CLEAR_IT(SPET_OUT_PIN);
//    }
//}



//// 外部中断回调（如OUT接PA5）
//void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

//}

/**
 * @brief       外部中断初始化程序
 * @param       无
 * @retval      无
 */
void GPIO_EXTI_Init(void)
{
//	GPIO_InitTypeDef gpio_init = {0};
//	
//    // 2. 配置OUT引脚为外部中断模式
//    GPIO_InitTypeDef GPIO_InitStruct = {0};
//    GPIO_InitStruct.Pin = TCS3200_OUT_PIN;
//    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; // 上升沿触发
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    HAL_GPIO_Init(TCS3200_OUT_PORT, &GPIO_InitStruct);

//    // 3. 启用EXTI中断
//    HAL_NVIC_SetPriority(TCS3200_EXTI_IRQn, 0, 0);
//    HAL_NVIC_EnableIRQ(TCS3200_EXTI_IRQn);
	
//		GPIO_InitTypeDef gpio_init = {0};
//	
//    // 2. 配置OUT引脚为外部中断模式
//    GPIO_InitTypeDef GPIO_InitStruct = {0};
//    GPIO_InitStruct.Pin = SPET_OUT_PIN;
//    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; // 上升沿触发
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    HAL_GPIO_Init(SPET_OUT_PORT, &GPIO_InitStruct);

//    // 3. 启用EXTI中断
//    HAL_NVIC_SetPriority(SPET_EXTI_IRQn, 0, 0);
//    HAL_NVIC_EnableIRQ(SPET_EXTI_IRQn);	
	
	
	
}





void GPIO_RELAY_Init(void)	
{	
		GPIO_InitTypeDef GPIO_InitStruct;
		RELAY_GPIO_CLK_ENABLE();
		GPIO_InitStruct.Pin = RELAY_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(RELAY_GPIO_PORT, &GPIO_InitStruct);
	
		RELAY1_GPIO_CLK_ENABLE();
		GPIO_InitStruct.Pin = RELAY1_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(RELAY1_GPIO_PORT, &GPIO_InitStruct);
	
		RELAY2_GPIO_CLK_ENABLE();
		GPIO_InitStruct.Pin = RELAY2_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(RELAY2_GPIO_PORT, &GPIO_InitStruct);
		
//		RELAY3_GPIO_CLK_ENABLE();
//		GPIO_InitStruct.Pin = RELAY3_GPIO_PIN;
//		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
//		GPIO_InitStruct.Pull = GPIO_PULLUP;
//		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//		HAL_GPIO_Init(RELAY3_GPIO_PORT, &GPIO_InitStruct);
		
}	

void GPIO_INFRTA_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    INFRA1_GPIO_CLK_ENABLE();                                  /* KEY0时钟使能 */
    INFRA2_GPIO_CLK_ENABLE();                                     /* KEY1时钟使能 */
    INFRA3_GPIO_CLK_ENABLE();                                    /* KEY1时钟使能 */
	
	
    GPIO_InitStruct.Pin = INFRA1_GPIO_PIN;                       /* KEY0引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(INFRA1_GPIO_PORT, &GPIO_InitStruct);           /* KEY0引脚模式设置,上拉输入 */

    GPIO_InitStruct.Pin = INFRA2_GPIO_PIN;                       /* KEY1引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(INFRA2_GPIO_PORT, &GPIO_InitStruct);           /* KEY1引脚模式设置,上拉输入 */

    GPIO_InitStruct.Pin = INFRA3_GPIO_PIN;                       /* KEY1引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(INFRA3_GPIO_PORT, &GPIO_InitStruct);           /* KEY1引脚模式设置,上拉输入 */	
}

void GPIO_BEEP_Init(void)	
{	
		GPIO_InitTypeDef GPIO_InitStruct;
		BEEP_GPIO_CLK_ENABLE();
		GPIO_InitStruct.Pin = BEEP_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(BEEP_GPIO_PORT, &GPIO_InitStruct);
}	


void BEEP_DOING(void)
{
	BEEP(Hi); //蜂鸣器响
	delay_ms(200);
	BEEP(Lo); 	
}	

void BuzzerRingsNum(u8 num)//蜂鸣器函数
{
	  for(uint8_t i=0; i<num; i++)
		{
				BEEP(Hi);
			  delay_ms(100);
			 	BEEP(Lo);
			  if(num!=1)
				delay_ms(50);
		}
}



void MX_Hard_Init(void)
{
		OLED_Init();
		OLED_CLS();	
		KEY_Init();      //初始化选择，键盘扫描or单个按键
		memset(keyNum,0,sizeof(keyNum));
		GPIO_RELAY_Init();
		GPIO_INFRTA_Init();
		GPIO_BEEP_Init();
		MOTORS_FOOD_Init();
		DS1302_Init(&SysDate); // 时间模块初始化
		RC522_Init();
		OLED_CLS();	
	
	
	
	
	
	
	
	
	
	
}	