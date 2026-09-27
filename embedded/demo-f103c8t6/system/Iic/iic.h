#ifndef __IIC_h__
#define __IIC_h__

#include "main.h"
//typedef   unsigned char  u8 ;
//typedef   unsigned short  u16 ;
//typedef   short int  s16 ;
//typedef   unsigned int  u32 ;
typedef struct
{
    uint8_t(*sdaIn)(void);
    void(*sdaHi)(void);
    void(*sdaLo)(void);
    void(*sclHi)(void);
    void(*sclLo)(void);

} IIC_HardWareTypedef;

void MX_IIC_Init(void);
extern IIC_HardWareTypedef iicina219;
extern void I2C_delay(void);
extern uint8_t I2C_Start(IIC_HardWareTypedef iic);
extern void I2C_Stop(IIC_HardWareTypedef iic);
extern void I2C_Ack(IIC_HardWareTypedef iic);
extern void I2C_NoAck(IIC_HardWareTypedef iic);
extern uint8_t I2C_WaitAck(IIC_HardWareTypedef iic);
extern void I2C_SendByte(IIC_HardWareTypedef iic,uint8_t SendByte);
extern uint8_t I2C_ReceiveByte(IIC_HardWareTypedef iic);



void IIC_Write_Device_2Byte(uint8_t address,uint8_t Reg,uint8_t byte,uint16_t data,IIC_HardWareTypedef device);
uint8_t IIC_Read_Device_2Byte_Len(uint8_t address,uint8_t Reg,uint8_t byte,uint8_t *res,IIC_HardWareTypedef device);

void IIC_Write_Device_Byte(uint8_t address,uint8_t Reg,uint16_t data,IIC_HardWareTypedef device);
uint8_t IIC_Read_Device_Byte_Len(uint8_t address,uint8_t Reg,uint8_t *res,IIC_HardWareTypedef device);

#endif





