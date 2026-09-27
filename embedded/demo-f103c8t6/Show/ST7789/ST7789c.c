#include "main.h"
#include "ST7789C.h"
#include "ST7789C_Font.h"
#include "pic.h"

_lcd_dev lcddev;

uint16_t POINT_COLOR = 0x0000,BACK_COLOR = 0xFFFF;  
uint16_t DeviceCode;	 


void LCD_ST7789_Init(void)
{
		GPIO_InitTypeDef GPIO_InitStruct;
	
	  ST7789_RST_CLK_ENABLE();  
		ST7789_DC_CLK_ENABLE();
//	ST7789_BK_CLK_ENABLE();
	
		GPIO_InitStruct.Pin = ST7789_RST_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		HAL_GPIO_Init(ST7789_RST_PORT, &GPIO_InitStruct);

		GPIO_InitStruct.Pin = ST7789_DC_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
		HAL_GPIO_Init(ST7789_DC_PORT, &GPIO_InitStruct);

//		GPIO_InitStruct.Pin = ST7789_BK_PIN;
//		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
//		GPIO_InitStruct.Pull = GPIO_PULLUP;
//		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//		HAL_GPIO_Init(ST7789_BK_PORT, &GPIO_InitStruct);

/**********************************************/
	 	BACK_COLOR = BLACK;
		POINT_COLOR = WHITE; 
}	


void LCD_Wr_Reg(uint8_t data)
{
	ST7789_REG_IN;
	spi2_read_write_byte(data);
}	


void LCD_Wr_Data(uint8_t data)
{
	ST7789_DATA_IN;
	spi2_read_write_byte(data);
}	

void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue)
{
	LCD_Wr_Reg(LCD_Reg);
	LCD_Wr_Data(LCD_RegValue);
}	


void LCD_WriteRAM_Prepare(void)
{
	LCD_Wr_Reg(lcddev.wramcmd);
}	 

void Lcd_WriteData_16Bit(uint16_t Data)
{	
   ST7789_DATA_IN; 
   spi2_read_write_byte(Data>>8);
	 spi2_read_write_byte(Data);
}

void LCD_DrawPoint(uint16_t x,uint16_t y)
{
	LCD_SetCursor(x,y);//设置光标位置 
	Lcd_WriteData_16Bit(POINT_COLOR); 
}


void LCD_Clear(uint16_t Color)
{
  unsigned int i,m;  
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);   
	ST7789_DATA_IN;
	for(i=0;i<lcddev.height;i++)
	{
    for(m=0;m<lcddev.width;m++)
    {	
			spi2_read_write_byte(Color>>8);
			spi2_read_write_byte(Color);
		}
	}
} 


/*****************************************************************************
 * @name       :void LCD_SetWindows(uint16_t xStar, uint16_t yStar,uint16_t xEnd,uint16_t yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
								yStar:the bebinning y coordinate of the LCD display window
								xEnd:the endning x coordinate of the LCD display window
								yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(uint16_t xStar, uint16_t yStar,uint16_t xEnd,uint16_t yEnd)
{	
	LCD_Wr_Reg(lcddev.setxcmd);	
	LCD_Wr_Data((xStar+lcddev.xoffset)>>8);
	LCD_Wr_Data(xStar+lcddev.xoffset);		
	LCD_Wr_Data((xEnd+lcddev.xoffset)>>8);
	LCD_Wr_Data(xEnd+lcddev.xoffset);

	LCD_Wr_Reg(lcddev.setycmd);	
	LCD_Wr_Data((yStar+lcddev.yoffset)>>8);
	LCD_Wr_Data(yStar+lcddev.yoffset);		
	LCD_Wr_Data((yEnd+lcddev.yoffset)>>8);
	LCD_Wr_Data(yEnd+lcddev.yoffset);

	LCD_WriteRAM_Prepare();	//开始写入GRAM			
}   


void LCD_direction(uint8_t direction)
{ 
			lcddev.setxcmd=0x2A;
			lcddev.setycmd=0x2B;
			lcddev.wramcmd=0x2C;
	switch(direction){		  
		case 0:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;	
			lcddev.xoffset=0;
			lcddev.yoffset=0;
			LCD_WriteReg(0x36,0);//BGR==1,MY==0,MX==0,MV==0
		break;
		case 1:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			lcddev.xoffset=0;
			lcddev.yoffset=0;
			LCD_WriteReg(0x36,(1<<6)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;
		case 2:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;
      lcddev.xoffset=0;
			lcddev.yoffset=80;			
			LCD_WriteReg(0x36,(1<<6)|(1<<7));//BGR==1,MY==0,MX==0,MV==0
		break;
		case 3:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			lcddev.xoffset=80;
			lcddev.yoffset=0;
			LCD_WriteReg(0x36,(1<<7)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;	
		default:break;
	}		
}	 

void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{	  	    			
	LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);	
} 

void LCD_RESET(void)
{
	ST7789_RESET;
	delay_ms(20);	
	ST7789_SET;
	delay_ms(20);
}


void LCD_Init(void)
{  
	LCD_ST7789_Init();		//LCD GPIO初始化										 
 	LCD_RESET(); 					//LCD 复位

	
	LCD_Wr_Reg(0x36); 
	LCD_Wr_Data(0x00);

	LCD_Wr_Reg(0x3A); 
	LCD_Wr_Data(0x05);

	LCD_Wr_Reg(0xB2);
	LCD_Wr_Data(0x0C);
	LCD_Wr_Data(0x0C);
	LCD_Wr_Data(0x00);
	LCD_Wr_Data(0x33);
	LCD_Wr_Data(0x33);

	LCD_Wr_Reg(0xB7); 
	LCD_Wr_Data(0x35);  

	LCD_Wr_Reg(0xBB);
	LCD_Wr_Data(0x19);

	LCD_Wr_Reg(0xC0);
	LCD_Wr_Data(0x2C);

	LCD_Wr_Reg(0xC2);
	LCD_Wr_Data(0x01);

	LCD_Wr_Reg(0xC3);
	LCD_Wr_Data(0x12);   

	LCD_Wr_Reg(0xC4);
	LCD_Wr_Data(0x20);  

	LCD_Wr_Reg(0xC6); 
	LCD_Wr_Data(0x0F);    

	LCD_Wr_Reg(0xD0); 
	LCD_Wr_Data(0xA4);
	LCD_Wr_Data(0xA1);

	LCD_Wr_Reg(0xE0);
	LCD_Wr_Data(0xD0);
	LCD_Wr_Data(0x04);
	LCD_Wr_Data(0x0D);
	LCD_Wr_Data(0x11);
	LCD_Wr_Data(0x13);
	LCD_Wr_Data(0x2B);
	LCD_Wr_Data(0x3F);
	LCD_Wr_Data(0x54);
	LCD_Wr_Data(0x4C);
	LCD_Wr_Data(0x18);
	LCD_Wr_Data(0x0D);
	LCD_Wr_Data(0x0B);
	LCD_Wr_Data(0x1F);
	LCD_Wr_Data(0x23);

	LCD_Wr_Reg(0xE1);
	LCD_Wr_Data(0xD0);
	LCD_Wr_Data(0x04);
	LCD_Wr_Data(0x0C);
	LCD_Wr_Data(0x11);
	LCD_Wr_Data(0x13);
	LCD_Wr_Data(0x2C);
	LCD_Wr_Data(0x3F);
	LCD_Wr_Data(0x44);
	LCD_Wr_Data(0x51);
	LCD_Wr_Data(0x2F);
	LCD_Wr_Data(0x1F);
	LCD_Wr_Data(0x1F);
	LCD_Wr_Data(0x20);
	LCD_Wr_Data(0x23);

	LCD_Wr_Reg(0x21); 

	LCD_Wr_Reg(0x11); 
	//Delay (120); 

	LCD_Wr_Reg(0x29); 	
  LCD_direction(USE_HORIZONTAL);//设置LCD显示方向
	
//	ST7789_BK_ON;//点亮背光	 
	LCD_Clear(WHITE);//清全屏白色
}



/*******************************************************************
 * @name       :void GUI_DrawPoint(uint16_t x,uint16_t y,uint16_t color)
 * @date       :2018-08-09 
 * @function   :draw a point in LCD screen
 * @parameters :x:the x coordinate of the point
                y:the y coordinate of the point
								color:the color value of the point
 * @retvalue   :None
********************************************************************/
void GUI_DrawPoint(uint16_t x,uint16_t y,uint16_t color)
{
	LCD_SetCursor(x,y);//设置光标位置 
	Lcd_WriteData_16Bit(color); 
}

