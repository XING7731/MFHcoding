#include "main.h"
#include "Spi.h"

SPI_HandleTypeDef hspi1,hspi2,hspi3;

/* SPI2句柄 */
/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
#if(Hard_SPI)		
void MX_SPIx_Init(void)
{
#if(SPI_1)	
	 MX_SPI1_Init();
#endif	
#if(SPI_2)
	 MX_SPI2_Init();
#endif	
#if(SPI_3)	
	 MX_SPI3_Init();
#endif	
}
#if(SPI_1)
void MX_SPI1_Init(void)
{
	  __HAL_RCC_SPI1_CLK_ENABLE(); /* SPI2时钟使能 */

    hspi1.Instance = SPI1;                               		  /* SPI2 */
    hspi1.Init.Mode = SPI_MODE_MASTER;                        /* 设置SPI工作模式，设置为主模式 */
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;              /* 设置SPI单向或者双向的数据模式:SPI设置为双线模式 */
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;                  /* 设置SPI的数据大小:SPI发送接收8位帧结构 */
    hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;               /* 串行同步时钟的空闲状态为高电平 */
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;                    /* 串行同步时钟的第二个跳变沿（上升或下降）数据被采样 */
    hspi1.Init.NSS = SPI_NSS_SOFT;                            /* NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制 */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* 定义波特率预分频的值:波特率预分频值为256 */
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;                   /* 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始 */
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;                   /* 关闭TI模式 */
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;   /* 关闭硬件CRC校验 */
    hspi1.Init.CRCPolynomial = 7;                             /* CRC值计算的多项式 */
    HAL_SPI_Init(&hspi1);                                     /* 初始化 */
	
		HAL_NVIC_SetPriority(SPI1_IRQn, 0, 0);    // SPI1最高优先级
		HAL_NVIC_EnableIRQ(SPI1_IRQn);
    __HAL_SPI_ENABLE(&hspi1); /* 使能SPI2 */
//	  spi2_read_write_byte(0Xff); 
}	
#endif
#if(SPI_2)
void MX_SPI2_Init(void)
{
	  __HAL_RCC_SPI2_CLK_ENABLE(); /* SPI2时钟使能 */

    hspi2.Instance = SPI2;                               		  /* SPI2 */
    hspi2.Init.Mode = SPI_MODE_MASTER;                        /* 设置SPI工作模式，设置为主模式 */
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;              /* 设置SPI单向或者双向的数据模式:SPI设置为双线模式 */
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;                  /* 设置SPI的数据大小:SPI发送接收8位帧结构 */
    hspi2.Init.CLKPolarity = SPI_POLARITY_HIGH;               /* 串行同步时钟的空闲状态为高电平 */
    hspi2.Init.CLKPhase = SPI_PHASE_2EDGE;                    /* 串行同步时钟的第二个跳变沿（上升或下降）数据被采样 */
    hspi2.Init.NSS = SPI_NSS_SOFT;                            /* NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制 */
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* 定义波特率预分频的值:波特率预分频值为256 */
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;                   /* 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始 */
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;                   /* 关闭TI模式 */
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;   /* 关闭硬件CRC校验 */
    hspi2.Init.CRCPolynomial = 7;                             /* CRC值计算的多项式 */
    HAL_SPI_Init(&hspi2);                                     /* 初始化 */

		HAL_NVIC_SetPriority(SPI2_IRQn, 1, 0);    // SPI1最高优先级
		HAL_NVIC_EnableIRQ(SPI2_IRQn);
    __HAL_SPI_ENABLE(&hspi2); /* 使能SPI2 */
//	  spi2_read_write_byte(0Xff); 
}	
#endif
//void MX_SPI3_Init(void)
//{
//		  __HAL_RCC_SPI3_CLK_ENABLE(); /* SPI2时钟使能 */

