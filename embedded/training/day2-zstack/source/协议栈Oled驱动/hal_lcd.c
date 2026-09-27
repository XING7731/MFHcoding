/**************************************************************************************************
  Filename:       hal_lcd.c
  Revised:        $Date: 2010-06-21 17:31:27 -0700 (Mon, 21 Jun 2010) $
  Revision:       $Revision: 22794 $

  Description:    This file contains the interface to the HAL LCD Service.


  Copyright 2007-2010 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED 揂S IS?WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/

/**************************************************************************************************
 *                                           INCLUDES
 **************************************************************************************************/
#include "hal_types.h"
#include "hal_lcd.h"
#include "OSAL.h"
#include "OnBoard.h"
#include "hal_assert.h"

#if defined (ZTOOL_P1) || defined (ZTOOL_P2)
  #include "DebugTrace.h"
#endif

/**************************************************************************************************
 *                                          CONSTANTS
 **************************************************************************************************/
/*
  LCD pins

  //control
  P0.0 - LCD_BLK
  
  P1.2 - LCD_CS

  //spi
  P1.5 - CLK
  P1.6 - MOSI
  P1.7 - MISO  ->LCD_MODE
*/

/* LCD Control lines */
#define HAL_LCD_MODE_PORT 1
#define HAL_LCD_MODE_PIN  7

#define HAL_LCD_BLK_PORT 0
#define HAL_LCD_BLK_PIN  0

#define HAL_LCD_CS_PORT 1
#define HAL_LCD_CS_PIN  2

/* LCD SPI lines */
#define HAL_LCD_CLK_PORT 1
#define HAL_LCD_CLK_PIN  5

#define HAL_LCD_MOSI_PORT 1
#define HAL_LCD_MOSI_PIN  6

#define HAL_LCD_MISO_PORT 1
#define HAL_LCD_MISO_PIN  7

/* SPI settings */
#define HAL_SPI_CLOCK_POL_LO       0x00
#define HAL_SPI_CLOCK_PHA_0        0x00
#define HAL_SPI_TRANSFER_MSB_FIRST 0x20

/* LCD lines */
#define LCD_MAX_LINE_COUNT              8
#define LCD_MAX_LINE_LENGTH             21
#define LCD_MAX_BUF                     21

/* Defines for HW LCD */

/* Set power save mode */
#define OSC_OFF                         0x00
#define OSC_ON                          0x01
#define POWER_SAVE_OFF                  0x00
#define POWER_SAVE_ON                   0x02
#define SET_POWER_SAVE_MODE(options)    HalLcd_HW_Control(0x0C | (options))

/* Function Set */
#define CGROM                           0x00
#define CGRAM                           0x01
#define COM_FORWARD                     0x00
#define COM_BACKWARD                    0x02
#define TWO_LINE                        0x00
#define THREE_LINE                      0x04
#define FUNCTION_SET(options)           HalLcd_HW_Control(0x10 | (options))

/* Set Display Start Line */
#define LINE1                           0x00
#define LINE2                           0x01
#define LINE3                           0x02
#define LINE4                           0x03
//以下是自已添加的，因为LCD只能支持5x7的ASCII码，所以可以有0-7行
#define LINE5                           0x04
#define LINE6                           0x05
#define LINE7                           0x06
#define LINE8                           0x07

#define SET_DISPLAY_START_LINE(line)    HalLcd_HW_Control(0xB0 | (line))

/* Bias control */
#define BIAS_1_9                        0xA2
#define BIAS_1_7                        0xA3
#define SET_BIAS_CTRL(bias)             HalLcd_HW_Control(bias)

/* Power control */
#define VOLTAGE_DIVIDER_OFF             0x00
#define VOLTAGE_DIVIDER_ON              0x01
#define CONVERTER_AND_REG_OFF           0x00
#define CONVERTER_AND_REG_ON            0x04
#define SET_POWER_CTRL(options)         HalLcd_HW_Control(0x20 | (options))

// Set display control
#define DISPLAY_CTRL_ON                 0x01
#define DISPLAY_CTRL_OFF                0x00
#define DISPLAY_CTRL_BLINK_ON           0x02
#define DISPLAY_CTRL_BLINK_OFF          0x00
#define DISPLAY_CTRL_CURSOR_ON          0x04
#define DISPLAY_CTRL_CURSOR_OFF         0x00
#define SET_DISPLAY_CTRL(options)       HalLcd_HW_Control(0x28 | (options))