/*******************************************************************
 * @name       :void LCD_Fill(uint16_t sx,uint16_t sy,uint16_t ex,uint16_t ey,uint16_t color)
 * @date       :2018-08-09 
 * @function   :fill the specified area
 * @parameters :sx:the bebinning x coordinate of the specified area
                sy:the bebinning y coordinate of the specified area
								ex:the ending x coordinate of the specified area
								ey:the ending y coordinate of the specified area
								color:the filled color value
 * @retvalue   :None
********************************************************************/
void LCD_Fill(uint16_t sx,uint16_t sy,uint16_t ex,uint16_t ey,uint16_t color)
{  	
	uint16_t i,j;			
	uint16_t width=ex-sx+1; 		//得到填充的宽度
	uint16_t height=ey-sy+1;		//高度
	LCD_SetWindows(sx,sy,ex,ey);//设置显示窗口
	for(i=0;i<height;i++)
	{
		for(j=0;j<width;j++)
		Lcd_WriteData_16Bit(color);	//写入数据 	 
	}
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复窗口设置为全屏
}

/*******************************************************************
 * @name       :void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
 * @date       :2018-08-09 
 * @function   :Draw a line between two points
 * @parameters :x1:the bebinning x coordinate of the line
                y1:the bebinning y coordinate of the line
								x2:the ending x coordinate of the line
								y2:the ending y coordinate of the line
 * @retvalue   :None
********************************************************************/
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	uint16_t t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance; 
	int incx,incy,uRow,uCol; 

	delta_x=x2-x1; //计算坐标增量 
	delta_y=y2-y1; 
	uRow=x1; 
	uCol=y1; 
	if(delta_x>0)incx=1; //设置单步方向 
	else if(delta_x==0)incx=0;//垂直线 
	else {incx=-1;delta_x=-delta_x;} 
	if(delta_y>0)incy=1; 
	else if(delta_y==0)incy=0;//水平线 
	else{incy=-1;delta_y=-delta_y;} 
	if( delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴 
	else distance=delta_y; 
	for(t=0;t<=distance+1;t++ )//画线输出 
	{  
		LCD_DrawPoint(uRow,uCol);//画点 
		xerr+=delta_x ; 
		yerr+=delta_y ; 
		if(xerr>distance) 
		{ 
			xerr-=distance; 
			uRow+=incx; 
		} 
		if(yerr>distance) 
		{ 
			yerr-=distance; 
			uCol+=incy; 
		} 
	}  
} 

/*****************************************************************************
 * @name       :void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
 * @date       :2018-08-09 
 * @function   :Draw a rectangle
 * @parameters :x1:the bebinning x coordinate of the rectangle
                y1:the bebinning y coordinate of the rectangle
								x2:the ending x coordinate of the rectangle
								y2:the ending y coordinate of the rectangle
 * @retvalue   :None
******************************************************************************/
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	LCD_DrawLine(x1,y1,x2,y1);
	LCD_DrawLine(x1,y1,x1,y2);
	LCD_DrawLine(x1,y2,x2,y2);
	LCD_DrawLine(x2,y1,x2,y2);
}  

