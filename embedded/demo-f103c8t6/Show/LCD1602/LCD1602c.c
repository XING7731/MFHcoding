/**
  ******************************************************************************
  * 文件名程: Soft_I2C.c
  * 作    者:
  * 版    本: V1.0
  * 编写日期: 2015-10-04
  * 功    能: i2c驱动程序（软件）
  ******************************************************************************
  */
/* 包含头文件 ----------------------------------------------------------------*/

#include "main.h"
#include "lcd1602b.h"
/* 私有类型定义 --------------------------------------------------------------*/
/* 私有宏定义 ----------------------------------------------------------------*/


/* 私有变量 ------------------------------------------------------------------*/

/* 扩展变量 ------------------------------------------------------------------*/
/* 私有函数原形 --------------------------------------------------------------*/
/* 函数体 --------------------------------------------------------------------*/
/**
  * 函数功能: IO引脚初始化.
  * 输入参数: 无
  * 返 回 值: 无
  * 说    明：使用宏定义方法代替具体引脚号，方便程序移植，只要简单修改.h
  *           文件相关宏定义就可以方便修改引脚。
  */

//I2C//////////////////////////////////////////////////////////////////////////////
unsigned char LCD_data=0x08;
//IIC初始化

void SDA_OUT(void)
{  
  	GPIO_InitTypeDef GPIO_InitStruct;
		SIIC_SDA_RCC_CLK_ENABLE();
		GPIO_InitStruct.Pin = SIIC_SDA_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(SIIC_SCL_GPIO, &GPIO_InitStruct);
}	

void SDA_IN(void)	
{	
		GPIO_InitTypeDef GPIO_InitStruct;
		SIIC_SDA_RCC_CLK_ENABLE();
		GPIO_InitStruct.Pin = SIIC_SDA_GPIO_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(SIIC_SCL_GPIO, &GPIO_InitStruct);
}

void IIC_Init(void)
{
	GPIO_InitTypeDef GPIO_Initure;
	SIIC_SCL_RCC_CLK_ENABLE();
	SIIC_SDA_RCC_CLK_ENABLE();

	GPIO_Initure.Pin=SIIC_SCL_GPIO_PIN|SIIC_SDA_GPIO_PIN;
	GPIO_Initure.Mode=GPIO_MODE_OUTPUT_OD;  //推挽输出
	GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
	GPIO_Initure.Speed=GPIO_SPEED_FREQ_HIGH;     //快速
	HAL_GPIO_Init(SIIC_SCL_GPIO,&GPIO_Initure);

	IIC_SDA(1);
	IIC_SCL(1);
}