/* Set DD/ CGRAM address */
#define SET_DDRAM_ADDR(page,charIndex)  HalLcd_HW_Control(0xB0 | (page)); \
                                        HalLcd_HW_Control(((charIndex >>4) & 0x0f) + 0x10);\
                                        HalLcd_HW_Control(charIndex & 0x0f)

#define SET_GCRAM_CHAR(specIndex)       HalLcd_HW_Control(0xC0 | (specIndex))

/* Set ICONRAM address */
#define CONTRAST_CTRL_REGISTER          0x10
#define SET_ICONRAM_ADDR(addr)          HalLcd_HW_Control(0x40 | (addr))

/* Set double height */
#define LINE_1_AND_2                    0x01
#define LINE_2_AND_3                    0x02
#define NORMAL_DISPLAY                  0x00
#define SET_DOUBLE_HEIGHT(options)      HalLcd_HW_Control(0x08 | (options))

//所有ASCII码字库，此LCD不带字库5X7:X =5 Y = 7
static const uint8 ascii_table_5x7[95][5]={
0x00,0x00,0x00,0x00,0x00,//space
0x00,0x00,0x4f,0x00,0x00,//!
0x00,0x07,0x00,0x07,0x00,//"
0x14,0x7f,0x14,0x7f,0x14,//#
0x24,0x2a,0x7f,0x2a,0x12,//$
0x23,0x13,0x08,0x64,0x62,//%
0x36,0x49,0x55,0x22,0x50,//&
0x00,0x05,0x07,0x00,0x00,//]
0x00,0x1c,0x22,0x41,0x00,//(
0x00,0x41,0x22,0x1c,0x00,//)
0x14,0x08,0x3e,0x08,0x14,//*
0x08,0x08,0x3e,0x08,0x08,//+
0x00,0x50,0x30,0x00,0x00,//,
0x08,0x08,0x08,0x08,0x08,//-
0x00,0x60,0x60,0x00,0x00,//.
0x20,0x10,0x08,0x04,0x02,///
0x3e,0x51,0x49,0x45,0x3e,//0
0x00,0x42,0x7f,0x40,0x00,//1
0x42,0x61,0x51,0x49,0x46,//2
0x21,0x41,0x45,0x4b,0x31,//3
0x18,0x14,0x12,0x7f,0x10,//4
0x27,0x45,0x45,0x45,0x39,//5
0x3c,0x4a,0x49,0x49,0x30,//6
0x01,0x71,0x09,0x05,0x03,//7
0x36,0x49,0x49,0x49,0x36,//8
0x06,0x49,0x49,0x29,0x1e,//9
0x00,0x36,0x36,0x00,0x00,//:
0x00,0x56,0x36,0x00,0x00,//;
0x08,0x14,0x22,0x41,0x00,//<
0x14,0x14,0x14,0x14,0x14,//=
0x00,0x41,0x22,0x14,0x08,//>
0x02,0x01,0x51,0x09,0x06,//?
0x32,0x49,0x79,0x41,0x3e,//@
0x7e,0x11,0x11,0x11,0x7e,//A
0x7f,0x49,0x49,0x49,0x36,//B
0x3e,0x41,0x41,0x41,0x22,//C
0x7f,0x41,0x41,0x22,0x1c,//D
0x7f,0x49,0x49,0x49,0x41,//E
0x7f,0x09,0x09,0x09,0x01,//F
0x3e,0x41,0x49,0x49,0x7a,//G
0x7f,0x08,0x08,0x08,0x7f,//H
0x00,0x41,0x7f,0x41,0x00,//I
0x20,0x40,0x41,0x3f,0x01,//J
0x7f,0x08,0x14,0x22,0x41,//K
0x7f,0x40,0x40,0x40,0x40,//L
0x7f,0x02,0x0c,0x02,0x7f,//M
0x7f,0x04,0x08,0x10,0x7f,//N
0x3e,0x41,0x41,0x41,0x3e,//O
0x7f,0x09,0x09,0x09,0x06,//P
0x3e,0x41,0x51,0x21,0x5e,//Q
0x7f,0x09,0x19,0x29,0x46,//R
0x46,0x49,0x49,0x49,0x31,//S
0x01,0x01,0x7f,0x01,0x01,//T
0x3f,0x40,0x40,0x40,0x3f,//U
0x1f,0x20,0x40,0x20,0x1f,//V
0x3f,0x40,0x38,0x40,0x3f,//W
0x63,0x14,0x08,0x14,0x63,//X
0x07,0x08,0x70,0x08,0x07,//Y
0x61,0x51,0x49,0x45,0x43,//Z
0x00,0x7f,0x41,0x41,0x00,//[
0x02,0x04,0x08,0x10,0x20,// 斜杠
0x00,0x41,0x41,0x7f,0x00,//]
0x04,0x02,0x01,0x02,0x04,//^
0x40,0x40,0x40,0x40,0x40,//_
0x01,0x02,0x04,0x00,0x00,//`
0x20,0x54,0x54,0x54,0x78,//a
0x7f,0x48,0x48,0x48,0x30,//b
0x38,0x44,0x44,0x44,0x44,//c
0x30,0x48,0x48,0x48,0x7f,//d
0x38,0x54,0x54,0x54,0x58,//e
0x00,0x08,0x7e,0x09,0x02,//f
0x48,0x54,0x54,0x54,0x3c,//g
0x7f,0x08,0x08,0x08,0x70,//h
0x00,0x00,0x7a,0x00,0x00,//i
0x20,0x40,0x40,0x3d,0x00,//j
0x7f,0x20,0x28,0x44,0x00,//k
0x00,0x41,0x7f,0x40,0x00,//l
0x7c,0x04,0x38,0x04,0x7c,//m
0x7c,0x08,0x04,0x04,0x78,//n
0x38,0x44,0x44,0x44,0x38,//o
0x7c,0x14,0x14,0x14,0x08,//p
0x08,0x14,0x14,0x14,0x7c,//q
0x7c,0x08,0x04,0x04,0x08,//r
0x48,0x54,0x54,0x54,0x24,//s
0x04,0x04,0x3f,0x44,0x24,//t
0x3c,0x40,0x40,0x40,0x3c,//u
0x1c,0x20,0x40,0x20,0x1c,//v
0x3c,0x40,0x30,0x40,0x3c,//w
0x44,0x28,0x10,0x28,0x44,//x
0x04,0x48,0x30,0x08,0x04,//y
0x44,0x64,0x54,0x4c,0x44,//z
0x08,0x36,0x41,0x41,0x00,//{
0x00,0x00,0x77,0x00,0x00,//|
0x00,0x41,0x41,0x36,0x08,//}
0x04,0x02,0x02,0x02,0x01,//~
};