/*****************************************************************************
 * @name       :void LCD_DrawFillRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
 * @date       :2018-08-09 
 * @function   :Filled a rectangle
 * @parameters :x1:the bebinning x coordinate of the filled rectangle
                y1:the bebinning y coordinate of the filled rectangle
								x2:the ending x coordinate of the filled rectangle
								y2:the ending y coordinate of the filled rectangle
 * @retvalue   :None
******************************************************************************/  
void LCD_DrawFillRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	LCD_Fill(x1,y1,x2,y2,POINT_COLOR);
}
 
/*****************************************************************************
 * @name       :void _draw_circle_8(int xc, int yc, int x, int y, uint16_t c)
 * @date       :2018-08-09 
 * @function   :8 symmetry circle drawing algorithm (internal call)
 * @parameters :xc:the x coordinate of the Circular center 
                yc:the y coordinate of the Circular center 
								x:the x coordinate relative to the Circular center 
								y:the y coordinate relative to the Circular center 
								c:the color value of the circle
 * @retvalue   :None
******************************************************************************/  
void _draw_circle_8(int xc, int yc, int x, int y, uint16_t c)
{
	GUI_DrawPoint(xc + x, yc + y, c);

	GUI_DrawPoint(xc - x, yc + y, c);

	GUI_DrawPoint(xc + x, yc - y, c);

	GUI_DrawPoint(xc - x, yc - y, c);

	GUI_DrawPoint(xc + y, yc + x, c);

	GUI_DrawPoint(xc - y, yc + x, c);

	GUI_DrawPoint(xc + y, yc - x, c);

	GUI_DrawPoint(xc - y, yc - x, c);
}

/*****************************************************************************
 * @name       :void gui_circle(int xc, int yc,uint16_t c,int r, int fill)
 * @date       :2018-08-09 
 * @function   :Draw a circle of specified size at a specified location
 * @parameters :xc:the x coordinate of the Circular center 
                yc:the y coordinate of the Circular center 
								r:Circular radius
								fill:1-filling,0-no filling
 * @retvalue   :None
******************************************************************************/  
void gui_circle(int xc, int yc,uint16_t c,int r, int fill)
{
	int x = 0, y = r, yi, d;

	d = 3 - 2 * r;


	if (fill) 
	{
		// 如果填充（画实心圆）
		while (x <= y) {
			for (yi = x; yi <= y; yi++)
				_draw_circle_8(xc, yc, x, yi, c);

			if (d < 0) {
				d = d + 4 * x + 6;
			} else {
				d = d + 4 * (x - y) + 10;
				y--;
			}
			x++;
		}
	} else 
	{
		// 如果不填充（画空心圆）
		while (x <= y) {
			_draw_circle_8(xc, yc, x, y, c);
			if (d < 0) {
				d = d + 4 * x + 6;
			} else {
				d = d + 4 * (x - y) + 10;
				y--;
			}
			x++;
		}
	}
}

/*****************************************************************************
 * @name       :void Draw_Triangel(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
 * @date       :2018-08-09 
 * @function   :Draw a triangle at a specified position
 * @parameters :x0:the bebinning x coordinate of the triangular edge 
                y0:the bebinning y coordinate of the triangular edge 
								x1:the vertex x coordinate of the triangular
								y1:the vertex y coordinate of the triangular
								x2:the ending x coordinate of the triangular edge 
								y2:the ending y coordinate of the triangular edge 
 * @retvalue   :None
******************************************************************************/ 
void Draw_Triangel(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	LCD_DrawLine(x0,y0,x1,y1);
	LCD_DrawLine(x1,y1,x2,y2);
	LCD_DrawLine(x2,y2,x0,y0);
}

static void _swap(uint16_t *a, uint16_t *b)
{
	uint16_t tmp;
  tmp = *a;
	*a = *b;
	*b = tmp;
}

/*****************************************************************************
 * @name       :void Fill_Triangel(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
 * @date       :2018-08-09 
 * @function   :filling a triangle at a specified position
 * @parameters :x0:the bebinning x coordinate of the triangular edge 
                y0:the bebinning y coordinate of the triangular edge 
								x1:the vertex x coordinate of the triangular
								y1:the vertex y coordinate of the triangular
								x2:the ending x coordinate of the triangular edge 
								y2:the ending y coordinate of the triangular edge 
 * @retvalue   :None
******************************************************************************/ 
void Fill_Triangel(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	uint16_t a, b, y, last;
	int dx01, dy01, dx02, dy02, dx12, dy12;
	long sa = 0;
	long sb = 0;
 	if (y0 > y1) 
	{
    _swap(&y0,&y1); 
		_swap(&x0,&x1);
 	}
 	if (y1 > y2) 
	{
    _swap(&y2,&y1); 
		_swap(&x2,&x1);
 	}
  if (y0 > y1) 
	{
    _swap(&y0,&y1); 
		_swap(&x0,&x1);
  }
	if(y0 == y2) 
	{ 
		a = b = x0;
		if(x1 < a)
    {
			a = x1;
    }
    else if(x1 > b)
    {
			b = x1;
    }
    if(x2 < a)
    {
			a = x2;
    }
		else if(x2 > b)
    {
			b = x2;
    }
		LCD_Fill(a,y0,b,y0,POINT_COLOR);
    return;
	}
	dx01 = x1 - x0;
	dy01 = y1 - y0;
	dx02 = x2 - x0;
	dy02 = y2 - y0;
	dx12 = x2 - x1;
	dy12 = y2 - y1;
	
	if(y1 == y2)
	{
		last = y1; 
	}
  else
	{
		last = y1-1; 
	}
	for(y=y0; y<=last; y++) 
	{
		a = x0 + sa / dy01;
		b = x0 + sb / dy02;
		sa += dx01;
    sb += dx02;
    if(a > b)
    {
			_swap(&a,&b);
		}
		LCD_Fill(a,y,b,y,POINT_COLOR);
	}
	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);
	for(; y<=y2; y++) 
	{
		a = x1 + sa / dy12;
		b = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;
		if(a > b)
		{
			_swap(&a,&b);
		}
		LCD_Fill(a,y,b,y,POINT_COLOR);
	}
}

