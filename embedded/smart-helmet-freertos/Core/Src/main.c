/* ============================================================================
 * 本文件已停用 —— 主程序位于 user/Src/main.c
 * ============================================================================
 *
 * 【为什么这里没有代码】
 *   本工程的主程序(main 函数、SystemClock_Config、各外设 MX_xxx_Init、
 *   外设 MSP 初始化、中断回调、Error_Handler)全部集中在:
 *
 *       user/Src/main.c
 *
 *   如果此处再放一份 CubeMX 生成的 main()、SystemClock_Config()、
 *   Error_Handler(), 就会和 user/Src/main.c 里的同名函数冲突:
 *   armcc 会报 "multiply defined symbol" 链接错误。
 *
 *   因此本文件被清空, 只留这段说明。它现在是空编译单元, 无论是否被
 *   加入编译都不会产生任何符号, 也就不会冲突。
 *
 * 【以后用 CubeMX 重新生成代码时请注意】
 *   CubeMX 会重新写出本文件的内容。一旦重新生成, 请二选一:
 *     方案 A(推荐): 把 CubeMX 写出的 main.c 里的外设初始化代码, 合并进
 *                   user/Src/main.c, 然后再次清空本文件;
 *     方案 B: 在 Keil 里右键本文件 → Options for File → 勾选
 *                   "Exclude file from build"(从编译中排除)。
 *   更详细的做法见 docs/02-CubeMX配置指南.md。
 *
 * 【保留原 CubeMX 结构供对照】
 *   CubeMX 生成的 main.c 结构为:
 *     main() → HAL_Init() → SystemClock_Config() → MX_GPIO_Init()
 *            → while(1)
 *   本项目把 while(1) 换成了 vTaskStartScheduler(), 并把外设初始化
 *   扩展为 USART1/USART2/I2C1/ADC1/TIM3/RTC, 全部位于 user/Src/main.c。
 * ========================================================================== */
