#ifndef __ADC_h__
#define __ADC_h__

#include "main.h"



uint16_t map(float x,float in_min,float in_max,float out_min,float out_max);
void MX_ADC_Init(void);
uint16_t Get_Adc(void);
void Get_power(void);
uint16_t Get_Adc1(void);
uint16_t Get_Adc2(void);
#endif