/*****************************************************************************
 * @name       :void LCD_ShowChar(uint16_t x,uint16_t y,uint16_t fc, uint16_t bc, uint8_t num,uint8_t size,uint8_t mode)
 * @date       :2018-08-09 
 * @function   :Display a single English character
 * @parameters :x:the bebinning x coordinate of the Character display position
                y:the bebinning y coordinate of the Character display position
								fc:the color value of display character
								bc:the background color of display character
								num:the ascii code of display character(0~94)
								size:the size of display character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void LCD_ShowChar1(u16 x,u16 y,u16 fc, u16 bc, u8 num,u8 size,u8 mode)
{  
    u8 temp;
    u8 pos,t;
	u16 colortemp=POINT_COLOR;      
		   
	num=num-' ';//得到偏移后的值
	LCD_SetWindows(x,y,x+size/2-1,y+size-1);//设置单个文字显示窗口
	if(!mode) //非叠加方式
	{		
		for(pos=0;pos<size;pos++)
		{
			if(size==12)temp=asc2_1206[num][pos];//调用1206字体
			else temp=asc2_1608[num][pos];		 //调用1608字体
			for(t=0;t<size/2;t++)
		    {                 
		        if(temp&0x01)Lcd_WriteData_16Bit(fc); 
				else Lcd_WriteData_16Bit(bc); 
				temp>>=1; 
				
		    }
			
		}	
	}else//叠加方式
	{
		for(pos=0;pos<size;pos++)
		{
			if(size==12)temp=asc2_1206[num][pos];//调用1206字体
			else temp=asc2_1608[num][pos];		 //调用1608字体
			for(t=0;t<size/2;t++)
		    {   
				POINT_COLOR=fc;              
		        if(temp&0x01)LCD_DrawPoint(x+t,y+pos);//画一个点    
		        temp>>=1; 
		    }
		}
	}
	POINT_COLOR=colortemp;	
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复窗口为全屏    	   	 	  
}

void LCD_ShowChar(u16 x, u16 y, u16 fc, u16 bc, u8 num, u8 size, u8 mode)
{  
    u8 temp;
    u8 pos, t;
    u16 colortemp = POINT_COLOR;
    
    num = num - ' '; // 得到偏移后的值
    
    // 设置窗口宽度
    u8 width = size / 2;
    if(size == 24) width = 12; // 24x12字体
    else if(size == 12) width = 6; // 12x6字体
    // 16x8字体保持width=8
    
    LCD_SetWindows(x, y, x + width - 1, y + size - 1); // 设置单个文字显示窗口
    
    if(!mode) // 非叠加方式
    {        
        for(pos = 0; pos < size; pos++)
        {
            // 选择字库
            if(size == 24) temp = asc2_2412[num][pos];
            else if(size == 12) temp = asc2_1206[num][pos];
            else temp = asc2_1608[num][pos]; // 默认16x08
            
            for(t = 0; t < width; t++)
            {                 
                if(temp & 0x01) Lcd_WriteData_16Bit(fc); 
                else Lcd_WriteData_16Bit(bc); 
                temp >>= 1; 
            }
        }    
    }
    else // 叠加方式
    {
        for(pos = 0; pos < size; pos++)
        {
            // 选择字库
            if(size == 24) temp = asc2_2412[num][pos];
            else if(size == 12) temp = asc2_1206[num][pos];
            else temp = asc2_1608[num][pos]; // 默认16x08
            
            for(t = 0; t < width; t++)
            {   
                POINT_COLOR = fc;              
                if(temp & 0x01) LCD_DrawPoint(x + t, y + pos); // 画一个点    
                temp >>= 1; 
            }
        }
    }
    POINT_COLOR = colortemp;
    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1); // 恢复窗口为全屏
}

/*****************************************************************************
 * @name       :void LCD_ShowString(uint16_t x,uint16_t y,uint8_t size,uint8_t *p,uint8_t mode)
 * @date       :2018-08-09 
 * @function   :Display English string
 * @parameters :x:the bebinning x coordinate of the English string
                y:the bebinning y coordinate of the English string
								p:the start address of the English string
								size:the size of display character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/   	  
void LCD_ShowString(uint16_t x,uint16_t y,uint8_t size,uint8_t *p,uint8_t mode)
{         
    while((*p<='~')&&(*p>=' '))//判断是不是非法字符!
    {   
		if(x>(lcddev.width-1)||y>(lcddev.height-1)) 
		return;     
        LCD_ShowChar(x,y,POINT_COLOR,BACK_COLOR,*p,size,mode);
        x+=size/2;
        p++;
    }  
} 

/*****************************************************************************
 * @name       :u32 mypow(uint8_t m,uint8_t n)
 * @date       :2018-08-09 
 * @function   :get the nth power of m (internal call)
 * @parameters :m:the multiplier
                n:the power
 * @retvalue   :the nth power of m
******************************************************************************/ 
u32 mypow(uint8_t m,uint8_t n)
{
	u32 result=1;	 
	while(n--)result*=m;    
	return result;
}