//    hspi3.Instance = SPI3;                               		  /* SPI2 */
//    hspi3.Init.Mode = SPI_MODE_MASTER;                        /* 设置SPI工作模式，设置为主模式 */
//    hspi3.Init.Direction = SPI_DIRECTION_2LINES;              /* 设置SPI单向或者双向的数据模式:SPI设置为双线模式 */
//    hspi3.Init.DataSize = SPI_DATASIZE_8BIT;                  /* 设置SPI的数据大小:SPI发送接收8位帧结构 */
//    hspi3.Init.CLKPolarity = SPI_POLARITY_HIGH;               /* 串行同步时钟的空闲状态为高电平 */
//    hspi3.Init.CLKPhase = SPI_PHASE_2EDGE;                    /* 串行同步时钟的第二个跳变沿（上升或下降）数据被采样 */
//    hspi3.Init.NSS = SPI_NSS_SOFT;                            /* NSS信号由硬件（NSS管脚）还是软件（使用SSI位）管理:内部NSS信号有SSI位控制 */
//    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* 定义波特率预分频的值:波特率预分频值为256 */
//    hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;                   /* 指定数据传输从MSB位还是LSB位开始:数据传输从MSB位开始 */
//    hspi3.Init.TIMode = SPI_TIMODE_DISABLE;                   /* 关闭TI模式 */
//    hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;   /* 关闭硬件CRC校验 */
//    hspi3.Init.CRCPolynomial = 7;                             /* CRC值计算的多项式 */
//    HAL_SPI_Init(&hspi3);                                     /* 初始化 */

//    __HAL_SPI_ENABLE(&hspi3); /* 使能SPI2 */
////	  spi2_read_write_byte(0Xff); 
//}	


/**
 * @brief       SPI底层驱动，时钟使能，引脚配置
 *   @note      此函数会被HAL_SPI_Init()调用
 * @param       hspi:SPI句柄
 * @retval      无
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{		
	GPIO_InitTypeDef GPIO_InitStruct = {0};

if(hspi->Instance==SPI1)
//  else if(hspi->Instance==SPI2)
  {
  /* USER CODE BEGIN SPI2_MspInit 0 */

  /* USER CODE END SPI2_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_SPI1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**SPI2 GPIO Configuration
    PA5     ------> SPI1_SCK
    PA6     ------> SPI1_MISO
    PA7     ------> SPI1_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN SPI2_MspInit 1 */
//		SPI1_CS_GPIO_CLK_ENABLE();
//		GPIO_InitStruct.Pin = SPI1_CS_GPIO_PIN;              
//		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
//		GPIO_InitStruct.Pull = GPIO_PULLUP;               
//		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
//		HAL_GPIO_Init(SPI1_CS_GPIO_PORT, &GPIO_InitStruct);  

//		SPI1_CS(Hi);

  /* USER CODE END SPI2_MspInit 1 */
	spi1_set_speed(SPI_SPEED_2);
  }
	
	
if(hspi->Instance==SPI2)
//  else if(hspi->Instance==SPI2)
  {
  /* USER CODE BEGIN SPI2_MspInit 0 */

  /* USER CODE END SPI2_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_SPI2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI2 GPIO Configuration
    PB13     ------> SPI2_SCK
    PB14     ------> SPI2_MISO
    PB15     ------> SPI2_MOSI
    */
    GPIO_InitStruct.Pin  = GPIO_PIN_13|GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN SPI2_MspInit 1 */
//		SPI2_CS_GPIO_CLK_ENABLE();
//		GPIO_InitStruct.Pin = SPI2_CS_GPIO_PIN;              
//		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
//		GPIO_InitStruct.Pull = GPIO_PULLUP;               
//		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
//		HAL_GPIO_Init(SPI2_CS_GPIO_PORT, &GPIO_InitStruct);  

	//	SPI2_CS(Hi);

		spi2_set_speed(SPI_SPEED_2);
  /* USER CODE END SPI2_MspInit 1 */
  }
	
//	if(hspi->Instance==SPI3)
////  else if(hspi->Instance==SPI2)
//  {
//  /* USER CODE BEGIN SPI2_MspInit 0 */

//  /* USER CODE END SPI2_MspInit 0 */
//    /* Peripheral clock enable */
//    __HAL_RCC_SPI3_CLK_ENABLE();

//    __HAL_RCC_GPIOB_CLK_ENABLE();
//    /**SPI2 GPIO Configuration
//    PB3     ------> SPI3_SCK
//    PB4     ------> SPI3_MISO
//    PB5     ------> SPI3_MOSI
//    */
//    GPIO_InitStruct.Pin  = GPIO_PIN_3|GPIO_PIN_5;
//    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
//		GPIO_InitStruct.Pull = GPIO_PULLUP;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

