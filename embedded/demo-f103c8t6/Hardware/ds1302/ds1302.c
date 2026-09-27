#include "main.h"
#include "ds1302.h"


DATE SysDate = {00, 00, 12, 22,7,4, 21};  //Date: 2021.7.22 Time: 12:00:00 Week: 4(Sat)


void DAT_MODE_OUT(void)//IO引脚配置成输出模式
{
	 GPIO_InitTypeDef GPIO_InitStruct;
	 
		GPIO_InitStruct.Pin = DS1302_DATA_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(DS1302_DATA_PORT, &GPIO_InitStruct);
}

void DAT_MODE_IN(void)//IO引脚配置成输入模式
{
    GPIO_InitTypeDef GPIO_InitStruct;
	
		GPIO_InitStruct.Pin = DS1302_DATA_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;  // 输入模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;      // 添加上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(DS1302_DATA_PORT, &GPIO_InitStruct);

}




void ds1302dataHi(void)
{
	  HAL_GPIO_WritePin(DS1302_DATA_PORT,DS1302_DATA_PIN,GPIO_PIN_SET);
}

void ds1302dataLo(void)
{
	  HAL_GPIO_WritePin(DS1302_DATA_PORT,DS1302_DATA_PIN,GPIO_PIN_RESET);
}

uint8_t ds1302dataIn(void)
{
	  return HAL_GPIO_ReadPin(DS1302_DATA_PORT,DS1302_DATA_PIN);
}
/**
 * @brief DS1302引脚初始化
 */
void DS1302_GPIO_Init(void)
{
	 GPIO_InitTypeDef GPIO_InitStruct;
		DS1302_SCK_CLK_ENABLE();
		GPIO_InitStruct.Pin = DS1302_SCK_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(DS1302_SCK_PORT, &GPIO_InitStruct);

	  DS1302_RST_CLK_ENABLE(); 
		GPIO_InitStruct.Pin = DS1302_RST_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(DS1302_RST_PORT, &GPIO_InitStruct);
	
	  DS1302_DATA_CLK_ENABLE();
		GPIO_InitStruct.Pin = DS1302_DATA_PIN;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
		HAL_GPIO_Init(DS1302_DATA_PORT, &GPIO_InitStruct);
	
}

/**
 * @brief 单字节写
 * @param 待写的数据
 */
void DS1302_WriteOneByte(uint8_t data) 
{
	uint8_t index = 0;
	DAT_MODE_OUT();//IO引脚配置成输出模式
	for(index=0; index<8; index++)
	{
		DS1302_SCK(Lo);
 
		if (data & 0x01) //数据由低到高
        {
					ds1302dataHi();//置1
        }
		else
        { 
					ds1302dataLo();//置0
        }
				//tdc = 200ns, tcl = 1us
				delay_us(2);
				DS1302_SCK(Hi); /* 上升沿写数据 */
				//tcdh = 280ns, tch = 1us
				delay_us(2);
		data >>= 1;      /* 从最低位开始写 */
	}
}

/**
 * @brief 向指定地址写入指定数据
 * @param 指定地址
 * @param 指定数据
 */
void DS1302_WriteByte(const uint8_t addr, const uint8_t data)
{
	DS1302_RST(Lo);
	DS1302_SCK(Lo);
	//tcwh = 4us
	delay_us(5);
	DS1302_RST(Hi); 
	//tcc = 4us
	delay_us(5);
	DS1302_WriteOneByte(addr);
	DS1302_WriteOneByte(data);
	//tcch = 240ns, tch = 1us
	//delay_us(1);
	DS1302_RST(Lo);
	DS1302_SCK(Lo);

}

/**
 * @brief 单字节读
 * @param 待读的地址
 */
