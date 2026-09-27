/* ============================================================================
 * FreeRTOSConfig.h —— 智能工地安全帽边缘感知系统 专用配置
 *
 * 目标硬件: STM32F103C8T6 (Cortex-M3, 72MHz, 20KB SRAM, 64KB Flash)
 * 内核版本: FreeRTOS V10.0.1 (随 STM32Cube FW_F1 V1.8.7 提供)
 *
 * 【本配置的三个关键决定, 改错会直接导致系统异常】
 *
 * 1) SysTick 归 FreeRTOS, HAL 时基搬到 TIM2
 *    Cortex-M3 的 SysTick 只有一个。FreeRTOS 需要它做调度节拍,
 *    HAL 需要它做 HAL_Delay/HAL_GetTick。二者不能共用同一个寄存器,
 *    因此本工程用 Core/Src/stm32f1xx_hal_timebase_tim.c 把 HAL 时基
 *    改为 TIM2(1ms 中断)。见下方"中断向量"一节。
 *
 * 2) 中断优先级: configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
 *    调用任何 ...FromISR() 的中断, 其抢占优先级数值必须 >= 5(数值越大优先级越低),
 *    否则硬件优先级高于内核临界区 → 断言失败/系统跑飞。
 *    本工程 USART1_IRQn 与 EXTI4_IRQn 均设为抢占优先级 5(main.c 中设置), 正好合规。
 *
 * 3) 堆大小 configTOTAL_HEAP_SIZE = 10KB
 *    本工程的内存账(见 docs/01-系统架构与任务设计.md):
 *      5 个任务栈  128+256+256+384+256 = 1280 字 = 5120 B  ← 由本堆分配
 *      空闲任务栈  128 字 = 512 B
 *      5 个 TCB            ≈ 460 B
 *      2 个队列(含存储区)  ≈ 730 B
 *      3 个信号量/互斥量    ≈ 240 B
 *      heap_4 块头开销      ≈ 150 B
 *      合计 ≈ 7.2 KB → 取 10KB, 留约 2.8KB 余量
 *    实测(Keil 链接结果): ZI-data = 16160 B, 加 RW-data 后共 16.5KB,
 *    在 STM32F103C8T6 的 20KB SRAM 内, 还剩约 4KB。
 *    注意: 任务栈是从这个堆里切出来的, 不是 Keil 启动文件里的 Stack_Size。
 *          调大任务栈就必须同步调大这里, 否则 xTaskCreate 返回失败
 *          (app_tasks.c 的 APP_TasksCreate() 已对该返回值做了检查)。
 * ========================================================================== */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* armcc/armclang/gcc 下需要 stdint 提供 uint32_t; 汇编器不过这里 */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
 #include <stdint.h>
 extern uint32_t SystemCoreClock;
#endif

/* ------------------------- 调度器与节拍 ------------------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )   /* 1ms 节拍, 与 pdMS_TO_TICKS 配合 */
#define configMAX_PRIORITIES                    ( 7 )    /* 任务只用到 0~3, 留有余量 */
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIME_SLICING                  1        /* Display_Task 与空闲任务同为优先级 0, 靠时间片轮转 */

/* ------------------------- 内存 ------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0        /* 全用动态分配(heap_4) */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 10 * 1024 ) )
#define configAPPLICATION_ALLOCATED_HEAP        0        /* 堆由 heap_4.c 内部定义 */
#define configUSE_MALLOC_FAILED_HOOK            0

/* ------------------------- 同步原语 ------------------------- */
#define configUSE_MUTEXES                       1        /* xMutexI2C / xMutexSnapshot */
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8        /* 调试时可在 IDE 里看队列名 */

/* ------------------------- 调试与钩子 ------------------------- */
#define configUSE_TRACE_FACILITY                1
#define configGENERATE_RUN_TIME_STATS           0
/* 栈溢出检测: 2 = 在任务切换时检查栈末尾魔数(开销小, 能抓绝大多数溢出)。
 * 触发后进入 vApplicationStackOverflowHook()(定义在 user/Src/main.c),
 * 蜂鸣器长鸣 + 停在钩子里, 便于定位是哪个任务栈给小了。 */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configASSERT( x ) \
    if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

/* ------------------------- 软件定时器(本工程未用) ------------------------- */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               ( 2 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* ------------------------- 协程(未用) ------------------------- */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* ------------------------- 按需裁剪 API(省 Flash) ------------------------- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1        /* Sensor_Task / Display_Task 用 */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_eTaskGetState                   0        /* 未用, 关掉省 Flash */
#define INCLUDE_xTaskGetCurrentTaskHandle       1        /* 队列注册/调试用 */
/* 打开它才能用 uxTaskGetStackHighWaterMark() —— 本工程用它实测任务栈余量,
 * 见 user/Src/app_tasks.c 的 APP_ReportStackUsage()。 */
#define INCLUDE_uxTaskGetStackHighWaterMark     1

/* ------------------------- Cortex-M3 优先级配置 ------------------------- */
#ifdef __NVIC_PRIO_BITS
 #define configPRIO_BITS                        __NVIC_PRIO_BITS
#else
 #define configPRIO_BITS                        4
#endif

/* 最低优先级(数值最大) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          0x0f
/* 允许调用 ...FromISR() 的最高(=数值最小)优先级。5 与 main.c 中
 * HAL_NVIC_SetPriority(USART1_IRQn/EXTI4_IRQn, 5, 0) 保持一致。*/
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* ------------------------- 中断向量映射 -------------------------
 * 把 FreeRTOS 移植层(port.c)里的三个汇编函数直接改名成 CMSIS 标准名,
 * 这样启动文件 startup_stm32f103xb.s 的中断向量表无需修改即可指向它们。
 *
 * 重要: 因为这里做了映射, Core/Src/stm32f1xx_it.c 中【不能】再定义
 *       SVC_Handler / PendSV_Handler / SysTick_Handler, 否则重定义冲突。
 *       本工程已从该文件中移除这三个函数。
 * ================================================================ */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
