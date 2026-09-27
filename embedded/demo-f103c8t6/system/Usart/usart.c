#include "main.h"
#include "usart.h"
#include <stdarg.h>

#define UARTx_Handler UART1_Handler
#define GPSRec	  0              //GPS接收函数声明
#define as608 		0
#define air780Rec 0
#define esp32Rec  0
#define esp8266 		1
#define hc05    		0
extern TIM_HandleTypeDef htim1,htim2,htim3,htim4;

//extern _SaveData Save_Data;//GPS寄存器声明

u16 UART1_Rx_cnt=0,UART2_Rx_cnt=0,UART3_Rx_cnt=0; //角标
UART_HandleTypeDef UART1_Handler,UART2_Handler,UART3_Handler;

#define RXBUFFERSIZE   1 //缓存大小
u8 flag = 0;
u8 USART1Buf[RXBUFFERSIZE];//HAL库USART接收Buffer
u8 USART2Buf[RXBUFFERSIZE];//HAL库USART接收Buffer
u8 USART3Buf[RXBUFFERSIZE];//HAL库USART接收Buffer

u8 U1Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
u8 U2Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
u8 U3Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
u8 JWBuffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
u8 Send_answer = 0;
uint16_t co2 = 0;
uint16_t CO2 = 0;
u8 UART3_Rx_Do;


//初始化IO 串口1 
//bound:波特率
void UART_Setinit(uint32_t bound1,uint32_t bound2,uint32_t bound3)
{	
	//UART 初始化设置
#if(Enable_UART1)	
    UART1_Handler.Instance = USART1;
    UART1_Handler.Init.BaudRate = bound1;                   //波特率
    UART1_Handler.Init.WordLength = UART_WORDLENGTH_8B;     //字长8位
    UART1_Handler.Init.StopBits = UART_STOPBITS_1;          //停止位1  
    UART1_Handler.Init.Parity = UART_PARITY_NONE;           //无奇偶校验
    UART1_Handler.Init.Mode = UART_MODE_TX_RX;              //收发模式
    UART1_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE;     //无硬件流 
//	HAL_UART_DeInit(&UART1_Handler);
    HAL_UART_Init(&UART1_Handler);
	__HAL_UART_ENABLE_IT(&UART1_Handler,UART_IT_RXNE);
	
    HAL_UART_Receive_IT(&UART1_Handler,(u8 *)USART1Buf, 1);
#endif	
	
#if(Enable_UART2)		
		UART2_Handler.Instance = USART2;
		UART2_Handler.Init.BaudRate = bound2;                   
		UART2_Handler.Init.WordLength = UART_WORDLENGTH_8B;     
		UART2_Handler.Init.StopBits = UART_STOPBITS_1;           
		UART2_Handler.Init.Parity = UART_PARITY_NONE;           
		UART2_Handler.Init.Mode = UART_MODE_TX_RX;              
		UART2_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE;      
//  HAL_UART_DeInit(&UART2_Handler);
		HAL_UART_Init(&UART2_Handler);
	__HAL_UART_ENABLE_IT(&UART2_Handler,UART_IT_RXNE);

		HAL_UART_Receive_IT(&UART2_Handler, (u8 *)USART2Buf, 1);
#endif	

#if(Enable_UART3)	
		UART3_Handler.Instance = USART3;
		UART3_Handler.Init.BaudRate = bound3;                   
		UART3_Handler.Init.WordLength = UART_WORDLENGTH_8B;     
		UART3_Handler.Init.StopBits = UART_STOPBITS_1;            
		UART3_Handler.Init.Parity = UART_PARITY_NONE;           
		UART3_Handler.Init.Mode = UART_MODE_TX_RX;              
		UART3_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE;     
//	HAL_UART_DeInit(&UART3_Handler);
		HAL_UART_Init(&UART3_Handler);
	__HAL_UART_ENABLE_IT(&UART3_Handler,UART_IT_RXNE);
	
		HAL_UART_Receive_IT(&UART3_Handler, (u8 *)USART3Buf, 1);
#endif


}



void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    //GPIO端口设置
	GPIO_InitTypeDef GPIO_Initure;
