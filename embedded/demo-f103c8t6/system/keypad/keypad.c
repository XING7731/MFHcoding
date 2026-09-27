#include "main.h"
#include "keypad.h"

#if(button_Set)


/**
 * @brief       按键初始化函数
 * @param       无
 * @retval      无
 */
void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    KEY0_GPIO_CLK_ENABLE();                                     /* KEY0时钟使能 */
    KEY1_GPIO_CLK_ENABLE();                                     /* KEY1时钟使能 */
    KEY2_GPIO_CLK_ENABLE();                                     /* KEY1时钟使能 */
    KEY3_GPIO_CLK_ENABLE();                                     /* KEY1时钟使能 */
	
	
    GPIO_InitStruct.Pin = KEY0_GPIO_PIN;                       /* KEY0引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(KEY0_GPIO_PORT, &GPIO_InitStruct);           /* KEY0引脚模式设置,上拉输入 */

    GPIO_InitStruct.Pin = KEY1_GPIO_PIN;                       /* KEY1引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(KEY1_GPIO_PORT, &GPIO_InitStruct);           /* KEY1引脚模式设置,上拉输入 */

    GPIO_InitStruct.Pin = KEY2_GPIO_PIN;                       /* KEY1引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(KEY2_GPIO_PORT, &GPIO_InitStruct);           /* KEY1引脚模式设置,上拉输入 */	

    GPIO_InitStruct.Pin = KEY3_GPIO_PIN;                       /* KEY1引脚 */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;                    /* 输入 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;                        /* 上拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
    HAL_GPIO_Init(KEY3_GPIO_PORT, &GPIO_InitStruct);           /* KEY1引脚模式设置,上拉输入 */	
		


}

uint8_t keyNum[4],setn = 0;
uint16_t cardNum = 0,pay1 = 0,pay2 = 0,pay3 = 0,seccost = 1;
uint8_t inSec1,inSec2,inSec3,outSec1,outSec2,outSec3;//出入库时间,秒
uint8_t inMin1,inMin2,inMin3,outMin1,outMin2,outMin3;//出入库时间,分
uint8_t inHour1,inHour2,inHour3,outHour1,outHour2,outHour3;//出入库时间,时
uint8_t car1 = 0,car2 = 0,car3 = 0,space1Flag = 0,space2Flag = 0,space3Flag = 0;
uint8_t Flag_10s = 0;
uint8_t Register = 0;//注册卡标志
uint8_t Delete = 0;//删除卡标志
uint8_t warningFlag = 0,doorFlag = 0;
uint8_t InitDisplay=1;//返回主页面标志
uint8_t ucArray1_ID[16] = {0};//存放IC卡号
uint8_t ucArray2_ID[16] = {0};//存放IC卡号
uint8_t ucArray3_ID[16] = {0};//存放IC卡号
uint8_t p_r = 0;					 // 平年/润年  =0表示平年，=1表示润年

