#ifndef __TIMER_h__
#define __TIMER_h__
#include "main.h"
extern uint16_t timer1_flag_1;
extern uint16_t timer2_flag_1;
extern int miao,fen,shi;
extern int Flag_15s;


void MX_TIMx_Init(void);
void TIM1_Init(uint16_t arr,uint16_t psc);
void TIM2_Init(uint16_t arr,uint16_t psc);
void TIM3_Init(uint16_t arr,uint16_t psc);
void TIM4_Init(uint16_t arr,uint16_t psc);
void Capture_TIMx_Init(void);

void timer_1_handle(void);
void timer_2_handle(void);
void timer_3_handle(void);
void timer_4_handle(void);


#endif





