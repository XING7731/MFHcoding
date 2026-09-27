# 02 - STM32CubeMX 配置指南（逐步）

> 芯片: STM32F103C8T6 (LQFP48)　工具链: MDK-ARM (Keil5)

## 1. 新建工程

1. 打开 CubeMX → `ACCESS TO MCU SELECTOR` → 搜索 `STM32F103C8Tx` → Start Project。
2. 左侧 `Project Manager`：
   - Project Name: `dht11_cloud`
   - Toolchain/IDE: **MDK-ARM**（Min Version V5）
   - 勾选 `Generate peripheral initialization as a pair of .c/.h files`（可选，推荐）

## 2. 时钟配置

1. `System Core → RCC`：HSE 选 **Crystal/Ceramic Resonator**（8MHz 外部晶振）。
2. `System Core → SYS`：Debug 选 **Serial Wire**（SWD 烧录/调试）。
3. `Clock Configuration` 页：
   - HSE=8MHz，PLL ×9 → **SYSCLK = 72MHz**
   - APB1 Prescaler = /2（PCLK1=36MHz，TIM3 时钟实际为 72MHz）
   - APB2 = 72MHz

## 3. 外设配置

### USART1 —— 接 ESP8266
- `Connectivity → USART1`：Mode = **Asynchronous**
- Baud Rate: **115200**，8-N-1
- NVIC Settings: 勾选 **USART1 global interrupt**（必须！驱动用中断收数据）
- 引脚: PA9(TX) → ESP8266 RX，PA10(RX) → ESP8266 TX

### USART2 —— 调试串口（可选但强烈建议）
- 同上配置，**不需要开中断**。PA2(TX) → USB-TTL 的 RX。

### TIM3 —— 微秒延时基准
- `Timers → TIM3`：Clock Source = **Internal Clock**
- Prescaler: **72-1**（72分频 → 计数频率 1MHz，1个计数=1us）
- Counter Period: **65535**
- 其余保持默认，不用开中断

### GPIO —— DHT11
- `PA1` → 设为 **GPIO_Output**，上电默认输出即可（驱动会动态切换输入/输出）
- 用户标签可不填（驱动里直接用 `user_config.h` 中的 `DHT11_PORT/DHT11_PIN`）

## 4. 生成代码

1. `Project Manager → Code Generator`：勾选 **Generate peripheral initialization as a pair of .c/.h files**。
2. 点击 `GENERATE CODE` → Open Project（用 Keil 打开）。

## 5. Keil 里添加用户代码

1. 在 Keil 工程里新建 Group，命名为 `USER`。
2. 把本项目 `user/Src` 下的 4 个 .c 文件加入：
   `bsp_delay.c`、`dht11.c`、`esp8266.c`（main.c 不用加，参考下一节）
3. `Options for Target → C/C++ → Include Paths`：添加本项目 `user/Inc` 目录。
4. **main.c 处理方式（推荐 A 方案）**：
   - **方案 A（推荐）**：不替换 CubeMX 生成的 main.c，把本项目 `user/Src/main.c` 中的
     `#include`、全局变量、`DBG` 宏、`connect_cloud()` 和 while(1) 循环体，
     拷进生成 main.c 对应的 `/* USER CODE BEGIN x */ ... /* USER CODE END x */` 区域：
     - 头文件 → `USER CODE BEGIN Includes`
     - 句柄 extern、变量、宏、函数 → `USER CODE BEGIN 0`
     - `bsp_delay_init` / `ESP8266_Init` / `connect_cloud` → `USER CODE BEGIN 2`（在 MX_xxx_Init 之后）
     - while 循环内容 → `USER CODE BEGIN 3`
   - **方案 B**：整文件替换 Core/Src/main.c，并从原生成文件把
     `SystemClock_Config / MX_GPIO_Init / MX_USART1_UART_Init / MX_USART2_UART_Init / MX_TIM3_Init`
     这几个函数体原样拷到文件末尾。
5. `Options for Target → Debug`：选择你的烧录器（ST-Link），Settings 里 Flash Download 勾选 **Reset and Run**。

## 6. 编译烧录

- `F7` 编译，0 Error 后 `F8` 下载。
- 用 USB-TTL 接 PA2/PA3，串口助手 115200 看调试输出。
