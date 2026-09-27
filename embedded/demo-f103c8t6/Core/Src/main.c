/**
 ****************************************************************************************************
 * @file        main.c
 * @author      
 * @version     V1.0
 * @date        
 * @brief       公版程序组
 ****************************************************************************************************
 ****************************************************************************************************
 */
#include "main.h"

int main(void)
{

    HAL_Init();                                 /* 初始化HAL库 */
		HAL_Delay(10);
    SystemClock_Config(RCC_PLL_MUL9);        	  /* 设置时钟, 72Mhz */
    delay_init(72);                             /* 延时初始化 */
		MX_IIC_Init();
    Uart_Init();   

	/* 串口初始化为115200 */
    MX_ADC_Init();															/* ADC初始化 */
    MX_SPIx_Init();															/* SPI初始化 */
    MX_TIMx_Init();															/* 定时器初始化 */
		MX_Hard_Init();                             /* 所有外设初始化 */ 


#if(WDT_RUN)	  	
  IWDG_init(IWDG_PRESCALER_64, 625*6);//4S      /* 看门狗初始化 */
	IWDG_feed();                          				/* 喂狗 */  
#endif	
		static uint32_t lastReadTime = 0;
#define RC522_READ_INTERVAL_MS  200   // 200ms 读一次
    while (1)
    {
			KeyScanHandle();
			if(setn < 8)
			{
				if(Register == 0 && Delete == 0)DisplayTime();
				if (setn == 0) 
				{
						if (setn == 0 && (HAL_GetTick() - lastReadTime >= RC522_READ_INTERVAL_MS)) {
							lastReadTime = HAL_GetTick();
							RC522_Start(0, readID, Write_Card_Data);
							if (InitDisplay == 1) {
									InitDisplay = 0;
									Display();
							}
							RC522_InfraredTick();
					}
				}
			}


#if(WDT_RUN)					
				IWDG_feed();														/* 喂狗 */ 
#endif				
    }
}


void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