//    GPIO_InitStruct.Pin  = GPIO_PIN_4;
//    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

//  /* USER CODE BEGIN SPI3_MspInit 1 */
//		SPI3_CS_GPIO_CLK_ENABLE();
//		GPIO_InitStruct.Pin = SPI3_CS_GPIO_PIN;              
//		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
//		GPIO_InitStruct.Pull = GPIO_PULLUP;               
//		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
//		HAL_GPIO_Init(SPI3_CS_GPIO_PORT, &GPIO_InitStruct);  

////		SPI3_CS(Hi);
//		spi3_set_speed(SPI_SPEED_2);	
//  /* USER CODE END SPI2_MspInit 1 */
//  }
			
}

/**
 * @brief       SPI2速度设置函数
 *   @note      SPI2时钟选择来自APB1, 即PCLK1, 为36Mhz
 *              SPI速度 = PCLK1 / 2^(speed + 1)
 * @param       speed   : SPI2时钟分频系数
                        取值为SPI_BAUDRATEPRESCALER_2~SPI_BAUDRATEPRESCALER_2 256
 * @retval      无
 */
void spi1_set_speed(uint8_t speed)
{
    assert_param(IS_SPI_BAUDRATE_PRESCALER(speed)); /* 判断有效性 */
    __HAL_SPI_DISABLE(&hspi1);             /* 关闭SPI */
    hspi1.Instance->CR1 &= 0XFFC7;         /* 位3-5清零，用来设置波特率 */
    hspi1.Instance->CR1 |= speed << 3;     /* 设置SPI速度 */
    __HAL_SPI_ENABLE(&hspi1);              /* 使能SPI */
	
	
}

void spi2_set_speed(uint8_t speed)
{

    assert_param(IS_SPI_BAUDRATE_PRESCALER(speed)); /* 判断有效性 */
    __HAL_SPI_DISABLE(&hspi2);             /* 关闭SPI */
    hspi2.Instance->CR1 &= 0XFFC7;         /* 位3-5清零，用来设置波特率 */
    hspi2.Instance->CR1 |= speed << 3;     /* 设置SPI速度 */
    __HAL_SPI_ENABLE(&hspi2);              /* 使能SPI */			
	
}

void spi3_set_speed(uint8_t speed)
{
    assert_param(IS_SPI_BAUDRATE_PRESCALER(speed)); /* 判断有效性 */
    __HAL_SPI_DISABLE(&hspi3);             /* 关闭SPI */
    hspi3.Instance->CR1 &= 0XFFC7;         /* 位3-5清零，用来设置波特率 */
    hspi3.Instance->CR1 |= speed << 3;     /* 设置SPI速度 */
    __HAL_SPI_ENABLE(&hspi3);              /* 使能SPI */		
	
}
/**
 * @brief       SPI2读写一个字节数据
 * @param       txdata  : 要发送的数据(1字节)
 * @retval      接收到的数据(1字节)
 */
#if(SPI_3)	
uint8_t spi3_read_write_byte(uint8_t txdata)
{
    uint8_t rxdata;
    HAL_SPI_TransmitReceive(&hspi3, &txdata, &rxdata, 1, 1000);
    return rxdata; /* 返回收到的数据 */
}
#endif

#if(SPI_2)	
uint8_t spi2_read_write_byte(uint8_t txdata)
{
    uint8_t rxdata;
    HAL_SPI_TransmitReceive(&hspi2, &txdata, &rxdata, 1, 1000);
    return rxdata; /* 返回收到的数据 */
}
#endif

#if(SPI_1)	
uint8_t spi1_read_write_byte(uint8_t txdata)
{
    uint8_t rxdata;
    HAL_SPI_TransmitReceive(&hspi1, &txdata, &rxdata, 1, 1000);
    return rxdata; /* 返回收到的数据 */
}
#endif

#endif

