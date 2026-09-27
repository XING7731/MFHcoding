#include "main.h"
#include "iic.h"

extern IIC_HardWareTypedef iicmlx90614,iicbmp280,iicsd1306s,iicadxl345,iicmax30102,iicpcf8574,iicina219;
extern IIC_HardWareTypedef iicmpu6050;

#if(mpu6050)
void mpu6050SCLHi(void)
{  
	  HAL_GPIO_WritePin(IICMPU6050_GPIO_PORT,IICMPU6050_GPIO_SCL,GPIO_PIN_SET);
}

void mpu6050SCLLo(void)
{
	  HAL_GPIO_WritePin(IICMPU6050_GPIO_PORT,IICMPU6050_GPIO_SCL,GPIO_PIN_RESET);
}

void mpu6050SDAHi(void)
{
	  HAL_GPIO_WritePin(IICMPU6050_GPIO_PORT,IICMPU6050_GPIO_SDA,GPIO_PIN_SET);
}

void mpu6050SDALo(void)
{
	  HAL_GPIO_WritePin(IICMPU6050_GPIO_PORT,IICMPU6050_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t mpu6050SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICMPU6050_GPIO_PORT,IICMPU6050_GPIO_SDA);
}
#endif


#if(pcf8574)
void pcf8574SCLHi(void)
{  
	  HAL_GPIO_WritePin(IICPCF8574_GPIO_PORT,IICPCF8574_GPIO_SCL,GPIO_PIN_SET);
}

void pcf8574SCLLo(void)
{
	  HAL_GPIO_WritePin(IICPCF8574_GPIO_PORT,IICPCF8574_GPIO_SCL,GPIO_PIN_RESET);
}

void pcf8574SDAHi(void)
{
	  HAL_GPIO_WritePin(IICPCF8574_GPIO_PORT,IICPCF8574_GPIO_SDA,GPIO_PIN_SET);
}

void pcf8574SDALo(void)
{
	  HAL_GPIO_WritePin(IICPCF8574_GPIO_PORT,IICPCF8574_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t pcf8574SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICPCF8574_GPIO_PORT,IICPCF8574_GPIO_SDA);
}

#endif
#if(max30102)
void max30102SCLHi(void)
{  
	  HAL_GPIO_WritePin(IICMAX30102_GPIO_PORT,IICMAX30102_GPIO_SCL,GPIO_PIN_SET);
}

void max30102SCLLo(void)
{
	  HAL_GPIO_WritePin(IICMAX30102_GPIO_PORT,IICMAX30102_GPIO_SCL,GPIO_PIN_RESET);
}

void max30102SDAHi(void)
{
	  HAL_GPIO_WritePin(IICMAX30102_GPIO_PORT,IICMAX30102_GPIO_SDA,GPIO_PIN_SET);
}

void max30102SDALo(void)
{
	  HAL_GPIO_WritePin(IICMAX30102_GPIO_PORT,IICMAX30102_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t max30102SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICMAX30102_GPIO_PORT,IICMAX30102_GPIO_SDA);
}

#endif
#if(adxl345)
void adxl345SCLHi(void)
{  
	  HAL_GPIO_WritePin(IICADXL345_GPIO_PORT,IICADXL345_GPIO_SCL,GPIO_PIN_SET);
}

void adxl345SCLLo(void)
{
	  HAL_GPIO_WritePin(IICADXL345_GPIO_PORT,IICADXL345_GPIO_SCL,GPIO_PIN_RESET);
}

void adxl345SDAHi(void)
{
	  HAL_GPIO_WritePin(IICADXL345_GPIO_PORT,IICADXL345_GPIO_SDA,GPIO_PIN_SET);
}

void adxl345SDALo(void)
{
	  HAL_GPIO_WritePin(IICADXL345_GPIO_PORT,IICADXL345_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t max30102SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICADXL345_GPIO_PORT,IICADXL345_GPIO_SDA);
}

#endif
#if(Ina219)
void Ina219SCLHi(void)
{  
	  HAL_GPIO_WritePin(IICINA219_GPIO_PORT,IICINA219_GPIO_SCL,GPIO_PIN_SET);
}

void Ina219SCLLo(void)
{
	  HAL_GPIO_WritePin(IICINA219_GPIO_PORT,IICINA219_GPIO_SCL,GPIO_PIN_RESET);
}

