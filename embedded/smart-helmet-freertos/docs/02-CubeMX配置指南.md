# 02 STM32CubeMX + Keil 配置指南

目标：生成一个 FreeRTOS + HAL 的 MDK-ARM 工程，然后加入 `user/` 目录的代码。

---

## 一、新建工程与基础配置

1. CubeMX → `New Project` → 选芯片 **STM32F103C8Tx**（LQFP48）。
2. `Project Manager`：
   - Toolchain / IDE：**MDK-ARM V5**
   - Minimum Heap Size：**0x200**
   - Minimum Stack Size：**0x400**
3. `Pinout & Configuration → System Core → SYS`：Debug 选 **Serial Wire**（否则锁死 SWD）。

## 二、时钟（关键：必须 72MHz）

`RCC`：
- High Speed Clock (HSE)：**Crystal/Ceramic Resonator**
- Low Speed Clock (LSE)：**Crystal/Ceramic Resonator**
  （若你的板子没有 32.768kHz 晶振，请把 RTC 时钟源改成 LSI，见第 5 节）

`Clock Configuration`：
- Input frequency：8 MHz（按你板子的晶振填）
- PLL Source：HSE，**PLLMul = ×9** → SYSCLK = **72MHz**
- AHB Prescaler = /1，APB1 = /2（36MHz），APB2 = /1（72MHz）
- ADC Prescaler：/6（ADC 时钟 12MHz，不超过 14MHz 上限）

> ⚠️ TIM3 的 1µs 基准完全依赖 72MHz。若系统时钟不是 72MHz，
> DHT11 与 DS18B20 的时序会整体错位，表现为"永远读不到数据"。

## 三、外设配置

### 3.1 GPIO（按 docs/01 的引脚表逐个设置）

| 引脚 | CubeMX 设置 | GPIO 参数 |
|------|-------------|-----------|
| PA1 | GPIO_Input | Pull-up（DHT11 读取时驱动会切输出，无需改这里） |
| PB12 | GPIO_Output | **Output Open Drain**，High speed，初始电平 **High** |
| PB5 | GPIO_Output | Output Push Pull，Low speed，初始 **Low** |
| PA4 | GPIO_EXTI4 | **External Interrupt Mode with Falling edge trigger** + Pull-up |
| PB0 / PB1 | ADC1_IN8 / ADC1_IN9 | 无需额外设置（ADC 配置里选） |
| PB6 / PB7 | I2C1_SCL / I2C1_SDA | 由 I2C1 配置自动生成 |

