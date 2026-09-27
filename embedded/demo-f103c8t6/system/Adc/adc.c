#include "main.h"
#include "adc.h"

ADC_HandleTypeDef hadc1,hadc2;


/* ADC Initialization Function */
void MX_ADC_Init(void)
{

	ADC_ChannelConfTypeDef sConfig = {0};



  /* USER CODE END ADC_Init 2 */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;				//非扫描模式	
  hadc1.Init.ContinuousConvMode = DISABLE;					//关闭连续转换
  hadc1.Init.DiscontinuousConvMode = DISABLE;				//禁止不连续采样模式
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START; //软件触发
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;				//右对齐
	hadc1.Init.NbrOfDiscConversion = 0;               //配置间断模式的规则通道个数，禁止规则通道组间断模式后，此参数忽略
  hadc1.Init.NbrOfConversion = 1;										//赋值范围是1~16
  HAL_ADC_Init(&hadc1);

	HAL_ADCEx_Calibration_Start(&hadc1);              //校准ADC 
	
  sConfig.Channel = ADC_CHANNEL_8;												//通道
  sConfig.Rank = ADC_REGULAR_RANK_1;											//第1个序列，序列1
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;					//采样时间
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);								//通道配置



  /* USER CODE END ADC_Init 2 */
  hadc2.Instance = ADC2;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;				//非扫描模式	
  hadc2.Init.ContinuousConvMode = DISABLE;					//关闭连续转换
  hadc2.Init.DiscontinuousConvMode = DISABLE;				//禁止不连续采样模式
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START; //软件触发
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;				//右对齐
	hadc2.Init.NbrOfDiscConversion = 0;               //配置间断模式的规则通道个数，禁止规则通道组间断模式后，此参数忽略
  hadc2.Init.NbrOfConversion = 1;										//赋值范围是1~16
  HAL_ADC_Init(&hadc2);

	HAL_ADCEx_Calibration_Start(&hadc2);              //校准ADC 
	
  sConfig.Channel = ADC_CHANNEL_9;												//通道
  sConfig.Rank = ADC_REGULAR_RANK_1;											//第1个序列，序列1
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;					//采样时间
  HAL_ADC_ConfigChannel(&hadc2, &sConfig);								//通道配置



}


void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
{
//	RCC_PeriphCLKInitTypeDef adc_clk_init = {0};
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(hadc->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

//		/* 设置ADC时钟 */
//		adc_clk_init.PeriphClockSelection = RCC_PERIPHCLK_ADC;    /* ADC外设时钟 */
//		adc_clk_init.AdcClockSelection = RCC_ADCPCLK2_DIV6;       /* 分频因子6时钟为72M/6=12MHz */
//		HAL_RCCEx_PeriphCLKConfig(&adc_clk_init);                 /* 设置ADC时钟 */		
		
    /**ADC1 GPIO Configuration
    PA0-WKUP     ------> ADC1_IN0
			
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */

  }
 else if(hadc->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspInit 0 */

  /* USER CODE END ADC2_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_ADC2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC2 GPIO Configuration
    PA1     ------> ADC2_IN1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN ADC2_MspInit 1 */

  /* USER CODE END ADC2_MspInit 1 */
  }
}

/* ADC Start Conversion Function */
void ADC_StartConversion(void)
{
  /* Start ADC conversion */
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ADC Read Data Function */
//uint32_t ADC_ReadData(void)
//{
//  /* Wait for ADC conversion to complete */
//  if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK)
//  {
//    Error_Handler();
//  }

//  /* Read ADC converted value */
//  uint32_t adc_value = HAL_ADC_GetValue(&hadc1);

//  return adc_value;
//}




/**
* @brief ADC MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hadc: ADC handle pointer
* @retval None
*/
void HAL_ADC_MspDeInit(ADC_HandleTypeDef* hadc)
{
  if(hadc->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC1_CLK_DISABLE();

    /**ADC1 GPIO Configuration
    PB0     ------> ADC1_IN8
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0);

  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
  else if(hadc->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspDeInit 0 */

  /* USER CODE END ADC2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC2_CLK_DISABLE();

    /**ADC2 GPIO Configuration
    PB1     ------> ADC2_IN9
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_1);

  /* USER CODE BEGIN ADC2_MspDeInit 1 */

  /* USER CODE END ADC2_MspDeInit 1 */
  }

}


uint16_t Get_Adc1(void)
{

    HAL_ADC_Start(&hadc1);                               //开启ADC

    HAL_ADC_PollForConversion(&hadc1,HAL_MAX_DELAY);     //轮询转换
	
		uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
	
		return adc_value;

}




uint16_t Get_Adc2(void)
{

    HAL_ADC_Start(&hadc2);                               //开启ADC

    HAL_ADC_PollForConversion(&hadc2,HAL_MAX_DELAY);     //轮询转换
	
		uint32_t adc_value = HAL_ADC_GetValue(&hadc2);
	
		return adc_value;
}