/*****************************************************************************
 * @name       :void LCD_ShowNum(uint16_t x,uint16_t y,u32 num,uint8_t len,uint8_t size)
 * @date       :2018-08-09 
 * @function   :Display number
 * @parameters :x:the bebinning x coordinate of the number
                y:the bebinning y coordinate of the number
								num:the number(0~4294967295)
								len:the length of the display number
								size:the size of display number
 * @retvalue   :None
******************************************************************************/  			 
void LCD_ShowNum(uint16_t x,uint16_t y,u32 num,uint8_t len,uint8_t size)
{         	
	uint8_t t,temp;
	uint8_t enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/mypow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				LCD_ShowChar(x+(size/2)*t,y,POINT_COLOR,BACK_COLOR,' ',size,0);
				continue;
			}else enshow=1; 
		}
	 	LCD_ShowChar(x+(size/2)*t,y,POINT_COLOR,BACK_COLOR,temp+'0',size,0); 
	}
} 
/*****************************************************************************
 * @name       :void GUI_DrawFont16(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
 * @date       :2018-08-09 
 * @function   :Display a single 16x16 Chinese character
 * @parameters :x:the bebinning x coordinate of the Chinese character
                y:the bebinning y coordinate of the Chinese character
								fc:the color value of Chinese character
								bc:the background color of Chinese character
								s:the start address of the Chinese character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void GUI_DrawFont16(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
{
	u8 i,j;
	u16 k;
	u16 HZnum;
	u16 x0=x;
	HZnum=sizeof(tfont16)/sizeof(typFNT_GB16);	//自动统计汉字数目
	
			
	for (k=0;k<HZnum;k++) 
	{
	  if ((tfont16[k].Index[0]==*(s))&&(tfont16[k].Index[1]==*(s+1)))
	  { 	LCD_SetWindows(x,y,x+16-1,y+16-1);
		    for(i=0;i<16*2;i++)
		    {
				for(j=0;j<8;j++)
		    	{	
					if(!mode) //非叠加方式
					{
						if(tfont16[k].Msk[i]&(0x80>>j))	Lcd_WriteData_16Bit(fc);
						else Lcd_WriteData_16Bit(bc);
					}
					else
					{
						POINT_COLOR=fc;
						if(tfont16[k].Msk[i]&(0x80>>j))	LCD_DrawPoint(x,y);//画一个点
						x++;
						if((x-x0)==16)
						{
							x=x0;
							y++;
							break;
						}
					}

				}
				
			}
			
			
		}				  	
		continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
	}

	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复窗口为全屏  
} 
/*****************************************************************************
 * @name       :void GUI_DrawFont24(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
 * @date       :2018-08-09 
 * @function   :Display a single 24x24 Chinese character
 * @parameters :x:the bebinning x coordinate of the Chinese character
                y:the bebinning y coordinate of the Chinese character
								fc:the color value of Chinese character
								bc:the background color of Chinese character
								s:the start address of the Chinese character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void GUI_DrawFont24(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
{
	u8 i,j;
	u16 k;
	u16 HZnum;
	u16 x0=x;
	HZnum=sizeof(tfont24)/sizeof(typFNT_GB24);	//自动统计汉字数目
		
			for (k=0;k<HZnum;k++) 
			{
			  if ((tfont24[k].Index[0]==*(s))&&(tfont24[k].Index[1]==*(s+1)))
			  { 	LCD_SetWindows(x,y,x+24-1,y+24-1);
				    for(i=0;i<24*3;i++)
				    {
							for(j=0;j<8;j++)
							{
								if(!mode) //非叠加方式
								{
									if(tfont24[k].Msk[i]&(0x80>>j))	Lcd_WriteData_16Bit(fc);
									else Lcd_WriteData_16Bit(bc);
								}
							else
							{
								POINT_COLOR=fc;
								if(tfont24[k].Msk[i]&(0x80>>j))	LCD_DrawPoint(x,y);//画一个点
								x++;
								if((x-x0)==24)
								{
									x=x0;
									y++;
									break;
								}
							}
						}
					}
					
					
				}				  	
				continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
			}

	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复窗口为全屏  
}

/*****************************************************************************
 * @name       :void GUI_DrawFont32(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
 * @date       :2018-08-09 
 * @function   :Display a single 32x32 Chinese character
 * @parameters :x:the bebinning x coordinate of the Chinese character
                y:the bebinning y coordinate of the Chinese character
								fc:the color value of Chinese character
								bc:the background color of Chinese character
								s:the start address of the Chinese character
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void GUI_DrawFont32(u16 x, u16 y, u16 fc, u16 bc, u8 *s,u8 mode)
{
	u8 i,j;
	u16 k;
	u16 HZnum;
	u16 x0=x;
	HZnum=sizeof(tfont32)/sizeof(typFNT_GB32);	//自动统计汉字数目
	for (k=0;k<HZnum;k++) 
			{
			  if ((tfont32[k].Index[0]==*(s))&&(tfont32[k].Index[1]==*(s+1)))
			  { 	LCD_SetWindows(x,y,x+32-1,y+32-1);
				    for(i=0;i<32*4;i++)
				    {
						for(j=0;j<8;j++)
				    	{
							if(!mode) //非叠加方式
							{
								if(tfont32[k].Msk[i]&(0x80>>j))	Lcd_WriteData_16Bit(fc);
								else Lcd_WriteData_16Bit(bc);
							}
							else
							{
								POINT_COLOR=fc;
								if(tfont32[k].Msk[i]&(0x80>>j))	LCD_DrawPoint(x,y);//画一个点
								x++;
								if((x-x0)==32)
								{
									x=x0;
									y++;
									break;
								}
							}
						}
					}
					
					
				}				  	
				continue;  //查找到对应点阵字库立即退出，防止多个汉字重复取模带来影响
			}
	
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复窗口为全屏  
} 


/*****************************************************************************
 * @name       :void Show_Str(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *str,uint8_t size,uint8_t mode)
 * @date       :2018-08-09 
 * @function   :Display Chinese and English strings
 * @parameters :x:the bebinning x coordinate of the Chinese and English strings
                y:the bebinning y coordinate of the Chinese and English strings
								fc:the color value of Chinese and English strings
								bc:the background color of Chinese and English strings
								str:the start address of the Chinese and English strings
								size:the size of Chinese and English strings
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/	   		   
void Show_Str(u16 x, u16 y, u16 fc, u16 bc, u8 *str,u8 size,u8 mode)
{					
	u16 x0=x;							  	  
  	u8 bHz=0;     //字符或者中文 
    while(*str!=0)//数据未结束
    { 
        if(!bHz)
        {
			if(x>(lcddev.width-size/2)||y>(lcddev.height-size)) 
			return; 
	        if(*str>0x80)bHz=1;//中文 
	        else              //字符
	        {          
		        if(*str==0x0D)//换行符号
		        {         
		            y+=size;
					x=x0;
		            str++; 
		        }  
		        else
				{
					if(size>16)//字库中没有集成12X24 16X32的英文字体,用8X16代替
					{  
					LCD_ShowChar(x,y,fc,bc,*str,16,mode);
					x+=8; //字符,为全字的一半 
					}
					else
					{
					LCD_ShowChar(x,y,fc,bc,*str,size,mode);
					x+=size/2; //字符,为全字的一半 
					}
				} 
				str++; 
		        
	        }
        }else//中文 
        {   
					if(x>(lcddev.width-size)||y>(lcddev.height-size)) 
					return;  
								bHz=0;//有汉字库    
					if(size==32)
					GUI_DrawFont32(x,y,fc,bc,str,mode);	 	
					else if(size==24)
					GUI_DrawFont24(x,y,fc,bc,str,mode);	
					else
					GUI_DrawFont16(x,y,fc,bc,str,mode);
				
	        str+=2; 
	        x+=size;//下一个汉字偏移	    
        }						 
    }   
}
// 显示字符串函数
void Show_Str1(u16 x, u16 y, u16 fc, u16 bc, u8 *str, u8 size, u8 mode)
{					
	u16 x0 = x;							  	  
	u8 bHz = 0;     // 字符或者中文标志 
	
	while(*str != 0) // 数据未结束
	{ 
		if(!bHz)
		{
			if(x > (lcddev.width - size/2) || y > (lcddev.height - size)) 
				return; 
			
			if(*str > 0x80) bHz = 1; // 中文 
			else // 字符
			{          
				if(*str == 0x0D) // 换行符号
				{         
					y += size;
					x = x0;
					str++; 
				}  
				else
				{
					// 根据选择的大小调用不同的字库
					if(size == 24) {
						LCD_ShowChar(x, y, fc, bc, *str, 24, mode);
						x += 12; // 24x12字体，宽度为12
					}
					else if(size == 16) {
						LCD_ShowChar(x, y, fc, bc, *str, 16, mode);
						x += 8; // 16x8字体，宽度为8
					}
					else if(size == 12) {
						LCD_ShowChar(x, y, fc, bc, *str, 12, mode);
						x += 6; // 12x6字体，宽度为6
					}
					else {
						// 默认使用16x8字体
						LCD_ShowChar(x, y, fc, bc, *str, 16, mode);
						x += 8;
					}
				} 
				str++; 
			}
		}
		else // 中文 
		{   
			if(x > (lcddev.width - size) || y > (lcddev.height - size)) 
				return;  
			
			bHz = 0; // 有汉字库    
			if(size == 32)
				GUI_DrawFont32(x, y, fc, bc, str, mode);	 	
			else if(size == 24)
				GUI_DrawFont24(x, y, fc, bc, str, mode);	
			else
				GUI_DrawFont16(x, y, fc, bc, str, mode);
			
			str += 2; 
			x += size; // 下一个汉字偏移	    
		}						 
	}   
}
/*****************************************************************************
 * @name       :void Gui_StrCenter(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *str,uint8_t size,uint8_t mode)
 * @date       :2018-08-09 
 * @function   :Centered display of English and Chinese strings
 * @parameters :x:the bebinning x coordinate of the Chinese and English strings
                y:the bebinning y coordinate of the Chinese and English strings
								fc:the color value of Chinese and English strings
								bc:the background color of Chinese and English strings
								str:the start address of the Chinese and English strings
								size:the size of Chinese and English strings
								mode:0-no overlying,1-overlying
 * @retvalue   :None
******************************************************************************/ 
void Gui_StrCenter(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t *str,uint8_t size,uint8_t mode)
{
	uint16_t len=strlen((const char *)str);
	uint16_t x1=(lcddev.width-len*8)/2;
	Show_Str(x1,y,fc,bc,str,size,mode);
} 
 