void Ina219SDAHi(void)
{
	  HAL_GPIO_WritePin(IICINA219_GPIO_PORT,IICINA219_GPIO_SDA,GPIO_PIN_SET);
}

void Ina219SDALo(void)
{
	  HAL_GPIO_WritePin(IICINA219_GPIO_PORT,IICINA219_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t Ina219SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICINA219_GPIO_PORT,IICINA219_GPIO_SDA);
}

#endif	

#if(AT24CXX)
void AT24CXXSCLHi(void)
{
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_9,GPIO_PIN_SET);
}

void AT24CXXSCLLo(void)
{
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_9,GPIO_PIN_RESET);
}

void AT24CXXSDAHi(void)
{
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_8,GPIO_PIN_SET);
}

void AT24CXXSDALo(void)
{
	  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_8,GPIO_PIN_RESET);
}

uint8_t AT24CXXSDAIn(void)
{
	  return HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_8);
}
#endif

#if(mlx906)
void mlx906SCLHi(void)
{
	  HAL_GPIO_WritePin(IICMLX906_GPIO_PORT,IICMLX906_GPIO_SCL,GPIO_PIN_SET);
}

void mlx906SCLLo(void)
{
	  HAL_GPIO_WritePin(IICMLX906_GPIO_PORT,IICMLX906_GPIO_SCL,GPIO_PIN_RESET);
}

void mlx906SDAHi(void)
{
	  HAL_GPIO_WritePin(IICMLX906_GPIO_PORT,IICMLX906_GPIO_SDA,GPIO_PIN_SET);
}

void mlx906SDALo(void)
{
	  HAL_GPIO_WritePin(IICMLX906_GPIO_PORT,IICMLX906_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t mlx906SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICMLX906_GPIO_PORT,IICMLX906_GPIO_SDA);
}
#endif

#if(bmp280)
void bmp280SCLHi(void)
{
	  HAL_GPIO_WritePin(IICBMP280_GPIO_PORT,IICBMP280_GPIO_SCL,GPIO_PIN_SET);
}

void bmp280SCLLo(void)
{
	  HAL_GPIO_WritePin(IICBMP280_GPIO_PORT,IICBMP280_GPIO_SCL,GPIO_PIN_RESET);
}

void bmp280SDAHi(void)
{
	  HAL_GPIO_WritePin(IICBMP280_GPIO_PORT,IICBMP280_GPIO_SDA,GPIO_PIN_SET);
}

void bmp280SDALo(void)
{
	  HAL_GPIO_WritePin(IICBMP280_GPIO_PORT,IICBMP280_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t bmp280SDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICBMP280_GPIO_PORT,IICBMP280_GPIO_SDA);
}
#endif

#if(sd1306s)
void sd1306sSCLHi(void)
{
	  HAL_GPIO_WritePin(IICSD1306S_GPIO_PORT,IICSD1306S_GPIO_SCL,GPIO_PIN_SET);
}

void sd1306sSCLLo(void)
{
	  HAL_GPIO_WritePin(IICSD1306S_GPIO_PORT,IICSD1306S_GPIO_SCL,GPIO_PIN_RESET);
}

void sd1306sSDAHi(void)
{
	  HAL_GPIO_WritePin(IICSD1306S_GPIO_PORT,IICSD1306S_GPIO_SDA,GPIO_PIN_SET);
}

void sd1306sSDALo(void)
{
	  HAL_GPIO_WritePin(IICSD1306S_GPIO_PORT,IICSD1306S_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t sd1306sSDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICSD1306S_GPIO_PORT,IICSD1306S_GPIO_SDA);
}
#endif

#if(sd1306b)
void sd1306bSCLHi(void)
{
	  HAL_GPIO_WritePin(IICSD1306B_GPIO_PORT,IICSD1306B_GPIO_SCL,GPIO_PIN_SET);
}

void sd1306bSCLLo(void)
{
	  HAL_GPIO_WritePin(IICSD1306B_GPIO_PORT,IICSD1306B_GPIO_SCL,GPIO_PIN_RESET);
}

void sd1306bSDAHi(void)
{
	  HAL_GPIO_WritePin(IICSD1306B_GPIO_PORT,IICSD1306B_GPIO_SDA,GPIO_PIN_SET);
}

