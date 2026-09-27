/* =========================================================================
 * MQ-2 气体/烟雾传感器驱动 (模拟输出 → ADC)
 *
 * 输出定义(见 mq2.h 说明):
 *     gas_level = ADC原始值 × 100 / 4095      → 0~100 相对量程百分数
 * 这是"相对量程比例", 不是 ppm, 也不是 %LEL。需求给的阈值 50 即
 * "ADC 量程的 50%"。若要得到 ppm, 必须在标准气体下标定后替换本函数。
 *
 * 预热: 半导体气敏元件需要预热后读数才稳定(预热时间请查你所购器件的
 *       数据手册 / 实测确定)。预热期内本函数返回失败, 上层不参与报警判断,
 *       避免上电瞬间误报。
 * ========================================================================= */
#include "mq2.h"
#include "user_config.h"

static ADC_HandleTypeDef *s_adc = 0;

void MQ2_Init(ADC_HandleTypeDef *hadc)
{
    s_adc = hadc;
}

uint8_t MQ2_Read(uint16_t *level_0_100)
{
    ADC_ChannelConfTypeDef cfg = {0};
    uint32_t raw = 0;

    if (s_adc == 0) return 1;

    /* 预热期未过: 数据不可信 */
    if (HAL_GetTick() < MQ2_WARMUP_MS) return 1;

    cfg.Channel      = ADC_CHANNEL_9;          /* PB1 → ADC1_IN9 */
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(s_adc, &cfg) != HAL_OK) return 2;

    if (HAL_ADC_Start(s_adc) != HAL_OK) return 3;
    if (HAL_ADC_PollForConversion(s_adc, 10) != HAL_OK)
    {
        HAL_ADC_Stop(s_adc);
        return 4;
    }
    raw = HAL_ADC_GetValue(s_adc);
    HAL_ADC_Stop(s_adc);

    /* 12 位 ADC: 0~4095 → 0~100 (四舍五入) */
    *level_0_100 = (uint16_t)((raw * 100U + 2047U) / 4095U);
    return 0;
}