/**************************************************************************************************
 *                                           MACROS
 **************************************************************************************************/

#define HAL_IO_SET(port, pin, val)        HAL_IO_SET_PREP(port, pin, val)
#define HAL_IO_SET_PREP(port, pin, val)   st( P##port##_##pin## = val; )

#define HAL_CONFIG_IO_OUTPUT(port, pin, val)      HAL_CONFIG_IO_OUTPUT_PREP(port, pin, val)
#define HAL_CONFIG_IO_OUTPUT_PREP(port, pin, val) st( P##port##SEL &= ~BV(pin); \
                                                      P##port##_##pin## = val; \
                                                      P##port##DIR |= BV(pin); )

#define HAL_CONFIG_IO_PERIPHERAL(port, pin)      HAL_CONFIG_IO_PERIPHERAL_PREP(port, pin)
#define HAL_CONFIG_IO_PERIPHERAL_PREP(port, pin) st( P##port##SEL |= BV(pin); )



/* SPI interface control */
#define LCD_SPI_BEGIN()     HAL_IO_SET(HAL_LCD_CS_PORT,  HAL_LCD_CS_PIN,  0); /* chip select */
#define LCD_SPI_END()                                                         \
{                                                                             \
  asm("NOP");                                                                 \
  asm("NOP");                                                                 \
  asm("NOP");                                                                 \
  asm("NOP");                                                                 \
  HAL_IO_SET(HAL_LCD_CS_PORT,  HAL_LCD_CS_PIN,  1); /* chip select */         \
}
/* clear the received and transmit byte status, write tx data to buffer, wait till transmit done */
#define LCD_SPI_TX(x)                   { U1CSR &= ~(BV(2) | BV(1)); U1DBUF = x; while( !(U1CSR & BV(1)) ); }
#define LCD_SPI_WAIT_RXRDY()            { while(!(U1CSR & BV(1))); }


