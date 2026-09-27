#ifndef __MAX30102_H
#define __MAX30102_H

#include "stm32f1xx_hal.h"
#include "app_events.h"   /* 使用 PPG_BATCH_MAX */

/* ============================ 重要说明 ============================
 * MAX30102 输出的是光电容积脉搏波(PPG)原始数据。由 PPG 计算心率/血氧
 * 属于算法问题, 且血氧换算系数需要针对具体器件与佩戴条件做标定,
 * 本驱动只负责"取到干净的原始数据", 算法在 heart_rate.c 中。
 *
 * 本驱动提供的功能:
 *   - 器件 ID 校验, 确认 I2C 通信正常
 *   - SpO2 模式(红光+红外)配置与 FIFO 批量读取
 *   - 上电预热样本丢弃(PPG_WARMUP_SAMPLES)
 *   - 【2026-09-16 新增】FIFO 溢出检测与上报(overflow)
 * 本驱动不做:
 *   - 血氧标定 (标定系数需自行实测, 见 user_config.h 说明)
 * ================================================================= */

typedef struct
{
    uint16_t red[PPG_BATCH_MAX];
    uint16_t ir[PPG_BATCH_MAX];
    uint8_t  n;

    /* 本次读取前, FIFO 曾经溢出而丢掉的样本数(0 = 没丢)。
     *
     * 【为什么必须回报这个数】溢出意味着有样本永远没到达 MCU, 于是
     * "样本序号 × 20ms" 这条时间轴就被压缩了。心率算法用 IBI(两次搏动
     * 之间的样本数)算心率, 时间轴一压缩, IBI 就系统性偏短 ⇒ 心率整体
     * 偏高。旧驱动读了 OVF_COUNTER 却把它丢在一边, 算法完全不知情 ——
     * 现场表现就是"心率偏高且不稳, 但怎么看代码都找不出理由"。
     * 拿到这个数后, 任务层调 HR_NotifyDrop() 把时间轴补回来即可。 */
    uint8_t  lost;
} MAX30102_Fifo_t;

uint8_t MAX30102_Init(I2C_HandleTypeDef *hi2c);           /* 0 = 成功 */
uint8_t MAX30102_ReadFifo(MAX30102_Fifo_t *f);            /* 0 = 成功 */
uint8_t MAX30102_IsWarmedUp(void);                        /* 1 = 预热完成 */

/* 器件是否真的挂在 I2C 总线上(即 Init 是否成功)。
 * 【为什么要单独暴露这个状态】
 *   MAX30102 与 OLED、MPU6050 共用 PB6/PB7 这一对引脚, 靠地址 0x57 区分。
 *   一旦它没接好/没供电/地址被拉错, MAX30102_ReadFifo() 会一直失败,
 *   心率就永远显示 "--" —— 但屏幕上分不清是"器件不在总线上"还是
 *   "手指没放好/还没预热"。用本函数把前者单独标出来, 显示层就能直接写
 *   "MAX30102 NG", 省掉一轮猜测。 */
uint8_t MAX30102_IsPresent(void);                         /* 1 = 器件已就绪 */
uint8_t MAX30102_LastInitCode(void);                      /* Init 的返回码, 便于定位 */


#endif /* __MAX30102_H */