#if(Enable_UART1)		
	if(huart->Instance==USART1)//USB
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();						//使能GPIOA时钟
		__HAL_RCC_USART1_CLK_ENABLE();					//使能USART1时钟
	
		GPIO_Initure.Pin=GPIO_PIN_9;						//PA9
		GPIO_Initure.Mode=GPIO_MODE_AF_PP;			//复用推挽输出
		GPIO_Initure.Pull=GPIO_PULLUP;					//上拉
		GPIO_Initure.Speed=GPIO_SPEED_FREQ_HIGH;//高速
		HAL_GPIO_Init(GPIOA,&GPIO_Initure);	   	//初始化PA9

		GPIO_Initure.Pin=GPIO_PIN_10;						//PA10
		GPIO_Initure.Mode=GPIO_MODE_AF_INPUT;			//PA10                     
		HAL_GPIO_Init(GPIOA,&GPIO_Initure);	   	//初始化PA10
		

		HAL_NVIC_EnableIRQ(USART1_IRQn);				//使能USART1中断通道
		HAL_NVIC_SetPriority(USART1_IRQn,3,3);	//抢占优先级3，子优先级3
	}	
#endif		
	
#if(Enable_UART2)	
		if(huart->Instance==USART2)								//SYS
	{
		__HAL_RCC_GPIOA_CLK_ENABLE();							//使能GPIOA时钟
		__HAL_RCC_USART2_CLK_ENABLE();						//使能USART2时钟
	
		GPIO_Initure.Pin=GPIO_PIN_2;							//PA2
		GPIO_Initure.Mode=GPIO_MODE_AF_PP;				//复用推挽输出
		GPIO_Initure.Pull=GPIO_PULLUP;						//上拉
		GPIO_Initure.Speed=GPIO_SPEED_FREQ_HIGH;	//高速
		HAL_GPIO_Init(GPIOA,&GPIO_Initure);	   		//初始化PA2

		GPIO_Initure.Pin=GPIO_PIN_3;							//PA3
		GPIO_Initure.Mode=GPIO_MODE_AF_INPUT;			//PA3
		HAL_GPIO_Init(GPIOA,&GPIO_Initure);	   		//初始化PA3
		

		HAL_NVIC_EnableIRQ(USART2_IRQn);					//使能USART1中断通道
		HAL_NVIC_SetPriority(USART2_IRQn,3,2);		//抢占优先级3，子优先级3
	}
#endif		
	
#if(Enable_UART3)		
		if(huart->Instance==USART3)								//WIFI
	{
		__HAL_RCC_GPIOB_CLK_ENABLE();							//使能GPIOA时钟
		__HAL_RCC_USART3_CLK_ENABLE();						//使能USART1时钟
	
		GPIO_Initure.Pin=GPIO_PIN_10;							//PB10
		GPIO_Initure.Mode=GPIO_MODE_AF_PP;				//复用推挽输出
		GPIO_Initure.Pull=GPIO_PULLUP;						//上拉
		GPIO_Initure.Speed=GPIO_SPEED_FREQ_HIGH;	//高速
		HAL_GPIO_Init(GPIOB,&GPIO_Initure);	   		//初始化PB10

		GPIO_Initure.Pin=GPIO_PIN_11;							//PB11
		GPIO_Initure.Mode = GPIO_MODE_INPUT; 			// 浮空输入
		HAL_GPIO_Init(GPIOB,&GPIO_Initure);	   		//初始化PB11
		

		HAL_NVIC_EnableIRQ(USART3_IRQn);					//使能USART1中断通道
		HAL_NVIC_SetPriority(USART3_IRQn,3,1);		//抢占优先级3，子优先级3
	}
#endif		
	
}


 
//串口1中断服务程序
//网口
void USART1_IRQHandler(void)                	
{ 
	HAL_UART_IRQHandler(&UART1_Handler);	
} 
//uart
void USART2_IRQHandler(void)      	
{ 	
	HAL_UART_IRQHandler(&UART2_Handler);		
} 
//485
void USART3_IRQHandler(void)                	
{ 
	HAL_UART_IRQHandler(&UART3_Handler);
} 


void USART1_send(u8* buf,u8 len)
{
	HAL_UART_Transmit(&UART1_Handler,buf,len,0xffff);
}	

void USART2_send(u8* buf,u8 len)
{
	HAL_UART_Transmit(&UART2_Handler,buf,len,0xffff);
}

void USART3_send(u8* buf,u8 len)
{
	HAL_UART_Transmit(&UART3_Handler,buf,len,0xffff);
}