/* Control macros */
#define LCD_DO_WRITE()        HAL_IO_SET(HAL_LCD_MODE_PORT,  HAL_LCD_MODE_PIN,  1);
#define LCD_DO_CONTROL()      HAL_IO_SET(HAL_LCD_MODE_PORT,  HAL_LCD_MODE_PIN,  0);

//#define LCD_ACTIVATE_RESET()  HAL_IO_SET(HAL_LCD_RESET_PORT, HAL_LCD_RESET_PIN, 0);
#define  LCD_ACTIVATE_RESET()   HalLcd_HW_Control(0xE2)
//#define LCD_RELEASE_RESET()   HAL_IO_SET(HAL_LCD_RESET_PORT, HAL_LCD_RESET_PIN, 1);
#define LCD_RELEASE_RESET()     asm("NOP")
#if (HAL_LCD == TRUE)
/**************************************************************************************************
 *                                       LOCAL VARIABLES
 **************************************************************************************************/
typedef const char *LcdScreen;
typedef struct
{
  LcdScreen  Lcd[6];
  signed char header;
  signed char tail;
  uint8 lcdbuf[LCD_MAX_BUF];  //变量显示缓存
}LcdCache;
LcdCache LcdBuffer;

//static uint8 *Lcd_Line1;

/**************************************************************************************************
 *                                       FUNCTIONS - API
 **************************************************************************************************/

void HalLcd_HW_Init(void);
void HalLcd_HW_WaitUs(uint16 i);
void HalLcd_HW_Clear(void);
void HalLcd_HW_ClearAllSpecChars(void);
void HalLcd_HW_Control(uint8 cmd);
void HalLcd_HW_Write(uint8 data);
//void HalLcd_HW_SetContrast(uint8 value);
void HalLcd_HW_WriteChar(uint8 line, uint8 col, char text);
void HalLcd_HW_WriteLine(uint8 line, const char *pText);
void HalLcdBufferInit(void);
void set_ddram_line_col(uint8 line,uint8 col);
/*
作用    设置LCD 文本显示的其实行和列
参数1   line,范围:0~7,即能够显示的行为1~8行，也就是lcd手册里提到的page
参数2   col,范围:0~127,即lcd的总列数，显示的起始位置可以设置到每一列
*/
void set_ddram_line_col(uint8 line,uint8 col)
{
  uint8 page,coll,coll_l,coll_h;
  page = line;
  coll = col;
  coll_h = coll>>4;
  coll_l = coll&0x0f;
  HalLcd_HW_Control(0xB0+page);
  HalLcd_HW_WaitUs(15); // 15 us
  HalLcd_HW_Control(0x10+coll_h);
  HalLcd_HW_WaitUs(15); // 15 us
  HalLcd_HW_Control(0x00+coll_l);
  HalLcd_HW_WaitUs(15); // 15 us
  
}

