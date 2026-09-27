/* ============================================================================
 * stm32f1xx_hal_timebase_tim.c —— 把 HAL 的 1ms 时基从 SysTick 改到 TIM2
 *
 * 【为什么必须有这个文件】
 *   Cortex-M3 只有一个 SysTick。FreeRTOS 要用它产生调度节拍, HAL 要用它
 *   驱动 HAL_Delay()/HAL_GetTick()。二者不能共用, 否则:
 *     - FreeRTOS 的节拍会被 HAL_IncTick 干扰;
 *     - 或者 HAL_Delay 在调度器启动后彻底失准。
 *   标准做法就是本文件: 用一个独立的硬件定时器(TIM2)产生 1ms 中断,
 *   在中断里调用 HAL_IncTick()。于是 SysTick 完全归 FreeRTOS 使用。
 *
 * 【占用的资源】
 *   TIM2 (基本定时, 不占用任何 GPIO, 也不产生输出)
 *   本工程 TIM3 已用于微秒延时(bsp_delay.c), 二者不冲突。
 *
 * 【与 CubeMX 的关系】
 *   若用 CubeMX 重新生成代码, 需在 Project Manager → Advanced Settings 里
 *   把 "Timebase Source" 从 SysTick 改为 TIM2, CubeMX 会自动生成等价文件。
 * ========================================================================== */
#include "stm32f1xx_hal.h"

/* 该文件内部使用的 TIM 句柄(仅服务于 HAL 时基, 与用户外设无关) */
TIM_HandleTypeDef htim2_timebase;

void TIM2_IRQHandler(void);

/**
  * @brief  用 TIM2 作为 1ms 时基源。
  * @note   由 HAL_Init() 与 HAL_RCC_ClockConfig() 自动调用:
  *         - 第一次在时钟还没提到 72MHz 时(HSI 8MHz)先建一个能用的节拍;
  *         - 第二次在 PLL 稳定后按新的 PCLK1 重新计算分频, 保证仍是精确 1ms。
  * @param  TickPriority: 节拍中断优先级(取自 hal_conf.h 的 TICK_INT_PRIORITY = 15)
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    RCC_ClkInitTypeDef clkconfig;
    uint32_t           uwTimclock      = 0U;
    uint32_t           uwAPB1Prescaler = 0U;
    uint32_t           uwPrescalerValue = 0U;
    uint32_t           pFLatency;

    /* 配置 TIM2 中断优先级并开启中断 */
    HAL_NVIC_SetPriority(TIM2_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    /* 打开 TIM2 时钟 */
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* 取当前时钟配置, 判断 APB1 分频 */
    HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);
    uwAPB1Prescaler = clkconfig.APB1CLKDivider;

    /* APB1 分频不为 1 时, 定时器时钟是 PCLK1 的 2 倍(STM32F1 的时钟树特性) */
    if (uwAPB1Prescaler == RCC_HCLK_DIV1)
    {
        uwTimclock = HAL_RCC_GetPCLK1Freq();
    }
    else
    {
        uwTimclock = 2U * HAL_RCC_GetPCLK1Freq();
    }

    /* 分频到 1MHz 计数器时钟 */
    uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

    /* 计数 1000 次 = 1ms */
    htim2_timebase.Instance               = TIM2;
    htim2_timebase.Init.Period            = (1000000U / 1000U) - 1U;
    htim2_timebase.Init.Prescaler         = uwPrescalerValue;
    htim2_timebase.Init.ClockDivision     = 0U;
    htim2_timebase.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2_timebase.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2_timebase) == HAL_OK)
    {
        return HAL_TIM_Base_Start_IT(&htim2_timebase);
    }

    return HAL_ERROR;
}

/** @brief 挂起节拍(HAL_SuspendTick 的弱函数覆盖) */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim2_timebase, TIM_IT_UPDATE);
}

/** @brief 恢复节拍(HAL_ResumeTick 的弱函数覆盖) */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim2_timebase, TIM_IT_UPDATE);
}

/**
  * @brief  TIM 更新中断回调 —— 每 1ms 递增 HAL 的 uwTick。
  * @note   本工程只有 TIM2 会触发本回调(TIM3 仅作计数器读取, 不开中断);
  *         若以后给别的 TIM 开了中断, 这里需要按 htim->Instance 区分。
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        HAL_IncTick();
    }
}

/** @brief TIM2 中断入口 */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2_timebase);
}
