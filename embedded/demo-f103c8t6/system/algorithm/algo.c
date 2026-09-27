#include "main.h"
#include "algo.h"




/*--------------------------------------------*/
/******************************************* 
 * 数据快响应平滑函数 
* Buffer 表示数据缓存 
* Buffer_n 表示数据缓存索引 
* Buffer_len 表示平滑个数 
* Buffer_cnt 平滑计数器 
* Buffer_lenAll 表示平滑总数 
* f_new 表示要平滑的数 
* f_old 表示上次平滑的结果 
*******************************************/ 
float Data_FastSoothing(float *Buffer, uint8_t *Buffer_n, uint8_t *Buffer_len, uint8_t *Buffer_cnt, 
                  uint8_t Buffer_lenAll, float f_new, float f_old) 
{ 
   float f; 
   float f_sum = 0; 
   float f_min_k, f_max_k; 
   float f_min, f_max; 
   uint8_t i; 

	    f_min_k = 0.5; 
      f_max_k = 2; 
	

	 if ((f_new > (f_old * f_max_k) || f_new < (f_old * f_min_k)) && f_min_k > 0 && f_max_k > 0)
   { 
      *Buffer_cnt = *Buffer_cnt + 1; 
   } 
   else 
   { 
      *Buffer_cnt = 0; 
   } 

   if (*Buffer_cnt) 
   { 
      if (*Buffer_cnt >= 3) 
      { 
         *Buffer_n = 0; 
         *Buffer_len = 0; 
         *Buffer_cnt = 0; 
      } 
   } 

   if (*Buffer_n < Buffer_lenAll) 
   { 
      Buffer[*Buffer_n] = f_new; 
      *Buffer_n += 1; 
      if (*Buffer_len < Buffer_lenAll) 
      { 
         *Buffer_len += 1; 
      } 
   } 
   else 
   { 
      *Buffer_n = 1; 
      Buffer[0] = f_new; 
   } 

   for (i = 0; i <= *Buffer_len - 1; i++) 
   { 
      f_sum += Buffer[i]; 
   } 
   if (*Buffer_len > 3) 
   { 
      f_max = Buffer[0]; 
      f_min = Buffer[0]; 
      for (i = 0; i <= *Buffer_len - 1; i++) 
      { 
         if (Buffer[i] > f_max) 
            f_max = Buffer[i]; 
         if (Buffer[i] < f_min) 
            f_min = Buffer[i]; 
      } 
      f_sum = f_sum - f_min - f_max; 
      f = f_sum / (*Buffer_len - 2); 
   } 
   else 
      f = f_sum / (*Buffer_len); 

   return f; 
}




float Wavefiltering(float Diy, Array* arr, u8 n)
{
    float sum = 0;
    u8 i;

    if (n <= 10)
        n = 20;

    arr->data[arr->l] = Diy;
    (arr->l)++;
    if (arr->l>= n)
        arr->l = 0;

    for (i = 0; i < n; ++i) // 循环数组累加
    {
        sum += arr->data[i];
    }
    sum = sum / n;

    return sum;
}

uint32_t _Wavefiltering(uint32_t Diy, _Array* arr, u8 n)
{
    uint32_t sum = 0;
    u8 i;

    if (n <= 9)
        n = 20;

    arr->data[arr->l] = Diy;
    (arr->l)++;
    if (arr->l >= n)
        arr->l = 0;

    for (i = 0; i < n; ++i) // 循环数组累加
    {
        sum += arr->data[i];
    }
    sum = sum/n;

    return sum;
}



float Wavefiltering1(float Diy, Array* arr, u8 n)
{
    float sum = 0;
    float maxVal = arr->data[0]; // 最大值初始化为数组的第一个元素
    float minVal = arr->data[0]; // 最小值初始化为数组的第一个元素
    u8 i;

    if (n <= 10)
        n = 20;

    arr->data[arr->l] = Diy;
    (arr->l)++;
    if (arr->l >= n)
        arr->l = 0;

    for (i = 0; i < n; ++i) // 循环数组累加
    {
        sum += arr->data[i];

        // 更新最大值和最小值
        if (arr->data[i] > maxVal)
            maxVal = arr->data[i];
        if (arr->data[i] < minVal)
            minVal = arr->data[i];
    }
    sum = (sum - maxVal - minVal) / (n - 2); // 减去最大值和最小值后求平均值

    return sum;
}

float Wavefiltering2(float Diy, Array* arr, u8 n, float ratio)
{
    float sum = 0;
    float maxVal = arr->data[0]; // 最大值初始化为数组的第一个元素
    float minVal = arr->data[0]; // 最小值初始化为数组的第一个元素
    u8 i;

    if (n <= 10)
        n = 20;

    arr->data[arr->l] = Diy;
    (arr->l)++;
    if (arr->l >= n)
        arr->l = 0;

    for (i = 0; i < n; ++i) // 循环数组累加
    {
        sum += arr->data[i];

        // 更新最大值和最小值
        if (arr->data[i] > maxVal) maxVal = arr->data[i];
        if (arr->data[i] < minVal) minVal = arr->data[i];
    }
		
		
    // 判断数据是否合理
   if ((Diy >= get_buff_data( arr->data,arr->l-2,n) * ratio && Diy <= get_buff_data( arr->data,arr->l+2,n) * ratio) ||
       (Diy <= get_buff_data( arr->data,arr->l-2,n) * ratio && Diy >= get_buff_data( arr->data,arr->l-2,n) * ratio)) 
		
//    // 判断数据是否合理
//   if ((Diy >= arr->data[arr->l - 2] * ratio && Diy <= arr->data[arr->l + 2] * ratio) ||
//       (Diy <= arr->data[arr->l - 2] * ratio && Diy >= arr->data[arr->l + 2] * ratio)) 
	    {
				sum = (sum - maxVal - minVal) / (n - 2); // 减去最大值和最小值后求平均值
      } 
			else 
			{
				sum = arr->data[arr->l - 1]; // 不合理的数据，显示上一次的值
			}

    return sum;
}

float get_buff_data( float* BUFF,char n,uint8_t Buffer_lenAll)
{
//	if(n<0)
//	return  BUFF[Buffer_lenAll+n];
	if(n>=Buffer_lenAll)
	return  BUFF[n-Buffer_lenAll];
	
	return  BUFF[n];
}

