# MFHcoding

> 个人项目归档仓库 —— 按**技术方向**分类，每个子目录都是一个可直接打开、直接编译的完整工程。

本仓库只收录**源码与工程框架**：编译产物（Keil 的 `obj/axf`、IAR 的 `Exe/List/Obj`）、
PyPI/npm 依赖、压缩包副本、虚拟机镜像都已排除。
凡是"clone 下来要能重新编译"必需的东西（含 Keil 的 `.uvprojx`/`.uvoptx`/`RTE/`、
小程序的 `vant` 组件库 `dist/`）都保留。

---

## 分类总览

| 分类 | 说明 | 项目数 |
|---|---|---|
| [`embedded/`](#embedded--嵌入式--单片机) | STM32 / 单片机固件（Keil、IAR） | 6 |
| [`wechat-miniprogram/`](#wechat-miniprogram--微信小程序) | 微信小程序 | 2 |
| [`desktop-app/`](#desktop-app--桌面应用) | 桌面上位机 | 1 |
| [`hardware/`](#hardware--硬件设计) | 电路 / PCB 设计 | 1 |

---

## 目录结构

```
MFHcoding/
├── embedded/                              STM32 / 单片机
│   ├── smart-helmet-freertos/             ★ 智能安全帽（主项目，STM32F103 + FreeRTOS）
│   ├── stm32-dht11-cloud/                 DHT11 温湿度上云
│   ├── demo-f103c8t6/                     F103C8T6 课程 Demo（Keil）
│   ├── makefile-hw/                       Makefile 作业
│   └── training/                          信盈达实训
│       ├── day1-iar-gpio/                 IAR 环境搭建 & GPIO
│       └── day2-zstack/                   ZStack 协议栈
│
├── wechat-miniprogram/                    微信小程序
│   ├── smart-helmet-monitor/              智能头盔监控端
│   └── smart-hotel/                       未来酒店·智慧监控
│
├── desktop-app/
│   └── face-recognition/                  Qt + OpenCV 人脸识别上位机
│
└── hardware/
    └── pcb01/                             EasyEDA PCB 工程
```

---

## embedded / 嵌入式 · 单片机

### ★ smart-helmet-freertos —— 智能工地安全帽边缘感知系统

STM32F103C8T6 + HAL 库 + **FreeRTOS** 的多传感器融合实时监控终端。
固件为 Keil MDK 工程，配套微信小程序监控端见 [`wechat-miniprogram/smart-helmet-monitor`](#smart-helmet-monitor--智能头盔监控端)。

| 项目 | 内容 |
|---|---|
| 采集 | MAX30102（心率血氧）· MQ-2（气体）· DS18B20 + DHT11（温湿度）· MPU6050（六轴姿态） |
| 本地 | OLED 0.96" SSD1306 显示 + 蜂鸣器报警 + SOS 紧急按键（EXTI4 中断） |
| 联网 | ESP01S 走 AT 指令自组 **MQTT 3.1.1** 报文，上报巴法云 |
| 边缘判定 | 摔倒撞击状态机、气体超标、温度上下限、DHT11→DS18B20 容错降级 |
| 在线配置 | 门限可由小程序下发（9 项 + `cv` 16 位指纹回读确认），掉电回默认（刻意不写 Flash） |

- **过程文档 40 篇**：[`docs/`](embedded/smart-helmet-freertos/docs) —— 每篇记录一次真实故障的
  "起因 / 根因 / 修法 / 验证数据"，含被仿真否掉的假设。
  其中几篇值得一读：
  - `docs/38` 心率测不出的真因：**ADC 饱和**（`d` 顶到 65535 恰好等价 18 位饱和），
    以及"把模型里可任意放大的量当成真机也能"的同族错误；
  - `docs/40` 只让小程序告警：**改门限是两步**（拖滑块只改界面，点按钮才落本机）；
  - `docs/39` 删除光照功能全链路：枚举被当下标用时**改名占位而不是删除**。
- **验证脚本 30 个**：[`tools/`](embedded/smart-helmet-freertos/tools) ——
  仿真（`*_sim.py`，断言与结论落 `*_result.txt`）、小程序静态回归
  （`wxmp_*.js`，用最小 `wx` 假环境在 Node 里 `require` 真实页面代码）、
  只读诊断探针（`thresh_why.py`）。
- **接线对应表**：[`docs/06-接线对应表.md`](embedded/smart-helmet-freertos/docs/06-接线对应表.md)
  —— 总览速查 / 引脚占用总表 / 电源分配 / 上拉清单 / 逐模块接线 / 上电自查清单。
- 无头编译：`python tools/build_keil.py` → 看 `MDK-ARM/build_report.txt`，
  当前交付状态 **0 Error / 0 Warning**。

> 工程约定：`_` 开头的文件是一次性临时探针，不进仓库。

### stm32-dht11-cloud —— 温湿度上云

STM32 每 6 秒读 DHT11，经 ESP8266-01S 连 WiFi，把 `温度,湿度` 上传巴法云 TCP 创客云；
每 25 秒心跳保活、断线自动重连。

三个新手最容易卡住的点（README 里有展开）：**ESP8266 必须独立 3.3V ≥300mA**、
DHT11 DATA 需上拉、所有模块必须共地。

### demo-f103c8t6 —— F103C8T6 课程 Demo

CubeMX 生成的 Keil MDK 工程骨架（Core / Drivers / MDK-ARM / Hardware 分层），
作为后续项目的模板。

### makefile-hw —— Makefile 作业

手写 Makefile 的交叉编译作业（`Makefile` + `src/` + `include/` + 报告）。
学号 202310916218。

### training/ —— 信盈达实训

| 目录 | 内容 |
|---|---|
| `day1-iar-gpio/` | IAR 环境搭建与 GPIO —— 含 `Future_Hotel` IAR 工程（`.ewp`/`.ewd`/`.eww`） |
| `day2-zstack/` | ZStack 协议栈 |

> 只收录了 `04.项目源码`（源码与 IAR 工程文件）。
> `01.项目资料`（39 MB 课程讲义）与 `02.项目软件`（**387 MB** 工具安装包）未收录。

---

## wechat-miniprogram / 微信小程序

### smart-helmet-monitor —— 智能头盔监控端

配套上面 `smart-helmet-freertos` 的实时监控小程序，三个 tab：

| Tab | 功能 |
|---|---|
| 实时监测 | 生命体征 / 环境 / 姿态分区显示，**阈值预警横幅**（小程序侧独立判读），一键 SOS |
| 趋势与记录 | 自绘折线图（`utils/chart.js` 拆 `buildPlan()`/`drawPlan()`），温湿度双轴视图 |
| 阈值设置 | 温湿度 / 心率 / 血氧 / 气体门限滑块，两个按钮：**「仅存本机」** 与 **「保存并下发设备」** |

双通道：`wss://bemfa.com` 实时推送，连不上自动退回 HTTP 轮询。
工程内自带 `README.md` 说明报文格式与排查表。

### smart-hotel —— 未来酒店 · 智慧监控

微信小程序（`pages/index` `pages/rooms` `pages/setting`），
UI 基于 **vant-weapp**（组件库以 `dist/` 形式内置于工程，无 `node_modules`）。

> ⚠️ `dist/` 是**必须提交**的组件库，不是编译产物 —— 见 [.gitignore](.gitignore) 里的说明。

---

## desktop-app / 桌面应用

### face-recognition —— Qt + OpenCV 人脸识别上位机

Qt Creator 工程（`QT += core gui network`），依赖 **OpenCV 3.4.5**。
包含串口通信（`jx_uart_send`）、酒店业务 API（`future_hotel_api`）、
传感器与设置界面（`sensor.ui` / `setting.ui`）、自定义控件（`lldk.ui` / `widget.ui`）。

> `.pro` 里的 OpenCV 头文件路径写的是作者本机的 `/opt/opencv/opencv-3.4.5/install/...`，
> clone 后需按自己环境改 `INCLUDEPATH` / `LIBS`。

---

## hardware / 硬件设计

### pcb01 —— EasyEDA PCB 工程

立创/EasyEDA 工程文件（`1.eprj2`）+ 一份备份。

---

## 未收录的内容

| 内容 | 原因 |
|---|---|
| `linux/Smart_hotel_Study` | **35 GB** 虚拟机镜像，不适合进 Git |
| `02.项目软件`（信盈达实训） | 387 MB 的 IAR / 工具安装包，可自行下载 |
| 各工程的编译产物 | Keil `obj/axf`、IAR `Exe/List/Obj`，可重新生成 |
| CMSIS 的 `Lib/` 与 `DSP/` | 合计 49 MB 预编译库，且本工程 `uvprojx` 并未引用 |
| 桌面上的 `.zip` / `.7z` | 与目录内容重复的副本 |

---

## 说明

- 各工程原有的 `README.md` / `docs/` 保持原样，本文件只做**索引与分类**，不替代它们。
- 提交时保留了各工程所需的工程文件（Keil `.uvprojx`/`.uvoptx`/`RTE/`、IAR `.ewp`/`.ewd`、
  Qt `.pro`），目标是**clone 下来能直接打开**。