/*****************************************************************************
 * @name       :void Gui_Drawbmp16(uint16_t x,uint16_t y,const unsigned char *p)
 * @date       :2018-08-09 
 * @function   :Display a 16-bit BMP image
 * @parameters :x:the bebinning x coordinate of the BMP image
                y:the bebinning y coordinate of the BMP image
								p:the start address of image array
 * @retvalue   :None
******************************************************************************/ 
void Gui_Drawbmp16(uint16_t x,uint16_t y,const unsigned char *p) //显示40*40 QQ图片
{
  	int i; 
	unsigned char picH,picL; 
	LCD_SetWindows(x,y,x+40-1,y+40-1);//窗口设置
    for(i=0;i<40*40;i++)
	{	
	 	picL=*(p+i*2);	//数据低位在前
		picH=*(p+i*2+1);				
		Lcd_WriteData_16Bit(picH<<8|picL);  						
	}	
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);//恢复显示窗口为全屏	
}


#if(1)


//========================variable==========================//
uint16_t ColorTab[5]={RED,GREEN,BLUE,YELLOW,BRED};//定义颜色数组
//=====================end of variable======================//

/*****************************************************************************
 * @name       :void DrawTestPage(uint8_t *str)
 * @date       :2018-08-09 
 * @function   :Drawing test interface
 * @parameters :str:the start address of the Chinese and English strings
 * @retvalue   :None
******************************************************************************/ 
void DrawTestPage(uint8_t *str)
{
//绘制固定栏up
LCD_Clear(WHITE);
LCD_Fill(0,0,lcddev.width,20,BLUE);
//绘制固定栏down
LCD_Fill(0,lcddev.height-20,lcddev.width,lcddev.height,BLUE);
POINT_COLOR=WHITE;
Gui_StrCenter(0,2,WHITE,BLUE,str,16,1);//居中显示
Gui_StrCenter(0,lcddev.height-18,WHITE,BLUE,"www.lcdwiki.com",16,1);//居中显示
//绘制测试区域
//LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE);
}

