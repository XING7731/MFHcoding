#include "main.h"
#include "SD1306.h"
#include "SD1306_Font.h"
IIC_HardWareTypedef iicsd1306s;



void OLED_Init(void)
{	
	
	WriteCmd(0xAE); //display off
	WriteCmd(0x20);	//Set Memory Addressing Mode	
	WriteCmd(0x10);	//00,Horizontal Addressing Mode;01,Vertical Addressing Mode;10,Page Addressing Mode (RESET);11,Invalid
	WriteCmd(0xb0);	//Set Page Start Address for Page Addressing Mode,0-7
	WriteCmd(0xc8);	//Set COM Output Scan Direction
	WriteCmd(0x00); //---set low column address
	WriteCmd(0x10); //---set high column address
	WriteCmd(0x40); //--set start line address
	WriteCmd(0x81); //--set contrast control register
	WriteCmd(0xff); //亮度调节 0x00~0xff
	WriteCmd(0xa1); //--set segment re-map 0 to 127
	WriteCmd(0xa6); //--set normal display
	WriteCmd(0xa8); //--set multiplex ratio(1 to 64)
	WriteCmd(0x3F); //
	WriteCmd(0xa4); //0xa4,Output follows RAM content;0xa5,Output ignores RAM content
	WriteCmd(0xd3); //-set display offset
	WriteCmd(0x00); //-not offset
	WriteCmd(0xd5); //--set display clock divide ratio/oscillator frequency
	WriteCmd(0xf0); //--set divide ratio
	WriteCmd(0xd9); //--set pre-charge period
	WriteCmd(0x22); //
	WriteCmd(0xda); //--set com pins hardware configuration
	WriteCmd(0x12);
	WriteCmd(0xdb); //--set vcomh
	WriteCmd(0x20); //0x20,0.77xVcc
	WriteCmd(0x8d); //--set DC-DC enable
	WriteCmd(0x14); //
	WriteCmd(0xaf); //--turn on oled panel
}


void WriteCmd(unsigned char I2C_Command)//写命令
{
	IIC_Write_Device_Byte(OLED_ADDRESS, OLED_cmd, I2C_Command, iicsd1306s);
}

void WriteDat(unsigned char I2C_Data)//写数据
{
	IIC_Write_Device_Byte(OLED_ADDRESS, OLED_dat, I2C_Data, iicsd1306s);
}

void OLED_SetPos(unsigned char x, unsigned char y) //设置起始点坐标
{ 
	WriteCmd(0xb0+y);
	WriteCmd(((x&0xf0)>>4)|0x10);
	WriteCmd((x&0x0f)|0x01);
}

void OLED_Fill(unsigned char fill_Data)//全屏填充
{
	unsigned char m,n;
	for(m=0;m<8;m++)
	{
		WriteCmd(0xb0+m);		//page0-page1
		WriteCmd(0x00);		//low column start address
		WriteCmd(0x10);		//high column start address
		for(n=0;n<128;n++)
			{
				WriteDat(fill_Data);
			}
	}
}

void OLED_CLS(void)//清屏
{
	OLED_Fill(0x00);
}

//--------------------------------------------------------------
// Prototype      : void OLED_ON(void)
// Calls          : 
// Parameters     : none
// Description    : 将OLED从休眠中唤醒
//--------------------------------------------------------------
void OLED_ON(void)
{
	WriteCmd(0X8D);  //设置电荷泵
	WriteCmd(0X14);  //开启电荷泵
	WriteCmd(0XAF);  //OLED唤醒
}

//--------------------------------------------------------------
// Prototype      : void OLED_OFF(void)
// Calls          : 
// Parameters     : none
// Description    : 让OLED休眠 -- 休眠模式下,OLED功耗不到10uA
//--------------------------------------------------------------
void OLED_OFF(void)
{
	WriteCmd(0X8D);  //设置电荷泵
	WriteCmd(0X10);  //关闭电荷泵
	WriteCmd(0XAE);  //OLED休眠
}

//在指定位置显示一个字符,包括部分字符
//x:0~127
//y:0~7
//chr:显示的字符		 
//TextSize:字符大小(1:6*8 ; 2:8*16)
//mode:1,反白显示;0,正常显示		
void OLED_ShowChar(unsigned char x,unsigned char y,unsigned char chr,unsigned char TextSize,u8 mode)
{      	
		unsigned char c=0,i=0;	
	
		c=chr-' ';//得到偏移后的值	
		if(TextSize == 2)
		{
				if(x>120){x=0;y++;}
			  OLED_SetPos(x,y);
				for(i=0;i<8;i++)
					if(mode==1)WriteDat(~(F8X16[c*16+i]));else WriteDat(F8X16[c*16+i]);
				OLED_SetPos(x,y+1);
				for(i=0;i<8;i++)
					if(mode==1)WriteDat(~(F8X16[c*16+i+8]));else WriteDat(F8X16[c*16+i+8]);
		}
		else 
		{	
				if(x>126){x=0;y++;}
				OLED_SetPos(x,y);
				for(i=0;i<6;i++)
					if(mode==1)WriteDat(~(F6x8[c][i])); else WriteDat(F6x8[c][i]);
		}
}

