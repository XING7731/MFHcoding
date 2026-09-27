#ifndef __ALGO_h__
#define __ALGO_h__

#include "main.h"


typedef struct
{
    float data[50]; // 数组滤波数据
    char l; 				// 角标
} Array;

typedef struct
{
    uint32_t data[50]; // 数组滤波数据
    char l; 			  	 // 角标
} _Array;

float Data_FastSoothing(float *Buffer, uint8_t *Buffer_n, uint8_t *Buffer_len, uint8_t *Buffer_cnt, 
                  uint8_t Buffer_lenAll, float f_new, float f_old);
float Wavefiltering(float Diy, Array* arr, u8 n);
uint32_t _Wavefiltering(uint32_t Diy, _Array* arr, u8 n);
float Wavefiltering1(float Diy, Array* arr, u8 n);
float Wavefiltering2(float Diy, Array* arr, u8 n, float ratio);
float get_buff_data( float* BUFF,char n,uint8_t Buffer_lenAll);



#endif





