#include "buzzer.h"
#include "user_config.h"

void Buzzer_Init(void)
{
    /* 引脚方向由 CubeMX 的 MX_GPIO_Init() 配置为推挽输出, 这里只保证初始静音 */
    Buzzer_Off();
}

void Buzzer_On(void)
{
#if BUZZER_ACTIVE_HIGH
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
#else
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

void Buzzer_Off(void)
{
#if BUZZER_ACTIVE_HIGH
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
#endif
}