void KeyScanHandle(void)  //按键扫描
{
	 if(keyNum[0] == 1) //设置键
	 {
		HAL_Delay(50);
		if(keyNum[0] == 1)
		{
			setn ++;
			if(setn <= 8)
			{
				if(setn == 8)OLED_CLS();//清屏
				DISplaysetvalue();
			}else{
				setn = 0;
				OLED_CLS();    //清屏
			}
			keyNum[0] = 0;
		}
	 }
	 if(keyNum[1] == 1) //加键
	 {
		HAL_Delay(50);
		if(keyNum[1] == 1)
		{		
			if(setn == 0)
			{
				Register = !Register;
				Delete = 0;
				if(Register==1)
				{
					OLED_CLS();
					OLED_ShowStr(0,0,(uint8_t *)"Add IC MODE",2,0);
				}else 
				{
					OLED_CLS();
					InitDisplay = 1;
				}
			}
			if(setn == 1)//设置年
				{
						SysDate.year ++;
						if(SysDate.year == 100)SysDate.year=0;
						DS1302_DateSet(&SysDate);//设置时间
				}
			if(setn == 2)//设置月
			{
					SysDate.mon ++;
					if(SysDate.mon == 13)SysDate.mon = 1;
					if((SysDate.mon==4)||(SysDate.mon==6)||(SysDate.mon==9)||(SysDate.mon==11))
					{
							if(SysDate.day>30)SysDate.day=1;
					}
					else
					{
							if(SysDate.mon==2)
							{
									if(p_r==1)
									{
											if(SysDate.day>29)
													SysDate.day=1;
									}
									else
									{
											if(SysDate.day>28)
													SysDate.day=1;
									}
							}
					}
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 3)//设置日
			{
					SysDate.day ++;
					if((SysDate.mon==1)||(SysDate.mon==3)||(SysDate.mon==5)||(SysDate.mon==7)||(SysDate.mon==8)||(SysDate.mon==10)||(SysDate.mon==12))//大月
					{
							if(SysDate.day==32)//最大31天
									SysDate.day=1;//从1开始
					}
					else
					{
							if(SysDate.mon==2)//二月份
							{
									if(p_r==1)//闰年
									{
											if(SysDate.day==30)//最大29天
													SysDate.day=1;
									}
									else
									{
											if(SysDate.day==29)//最大28天
													SysDate.day=1;
									}
							}
							else
							{
									if(SysDate.day==31)//最大30天
											SysDate.day=1;
							}
					}
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 4)//设置星期
			{
					SysDate.week ++;
					if(SysDate.week == 8)SysDate.week=1;
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 5)//设置时
			{
					SysDate.hour ++;
					if(SysDate.hour == 24)SysDate.hour=0;
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 6)//设置分
			{
					SysDate.min ++;
					if(SysDate.min == 60)SysDate.min=0;
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 7)//设置秒
			{
					SysDate.sec ++;
					if(SysDate.sec == 60)SysDate.sec=0;
					DS1302_DateSet(&SysDate);//设置时间
			}
			if(setn == 8 && seccost < 9)seccost++;
				DISplaysetvalue();   //显示设置值
				keyNum[1] = 0;
		}
	 }
	 if(keyNum[2] == 1) //减键
	 {
		HAL_Delay(50);
		if(keyNum[2] == 1)
		{	
			if(setn == 0)
			{
				Delete = !Delete;
						Register = 0;
						if(Delete==1)
						{
								OLED_CLS();
								OLED_ShowStr(0,0,(uint8_t *)"Delete IC MODE",2,0);
						}else 
						{
							OLED_CLS();
							InitDisplay = 1;
						}
			}
			if(setn == 1)//设置年
			{
					if(SysDate.year == 0)SysDate.year=100;
					SysDate.year --;
					DS1302_DateSet(&SysDate);
			}
			if(setn == 2)//设置月
			{
					if(SysDate.mon == 1)SysDate.mon=13;
					SysDate.mon --;
					if((SysDate.mon==4)||(SysDate.mon==6)||(SysDate.mon==9)||(SysDate.mon==11))
					{
							if(SysDate.day>30)
									SysDate.day=1;
					}
					else
					{
							if(SysDate.mon==2)
							{
									if(p_r==1)
									{
											if(SysDate.day>29)
													SysDate.day=1;
									}
									else
									{
											if(SysDate.day>28)
													SysDate.day=1;
									}
							}
					}
					DS1302_DateSet(&SysDate);
			}
			if(setn == 3)//设置日
			{
					SysDate.day --;
					if((SysDate.mon==1)||(SysDate.mon==3)||(SysDate.mon==5)||(SysDate.mon==7)||(SysDate.mon==8)||(SysDate.mon==10)||(SysDate.mon==12))
					{
							if(SysDate.day==0)
									SysDate.day=31;
					}
					else
					{
							if(SysDate.mon==2)
							{
									if(p_r==1)
									{
											if(SysDate.day==0)
													SysDate.day=29;
									}
									else
									{
											if(SysDate.day==0)
													SysDate.day=28;
									}
							}
							else
							{
									if(SysDate.day==0)
											SysDate.day=30;
							}
					}
					DS1302_DateSet(&SysDate);
			}
			if(setn == 4)//设置星期
			{
					if(SysDate.week == 1)SysDate.week=8;
					SysDate.week --;
					DS1302_DateSet(&SysDate);
			}
			if(setn == 5)//设置时
			{
					if(SysDate.hour == 0)SysDate.hour=24;
					SysDate.hour --;
					DS1302_DateSet(&SysDate);
			}
			if(setn == 6)//设置分
			{
					if(SysDate.min == 0)SysDate.min=60;
					SysDate.min --;
					DS1302_DateSet(&SysDate);
			}
			if(setn == 7)//设置秒
			{
					if(SysDate.sec == 0)SysDate.sec=60;
					SysDate.sec --;
					DS1302_DateSet(&SysDate);
			}
			if(setn == 8 && seccost > 0)seccost--;
			DISplaysetvalue();
				keyNum[2] = 0;
		}
	 }
}

void KeyScan(void)//获取当前按键值
{
	if(KEY0 == 0)
	{
		keyNum[0] = 1;
	}
	if(KEY1 == 0)
	{
		keyNum[1] = 1;
	}
	if(KEY2 == 0)
	{
		keyNum[2]= 1;
	}
	if(KEY3 == 0)
	{
		keyNum[3]= 1;
	}
}
#endif