#endif //LCD
/******************************************************************************
*名    称：HalLcdBufferInit
*功    能：初始化LCDBUFFER 为初始值
*入口参数：无
*出口参数：无
*返回值  ：无
*******************************************************************************/
void HalLcdBufferInit(void)
{
  uint8 i;
  for(i=0;i < 6;i++ )
  {
    LcdBuffer.Lcd[i] = NULL;
  }
  LcdBuffer.lcdbuf[0] = '\0';
  LcdBuffer.header = 0;
  LcdBuffer.tail = 0;
}
/******************************************************************************
*名    称：HalLcdBufferAdd
*功    能：向显示缓存中增加显示条目
*入口参数：要显示的条目，将出现在动态条目的最后一行
*出口参数：无
*返回值  ：无
*******************************************************************************/
void HalLcdBufferAdd(const char *str)
{
  LcdBuffer.Lcd[LcdBuffer.tail++] = str;    //向显存的尾部添加显示数值指针
  if(LcdBuffer.tail == 6)                   //调整显存指针
  {
    LcdBuffer.tail = 0;
  }
  if(LcdBuffer.header == LcdBuffer.tail)
  {
     LcdBuffer.header++;
     if(LcdBuffer.header == 6)
     {
      LcdBuffer.header = 0; 
     }
  }
}
/******************************************************************************
*名    称：HalLcdDisplayRoll
*功    能：显示LCD缓存中的数据
*入口参数：无
*出口参数：无
*返回值  ：无
*******************************************************************************/
void HalLcdDisplayRoll(void)
{
  uint8 i,j;
  j = 0;
  i = LcdBuffer.header;
  do                                        //以显存的头指针向尾指针的顺序
  {                                         //显示显存中的数值
    j++;
    HalLcd_HW_WriteLine(j, LcdBuffer.Lcd[i]);    
    i++;
    if(i== 6)
    {
     i = 0; 
    }
    
  }while(!(i == LcdBuffer.tail));
}
/******************************************************************************
*名    称：HalLcdBufferAddFix
*功    能：向显存中添加显示数据
*入口参数：无
*出口参数：无
*返回值  ：无
*******************************************************************************/
void HalLcdBufferAddFix(const char *str,uint16 value, uint8 format,uint8 offset)
{
  uint8 tmpLen;
  uint32 err;
  
  //将数值转化成字符串
  tmpLen = (uint8)osal_strlen( (char*)str );
  osal_memcpy( LcdBuffer.lcdbuf, str, tmpLen );
  LcdBuffer.lcdbuf[tmpLen] = ' ';
  err = (uint32)(value);
  _ltoa( err, &LcdBuffer.lcdbuf[tmpLen+1], format );

  if(offset > 4)
  {
    offset = 4; 
  }
  LcdBuffer.Lcd[(LcdBuffer.tail - (offset+1)+6) % 6] =( const char *) LcdBuffer.lcdbuf;
  
}
/**************************************************************************************************
 * @fn      HalLcdInit
 *
 * @brief   Initilize LCD Service
 *
 * @param   init - pointer to void that contains the initialized value
 *
 * @return  None
 **************************************************************************************************/
void HalLcdInit(void)
{
#if (HAL_LCD == TRUE)
//  Lcd_Line1 = NULL;
  HalLcd_HW_Init();
#endif
}

/*************************************************************************************************
 *                    LCD EMULATION FUNCTIONS
 *
 * Some evaluation boards are equipped with Liquid Crystal Displays
 * (LCD) which may be used to display diagnostic information. These
 * functions provide LCD emulation, sending the diagnostic strings
 * to Z-Tool via the RS232 serial port. These functions are enabled
 * when the "LCD_SUPPORTED" compiler flag is placed in the makefile.
 *
 * Most applications update both lines (1 and 2) of the LCD whenever
 * text is posted to the device. This emulator assumes that line 1 is
 * updated first (saved locally) and the formatting and send operation
 * is triggered by receipt of line 2. Nothing will be transmitted if
 * only line 1 is updated.
 *
 *************************************************************************************************/


/**************************************************************************************************
 * @fn      HalLcdWriteString
 *
 * @brief   Write a string to the LCD
 *
 * @param   str    - pointer to the string that will be displayed
 *          option - display options
 *
 * @return  None
 **************************************************************************************************/
void HalLcdWriteString ( char *str, uint8 option)
{
#if (HAL_LCD == TRUE)
  /* Display the string */
  HalLcd_HW_WriteLine (option, str);

#endif //HAL_LCD

}

/**************************************************************************************************
 * @fn      HalLcdWriteValue
 *
 * @brief   Write a value to the LCD
 *
 * @param   value  - value that will be displayed
 *          radix  - 8, 10, 16
 *          option - display options
 *
 * @return  None
 **************************************************************************************************/
void HalLcdWriteValue ( uint32 value, const uint8 radix, uint8 option)
{
#if (HAL_LCD == TRUE)
  uint8 buf[LCD_MAX_BUF];

  _ltoa( value, &buf[0], radix );
  HalLcdWriteString( (char*)buf, option );
#endif
}

/**************************************************************************************************
 * @fn      HalLcdWriteScreen
 *
 * @brief   Write a value to the LCD
 *
 * @param   line1  - string that will be displayed on line 1
 *          line2  - string that will be displayed on line 2
 *
 * @return  None
 **************************************************************************************************/
void HalLcdWriteScreen( char *line1, char *line2 )
{
#if (HAL_LCD == TRUE)
  HalLcdWriteString( line1, 1 );
  HalLcdWriteString( line2, 2 );
#endif
}

