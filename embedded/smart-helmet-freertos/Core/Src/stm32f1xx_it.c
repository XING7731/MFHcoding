/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern UART_HandleTypeDef huart1;   /* ESP01S 串口句柄, 定义在 user/Src/main.c */

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/* ==========================================================================
 * 注意: 以下三个异常处理函数已【移出本文件】, 改由 FreeRTOS 移植层提供。
 *
 *   SVC_Handler     ←  FreeRTOS port.c 中的 vPortSVCHandler
 *   PendSV_Handler  ←  FreeRTOS port.c 中的 xPortPendSVHandler
 *   SysTick_Handler ←  FreeRTOS port.c 中的 xPortSysTickHandler
 *
 * 改名映射在 Core/Inc/FreeRTOSConfig.h 里通过宏完成:
 *     #define vPortSVCHandler     SVC_Handler
 *     #define xPortPendSVHandler  PendSV_Handler
 *     #define xPortSysTickHandler SysTick_Handler
 *
 * 所以本文件【不能】再定义这三个函数, 否则与 port.c 中的符号重复, 链接失败。
 *
 * 另外: HAL 的 1ms 时基因 SysTick 已被 FreeRTOS 占用, 不能再用 SysTick,
 *       已改由 TIM2 提供, 见 Core/Src/stm32f1xx_hal_timebase_tim.c
 *       (TIM2_IRQHandler → HAL_TIM_IRQHandler → HAL_IncTick)。
 * ========================================================================== */

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */

/**
  * @brief USART1 中断 —— ESP01S 的 AT 应答接收。
  * @note  HAL_UART_IRQHandler 内部会调用 HAL_UART_RxCpltCallback(),
  *        后者在 user/Src/main.c 中实现, 最终把字节交给 ESP01S_RxCplt()
  *        存入环形缓冲。这条链路不调用 FreeRTOS API, 只做缓冲搬移。
  *        优先级在 main.c 中设为抢占 5(main.c: HAL_NVIC_SetPriority)。
  */
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);
}

/**
  * @brief EXTI4 中断 —— PA4 上的 SOS 按键(下降沿)。
  * @note  HAL_GPIO_EXTI_IRQHandler 会清中断标志并调用
  *        HAL_GPIO_EXTI_Callback()(实现在 user/Src/main.c),
  *        其中调用 APP_PostAlarmFromISR() 把紧急事件直接投给
  *        最高优先级的 Alarm_Task, 并唤醒 WiFi_Task 抢发 SOS 报文。
  *
  *        【关键】该 ISR 调用了 ...FromISR() API(使用队列/信号量),
  *        因此其抢占优先级数值必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5),
  *        main.c 中设为 5, 恰好满足。若改动此优先级, 必须同步核对
  *        Core/Inc/FreeRTOSConfig.h 中的 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。
  */
void EXTI4_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

/* USER CODE END 1 */
