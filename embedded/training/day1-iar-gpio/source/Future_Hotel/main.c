#include <ioCC2530.h>

#define LED1 P1_0
#define LED2 P1_4
#define LED3 P1_1

typedef unsigned char uint8;             //数据类型定义
typedef unsigned short int  uint16;

void DelayMS(uint16 msec)
{
    uint16 i,j;
	for (i=0; i<msec; i++)
		for (j=0; j<536; j++);     //时间轴测量脉冲时间
}

/* 函数功能：初始化Led的引脚
 * 函数名字：Led_Init
 * 函数参数：无
 * 函数返回值：无
*/
void Led_Init(void)
{
//  P1SEL &= ~(1<<0);
//  P1DIR |= (1<<0);
//  
//  P1SEL &= ~(1<<1);
//  P1DIR |= (1<<1);
//  
//  P1SEL &= ~(1<<4);
//  P1DIR |= (1<<4);
  P1SEL &= ~0x13;
  P1DIR |= 0x13;
}

int main(void)
{
  Led_Init();   //Led的配置函数
  LED1 = 0;     //点亮Led1
  LED2 = 0;     //点亮Led2
  LED3 = 0;     //点亮Led3
  
  while(1)
  {
    LED1 ^= 1;  //状态取反 --- 亮边灭，灭变亮
    DelayMS(500);
    LED1 ^= 1;  //状态取反
    LED2 ^= 1;
    DelayMS(500);
    LED2 ^= 1;  //状态取反
    LED3 ^= 1;
    DelayMS(500);
    LED3 ^= 1;
    DelayMS(500);
  }
//  while(1)
//  {
//    LED1 = 1;     //点亮Led1
//    LED2 = 0;     
//    LED3 = 0;     
//    DelayMS(500);
//    LED1 = 0;     
//    LED2 = 1;     //点亮Led2
//    LED3 = 0;     
//    DelayMS(500);
//    LED1 = 0;     
//    LED2 = 0;     
//    LED3 = 1;     //点亮Led3
//    DelayMS(500);
//    LED1 = 0;     
//    LED2 = 0;     
//    LED3 = 0;     
//    DelayMS(500);
//  }
  return 0;
}