void sd1306bSDALo(void)
{
	  HAL_GPIO_WritePin(IICSD1306B_GPIO_PORT,IICSD1306B_GPIO_SDA,GPIO_PIN_RESET);
}

uint8_t sd1306bSDAIn(void)
{
	  return HAL_GPIO_ReadPin(IICSD1306B_GPIO_PORT,IICSD1306B_GPIO_SDA);
}
#endif
/*******************************************************************************
* Function Name  : AT24C02_Configuration
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void MX_IIC_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	
#if(mpu6050)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
	
		GPIO_InitStruct.Pin = IICMPU6050_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICMPU6050_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICMPU6050_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICMPU6050_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicmpu6050.sclHi=mpu6050SCLHi;
    iicmpu6050.sclLo=mpu6050SCLLo;
    iicmpu6050.sdaHi=mpu6050SDAHi;
    iicmpu6050.sdaLo=mpu6050SDALo;
    iicmpu6050.sdaIn=mpu6050SDAIn;
		
    iicmpu6050.sclHi();
    iicmpu6050.sdaHi();
#endif

#if(pcf8574)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
	
		GPIO_InitStruct.Pin = IICPCF8574_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICPCF8574_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICPCF8574_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICPCF8574_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicpcf8574.sclHi=pcf8574SCLHi;
    iicpcf8574.sclLo=pcf8574SCLLo;
    iicpcf8574.sdaHi=pcf8574SDAHi;
    iicpcf8574.sdaLo=pcf8574SDALo;
    iicpcf8574.sdaIn=pcf8574SDAIn;
		
    iicpcf8574.sclHi();
    iicpcf8574.sdaHi();
#endif		
#if(max30102)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
	
		GPIO_InitStruct.Pin = IICMAX30102_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICMAX30102_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICMAX30102_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICMAX30102_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicmax30102.sclHi=max30102SCLHi;
    iicmax30102.sclLo=max30102SCLLo;
    iicmax30102.sdaHi=max30102SDAHi;
    iicmax30102.sdaLo=max30102SDALo;
    iicmax30102.sdaIn=max30102SDAIn;
		
    iicmax30102.sclHi();
    iicmax30102.sdaHi();
#endif	
	
	
#if(adxl345)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
	
		GPIO_InitStruct.Pin = IICADXL345_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICADXL345_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICADXL345_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICADXL345_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicadxl345.sclHi=adxl345SCLHi;
    iicadxl345.sclLo=adxl345SCLLo;
    iicadxl345.sdaHi=adxl345SDAHi;
    iicadxl345.sdaLo=adxl345SDALo;
    iicadxl345.sdaIn=adxl345SDAIn;
		
    iicadxl345.sclHi();
    iicadxl345.sdaHi();
#endif	
#if(Ina219)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
		IICINA219_SCL_ENABLE();
		GPIO_InitStruct.Pin = IICINA219_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICINA219_GPIO_PORT, &GPIO_InitStruct);
		IICINA219_SDA_ENABLE(); 
		GPIO_InitStruct.Pin = IICINA219_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICINA219_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicina219.sclHi=Ina219SCLHi;
    iicina219.sclLo=Ina219SCLLo;
    iicina219.sdaHi=Ina219SDAHi;
    iicina219.sdaLo=Ina219SDALo;
    iicina219.sdaIn=Ina219SDAIn;
		
    iicina219.sclHi();
    iicina219.sdaHi();
#endif	

#if(AT24CXX)			
		/*Configure Ina219 : PB11,10-LSCL&LSDA */
		GPIO_InitStruct.Pin = IICAT24CXX_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICAT24CXX_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICAT24CXX_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICAT24CXX_GPIO_PORT, &GPIO_InitStruct);
				
	
		iicEEp.sclHi=AT24CXXSCLHi;
    iicEEp.sclLo=AT24CXXSCLLo;
    iicEEp.sdaHi=AT24CXXSDAHi;
    iicEEp.sdaLo=AT24CXXSDALo;
    iicEEp.sdaIn=AT24CXXSDAIn;
		
    iicEEp.sclHi();
    iicEEp.sdaHi();
#endif	