//--------------------------------------------------------------
// Prototype      : void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char ch[], unsigned char TextSize)
// Calls          : 
// Parameters     : x,y -- 起始点坐标(x:0~127, y:0~7); ch[] -- 要显示的字符串; TextSize -- 字符大小(1:6*8 ; 2:8*16)
// Description    : 显示codetab.h中的ASCII字符,有6*8和8*16可选择
//--------------------------------------------------------------
void OLED_ShowStr(unsigned char x, unsigned char y, unsigned char ch[], unsigned char TextSize,u8 mode)
{
	unsigned char c = 0,i = 0,j = 0;
	switch(TextSize)
	{
		case 1:
		{
			while(ch[j] != '\0')
			{
				c = ch[j] - 32;
				if(x > 126)
				{
					x = 0;
					y++;
				}
				OLED_SetPos(x,y);
				for(i=0;i<6;i++)
				if(mode==1)WriteDat(~(F6x8[c][i]));
				else 	WriteDat(F6x8[c][i]);
				x += 6;
				j++;
			}
		}break;
		case 2:
		{
			while(ch[j] != '\0')
			{
				c = ch[j] - 32;
				if(x > 120)
				{
					x = 0;
					y++;
				}
				OLED_SetPos(x,y);
				for(i=0;i<8;i++)
				if(mode==1)WriteDat(~(F8X16[c*16+i]));
				else 	WriteDat(F8X16[c*16+i]);
				OLED_SetPos(x,y+1);
				for(i=0;i<8;i++)
				if(mode==1)WriteDat(~(F8X16[c*16+i+8]));
				else	WriteDat(F8X16[c*16+i+8]);
				x += 8;
				j++;
			}
		}break;
	}
}

//--------------------------------------------------------------
// Prototype      : void OLED_ShowCN(unsigned char x, unsigned char y, unsigned char N)
// Calls          : 
// Parameters     : x,y -- 起始点坐标(x:0~127, y:0~7); N:汉字在codetab.h中的索引
// Description    : 显示codetab.h中的汉字,16*16点阵
//mode:1,反白显示;0,正常显示	
//--------------------------------------------------------------
void OLED_ShowCN(unsigned char x, unsigned char y, unsigned char N,u8 mode)
{
	unsigned char wm=0;
	unsigned int  adder=32*N;
	OLED_SetPos(x , y);
	for(wm = 0;wm < 16;wm++)
	{
		if(mode==1)WriteDat(~(F16x16[adder]));else WriteDat(F16x16[adder]);
		adder += 1;
	}
	OLED_SetPos(x,y + 1);
	for(wm = 0;wm < 16;wm++)
	{
		if(mode==1)WriteDat(~(F16x16[adder]));else WriteDat(F16x16[adder]);
		adder += 1;
	}
}

//--------------------------------------------------------------
// Prototype      : void OLED_DrawBMP(unsigned char x0,unsigned char y0,unsigned char x1,unsigned char y1,unsigned char BMP[]);
// Calls          : 
// Parameters     : x0,y0 -- 起始点坐标(x0:0~127, y0:0~7); x1,y1 -- 起点对角线(结束点)的坐标(x1:1~128,y1:1~8)
// Description    : 显示BMP位图
//--------------------------------------------------------------
void OLED_DrawBMP(unsigned char x0,unsigned char y0,unsigned char x1,unsigned char y1,unsigned char N)
{
	unsigned int j=0;
	unsigned char x,y;

  if(y1%8==0)
		y = y1/8;
  else
		y = y1/8 + 1;
	for(y=y0;y<y1;y++)
	{
		OLED_SetPos(x0,y);
    for(x=x0;x<x1;x++)
		{
			WriteDat(BMP2[N][j++]);
		}
	}
}

void OLED_ShowCentigrade(unsigned char x, unsigned char y)//显示℃
{
	unsigned char wm=0;
	unsigned char BUF[]={
	0x10,0x28,0x10,0xC0,0x20,0x10,0x10,0x10,0x20,0x70,0x00,0x00,0x00,0x00,0x00,0x07,
	0x08,0x10,0x10,0x10,0x10,0x08,0x04,0x00,/*"℃"*/
	};
	OLED_SetPos(x , y);
	for(wm = 0;wm < 12;wm++)
	{
		 WriteDat(BUF[wm]);
	}
	OLED_SetPos(x,y + 1);
	for(wm = 0;wm < 12;wm++)
	{
		 WriteDat(BUF[wm+12]);
	}
}

void OLED_ShowTH(unsigned char x, unsigned char y, unsigned char DATA)
{
		unsigned char i = 0;

		OLED_SetPos(x,y);
		for(i=0;i<8;i++)
			WriteDat(F8X16[(DATA/10+16)*16+i]);
		OLED_SetPos(x,y+1);
		for(i=0;i<8;i++)
			WriteDat(F8X16[(DATA/10+16)*16+i+8]);
	
		OLED_SetPos(x+8,y);
		for(i=0;i<8;i++)
			WriteDat(F8X16[(DATA%10+16)*16+i]);
		OLED_SetPos(x+8,y+1);
		for(i=0;i<8;i++)
			WriteDat(F8X16[(DATA%10+16)*16+i+8]);
}

/**
  * @brief  在OLED上显示格式化字符串
  * @param  x: 横坐标 (0-127)
  * @param  y: 纵坐标 (0-7) 
  * @param  format: 格式化字符串
  * @param  ...: 可变参数
  * @retval 无
  */
void OLED_ShowFormatStr(uint8_t x, uint8_t y,  const char* format, ...)
{
    char display_buf[32];  // 根据需求调整缓冲区大小
    va_list args;
    
    va_start(args, format);
    vsnprintf(display_buf, sizeof(display_buf), format, args);
    va_end(args);
    
    OLED_ShowStr(x, y, (u8 *)display_buf, 2, 0);
}