#define FRAME_HEADER      0x9C
#define FRAME_MIN_LENGTH  14      // FF 17 04 00 00 0E 13 88 3C 共9字节
#define UART1_BUFFER_SIZE 128    // 适当增大缓冲区

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	
#if(esp32Rec)	
//esp32主函数接收中断程序			
	if(huart->Instance==USART3)
	{
			if (UART3_Rx_cnt < USART_REC_LEN - 1) 
				{
        U3Buffer[UART3_Rx_cnt] = *USART3Buf;
        UART3_Rx_cnt++;
        
        // 检测结束符（根据协议自定义，例如换行符）
        if (*USART3Buf == '\n') 
				{
            U3Buffer[UART3_Rx_cnt] = '\0';  // 添加字符串终止符
            UART3_Rx_Do = 1;        // 标记接收完成
        }
				} else {
        // 缓冲区溢出时重置
        UART3_Rx_cnt = 0;
    }
    
    HAL_UART_Receive_IT(&UART3_Handler, USART3Buf, 1);
  }
#endif	
	
	
#if(air780Rec)	
//air780主函数接收中断程序			
	if(huart->Instance==USART2)
	{
			if(UART2_Rx_cnt<1024)		//还可以接收数据
		{
			U2Buffer[UART2_Rx_cnt]=*USART2Buf;		//记录接收到的值	
			++UART2_Rx_cnt;			
		} 
		HAL_UART_Receive_IT(&UART2_Handler,(u8 *)USART2Buf,1);	
  }
#endif


#if(ZigebRec)
Zigeb主函数接收中断程序
        uint8_t receivedByte = *USART1Buf;
        
        // 检查帧头
        if(UART1_Rx_cnt == 0 && receivedByte != FRAME_HEADER)
        {
            HAL_UART_Receive_IT(&UART1_Handler, (u8 *)USART1Buf, 1);
            return;
        }
        
        // 存储接收到的字节
        U1Buffer[UART1_Rx_cnt] = receivedByte;
        UART1_Rx_cnt++;
        
        // 检查是否收到完整帧
        if(UART1_Rx_cnt >= FRAME_MIN_LENGTH)
        {
            UR1_flg = 1;
            UART1_Rx_cnt = 0;
        }
        else if(UART1_Rx_cnt >= UART1_BUFFER_SIZE)
        {
            UART1_Rx_cnt = 0;
        }
        
        // 重新启动接收中断
        HAL_UART_Receive_IT(&UART1_Handler, (u8 *)USART1Buf, 1);
    }
#endif
#if(as608)
	if(huart->Instance==USART1)                	//串口1中断服务程序
{
		U1Buffer[UART1_Rx_cnt]=*USART1Buf;
		++UART1_Rx_cnt;
		if(UART1_Rx_cnt>255)UART1_Rx_cnt=0;
		HAL_UART_Receive_IT(&UART1_Handler,(u8 *)USART1Buf,1);		
} 
#endif
//	if(huart->Instance==USART1)                	//串口1中断服务程序
//{
//		U1Buffer[UART1_Rx_cnt]=*USART1Buf;
//		++UART1_Rx_cnt;
//		if(UART1_Rx_cnt>255)UART1_Rx_cnt=0;
//		HAL_UART_Receive_IT(&UART1_Handler,(u8 *)USART1Buf,1);		
//} 

#if(esp8266)
	if(huart->Instance==USART1)                	//串口1中断服务程序
{
		U1Buffer[UART1_Rx_cnt]=*USART1Buf;
		++UART1_Rx_cnt;
		if(UART1_Rx_cnt>255)UART1_Rx_cnt=0;
		HAL_UART_Receive_IT(&UART1_Handler,(u8 *)USART1Buf,1);		
} 
#endif
#if(hc05)
	if(huart->Instance==USART1)                	//串口1中断服务程序
{
		U1Buffer[UART1_Rx_cnt]=*USART1Buf;
		++UART1_Rx_cnt;
		if(UART1_Rx_cnt>255)UART1_Rx_cnt=0;
		HAL_UART_Receive_IT(&UART1_Handler,(u8 *)USART1Buf,1);		
} 
#endif
#if(JW01)
	if(huart->Instance==USART3)                	//串口1中断服务程序
{
	 JWBuffer[UART3_Rx_cnt]=*USART3Buf;
	 U3Buffer[UART3_Rx_cnt]=*USART3Buf;
	if(U3Buffer[UART3_Rx_cnt]==0x2C)
	{
		flag = 1;	
	}
		if(flag)
		{
			++UART3_Rx_cnt;
			if(UART3_Rx_cnt >= 6)
			{
				UART3_Rx_cnt = 0;
				flag = 0;
			}
		}	
		CO2 = (unsigned int)(JWBuffer[1] * 256 + JWBuffer[2]);	
		HAL_UART_Receive_IT(&UART3_Handler,(u8 *)USART3Buf,1);
	
} 
#endif