`NVIC`：勾选 **EXTI line4 interrupt → Enable**（并在 NVIC 里给优先级 **5**）。
> 优先级数值必须 ≥5（即不高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`），
> 否则在 ISR 里调用 `xQueueSendFromISR` 会触发 FreeRTOS 断言。

### 3.2 USART1（ESP01S）

| 参数 | 值 |
|------|-----|
| Mode | Asynchronous |
| Baud Rate | **115200**（AT 无应答时改 9600 重试） |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |
| NVIC | **勾选 USART1 global interrupt → Enable**，优先级 **5** |

### 3.3 USART2（调试串口）

同上，但**不勾选**中断。PA2=TX，PA3=RX。

### 3.4 I2C1（OLED / MPU6050 / MAX30102 共总线）

| 参数 | 值 |
|------|-----|
| I2C Speed Mode | **Fast Mode** |
| I2C Clock Speed | **400000 Hz**（三器件均支持） |
| Clock No Stretch Mode | Disabled |

> 三个器件地址不同（0x3C / 0x68 / 0x57），可以共挂一条 I2C；
> SDA/SCL 各接 4.7K 上拉（模块板若已带则不用重复加）。

### 3.5 ADC1（MQ-2）

> 🔻 2026-09-22 光照功能删除后，ADC1 **只服务 MQ-2**（`docs/39`）。
> 重跑 CubeMX 时只需配 IN9，**不要再配 PB0** —— 本文的图表保留原始内容供参考。

| 参数 | 值 |
|------|-----|
| Mode | Independent |
| Clock Prescaler | Synchronous clock mode divided by 6 |
| Scan Conversion Mode | Disabled |
| Continuous Conversion Mode | Disabled |
| Number Of Conversion | 1 |
| Rank 1 Channel | **ADC_IN9（MQ-2）**（原为 IN8，驱动会在运行时切到 IN9；现已无 PT0603） |
| Sampling Time | **55.5 Cycles** |

> STM32F1 系列 ADC 固定 12 位分辨率，CubeMX 里没有分辨率选项，这是正常的。

### 3.6 TIM3（微秒延时基准）

| 参数 | 值 |
|------|-----|
| Clock Source | Internal Clock |
| Prescaler | **72 - 1**（72MHz → 1MHz，1 计数 = 1µs） |
| Counter Mode | Up |
| Counter Period | **65535** |
| auto-reload preload | Disable |

### 3.7 RTC

| 参数 | 值 |
|------|-----|
| Activate Clock Source | Enable |
| RTC OUT | No output |
| Asynchronous Predivider | 127 |
| Synchronous Predivider | 255 |

（32768 / 128 / 256 = 1Hz）
RTC 时钟源在 `Clock Configuration` 页底部的 **RTC Clock Mux** 选择 **LSE**。

## 四、FreeRTOS 中间件配置（关键）

`Middleware and Software Packs → FREERTOS`：

| 选项 | 设置值 | 原因 |
|------|--------|------|
| Interface | **CMSIS_V1**（或 CMSIS_V2/原生均可，本工程只用原生 API，见下） | 兼容性 |
| USE_PREEMPTION | **Enabled** | 抢占式调度 |
| TICK_RATE_HZ | **1000** | `pdMS_TO_TICKS(1)` 才精确 |
| MAX_PRIORITIES | **7**（默认） | 必须 > 3 |
| MINIMAL_STACK_SIZE | 128 words | |
| TOTAL_HEAP_SIZE | **10240 bytes** | 见 docs/01 资源预算（实测占用约 7.4KB，留 2.8KB 余量） |
| Memory Allocation | **heap_4** | 碎片友好 |
| USE_TIME_SLICING | Enabled | 让 Display_Task 与空闲任务轮转 |
| USE_MUTEXES | **Enabled** | 本工程用了互斥量 |
| USE_COUNTING_SEMAPHORES | Enabled | |
| USE_TRACE_FACILITY | 可选 | 调试时可看任务栈水位 |

> **FreeRTOSConfig.h 里要顺带确认两项**：
> - `INCLUDE_vTaskDelayUntil` = 1（Sensor_Task 用它做 100ms 定周期；CubeMX 默认 1）
> - `INCLUDE_vTaskSuspend` = 1（这样 `portMAX_DELAY` 才是真正的"永久阻塞"；
>   若为 0 则最长阻塞 0xFFFFFFFF 个 tick，实际效果相同但语义不同）
> - `configMAX_SYSCALL_INTERRUPT_PRIORITY` 默认为 5，因此本工程所有会调用
>   FromISR API 的中断（EXTI4、USART1）优先级都设为 **5**，不能更小（更小=更高）。

### ⚠️ 重要：不要在 CubeMX 里创建任务

请把 `Tasks and Queues` 里的默认任务**全部删除**（包括 `defaultTask`、`myTask`、
以及 CubeMX 自动创建的 `Queue`/`Semaphore`/`Mutex`）。本工程的 5 个任务与
所有通信对象统一由 `APP_CommInit()` / `APP_TasksCreate()` 创建，避免重复创建
和优先级冲突。

> 如果你想用 CubeMX 图形界面建任务，也可以 —— 但必须保证：任务名/优先级/栈
> 与 `app_tasks.c` 一致，并删掉 `APP_TasksCreate()` 里的 `xTaskCreate` 调用。

## 五、生成代码并加入本工程文件

1. `Generate Code` → 用 Keil 打开 `MDK-ARM/xxx.uvprojx`。
2. 把 `user/Src/*.c` 加入工程（新建分组 `user`），把 `user/Inc` 加入
   `Options for Target → C/C++ → Include Paths`。
3. 把 `user/Src/main.c` 的内容按以下方式合入（两种方式选一）：
   - **方式 A（推荐）**：把 `main.c` 中 `main()` 的主体与外设初始化函数拷进
     CubeMX 生成的 `Core/Src/main.c`，替换掉原 `while(1)` 部分；
   - **方式 B**：直接用本工程的 `main.c` 覆盖生成的 main.c。此时需要自己去
     `stm32f1xx_it.c` 确认 `EXTI4_IRQHandler` / `USART1_IRQHandler` 已生成。
4. `Options for Target → Target`：勾选 **Use MicroLIB**（省 Flash，本工程不依赖
   `%f` 输出，勾上更稳）。
5. 编译；若报 `HAL_GPIO_EXTI_Callback` 重定义，说明 CubeMX 在别处也实现了它，
   删掉那一处即可。

## 六、CubeMX 未生成中断服务函数时的补法

`Core/Src/stm32f1xx_it.c` 中应存在（CubeMX 勾了 NVIC 就会自动生成）：

```c
void EXTI4_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(SOS_KEY_PIN); }
void USART1_IRQHandler(void)  { HAL_UART_IRQHandler(&huart1); }
```

若没有，请手动补上这两行（注意包含 `main.h` 与 `user_config.h`）。

## 七、烧录后自检顺序

1. 串口助手接 PA2/PA3，**115200**，应看到 `[NET] AT check...` 等日志。
2. OLED 应显示 `SMART HELMET` 后进入数据界面。
3. 未接 ESP01S 时，每 30s 左右会听到蜂鸣器 5s 报警（`ALARM_WIFI_FAIL`）
   —— 这是需求要求的行为，属于"预期现象"，不是故障。