/*****************************************************************************
 * @name       :void Display_ButtonUp(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
 * @date       :2018-08-24 
 * @function   :Drawing a 3D button
 * @parameters :x1:the bebinning x coordinate of the button
                y1:the bebinning y coordinate of the button
								x2:the ending x coordinate of the button
								y2:the ending y coordinate of the button
 * @retvalue   :None
******************************************************************************/ 
void Display_ButtonUp(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	POINT_COLOR=WHITE;
	LCD_DrawLine(x1,  y1,  x2,y1); //H
	LCD_DrawLine(x1,  y1,  x1,y2); //V
	
	POINT_COLOR=GRAY1;
	LCD_DrawLine(x1+1,y2-1,x2,y2-1);  //H
	POINT_COLOR=GRAY2;
	LCD_DrawLine(x1,y2,x2,y2);  //H
	POINT_COLOR=GRAY1;
	LCD_DrawLine(x2-1,y1+1,x2-1,y2);  //V
	POINT_COLOR=GRAY2;
  LCD_DrawLine(x2,y1,x2,y2); //V
}

/*****************************************************************************
 * @name       :void menu_test(void)
 * @date       :2018-08-24 
 * @function   :Drawing a 3D menu UI
 * @parameters :None
 * @retvalue   :None
******************************************************************************/ 
void menu_test(void)
{
	LCD_Clear(GRAY0);
	
	Display_ButtonUp(71,8,169,28); //x1,y1,x2,y2
	Gui_StrCenter(0,10,BRED,BLUE,"图形显示测试",16,1);

	Display_ButtonUp(71,38,169,58); //x1,y1,x2,y2
	Gui_StrCenter(0,40,BLACK,GRAY0,"纯色填充测试",16,1);
	
	Display_ButtonUp(71,68,169,88); //x1,y1,x2,y2
	Gui_StrCenter(0,70,BLUE,GRAY0,"中文显示测试",16,1);

	Display_ButtonUp(71,98,169,118); //x1,y1,x2,y2
	Gui_StrCenter(16,100,RED,GRAY0,"图片显示测试",16,1);
	delay_ms(1500);
	delay_ms(500);
}

