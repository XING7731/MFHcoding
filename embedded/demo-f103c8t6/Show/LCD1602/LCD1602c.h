#ifndef __LCD1602B_H__
#define	__LCD1602B_H__
/* 包含头文件 ----------------------------------------------------------------*/
#include "main.h"

/* 类型定义 ------------------------------------------------------------------*/
/* 宏定义 --------------------------------------------------------------------*/
#define SIIC_SCL_RCC_CLK_ENABLE()         __HAL_RCC_GPIOC_CLK_ENABLE()
#define SIIC_SCL_GPIO_PIN                 GPIO_PIN_13
#define SIIC_SCL_GPIO                     GPIOC

#define SIIC_SDA_RCC_CLK_ENABLE()         __HAL_RCC_GPIOC_CLK_ENABLE()
#define SIIC_SDA_GPIO_PIN                 GPIO_PIN_14
#define SIIC_SDA_GPIO                     GPIOC

#define SIIC_SCL_ON                       HAL_GPIO_WritePin(SIIC_SCL_GPIO,SIIC_SCL_GPIO_PIN,GPIO_PIN_SET)    // 输出高电平
#define SIIC_SCL_OFF                      HAL_GPIO_WritePin(SIIC_SCL_GPIO,SIIC_SCL_GPIO_PIN,GPIO_PIN_RESET)  // 输出低电平

#define SIIC_SDA_ON                       HAL_GPIO_WritePin(SIIC_SDA_GPIO,SIIC_SDA_GPIO_PIN,GPIO_PIN_SET)    // 输出高电平
#define SIIC_SDA_OFF                      HAL_GPIO_WritePin(SIIC_SDA_GPIO,SIIC_SDA_GPIO_PIN,GPIO_PIN_RESET)  // 输出低电平


//IO方向设置
//#define SDA_IN()  {SIIC_SDA_GPIO->MODER&=~(3<<(9*2));SIIC_SDA_GPIO->MODER|=0<<9*2;}	//PB9输入模式
//#define SDA_OUT() {SIIC_SDA_GPIO->MODER&=~(3<<(9*2));SIIC_SDA_GPIO->MODER|=1<<9*2;} //PB9输出模式
//IO操作
#define IIC_SCL(n)  (n?HAL_GPIO_WritePin(SIIC_SCL_GPIO,SIIC_SCL_GPIO_PIN,GPIO_PIN_SET):HAL_GPIO_WritePin(SIIC_SCL_GPIO,SIIC_SCL_GPIO_PIN,GPIO_PIN_RESET)) //SCL
#define IIC_SDA(n)  (n?HAL_GPIO_WritePin(SIIC_SDA_GPIO,SIIC_SDA_GPIO_PIN,GPIO_PIN_SET):HAL_GPIO_WritePin(SIIC_SDA_GPIO,SIIC_SDA_GPIO_PIN,GPIO_PIN_RESET)) //SDA
#define READ_SDA    HAL_GPIO_ReadPin(SIIC_SDA_GPIO,SIIC_SDA_GPIO_PIN)  //输入SDA

//IIC所有操作函数
void IIC_Init(void);                //初始化IIC的IO口				 
void IIC_Start(void);				//发送IIC开始信号
void IIC_Stop(void);	  			//发送IIC停止信号
void IIC_Send_Byte(u8 txd);			//IIC发送一个字节
u8 IIC_Read_Byte(unsigned char ack);//IIC读取一个字节
u8 IIC_Wait_Ack(void); 				//IIC等待ACK信号
void IIC_Ack(void);					//IIC发送ACK信号
void IIC_NAck(void);				//IIC不发送ACK信号

void IIC_Write_One_Byte(u8 daddr,u8 addr,u8 data);
u8 IIC_Read_One_Byte(u8 daddr,u8 addr);	 


///////////PCF8574/////////////////////////////////////////////////////////////////////////////

//#define PCF8574_INT  HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) //PCF8574 INT脚

#define PCF8574_ADDR 	0X4e	//PCF8574地址(左移了一位) //0x7e(PCF8574AT)   0x4e(PCF8574T);0x40;0x27

//PCF8574各个IO的功能
#define RS_IO         0		  //RS
#define RW_IO       1   	//RW
#define CS_IO    2    	//CS
#define BL_IO      3    	//P3,BackLight
#define P4_IO      		  4    	//P4
#define P5_IO      5   	//P5
#define P6_IO     6    	//P6
#define P7_IO    7    	//P7

u8 PCF8574_Init(u8 addr); 
u8 PCF8574_ReadOneByte(void); 
void PCF8574_WriteOneByte(u8 DataToWrite);
void PCF8574_WriteBit(u8 bit,u8 sta);
u8 PCF8574_ReadBit(u8 bit);


void LCD1602test_Init(void);
void LCD_Write_Com(unsigned char command); 
void LCD_Write_Data(unsigned char value);
void LCD_Write_String(unsigned char x,unsigned char y,unsigned char *s) ;
void LcdCurMove(uint8_t x,uint8_t y,uint8_t blink_en);

#endif /* __BSP_I2C_H__ */
/*本代码种适用于PCF8574驱动的LCD1602或2004屏。
1.需另外调用PCF8574.H ,Soft_I2C.h.
2.先初始化。LCD1602_Init();
LCD_Write_String(2,3,"IIC LCD2004 TEST");在第3行，第2个字开始写字符串。
LcdCurMove(2,3,1); 在第3行第2个字上光标闪烁，1：开启。0：关闭。
PCF8574_WriteBit(3,1);控制背光。3：IO口 P3；1：关闭。0：开启
*/
/******************* (C) COPYRIGHT 2015-2020 硬石嵌入式开发团队 *****END OF FILE****/
