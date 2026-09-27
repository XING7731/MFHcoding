#include "main.h"
#include "timer.h"

TIM_HandleTypeDef htim1,htim2,htim3,htim4;
uint8_t UR1_flg,UR2_flg,UR3_flg,UR4_flg;
uint16_t timer1_flag_1;
uint16_t timer2_flag_1,timer2_flag_2;
/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
	
//arr：自动重装值。
//psc：时钟预分频数
//定时器溢出时间计算方法:Tout=((arr+1)*(psc+1))/Ft us.
//Ft=定时器工作频率,单位:Mhz
//这里使用tout= 1*2^16*n+定时次数	
/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
	
#if(Enable_TIM1)			
void TIM1_Init(u16 arr,u16 psc)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = psc;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = arr;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim1);
	HAL_TIM_Base_Start_IT(&htim1); //使能定时器1和定时器1更新中断：TIM_IT_UPDATE   
}

#endif	

#if(Enable_TIM2)	
/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
void TIM2_Init(u16 arr,u16 psc)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = psc;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = arr;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim2);
	HAL_TIM_Base_Start_IT(&htim2); //使能定时器3和定时器3更新中断：TIM_IT_UPDATE   

}
#endif	

#if(Enable_TIM3)	
/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
void TIM3_Init(u16 arr,u16 psc)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = psc;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = arr;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim3);
	HAL_TIM_Base_Start_IT(&htim3); //使能定时器3和定时器3更新中断：TIM_IT_UPDATE   
}
#endif	

#if(Enable_TIM4)	
/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
void TIM4_Init(u16 arr,u16 psc)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = psc;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = arr;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim4);
	HAL_TIM_Base_Start_IT(&htim4); //使能定时器3和定时器3更新中断：TIM_IT_UPDATE   

}

#endif	


/**
* @brief TIM_Base MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param htim_base: TIM_Base handle pointer
* @retval None
*/

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
	GPIO_InitTypeDef GPIO_Initure;

	if(htim->Instance==TIM1)
		{
		 __HAL_RCC_TIM1_CLK_ENABLE();          
		 HAL_NVIC_SetPriority(TIM1_BRK_IRQn,1,0);    
		 HAL_NVIC_EnableIRQ(TIM1_BRK_IRQn); 		
		}	
		
	if(htim->Instance==TIM2)
		{
		
		 __HAL_RCC_TIM2_CLK_ENABLE();            
		 HAL_NVIC_SetPriority(TIM2_IRQn,2,0);    
		 HAL_NVIC_EnableIRQ(TIM2_IRQn);          
		}	
		
	if(htim->Instance==TIM3)
		{
		 __HAL_RCC_TIM3_CLK_ENABLE();            
		 HAL_NVIC_SetPriority(TIM3_IRQn,3,0);    
		 HAL_NVIC_EnableIRQ(TIM3_IRQn);          
		}	
	if(htim->Instance==TIM4)
		{
		 __HAL_RCC_TIM4_CLK_ENABLE();            
		 HAL_NVIC_SetPriority(TIM4_IRQn,4,0);    
		 HAL_NVIC_EnableIRQ(TIM4_IRQn);          
		}	
}

//arr：自动重装值。
//psc：时钟预分频数
//定时器溢出时间计算方法:Tout=((arr+1)*(psc+1))/Ft us.
//Ft=定时器工作频率,单位:Mhz
//这里使用tout= 1*2^16*n+定时次数

void MX_TIMx_Init(void)
{
//  TIM1_Init(7999,8999);  //根据时钟晶振计算准确1S触发 Tout=（8000*9000)/72M=1S
    TIM2_Init(7199,0);   //0.01S
		TIM3_Init(9999,7199);   //1S
		TIM4_Init(7999,1000);
}



//定时器1中断服务函数
void TIM1_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim1);
}

//定时器2中断服务函数
void TIM2_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim2);
	
}
//定时器3中断服务函数
void TIM3_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim3);
	
}//定时器4中断服务函数
void TIM4_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim4);
	
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{ 
	
	/*htim标准1S进入一次，统计各个通道整数值，计算个数值,且个基数清零*/
	if(htim->Instance==htim1.Instance){
		timer_1_handle();
		HAL_TIM_Base_Stop_IT(&htim1);	
	}
	if(htim->Instance==htim2.Instance){
	  timer_2_handle();
//	  HAL_TIM_Base_Stop_IT(&htim2);
	}
	if(htim->Instance==htim3.Instance){
	  timer_3_handle();
//	  HAL_TIM_Base_Stop_IT(&htim3);
	}	
	if(htim->Instance==htim4.Instance){
		timer_4_handle();
//		HAL_TIM_Base_Stop_IT(&htim4);
	}	
	
}

int miao=0,fen=0,shi=0;
int Flag_15s = 0;
void timer_1_handle(void)//根据主函数，1S进入一次
{
	 timer1_flag_1=1;
}	

void timer_2_handle(void)
{
	 timer2_flag_1=1;
	PWM_Config();
}

void timer_3_handle(void)
{
		// 只有闸门处于打开状态时才进行10秒计时
    if (doorFlag == 1) {
        Flag_15s++;
        if (Flag_15s >= 15) {   // 达到10秒
            PWM_Val = 5;         // 关闭闸门
            Flag_15s = 0;
            doorFlag = 0;        // 清除打开标志
//            JQ8900PlayTrackNum(21); // 播放：闸门已关闭
					
        }
    }
    // 如果闸门未打开，则不做任何操作，Flag_10s保持不变
}

void timer_4_handle(void)
{
		 KeyScan();
}






























/************************************捕获定时器设置****************************************/
#if(Enable_Capture)	
void Capture_TIMx_Init(void)
{
	TIM2_Init(0xFFFF,1);   //捕获定时器1
}	

void TIM2_Init(uint16_t arr,uint16_t psc )
{   
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
	TIM_IC_InitTypeDef sConfigIC = {0};
  
  htim2.Instance = TIM2;                                               //通用定时器1
  htim2.Init.Prescaler = psc;                                            //分频系数
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;                         //上计数
  htim2.Init.Period = arr;                                             //自动装载值
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;                   //时钟分频因子
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;       //关闭自动装载
  HAL_TIM_Base_Init(&htim2);

   // 2. 必须添加的输入捕获配置
   sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
   sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
   sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
   sConfigIC.ICFilter = 0;
   HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_1); // 选择正确通道 
/*-------------------------------------------------*/ 
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_EXTERNAL1;                        //外部模式
  sSlaveConfig.InputTrigger = TIM_TS_TI1F_ED;                              //触发源
  sSlaveConfig.TriggerFilter = 0;                                          //触发滤波
  HAL_TIM_SlaveConfigSynchro(&htim2, &sSlaveConfig);
  
  
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;       	//每次更新事件TRGO1输出一个上升沿
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;  //关闭主模式
  HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig); 
/*-------------------------------------------------*/ 
	HAL_TIM_Base_MspInit(&htim2);
	HAL_TIM_Base_Start_IT(&htim2);
	HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_1);
//	

}
#endif


	



