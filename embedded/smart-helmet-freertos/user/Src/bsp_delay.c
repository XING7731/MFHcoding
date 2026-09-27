#include "bsp_delay.h"
#include "user_config.h"

static TIM_HandleTypeDef *s_htim = 0;

void bsp_delay_init(TIM_HandleTypeDef *htim)
{
    s_htim = htim;
    HAL_TIM_Base_Start(htim);
}

/* 基于硬件计数器轮询, 不依赖中断 —— 因此在临界区内依然可用。
 * 前提: 该 TIM 预分频已配置为 72-1(1MHz 计数)。 */
void delay_us(uint16_t us)
{
    uint16_t start, now, elapsed;

    if (s_htim == 0 || us == 0) return;

    start = (uint16_t)__HAL_TIM_GET_COUNTER(s_htim);
    do
    {
        now = (uint16_t)__HAL_TIM_GET_COUNTER(s_htim);
        elapsed = (uint16_t)(now - start);   /* 无符号回绕安全 */
    } while (elapsed < us);
}