#if(mlx906)	
		GPIO_InitStruct.Pin = IICMLX906_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICMLX906_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICMLX906_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICMLX906_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicmlx90614.sclHi=mlx906SCLHi;
    iicmlx90614.sclLo=mlx906SCLLo;
    iicmlx90614.sdaHi=mlx906SDAHi;
    iicmlx90614.sdaLo=mlx906SDALo;
    iicmlx90614.sdaIn=mlx906SDAIn;
		
    iicmlx90614.sclHi();
    iicmlx90614.sdaHi();
#endif

#if(bmp280)
		GPIO_InitStruct.Pin = IICBMP280_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICBMP280_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICBMP280_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICBMP280_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicbmp280.sclHi=bmp280SCLHi;
    iicbmp280.sclLo=bmp280SCLLo;
    iicbmp280.sdaHi=bmp280SDAHi;
    iicbmp280.sdaLo=bmp280SDALo;
    iicbmp280.sdaIn=bmp280SDAIn;
		
    iicbmp280.sclHi();
    iicbmp280.sdaHi();
#endif


#if(sd1306s)
		GPIO_InitStruct.Pin = IICSD1306S_GPIO_SCL;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(IICSD1306S_GPIO_PORT, &GPIO_InitStruct);
	
		GPIO_InitStruct.Pin = IICSD1306S_GPIO_SDA;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(IICSD1306S_GPIO_PORT, &GPIO_InitStruct);
			
	
		iicsd1306s.sclHi=sd1306sSCLHi;
    iicsd1306s.sclLo=sd1306sSCLLo;
    iicsd1306s.sdaHi=sd1306sSDAHi;
    iicsd1306s.sdaLo=sd1306sSDALo;
    iicsd1306s.sdaIn=sd1306sSDAIn;
		
    iicsd1306s.sclHi();
    iicsd1306s.sdaHi();
#endif
}

/*******************************************************************************
* Function Name  : I2C_delay3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void I2C_delay(void)
{
//	unsigned char x;
//	for(x=5;x>0;x--)
//	{
//		__NOP();__NOP();__NOP();__NOP();__NOP();
//	}
	delay_us(1);
//    uint8_t i = 20; //这里可以优化速度  ，经测试最低到5还能写入
//    while(i)
//    {
//      i--;
//    }
//	HAL_Delay(1);
}
/*******************************************************************************
* Function Name  : I2C_Start3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
uint8_t I2C_Start(IIC_HardWareTypedef iic)
{
    iic.sdaHi();
    iic.sclHi();
    I2C_delay();
    if(!iic.sdaIn())
    {
        return FALSE;    //SDA线为低电平则总线忙,退出
    }
    iic.sdaLo();
    I2C_delay();
    if(iic.sdaIn())
    {
        return FALSE;    //SDA线为高电平则总线出错,退出
    }
    iic.sdaLo();
    I2C_delay();
    return TRUE;
}
/*******************************************************************************
* Function Name  : I2C_Stop3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void I2C_Stop(IIC_HardWareTypedef iic)
{
    iic.sclLo();
    I2C_delay();
    iic.sdaLo();
    I2C_delay();
    iic.sclHi();
    I2C_delay();
    iic.sdaHi();
    I2C_delay();
}
/*******************************************************************************
* Function Name  : I2C_Ack3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void I2C_Ack(IIC_HardWareTypedef iic)
{
    iic.sclLo();
    I2C_delay();
    iic.sdaLo();
    I2C_delay();
    iic.sclHi();
    I2C_delay();
    iic.sclLo();
    I2C_delay();
}
/*******************************************************************************
* Function Name  : I2C_NoAck3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void I2C_NoAck(IIC_HardWareTypedef iic)
{
    iic.sclLo();
    I2C_delay();
    iic.sdaHi();
    I2C_delay();
    iic.sclHi();
    I2C_delay();
    iic.sclLo();
    I2C_delay();
}
/*******************************************************************************
* Function Name  : I2C_WaitAck3
* Description    :
* Input          : None
* Output         : None
* Return         : 返回为:=1有ACK,=0无ACK
*******************************************************************************/
uint8_t I2C_WaitAck(IIC_HardWareTypedef iic)
{
    iic.sclLo();
    I2C_delay();
    iic.sdaHi();
    I2C_delay();
    iic.sclHi();
    I2C_delay();

	if(iic.sdaIn())
    {
        iic.sclLo();
        I2C_delay();
        return FALSE;
    }
    iic.sclLo();
    I2C_delay();
    return TRUE;
}
/*******************************************************************************
* Function Name  : I2C_SendByte3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void I2C_SendByte(IIC_HardWareTypedef iic,uint8_t SendByte) //数据从高位到低位//
{
	  uint8_t x  = 0; 
    uint8_t i = 8;

    while(i--)
    {
        iic.sclLo();
        I2C_delay();
        if(SendByte & 0x80)
        {
            iic.sdaHi();
        }
        else
        {
            iic.sdaLo();
        }
        SendByte <<= 1;
        I2C_delay();
        iic.sclHi();
        I2C_delay();
    }
    iic.sclLo();
    I2C_delay();
}

/*******************************************************************************
* Function Name  : I2C_ReceiveByte3
* Description    :
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
uint8_t I2C_ReceiveByte(IIC_HardWareTypedef iic)  //数据从高位到低位//
{
    uint8_t i = 8;
    uint8_t ReceiveByte = 0;
    iic.sdaHi();
    I2C_delay();
    while(i--)
    {
        ReceiveByte <<= 1;
        iic.sclLo();
        I2C_delay();
        iic.sclHi();
        I2C_delay();
        if(iic.sdaIn())
        {
            ReceiveByte |= 0x01;
        }
    }
    iic.sclLo();
    I2C_delay();
    return ReceiveByte;
}



void IIC_Write_Device_2Byte(uint8_t address,uint8_t Reg,uint8_t byte,uint16_t data,IIC_HardWareTypedef device)
{
	uint8_t regAddr[2]={0,0};
	uint8_t i=0;
  /* High Byte */
  regAddr[0] = (data >> 8) & 0xff;
  /* Low Byte */
  regAddr[1] = (data >> 0) & 0xff;
	
	I2C_Start(device);                //START
	I2C_SendByte(device,address);     //SAD+W
	I2C_WaitAck(device);              //SAK
	I2C_SendByte(device,Reg); 			  //REG
	I2C_WaitAck(device);              //SAK
	
	while(byte)
	{
		I2C_SendByte(device,regAddr[i]); 	//DATA	
    I2C_WaitAck(device);              //SAK
		i++;
		byte--;
		}		
	 I2C_Stop(device);                 //Stop

}