//产生IIC起始信号
void IIC_Start(void)
{
	SDA_OUT();     //sda线输出
	IIC_SDA(1);
	IIC_SCL(1);
	delay_us(4);
	IIC_SDA(0);//START:when CLK is high,DATA change form high to low
	delay_us(4);
	IIC_SCL(0);//钳住I2C总线，准备发送或接收数据
}
//产生IIC停止信号
void IIC_Stop(void)
{
	SDA_OUT();//sda线输出
	IIC_SCL(0);
	IIC_SDA(0);//STOP:when CLK is high DATA change form low to high
	delay_us(4);
	IIC_SCL(1);
	delay_us(4);
	IIC_SDA(1);//发送I2C总线结束信号
}
//等待应答信号到来
//返回值：1，接收应答失败
//        0，接收应答成功
u8 IIC_Wait_Ack(void)
{
	u8 ucErrTime=0;
	SDA_IN();      //SDA设置为输入
	IIC_SDA(1);
	delay_us(1);
	IIC_SCL(1);
	delay_us(1);
	while(READ_SDA)
	{
		ucErrTime++;
		if(ucErrTime>250)
		{
			IIC_Stop();
			return 1;
		}
	}
	IIC_SCL(0);//时钟输出0
	return 0;
}
//产生ACK应答
void IIC_Ack(void)
{
	IIC_SCL(0);
	SDA_OUT();
	IIC_SDA(0);
	delay_us(2);
	IIC_SCL(1);
	delay_us(2);
	IIC_SCL(0);
}
//不产生ACK应答
void IIC_NAck(void)
{
	IIC_SCL(0);
	SDA_OUT();
	IIC_SDA(1);
	delay_us(2);
	IIC_SCL(1);
	delay_us(2);
	IIC_SCL(0);
}
//IIC发送一个字节
//返回从机有无应答
//1，有应答
//0，无应答
void IIC_Send_Byte(u8 txd)
{
	u8 t;
	SDA_OUT();
	IIC_SCL(0);//拉低时钟开始数据传输
	for(t=0; t<8; t++)
	{
		IIC_SDA((txd&0x80)>>7);
		txd<<=1;
		delay_us(2);   //对TEA5767这三个延时都是必须的
		IIC_SCL(1);
		delay_us(2);
		IIC_SCL(0);
		delay_us(2);
	}
}
//读1个字节，ack=1时，发送ACK，ack=0，发送nACK
u8 IIC_Read_Byte(unsigned char ack)
{
	unsigned char i,receive=0;
	SDA_IN();//SDA设置为输入
	for(i=0; i<8; i++ )
	{
		IIC_SCL(0);
		delay_us(2);
		IIC_SCL(1);
		receive<<=1;
		if(READ_SDA)receive++;
		delay_us(1);
	}
	if (!ack)
		IIC_NAck();//发送nACK
	else
		IIC_Ack(); //发送ACK
	return receive;
}


//END I2C//////////////////////////////////////////////////////////////////////////////


void Enable_LCD_write()
{
	LCD_data|=(1<<(3-1));//E=1;
	PCF8574_WriteOneByte(LCD_data);
	LCD_data&=~(1<<(3-1));//E=0;
	PCF8574_WriteOneByte(LCD_data);
}


void LCD_Write_Com(unsigned char command) 
{ 
	delay_us(16);
	LCD_data&=~(1<<(1-1));//RS=0;
	LCD_data&=~(1<<(2-1));//RW=0;
	//LCD_data&=~(1<<(4-1));
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	
	LCD_data&=0X0f; //????
	LCD_data|=command & 0xf0; //????
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	Enable_LCD_write();
	
	command=command<<4; //????????
	LCD_data&=0x0f; //????
	LCD_data|=command&0xf0; //????
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	delay_us(2);
	Enable_LCD_write();
}

void LCD_Write_Data(unsigned char value) 
{ 
	delay_us(16);
	LCD_data|=(1<<(1-1));//RS=1;
	LCD_data&=~(1<<(2-1));//RW=0;
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	
	LCD_data&=0X0f; //????
	LCD_data|=value&0xf0; //????
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	delay_us(2);
	Enable_LCD_write();         
	
	value=value<<4; //????????
	LCD_data&=0x0f; //????
	LCD_data|=value&0xf0; //????
	delay_us(2);
	PCF8574_WriteOneByte(LCD_data);
	delay_us(2);
	Enable_LCD_write();
}

void LCD1602test_Init(void) 
{		
	while(PCF8574_Init(PCF8574_ADDR))		//检测不到PCF8574
	PCF8574_WriteOneByte(0x08);//默认关闭使能端，选择状态为写
	LCD_Write_Com(0x0F);
	delay_us(40);
	LCD_Write_Com(0x28); //4???!!!!!!!!!!!!!!!!!!
	PCF8574_WriteOneByte(0x0c); //
	PCF8574_WriteOneByte(0x08); //使能4线
	LCD_Write_Com(0x28);//显示模式设置4线
	LCD_Write_Com(0x01);//清屏
	LCD_Write_Com(0x0C);//功能设置0x0F为开光标，显示光标，光标闪烁，0x0C为开显示，无光标，无闪烁
	LCD_Write_Com(0x06);
	delay_ms(10);
}
void LCD_Write_String(unsigned char x,unsigned char y,unsigned char *s) 
{ if(x == 0) x +=1;
	if ((y == 1)| (y == 0))
	{     
		LCD_Write_Com(0x80 + x-1);     //
	}
	else if (y == 2) 
	{      
		LCD_Write_Com(0xC0 + x-1);      //
	}    
	else if (y == 3) 
	{      
		LCD_Write_Com(0x94 + x-1);      //
	}  
	else if (y == 4) 
	{      
		LCD_Write_Com(0xD4 + x-1);      //
	}  	
	while (*s) 
	{     
		LCD_Write_Data( *s);     
		s ++;     
	}
}