#if(GPSRec)
if(huart->Instance == USART1)
    {
        uint8_t Res = *USART1Buf; // 获取接收到的数据
        
        // 处理接收到的数据
        if(Res == '$')
        {
            UART1_Rx_cnt = 0;
        }
        
        U1Buffer[UART1_Rx_cnt++] = Res;
        
        // 检查是否收到特定帧头
        if(U1Buffer[0] == '$' && U1Buffer[4] == 'M' && U1Buffer[5] == 'C')
        {
            if(Res == '\n') // 帧结束
            {
                memset(Save_Data.GPS_Buffer, 0, GPS_Buffer_Length); // 清空
                memcpy(Save_Data.GPS_Buffer, U1Buffer, UART1_Rx_cnt); // 保存数据
                Save_Data.isGetData = true;
                UART1_Rx_cnt = 0;
                memset(U1Buffer, 0, sizeof(U1Buffer)); // 清空缓冲区
            }
        }
        
        // 防止缓冲区溢出
        if(UART1_Rx_cnt >= sizeof(U1Buffer))
        {
            UART1_Rx_cnt = 0; // 或者可以设置为sizeof(U1Buffer)-1以保留最后一个位置
        }
        
        // 重新启动接收
        HAL_UART_Receive_IT(&UART1_Handler, USART1Buf, 1);
    }
#endif		


	
//	if(huart->Instance==USART2)
//	{
//	 if(0 == UART2_Rx_cnt)
//	   {
//		   __HAL_TIM_CLEAR_FLAG(&htim3,TIM_FLAG_UPDATE);
//		   HAL_TIM_Base_Start_IT(&htim3);//开启定时器
//	   }
//			__HAL_TIM_SET_COUNTER(&htim3,0);//清空计数
//		 
//		U2Buffer[UART2_Rx_cnt++]=*USART2Buf;

//		if(UART2_Rx_cnt>99)UART2_Rx_cnt=0;		 
//		HAL_UART_Receive_IT(&UART2_Handler,(u8 *)USART2Buf,1);

//	}
	
	
//	if(huart->Instance==USART3)
//	{
//	 if(0 == UART3_Rx_cnt)
//	   {
//		   __HAL_TIM_CLEAR_FLAG(&htim4,TIM_FLAG_UPDATE);
//		   HAL_TIM_Base_Start_IT(&htim4);//开启定时器
//	   }
//			__HAL_TIM_SET_COUNTER(&htim4,0);//清空计数
//		
//		U3Buffer[UART3_Rx_cnt]=*USART3Buf;
//		++UART3_Rx_cnt;
//		if(UART3_Rx_cnt>99)UART3_Rx_cnt=0;
//		HAL_UART_Receive_IT(&UART3_Handler,(u8 *)USART3Buf,1);
//	}	
}
 
//指纹bound=57600
//定位bound=9600
//蓝牙bound=9600
//短信bound=115200
//wifibound=115200
void Uart_Init(void)
{
	UART_Setinit(115200,115200,9600);
}





void send_message(void)
{
#if(English_Display)	
	  Serial_PutString("\r\n======================================================================");
		Serial_PutString("\r\n=           			     (C) COPYRIGHT 2023                						 =");
		Serial_PutString("\r\n=                                                                    =");
		Serial_PutString("\r\n=  STM32L0xx In-Application Programming Application  (Version 1.0.0) =");
		Serial_PutString("\r\n=                                                                    =");
		Serial_PutString("\r\n=                   By Eirc_lee Application                          =");
		Serial_PutString("\r\n======================================================================");
		Serial_PutString("\r\n=               Successfully run the APP program           					 =");
		Serial_PutString("\r\n=                                                                    =");
		Serial_PutString("\r\n======================================================================");
		Serial_PutString("\r\n\r\n");
#else	
//	Serial_PutString("\r\n======================================================================");
//	Serial_PutString("\r\n=          				   	 (C) COPYRIGHT 2023                            =");
//	Serial_PutString("\r\n=                                                                    =");
//	Serial_PutString("\r\n=          关于STM32f1xx bootloder主程序程序   (Version 1.0.0.A)  	 =");
//	Serial_PutString("\r\n=                                                                    =");
//	Serial_PutString("\r\n=                   By Eirc_lee Application                          =");
//	Serial_PutString("\r\n======================================================================"); 
//	Serial_PutString("\r\n=                  主程序执行中.............                       	 =");
//	Serial_PutString("\r\n======================================================================");
//	Serial_PutString("\r\n======================================================================");
//	Serial_PutString("\r\n\r\n");
#endif	
}