/*****************************************************************************
 * @name       :void main_test(void)
 * @date       :2018-08-09 
 * @function   :Drawing the main Interface of the Comprehensive Test Program
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void main_test(void)
{
	DrawTestPage("综合测试程序");	
	Gui_StrCenter(0,23,RED,BLUE,"全动电子",16,1);//居中显示
	Gui_StrCenter(0,40,RED,BLUE,"综合测试程序",16,1);//居中显示	
	Gui_StrCenter(0,57,GREEN,BLUE,"1.3\" ST7789",16,1);//居中显示
	Gui_StrCenter(0,74,GREEN,BLUE,"240X240",16,1);//居中显示
	Gui_StrCenter(0,91,BLUE,BLUE,"2019-07-17",16,1);//居中显示
	delay_ms(1500);		
	delay_ms(1500);
}

/*****************************************************************************
 * @name       :void Test_Color(void)
 * @date       :2018-08-09 
 * @function   :Color fill test(white,black,red,green,blue)
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void Test_Color(void)
{
	//DrawTestPage("测试1:纯色填充测试");
	LCD_Fill(0,0,lcddev.width,lcddev.height,WHITE);
	Show_Str(20,30,BLUE,YELLOW,"BL Test",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,RED);
	Show_Str(20,30,BLUE,YELLOW,"RED ",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,GREEN);
	Show_Str(20,30,BLUE,YELLOW,"GREEN ",16,1);delay_ms(800);
	LCD_Fill(0,0,lcddev.width,lcddev.height,BLUE);
	Show_Str(20,30,RED,YELLOW,"BLUE ",16,1);delay_ms(800);
}

/*****************************************************************************
 * @name       :void Test_FillRec(void)
 * @date       :2018-08-09 
 * @function   :Rectangular display and fill test
								Display red,green,blue,yellow,pink rectangular boxes in turn,
								1500 milliseconds later,
								Fill the rectangle in red,green,blue,yellow and pink in turn
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void Test_FillRec(void)
{
	uint8_t i=0;
	DrawTestPage("GUI矩形填充测试");
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE);
	for (i=0; i<5; i++) 
	{
		POINT_COLOR=ColorTab[i];
		LCD_DrawRectangle(lcddev.width/2-40+(i*16),lcddev.height/2-40+(i*13),lcddev.width/2-40+(i*16)+30,lcddev.height/2-40+(i*13)+30); 
	}
	delay_ms(1500);	
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE); 
	for (i=0; i<5; i++) 
	{
		POINT_COLOR=ColorTab[i];
		LCD_DrawFillRectangle(lcddev.width/2-40+(i*16),lcddev.height/2-40+(i*13),lcddev.width/2-40+(i*16)+30,lcddev.height/2-40+(i*13)+30); 
	}
	delay_ms(1500);
}

/*****************************************************************************
 * @name       :void Test_Circle(void)
 * @date       :2018-08-09 
 * @function   :circular display and fill test
								Display red,green,blue,yellow,pink circular boxes in turn,
								1500 milliseconds later,
								Fill the circular in red,green,blue,yellow and pink in turn
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void Test_Circle(void)
{
	uint8_t i=0;
	DrawTestPage("GUI画圆填充测试");
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE);
	for (i=0; i<5; i++)  
		gui_circle(lcddev.width/2-40+(i*15),lcddev.height/2-25+(i*13),ColorTab[i],15,0);
	delay_ms(1500);	
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE); 
	for (i=0; i<5; i++) 
	  	gui_circle(lcddev.width/2-40+(i*15),lcddev.height/2-25+(i*13),ColorTab[i],15,1);
	delay_ms(1500);
}

/*****************************************************************************
 * @name       :void English_Font_test(void)
 * @date       :2018-08-09 
 * @function   :English display test
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void English_Font_test(void)
{
	DrawTestPage("英文显示测试");
	Show_Str(10,22,BLUE,YELLOW,"6X12:abcdefgh01234567",12,0);
	Show_Str(10,34,BLUE,YELLOW,"6X12:ABCDEFGH01234567",12,1);
	Show_Str(10,47,BLUE,YELLOW,"6X12:~!@#$%^&*()_+{}:",12,0);
	Show_Str(10,60,BLUE,YELLOW,"8X16:abcde01234",16,0);
	Show_Str(10,76,BLUE,YELLOW,"8X16:ABCDE01234",16,1);
	Show_Str(10,92,BLUE,YELLOW,"8X16:~!@#$%^&*()",16,0); 
	delay_ms(1200);
}

/*****************************************************************************
 * @name       :void Test_Triangle(void)
 * @date       :2018-08-09 
 * @function   :triangle display and fill test
								Display red,green,blue,yellow,pink triangle boxes in turn,
								1500 milliseconds later,
								Fill the triangle in red,green,blue,yellow and pink in turn
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void Test_Triangle(void)
{
	uint8_t i=0;
	DrawTestPage("GUI Tri填充测试");
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE);
	for(i=0;i<5;i++)
	{
		POINT_COLOR=ColorTab[i];
		Draw_Triangel(lcddev.width/2-40+(i*15),lcddev.height/2-12+(i*11),lcddev.width/2-25-1+(i*15),lcddev.height/2-12-26-1+(i*11),lcddev.width/2-10-1+(i*15),lcddev.height/2-12+(i*11));
	}
	delay_ms(1500);	
	LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE); 
	for(i=0;i<5;i++)
	{
		POINT_COLOR=ColorTab[i];
		Fill_Triangel(lcddev.width/2-40+(i*15),lcddev.height/2-12+(i*11),lcddev.width/2-25-1+(i*15),lcddev.height/2-12-26-1+(i*11),lcddev.width/2-10-1+(i*15),lcddev.height/2-12+(i*11));
	}
	delay_ms(1500);
}

/*****************************************************************************
 * @name       :void Chinese_Font_test(void)
 * @date       :2018-08-09 
 * @function   :chinese display test
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
void Chinese_Font_test(void)
{	

	DrawTestPage("中文显示测试");
  Show_Str(10,25,BLUE,YELLOW,"16X16:深:14.7",16,0);	
  Show_Str(10,45,BLUE,YELLOW,"24X24:中:107.7℃",24,1);
  Show_Str(10,70,BLUE,YELLOW,"32X32:字",32,1);
	LCD_ShowNum(10,110,275.789,7,16);
	delay_ms(1200);
}

/*****************************************************************************
 * @name       :void Pic_test(void)
 * @date       :2018-08-09 
 * @function   :picture display test
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
//void Pic_test(void)
//{
//	DrawTestPage("图片显示测试");
//	//LCD_Fill(0,20,lcddev.width,lcddev.height-20,WHITE);
//	Gui_Drawbmp16(20,30,gImage_qq);
//	Show_Str(20+12,75,BLUE,YELLOW,"QQ",16,1);
//	Gui_Drawbmp16(70,30,gImage_qq);
//	Show_Str(70+12,75,BLUE,YELLOW,"QQ",16,1);
////	Gui_Drawbmp16(150,30,gImage_qq);
////	Show_Str(150+12,75,BLUE,YELLOW,"QQ",16,1);
//	delay_ms(1200);
//}

/*****************************************************************************
 * @name       :void Rotate_Test(void)
 * @date       :2018-08-09 
 * @function   :rotate test
 * @parameters :None
 * @retvalue   :None
******************************************************************************/
//void Rotate_Test(void)
//{
//uint8_t i=0;
//uint8_t *Direction[4]={"Rotation:0","Rotation:90","Rotation:180","Rotation:270"};

//for(i=0;i<4;i++)
//{
//LCD_direction(i);
//DrawTestPage("屏幕旋转测试");
//Show_Str(20,30,BLUE,YELLOW,Direction[i],16,1);
//Gui_Drawbmp16(30,50,gImage_qq);
//delay_ms(1000);delay_ms(1000);
//}
//LCD_direction(USE_HORIZONTAL);
//}


#endif

