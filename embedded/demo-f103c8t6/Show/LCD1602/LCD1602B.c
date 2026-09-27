#include "main.h"
#include "lcd1602b.h"

IIC_HardWareTypedef iicpcf8574;
static uint8_t LCD_data = 0x08;  // 默认E=0

/********************** 基础LCD操作函数 **********************/
static void LCD_Enable(void)
{
    LCD_data |= (1 << LCD_E_PIN);   // E=1
    PCF8574_WriteOneByte(LCD_data);
    delay_us(1);
    LCD_data &= ~(1 << LCD_E_PIN);  // E=0
    PCF8574_WriteOneByte(LCD_data);
    delay_us(1);
}

void LCD_Write_Com(uint8_t cmd) 
{
    delay_us(16);
    // 设置RS=0, RW=0
    LCD_data &= ~((1 << LCD_RS_PIN) | (1 << LCD_RW_PIN));
    PCF8574_WriteOneByte(LCD_data);
    delay_us(2);
    
    // 发送高4位
    LCD_data = (LCD_data & 0x0F) | (cmd & 0xF0);
    PCF8574_WriteOneByte(LCD_data);
    LCD_Enable();
    
    // 发送低4位
    cmd <<= 4;
    LCD_data = (LCD_data & 0x0F) | (cmd & 0xF0);
    PCF8574_WriteOneByte(LCD_data);
    delay_us(2);
    LCD_Enable();
}

void LCD_Write_Data(uint8_t dat) 
{
    delay_us(16);
    // 设置RS=1, RW=0
    LCD_data = (LCD_data | (1 << LCD_RS_PIN)) & ~(1 << LCD_RW_PIN);
    PCF8574_WriteOneByte(LCD_data);
    delay_us(2);
    
    // 发送高4位
    LCD_data = (LCD_data & 0x0F) | (dat & 0xF0);
    PCF8574_WriteOneByte(LCD_data);
    delay_us(2);
    LCD_Enable();
    
    // 发送低4位
    dat <<= 4;
    LCD_data = (LCD_data & 0x0F) | (dat & 0xF0);
    PCF8574_WriteOneByte(LCD_data);
    delay_us(2);
    LCD_Enable();
}

/********************** LCD高级功能函数 **********************/
void LCD1602_Init(void) 
{        
   PCF8574_Init(PCF8574_ADDR); // 等待PCF8574初始化成功
//    delay_ms(100);
    
    PCF8574_WriteOneByte(0x08);  // 默认关闭使能端
    
    LCD_Write_Com(0x0F);  // 显示开,光标开,闪烁开
   // delay_us(40);
    
    LCD_Write_Com(0x28);  // 4位总线,2行显示,5x8点阵
    PCF8574_WriteOneByte(0x0C);  // 背光控制
    PCF8574_WriteOneByte(0x08);  // 使能4线模式
    
    LCD_Write_Com(0x28);  // 再次确认4线模式
    LCD_Write_Com(0x01);  // 清屏
   // delay_ms(2);
    
    LCD_Write_Com(0x0C);  // 显示开,光标关,闪烁关
    LCD_Write_Com(0x06);  // 输入模式:增量不移位
   // delay_ms(10);
}

void LCD_Write_String(uint8_t x, uint8_t y, uint8_t *str) 
{
    if(x == 0) x = 1;  // 列位置修正
    
    // 设置显示位置
    if(y == 0 || y == 1) LCD_Write_Com(0x80 + x - 1);
    else if(y == 2) LCD_Write_Com(0xC0 + x - 1);
    else if(y == 3) LCD_Write_Com(0x94 + x - 1);
    else if(y == 4) LCD_Write_Com(0xD4 + x - 1);
    
    // 写入字符串
    while(*str) {
        LCD_Write_Data(*str++);
    }
}

void LCD_Move_Cursor(uint8_t x, uint8_t y, uint8_t blink) 
{
    LCD_Write_String(x, y, "");  // 移动光标
    LCD_Write_Com(blink ? 0x0F : 0x0C);  // 设置光标闪烁
}

void LCD1602_Clear(void)
{
    LCD_Write_Com(0x01);
    delay_ms(2);
}

/********************** PCF8574操作函数 **********************/
uint8_t PCF8574_Init(uint8_t addr)
{
    uint8_t temp = 1;  // 默认失败
    
    I2C_Start(iicpcf8574);
    I2C_SendByte(iicpcf8574,addr);
    temp = I2C_WaitAck(iicpcf8574);
    I2C_Stop(iicpcf8574);
    PCF8574_WriteOneByte(0xFF);  // 初始化所有IO为高电平

    
    return temp;
}

uint8_t PCF8574_ReadOneByte(void)
{
    uint8_t temp = 0;
    I2C_Start(iicpcf8574);
    I2C_SendByte(iicpcf8574,PCF8574_ADDR | 0x01);  // 读模式
    I2C_WaitAck(iicpcf8574);
    temp = I2C_ReceiveByte(iicpcf8574);
    I2C_Stop(iicpcf8574);
    return temp;
}

void PCF8574_WriteOneByte(uint8_t DataToWrite)
{
    I2C_Start(iicpcf8574);
    I2C_SendByte(iicpcf8574,PCF8574_ADDR);  // 写模式
    I2C_WaitAck(iicpcf8574);
    I2C_SendByte(iicpcf8574,DataToWrite);
    I2C_WaitAck(iicpcf8574);
    I2C_Stop(iicpcf8574);
    HAL_Delay(1);  // 保持原有延时
}

void PCF8574_WriteBit(uint8_t bit, uint8_t sta)
{
    uint8_t data = PCF8574_ReadOneByte();
    data = sta ? (data | (1 << bit)) : (data & ~(1 << bit));
    PCF8574_WriteOneByte(data);
}

uint8_t PCF8574_ReadBit(uint8_t bit)
{
    return (PCF8574_ReadOneByte() & (1 << bit)) ? 1 : 0;
}