uint8_t IIC_Read_Device_2Byte_Len(uint8_t address,uint8_t Reg,uint8_t byte,uint8_t *res,IIC_HardWareTypedef device)
{
	I2C_Start(device);              //start
	I2C_SendByte(device,address);   //SAW+D
	I2C_WaitAck(device);            //SAK
	I2C_SendByte(device,Reg);       //REG 	
	I2C_WaitAck(device);            //SAK	
	
	I2C_Start(device);              //StartRepeat
	I2C_SendByte(device,address+1); //SAD+R 
	I2C_WaitAck(device);            //SAK
	 while (byte) 
		{
        *res = I2C_ReceiveByte(device);
        if(byte == 1)
        {
					I2C_NoAck(device);
					I2C_Stop(device);
        }
        else
        {
					I2C_Ack(device);
        }
        res++;
        byte--;
    }
 return NULL;		
}

void IIC_Write_Device_Byte(uint8_t address,uint8_t Reg,uint16_t data,IIC_HardWareTypedef device)
{
	uint8_t regAddr;

  /*  Byte */
  regAddr= data  & 0xff;
	
	I2C_Start(device);                //START
	I2C_SendByte(device,address);     //SAD+W
	I2C_WaitAck(device);              //SAK
	I2C_SendByte(device,Reg); 			  //REG
	I2C_WaitAck(device);              //SAK
	I2C_SendByte(device,regAddr); 	//DATA	
  I2C_WaitAck(device);              //SAK
	I2C_Stop(device);                 //Stop

}




uint8_t IIC_Read_Device_Byte_Len(uint8_t address,uint8_t Reg,uint8_t *res,IIC_HardWareTypedef device)
{
	I2C_Start(device);              //start
	I2C_SendByte(device,address);   //SAW+D
	I2C_WaitAck(device);            //SAK
	I2C_SendByte(device,Reg);       //REG 	
	I2C_WaitAck(device);            //SAK	
	
	I2C_Start(device);              //StartRepeat
	I2C_SendByte(device,address+1); //SAD+R 
	
	I2C_WaitAck(device);            //SAK
	*res = I2C_ReceiveByte(device);
	I2C_NoAck(device);
	I2C_Stop(device);

	return NULL;		
}

