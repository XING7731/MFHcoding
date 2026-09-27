#include "main.h"

IWDG_HandleTypeDef g_iwdg_handle;  /* 独立看门狗句柄 */

/**
 * @brief       初始化独立看门狗 
 * @param       prer: IWDG_PRESCALER_4~IWDG_PRESCALER_256,对应4~256分频
 *   @arg       分频因子 = 4 * 2^prer. 但最大值只能是256!
 * @param       rlr: 自动重装载值,0~0XFFF. 
 * @note        时间计算(大概):Tout=((4 * 2^prer) * rlr) / 40 (ms). 
 * @retval      无  256 * 0xFFF / 40kHz = 16.384秒
 */
void IWDG_init(uint8_t prer, uint16_t rlr)
{
    g_iwdg_handle.Instance = IWDG;
    g_iwdg_handle.Init.Prescaler = prer; /* 设置IWDG分频系数 */
    g_iwdg_handle.Init.Reload = rlr;     /* 重装载值 */
    HAL_IWDG_Init(&g_iwdg_handle);       /* 初始化IWDG并启动 */
}

/**
 * @brief       喂独立看门狗
 * @param       无
 * @retval      无
 */
void IWDG_feed(void)
{
    HAL_IWDG_Refresh(&g_iwdg_handle);  /* 重装载计数器 */
}