char display[16];//显示存储数组

void Display(void)
{
		uint8_t i;
	if(Register == 0 && Delete == 0)
	{
		if(car1 == 1 || car1 == 2) 
		{ 
			OLED_DrawBMP(0,4,32,8,3);//入场
		}else{
			OLED_DrawBMP(0,4,32,8,0);//出场
		}
		if (car2 == 1 || car2 == 2) 
		{ 
			OLED_DrawBMP(48,4,80,8,4);
		}else{
			OLED_DrawBMP(48,4,80,8,1);
		}
		if (car3 == 1 || car3 == 2) 
		{ 
			OLED_DrawBMP(95,4,127,8,5);
		}else{
			OLED_DrawBMP(95,4,127,8,2);
		}
	}
		

}

void DISplaysetvalue(void)
{
	uint8_t i;		
	if(setn == 8)
	{	
		for(i=0;i<2;i++)OLED_ShowCN(i*16+16,0,i+8,0);//显示中文：设置
		for(i=0;i<4;i++)OLED_ShowCN(i*16+48,0,i+10,0);//显示中文：充电单价
		sprintf(display,"%2d",seccost); 
		OLED_ShowStr(48, 3,(uint8_t *)display, 2,0);	
	}
	
}


void DisplayTime(void)//显示时间函数
{
	  unsigned char i=0,x=0;
	  u16 year_temp;
	
	  if(setn==0)DS1302_DateRead(&SysDate);//读时间
	  year_temp=2000+SysDate.year;
		if((year_temp%400==0)||((year_temp%100!=0)&&(year_temp%4==0)))  //判断是否为闰年
				p_r=1;
		else
				p_r=0;
	  if(setn<8)
		{
	  OLED_ShowChar((x++)*8,0,'2',2,setn+1-1);
	  OLED_ShowChar((x++)*8,0,'0',2,setn+1-1);
	  OLED_ShowChar((x++)*8,0,SysDate.year/10+'0',2,setn+1-1);
	  OLED_ShowChar((x++)*8,0,SysDate.year%10+'0',2,setn+1-1);
	  OLED_ShowChar((x++)*8,0,'-',2,0);
	  OLED_ShowChar((x++)*8,0,SysDate.mon/10+'0',2,setn+1-2);
	  OLED_ShowChar((x++)*8,0,SysDate.mon%10+'0',2,setn+1-2);
	  OLED_ShowChar((x++)*8,0,'-',2,0);
	  OLED_ShowChar((x++)*8,0,SysDate.day/10+'0',2,setn+1-3);
	  OLED_ShowChar((x++)*8,0,SysDate.day%10+'0',2,setn+1-3);
	  
	  OLED_ShowCN(i*16+88,0,0,setn+1-4);//测试显示中文：周
	  switch(SysDate.week)
    {
    case 1: 
			  OLED_ShowCN(i*16+104,0,1,setn+1-4);//测试显示中文：一
        break;

    case 2: 
			  OLED_ShowCN(i*16+104,0,2,setn+1-4);//测试显示中文：二
        break;

    case 3: 
			  OLED_ShowCN(i*16+104,0,3,setn+1-4);//测试显示中文：三
        break;

    case 4: 
			  OLED_ShowCN(i*16+104,0,4,setn+1-4);//测试显示中文：四
        break;

    case 5: 
			  OLED_ShowCN(i*16+104,0,i+5,setn+1-4);//测试显示中文：五
        break;

    case 6: 
			  OLED_ShowCN(i*16+104,0,6,setn+1-4);//测试显示中文：六
        break;

    case 7: 
			  OLED_ShowCN(i*16+104,0,7,setn+1-4);//测试显示中文：日
        break;
    }
    x=0;
		
	  OLED_ShowChar((x++)*8,2,SysDate.hour/10+'0',2,setn+1-5);
	  OLED_ShowChar((x++)*8,2,SysDate.hour%10+'0',2,setn+1-5);
	  OLED_ShowChar((x++)*8,2,':',2,0);
	  OLED_ShowChar((x++)*8,2,SysDate.min/10+'0',2,setn+1-6);
	  OLED_ShowChar((x++)*8,2,SysDate.min%10+'0',2,setn+1-6);
	  OLED_ShowChar((x++)*8,2,':',2,0);
	  OLED_ShowChar((x++)*8,2,SysDate.sec/10+'0',2,setn+1-7);
	  OLED_ShowChar((x++)*8,2,SysDate.sec%10+'0',2,setn+1-7);
	}
}

void CotorFun(void)
{	
	
}