//LcdCurMove(8,4,1);	//光标移动到第4行，第8个字位置，闪动。
void LcdCurMove(uint8_t x,uint8_t y,uint8_t blink_en)
{  if(x == 0) x +=1;
	if ((y == 1)| (y == 0))
	{     
		LCD_Write_Com(0x80 + x-1);     //
	}
	else if (y == 2) 
	{      
		LCD_Write_Com(0xC0 + x-1);      //
	}    
	else if (y == 3) 
	{      
		LCD_Write_Com(0x94 + x-1);      //
	}  
	else if (y == 4) 
	{      
		LCD_Write_Com(0xD4 + x-1);      //
	}  	
	if(blink_en) LCD_Write_Com(0x0F);
	else LCD_Write_Com(0x0C);
} 


//初始化PCF8574
u8 PCF8574_Init(u8 addr)
{
	u8 temp=0;
	
//    GPIO_InitTypeDef GPIO_Initure;
//    __HAL_RCC_GPIOB_CLK_ENABLE();           //使能GPIOB时钟
//	
//    GPIO_Initure.Pin=GPIO_PIN_12;           //PB12
//    GPIO_Initure.Mode=GPIO_MODE_INPUT;      //输入
//    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
//    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
//    HAL_GPIO_Init(GPIOB,&GPIO_Initure);     //初始化
	
	IIC_Init();					            //IIC初始化 	
	//检查PCF8574是否在位
	IIC_Start();    
	IIC_Send_Byte(addr);            //写地址			   
	temp=IIC_Wait_Ack();		            //等待应答,通过判断是否有ACK应答,来判断PCF8574的状态
	IIC_Stop();					            //产生一个停止条件
	PCF8574_WriteOneByte(0XFF);	            //默认情况下所有IO输出高电平	  
	
	return temp;
}


//读取PCF8574的8位IO值
//返回值:读到的数据
u8 PCF8574_ReadOneByte(void)
{				  
	u8 temp=0;		  	    																 
	IIC_Start();    	 	   
	IIC_Send_Byte(PCF8574_ADDR|0X01);   //进入接收模式			   
	IIC_Wait_Ack();	 
	temp=IIC_Read_Byte(0);		   
	IIC_Stop();							//产生一个停止条件	    
	return temp;
}
//向PCF8574写入8位IO值  
//DataToWrite:要写入的数据
void PCF8574_WriteOneByte(u8 DataToWrite)
{				   	  	    																 
	IIC_Start();  
	IIC_Send_Byte(PCF8574_ADDR);   //发送器件地址0X4e,写数据 	 
	IIC_Wait_Ack();	    										  		   
	IIC_Send_Byte(DataToWrite);    	 	//发送字节	
	IIC_Wait_Ack();   
	IIC_Stop();							//产生一个停止条件 
	HAL_Delay(1);	 
}

//设置PCF8574某个IO的高低电平
//bit:要设置的IO编号,0~7
//sta:IO的状态;0或1
void PCF8574_WriteBit(u8 bit,u8 sta)
{
	u8 data;
	data=PCF8574_ReadOneByte(); //先读出原来的设置
	if(sta==0)data&=~(1<<bit);     
	else data|=1<<bit;
	PCF8574_WriteOneByte(data); //写入新的数据
}

//读取PCF8574的某个IO的值
//bit：要读取的IO编号,0~7
//返回值:此IO的值,0或1
u8 PCF8574_ReadBit(u8 bit)
{
	u8 data;
	data=PCF8574_ReadOneByte(); //先读取这个8位IO的值 
	if(data&(1<<bit))return 1;
	else return 0;   
}  