/**************************************************************************************************
 * @fn      HalLcdWriteStringValue
 *
 * @brief   Write a string followed by a value to the LCD
 *
 * @param   title  - Title that will be displayed before the value
 *          value  - value
 *          format - redix
 *          line   - line number
 *
 * @return  None
 **************************************************************************************************/
void HalLcdWriteStringValue( char *title, uint16 value, uint8 format, uint8 line )
{
#if (HAL_LCD == TRUE)
  uint8 tmpLen;
  uint8 buf[LCD_MAX_BUF];
  uint32 err;

  tmpLen = (uint8)osal_strlen( (char*)title );
  osal_memcpy( buf, title, tmpLen );
  buf[tmpLen] = ' ';
  err = (uint32)(value);
  _ltoa( err, &buf[tmpLen+1], format );
  HalLcdWriteString( (char*)buf, line );		
#endif
}

/**************************************************************************************************
 * @fn      HalLcdWriteStringValue
 *
 * @brief   Write a string followed by a value to the LCD
 *
 * @param   title   - Title that will be displayed before the value
 *          value1  - value #1
 *          format1 - redix of value #1
 *          value2  - value #2
 *          format2 - redix of value #2
 *          line    - line number
 *
 * @return  None
 **************************************************************************************************/
void HalLcdWriteStringValueValue( char *title, uint16 value1, uint8 format1,
                                  uint16 value2, uint8 format2, uint8 line )
{

#if (HAL_LCD == TRUE)

  uint8 tmpLen;
  uint8 buf[LCD_MAX_BUF];
  uint32 err;

  tmpLen = (uint8)osal_strlen( (char*)title );
  if ( tmpLen )
  {
    osal_memcpy( buf, title, tmpLen );
    buf[tmpLen++] = ' ';
  }

  err = (uint32)(value1);
  _ltoa( err, &buf[tmpLen], format1 );
  tmpLen = (uint8)osal_strlen( (char*)buf );

  buf[tmpLen++] = ',';
  buf[tmpLen++] = ' ';
  err = (uint32)(value2);
  _ltoa( err, &buf[tmpLen], format2 );

  HalLcdWriteString( (char *)buf, line );		

#endif
}

/**************************************************************************************************
 * @fn      HalLcdDisplayPercentBar
 *
 * @brief   Display percentage bar on the LCD
 *
 * @param   title   -
 *          value   -
 *
 * @return  None
 **************************************************************************************************/
void HalLcdDisplayPercentBar( char *title, uint8 value )
{
#if (HAL_LCD == TRUE)

  uint8 percent;
  uint8 leftOver;
  uint8 buf[17];
  uint32 err;
  uint8 x;

  /* Write the title: */
  HalLcdWriteString( title, HAL_LCD_LINE_1 );

  if ( value > 100 )
    value = 100;

  /* convert to blocks */
  percent = (uint8)(value / 10);
  leftOver = (uint8)(value % 10);

  /* Make window */
  osal_memcpy( buf, "[          ]  ", 15 );

  for ( x = 0; x < percent; x ++ )
  {
    buf[1+x] = '>';
  }

  if ( leftOver >= 5 )
    buf[1+x] = '+';

  err = (uint32)value;
  _ltoa( err, (uint8*)&buf[13], 10 );

  HalLcdWriteString( (char*)buf, HAL_LCD_LINE_2 );

#endif

}

#if (HAL_LCD == TRUE)
/**************************************************************************************************
 *                                    HARDWARE LCD
 **************************************************************************************************/

/**************************************************************************************************
 * @fn      halLcd_ConfigIO
 *
 * @brief   Configure IO lines needed for LCD control.
 *
 * @param   None
 *
 * @return  None
 **************************************************************************************************/
static void halLcd_ConfigIO(void)
{
  /* GPIO configuration */
  HAL_CONFIG_IO_OUTPUT(HAL_LCD_MODE_PORT,  HAL_LCD_MODE_PIN,  1);
  HAL_CONFIG_IO_OUTPUT(HAL_LCD_BLK_PORT, HAL_LCD_BLK_PIN, 1);
  HAL_CONFIG_IO_OUTPUT(HAL_LCD_CS_PORT,    HAL_LCD_CS_PIN,    1);
}

