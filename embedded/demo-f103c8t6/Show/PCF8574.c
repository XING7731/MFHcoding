#include "main.h"
#include "PCF8574.h"

IIC_HardWareTypedef iicpcf8574;
static uint8_t lcd_backlight_state = 1;  // 背光状态

// 严格使用的I2C函数写入PCF8574（Reg固定为0）
static void PCF8574_Write(uint8_t data) {
    IIC_Write_Device_Byte(PCF8574_ADDR, 0, data, iicpcf8574);
}

// 发送4位数据到LCD（高4位有效）
static void LCD1602_Write4Bits(uint8_t data, uint8_t rs_mode)
{ 										// 数据位（P4-P7） // RS状态 		// 背光
    uint8_t value = (data << 4)|(rs_mode << LCD_RS) |(lcd_backlight_state << LCD_BL);
    PCF8574_Write(value | LCD_E);  // E=1
    delay_us(1);
    PCF8574_Write(value & ~LCD_E); // E=0
    delay_us(100);
}

// 发送命令或数据（8位，分两次发送）
static void LCD1602_Write(uint8_t data, uint8_t rs_mode) {
    LCD1602_Write4Bits(data >> 4, rs_mode);   // 高4位
    LCD1602_Write4Bits(data & 0x0F, rs_mode); // 低4位
}

// 初始化LCD1602
void LCD1602_Init(void) 
{
    
    delay_ms(50);           // 上电延时

    // 4位模式初始化序列
	
	
	
    LCD1602_Write4Bits(0x03, 0);  // 尝试8位模式
    delay_ms(5);
    LCD1602_Write4Bits(0x03, 0);
    delay_us(100);
    LCD1602_Write4Bits(0x03, 0);
    delay_us(100);
    LCD1602_Write4Bits(0x02, 0);  // 切换到4位模式
    delay_us(100);

    // 配置显示模式
    LCD1602_Write(0x28, 0);  // 4位、2行、5x8点阵
    LCD1602_Write(0x0C, 0);  // 开显示，无光标
    LCD1602_Write(0x06, 0);  // 光标右移
    LCD1602_Clear();
}

// 清屏
void LCD1602_Clear(void) {
    LCD1602_Write(0x01, 0);
    delay_ms(2);
}

// 设置光标位置
void LCD1602_SetCursor(uint8_t row, uint8_t col) {
    uint8_t address = (row == 0) ? 0x80 : 0xC0;  // 行地址
    LCD1602_Write(address + col, 0);
}

// 写字符
void LCD1602_WriteChar(char ch) {
    LCD1602_Write(ch, 1);  // RS=1
}

// 写字符串
void LCD1602_WriteString(const char *str) {
    while (*str) LCD1602_WriteChar(*str++);
}

// 背光控制
void LCD1602_SetBacklight(uint8_t state) {
    lcd_backlight_state = state ? 1 : 0;
    PCF8574_Write(lcd_backlight_state << LCD_BL);
}