uint8_t DS1302_ReadByte(const uint8_t addr)
{
	uint8_t index = 0, data = 0;
	
	DS1302_RST(Lo);
	DS1302_SCK(Lo);
	//tcwh = 4us
	delay_us(5);		
	DS1302_RST(Hi);
	//tcc = 4us
	delay_us(5);	
	DS1302_WriteOneByte(addr);	
	DAT_MODE_IN();//IO配置成输入模式
	for(index=0; index<8; index++)
	{
		DS1302_SCK(Lo);
        //SCK_SET;
		//tcdd = 800ns tcl = 1us
		delay_us(2);
		data >>= 1; //数据是由高到低的顺序读取   0100 0000  
		if (ds1302dataIn()== 1) /* 下降沿读数据 */
    {
			data |= 0x80;   
		}
		//data >>= 1;
		//SCK_CLR;
		DS1302_SCK(Hi);
		//tch = 1us
		delay_us(2);
	}
		DS1302_SCK(Lo);
		DS1302_RST(Lo);
	return data;
}


/**
 * @brief DS1302设置
 * @param 日期/时间结构体指针
 */
int DS1302_DateSet(const DATE * const date)
{
	DS1302_WriteByte(WRITE_CONTROL_REG, 0x00); /* 去除写保护 */
	
	//The MSB of sec reg must be 0 to enable the clock!!
	DS1302_WriteByte(WRITE_SEC_REG,  DataToBcd(date->sec)); //写秒 
	
	DS1302_WriteByte(WRITE_MIN_REG,  DataToBcd(date->min));//写分
	DS1302_WriteByte(WRITE_HOUR_REG, DataToBcd(date->hour));//写小时
	DS1302_WriteByte(WRITE_DAY_REG,  DataToBcd(date->day));//写天
	DS1302_WriteByte(WRITE_MON_REG,  DataToBcd(date->mon));//写月
	DS1302_WriteByte(WRITE_WEEK_REG, DataToBcd(date->week));//写星期
	DS1302_WriteByte(WRITE_YEAR_REG, DataToBcd(date->year));//写年
    DS1302_WriteByte(WRITE_CONTROL_REG, 0x80); /* 加上写保护 */
	return 0;
}

/**
 * @brief DS1302读取
 * @param 日期/时间结构体指针
 */
int DS1302_DateRead(DATE * const date)
{
	date->sec  = BcdToData(DS1302_ReadByte(READ_SEC_REG));//读秒
	date->min  = BcdToData(DS1302_ReadByte(READ_MIN_REG));//读分
	date->hour = BcdToData(DS1302_ReadByte(READ_HOUR_REG));//读小时
	date->day  = BcdToData(DS1302_ReadByte(READ_DAY_REG));//读天
	date->mon  = BcdToData(DS1302_ReadByte(READ_MON_REG));//读月
	date->week = BcdToData(DS1302_ReadByte(READ_WEEK_REG));//读星期
	date->year = BcdToData(DS1302_ReadByte(READ_YEAR_REG));//读年
	return 0;
}

/**
 * @brief DS1302初始化
 * @param 日期/时间结构体指针
 * @note  如果是充电电池,可以开启在正常情况下电源对电池的涓流充电功能,只有在异常情况下才使用电池供电
 * @note  直接利用DS1302片上RAM实现 1>如果是第一次上电,则设置日期/时间 2>否则,就不需要设置日期/时间
 */
int DS1302_Init(const DATE * const date)
{
	DS1302_GPIO_Init();//引脚初始化
    if (BcdToData(DS1302_ReadByte(READ_RAM_REG)) == 0x01) /* 如果不是第一次上电,则直接退出 */
	{
		return 0;
	}
    DS1302_WriteByte(WRITE_CONTROL_REG, 0x00); /* 去除写保护 */
//  DS1302_WriteByte(WRITE_CHARGE_REG, 0xa9);  /* 使能电池涓流充电功能(一定要是充电电池才可以使用此功能!!!) */
    DS1302_WriteByte(WRITE_RAM_REG, DataToBcd(0x01));//标志写1
    DS1302_WriteByte(WRITE_CONTROL_REG, 0x80); /* 加上写保护 */
	DS1302_DateSet(date);
	return 0;
}

