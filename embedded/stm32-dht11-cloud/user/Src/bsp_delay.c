#include "bsp_delay.h"
#include "user_config.h"

static TIM_HandleTypeDef *s_htim_delay = 0;

void bsp_delay_init(TIM_HandleTypeDef *htim)
{
    s_htim_delay = htim;
    HAL_TIM_Base_Start(htim);
}

/* 阻塞式微秒延时, 最大 65535us */
void delay_us(uint16_t us)
{
    if (s_htim_delay == 0) return;
    __HAL_TIM_SET_COUNTER(s_htim_delay, 0);
    while (__HAL_TIM_GET_COUNTER(s_htim_delay) < us) { /* busy wait */ }
}