#if(Soft_SPI)	
void MX_SPIx_Init(void)
{		
  GPIO_InitTypeDef GPIO_InitStruct;
#if(Soft_SPI_1)	
	SPI2_SCK_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI2_SCK_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI2_SCK_GPIO_PORT, &GPIO_InitStruct);   

	SPI2_MISO_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI2_MISO_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;      
	HAL_GPIO_Init(SPI2_MISO_GPIO_PORT, &GPIO_InitStruct);    

	SPI2_MOSI_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI2_MOSI_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI2_MOSI_GPIO_PORT, &GPIO_InitStruct);    
	
	SPI2_CS_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI2_CS_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI2_CS_GPIO_PORT, &GPIO_InitStruct);   
	SPI2_CS(Hi);
#endif	
	SPI3_SCK_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI3_SCK_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI3_SCK_GPIO_PORT, &GPIO_InitStruct);   

	SPI3_MISO_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI3_MISO_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;        
	HAL_GPIO_Init(SPI3_MISO_GPIO_PORT, &GPIO_InitStruct);    

	SPI3_MOSI_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI3_MOSI_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI3_MOSI_GPIO_PORT, &GPIO_InitStruct);    
	
	SPI3_CS_GPIO_CLK_ENABLE();
	GPIO_InitStruct.Pin = SPI3_CS_GPIO_PIN;              
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       
	GPIO_InitStruct.Pull = GPIO_PULLUP;               
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      
	HAL_GPIO_Init(SPI3_CS_GPIO_PORT, &GPIO_InitStruct);  


//	SPI3_CS(Hi);
	
}

#if(Soft_SPI_1)	
void simulate_spi2_write_byte(u8 data)
{
     u8 kk;
		 SPI2_SCK(Lo);
     SIMULATE_DELAY_US;    
     SIMULATE_DELAY_US;
	   SIMULATE_DELAY_US;
//		delay_us(3);
    for(kk=0;kk<8;kk++)
    {
    if((data&0x80)==0x80) SPI2_MOSI(Hi);
    else         SPI2_MOSI(Lo);
			
    SIMULATE_DELAY_US; 
//	  delay_us(1);			
    SPI2_SCK(Hi);
    SIMULATE_DELAY_US;
//		delay_us(1);	
    SPI2_SCK(Lo); 
    data = data<<1;
    }
 
}
 
uint8_t simulate_spi2_read_byte(void)
{
    uint8_t kk=0, ret=0;
    SPI2_SCK(Lo);
	  SIMULATE_DELAY_US;  
	  SIMULATE_DELAY_US;
	  SIMULATE_DELAY_US;
 
    for(kk=0;kk<8;kk++)
    {
	  ret = ret<<1; 
    SIMULATE_DELAY_US;
    SPI2_SCK(Hi);
		SIMULATE_DELAY_US;
    if(SPI2_MISO) ret |= 0x01;
    SPI2_SCK(Lo);
    }
 
    return ret;
}
#endif	

void simulate_spi3_write_byte(uint8_t data)
{
     u8 kk;
		 SPI3_SCK(Lo);
     SIMULATE_DELAY_US;    
     SIMULATE_DELAY_US;
	   SIMULATE_DELAY_US;
//		delay_us(3);
    for(kk=0;kk<8;kk++)
    {
    if((data&0x80)==0x80) 
		{
			SPI3_MOSI(Hi);
		}
    else
		{
			SPI3_MOSI(Lo);
		}			
			
    SIMULATE_DELAY_US; 
//	  delay_us(1);			
    SPI3_SCK(Hi);
    SIMULATE_DELAY_US;
//		delay_us(1);	
    SPI3_SCK(Lo); 
    data = data<<1;
    }
 
}
 
u8 simulate_spi3_read_byte(void)
{
    u8 kk=0, ret=0;
    SPI3_SCK(Lo);
	  SIMULATE_DELAY_US;  
	  SIMULATE_DELAY_US;
	  SIMULATE_DELAY_US;
 
    for(kk=0;kk<8;kk++)
    {
	  ret = ret<<1; 
    SIMULATE_DELAY_US;
    SPI3_SCK(Hi);
		SIMULATE_DELAY_US;
    if(SPI3_MISO) ret |= 0x01;
    SPI3_SCK(Lo);
    }
 
    return ret;
}

#endif