/**************************************************************************************************
 * @fn      halLcd_ConfigSPI
 *
 * @brief   Configure SPI lines needed for talking to LCD.
 *
 * @param   None
 *
 * @return  None
 **************************************************************************************************/
static void halLcd_ConfigSPI(void)
{
  /* UART/SPI Peripheral configuration */

   uint8 baud_exponent;
   uint8 baud_mantissa;

  /* Set SPI on UART 1 alternative 2 */
  PERCFG |= 0x02;

  /* Configure clk, master out and master in lines */
  HAL_CONFIG_IO_PERIPHERAL(HAL_LCD_CLK_PORT,  HAL_LCD_CLK_PIN);
  HAL_CONFIG_IO_PERIPHERAL(HAL_LCD_MOSI_PORT, HAL_LCD_MOSI_PIN);
// HAL_CONFIG_IO_PERIPHERAL(HAL_LCD_MISO_PORT, HAL_LCD_MISO_PIN);


  /* Set SPI speed to 1 MHz (the values assume system clk of 32MHz)
   * Confirm on board that this results in 1MHz spi clk.
   */
  baud_exponent = 15;
  baud_mantissa =  0;

  /* Configure SPI */
  U1UCR  = 0x80;      /* Flush and goto IDLE state. 8-N-1. */
  U1CSR  = 0x00;      /* SPI mode, master. */
  U1GCR  = HAL_SPI_TRANSFER_MSB_FIRST | HAL_SPI_CLOCK_PHA_0 | HAL_SPI_CLOCK_POL_LO | baud_exponent;
  U1BAUD = baud_mantissa;
}

/**************************************************************************************************
 * @fn      HalLcd_HW_Init
 *
 * @brief   Initilize HW LCD Driver.
 *
 * @param   None
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_Init(void)
{
  /* Initialize LCD IO lines */
  halLcd_ConfigIO();
  /* Initialize SPI */
  halLcd_ConfigSPI();
  
  HalLcd_HW_Control(0xae);//--turn off oled panel
  HalLcd_HW_Control(0x00);//---set low column address
  HalLcd_HW_Control(0x10);//---set high column address
  HalLcd_HW_Control(0x40);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
  HalLcd_HW_Control(0x81);//--set contrast control register
  HalLcd_HW_Control(0xcf); // Set SEG Output Current Brightness
  HalLcd_HW_Control(0xa1);//--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
  HalLcd_HW_Control(0xc8);//Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
  HalLcd_HW_Control(0xa6);//--set normal display
  HalLcd_HW_Control(0xa8);//--set multiplex ratio(1 to 64)
  HalLcd_HW_Control(0x3f);//--1/64 duty
  HalLcd_HW_Control(0xd3);//-set display offset    Shift Mapping RAM Counter (0x00~0x3F)
  HalLcd_HW_Control(0x00);//-not offset
  HalLcd_HW_Control(0xd5);//--set display clock divide ratio/oscillator frequency
  HalLcd_HW_Control(0x80);//--set divide ratio, Set Clock as 100 Frames/Sec
  HalLcd_HW_Control(0xd9);//--set pre-charge period
  HalLcd_HW_Control(0xf1);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
  HalLcd_HW_Control(0xda);//--set com pins hardware configuration
  HalLcd_HW_Control(0x12);
  HalLcd_HW_Control(0xdb);//--set vcomh
  HalLcd_HW_Control(0x40);//Set VCOM Deselect Level
  HalLcd_HW_Control(0x20);//-Set Page Addressing Mode (0x00/0x01/0x02)
  HalLcd_HW_Control(0x02);//
  HalLcd_HW_Control(0x8d);//--set Charge Pump enable/disable
  HalLcd_HW_Control(0x14);//--set(0x10) disable
  HalLcd_HW_Control(0xa4);// Disable Entire Display On (0xa4/0xa5)
  HalLcd_HW_Control(0xa6);// Disable Inverse Display On (0xa6/a7) 
  HalLcd_HW_Control(0xaf);//--turn on oled panel

	/*初始化后一定要清屏*/
	HalLcd_HW_Clear();
	set_ddram_line_col(0,0); 
}