/**
  * @brief  Print a string on the HyperTerminal
  * @param  p_string: The string to be printed
  * @retval None
  */
//void Serial_PutString(uint8_t *p_string)
//{
//  uint16_t length = 0;

//  while (p_string[length] != '\0')
//  {
//    length++;
//  }
//  HAL_UART_Transmit(&UART1_Handler, p_string, length, TX_TIMEOUT);
//}

/**
 * 优化版字符串打印（带安全检测）
 * @param str 要发送的字符串（以'\0'结尾）
 */
void Serial_PutString(char *str) {
    uint16_t len = 0;
    while (str[len] != '\0' && len < 0xFFFF) len++; // 防止无终止符
    
    if (len > 0) {
        HAL_UART_Transmit(&UARTx_Handler, (void *)str, len, TX_TIMEOUT);
    }
}
#define UART_TX_BUF_SIZE 1024

/**
 * @brief 增强型安全打印
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @note 自动处理缓冲区溢出
 */
void Serial_Printf(const char *fmt, ...)
{
    static char tx_buf[UART_TX_BUF_SIZE];
    va_list args;
    int required_len;
    
    // 1. 预测所需长度
    va_start(args, fmt);
    required_len = vsnprintf(NULL, 0, fmt, args) + 1; // +1 for null-terminator
    va_end(args);
    
    // 2. 动态处理
    if(required_len <= UART_TX_BUF_SIZE) {
        va_start(args, fmt);
        vsnprintf(tx_buf, sizeof(tx_buf), fmt, args);
        va_end(args);
        
        HAL_UART_Transmit(&UARTx_Handler, (uint8_t*)tx_buf, strlen(tx_buf), HAL_MAX_DELAY);
    } else {
        // 使用标准库内存管理
        char *large_buf = malloc(required_len);  // 改用malloc
        if(large_buf) {
            va_start(args, fmt);
            vsnprintf(large_buf, required_len, fmt, args);
            va_end(args);
            
            for(int i=0; i<required_len; i+= UART_TX_BUF_SIZE-1) {
                int chunk_len = (required_len-i) > (UART_TX_BUF_SIZE-1) ? 
                              (UART_TX_BUF_SIZE-1) : (required_len-i);
                HAL_UART_Transmit(&UARTx_Handler, (uint8_t*)&large_buf[i], chunk_len, HAL_MAX_DELAY);
            }
            free(large_buf);  // 改用free
        } else {
            HAL_UART_Transmit(&UARTx_Handler, (uint8_t*)"[MEM_ERR]\r\n", 11, HAL_MAX_DELAY);
        }
    }
}


/**
 * 打印十六进制数据（替代printf("%02X")）
 * @param data 二进制数据数组
 * @param len  数据长度
 */
void Serial_PutHex(uint8_t *data, uint16_t len) {
    const char hexTable[] = "0123456789ABCDEF";
    uint8_t hexBuf[3] = {0}; // 存储单个字节的Hex表示（2字符+结束符）
    
    for (uint16_t i = 0; i < len; i++) {
        // 将字节转为2位十六进制
        hexBuf[0] = hexTable[(data[i] >> 4) & 0x0F];
        hexBuf[1] = hexTable[data[i] & 0x0F];
        
        HAL_UART_Transmit(&UARTx_Handler, hexBuf, 2, TX_TIMEOUT);
        
        // 可选：添加空格分隔（调试更清晰）
        if (i < len - 1) {
            uint8_t space = ' ';
            HAL_UART_Transmit(&UARTx_Handler, &space, 1, TX_TIMEOUT);
        }
    }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */

  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */

  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