/**************************************************************************************************
 * @fn      HalLcd_HW_Control
 *
 * @brief   Write 1 command to the LCD
 *
 * @param   uint8 cmd - command to be written to the LCD
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_Control(uint8 cmd)
{
  LCD_SPI_BEGIN();
  LCD_DO_CONTROL();
  LCD_SPI_TX(cmd);
  LCD_SPI_WAIT_RXRDY();
  LCD_SPI_END();
}

/**************************************************************************************************
 * @fn      HalLcd_HW_Write
 *
 * @brief   Write 1 byte to the LCD
 *
 * @param   uint8 data - data to be written to the LCD
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_Write(uint8 data)
{
  LCD_SPI_BEGIN();
  LCD_DO_WRITE();
  LCD_SPI_TX(data);
  LCD_SPI_WAIT_RXRDY();
  LCD_SPI_END();
}


/**************************************************************************************************
 * @fn      HalLcd_HW_Clear
 *
 * @brief   Clear the HW LCD
 *
 * @param   None
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_Clear(void)
{
  uint8 n,i;
  for(i = 0 ; i < 8 ; i++)
  {
    SET_DDRAM_ADDR(i,0x00);   
    for (n = 0; n < 128; n++)
    {
    //HalLcd_HW_Write(' ');
       HalLcd_HW_Write(0x00);
    }
  }
}

/**************************************************************************************************
 * @fn      HalLcd_HW_ClearAllSpecChars
 *
 * @brief   Clear all special chars
 *
 * @param   None
 *
 * @return  None
 **************************************************************************************************/
/*  不可能此LCD不支持
void HalLcd_HW_ClearAllSpecChars(void)
{
  uint8 n = 0;

  SET_GCRAM_CHAR(0);
  
  for (n = 0; n < (8 * 8); n++)
  {
    HalLcd_HW_Write(0x00);
  }
}
*/
/**************************************************************************************************
 * @fn      HalLcd_HW_WriteChar
 *
 * @brief   Write one char to the display
 *
 * @param   uint8 line - line number that the char will be displayed
 *          uint8 col - colum where the char will be displayed
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_WriteChar(uint8 line, uint8 col, char text)
{
  if (col < LCD_MAX_LINE_LENGTH)
  {
   // SET_DDRAM_ADDR((line - 1) * LCD_MAX_LINE_LENGTH + col);
    SET_DDRAM_ADDR(line-1,col*6);               //设定坐标
    if((text >= 0x20) && (text < 0x7E))
    {
      uint8 i = text - 0x20;                  //char 型的数据可以和8位整型进行计算的
      
      for(uint8 k = 0 ; k <5 ; k++) 
      {
        HalLcd_HW_Write(ascii_table_5x7[i][k]);
      }
    }
    else  //不可显示，将有空格代替
    {
      for(uint8 k = 0 ; k < 5 ; k++)
      HalLcd_HW_Write(0x00);
    }
    HalLcd_HW_Write(0x00);                   //相对第六列有写空，以分开相邻字符
  }
  else  
  {
    return;
  }
}

/**************************************************************************************************
 * @fn          halLcdWriteLine
 *
 * @brief       Write one line on display
 *
 * @param       uint8 line - display line
 *              char *pText - text buffer to write
 *
 * @return      none
 **************************************************************************************************/
void HalLcd_HW_WriteLine(uint8 line, const char *pText)
{
  uint8 count;
  uint8 totalLength = (uint8)osal_strlen( (char *)pText );

  /* Write the content first */
  for (count=0; count<totalLength; count++)
  {
    HalLcd_HW_WriteChar(line, count, (*(pText++)));
  }

  /* Write blank spaces to rest of the line */
  for(count=totalLength; count<LCD_MAX_LINE_LENGTH;count++)
  {
    HalLcd_HW_WriteChar(line, count, ' ');
  }
}

/**************************************************************************************************
 * @fn      HalLcd_HW_WaitUs
 *
 * @brief   wait for x us. @ 32MHz MCU clock it takes 32 "nop"s for 1 us delay.
 *
 * @param   x us. range[0-65536]
 *
 * @return  None
 **************************************************************************************************/
void HalLcd_HW_WaitUs(uint16 microSecs)
{
  while(microSecs--)
  {
    /* 32 NOPs == 1 usecs */
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop");
    asm("nop"); asm("nop");
  }
}
#endif


/**************************************************************************************************
**************************************************************************************************/



