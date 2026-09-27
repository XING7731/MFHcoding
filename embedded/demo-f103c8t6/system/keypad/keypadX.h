
#ifndef __KEYPADX_H__
#define __KEYPADX_H__
#include "main.h"

#define key_pad_Set  OFF
#define button_Set	 ON

// 行线配置（输出）
#define ROW1_PIN    GPIO_PIN_0
#define ROW2_PIN    GPIO_PIN_1
#define ROW3_PIN    GPIO_PIN_2
#define ROW4_PIN    GPIO_PIN_3
#define ROW_PORT    GPIOA

// 列线配置（输入）
#define COL1_PIN    GPIO_PIN_6
#define COL2_PIN    GPIO_PIN_7
#define COL3_PIN    GPIO_PIN_8
#define COL4_PIN    GPIO_PIN_9
#define COL_PORT    GPIOB







#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

// 按键定义
#define KEY_NONE   0xFF
#define KEY_1      0
#define KEY_2      1
#define KEY_3      2
#define KEY_reg    3
#define KEY_4      4
#define KEY_5      5
#define KEY_6      6
#define KEY_del    7
#define KEY_7      8
#define KEY_8      9
#define KEY_9      10
#define KEY_fin    11
#define KEY_set    12
#define KEY_0      13
#define KEY_ent    14
#define KEY_can    15

// 按键回调函数类型
typedef void (*KeyActionCallback)(void);

// 初始化函数
void Keypad_Init(void);
uint8_t Keypad_GetKey(void);
void KEY_Init(void);
// 回调系统
void Keypad_RegisterCallback(uint8_t key, KeyActionCallback callback);
void Keypad_HandleKeyPress(uint8_t key);
void Keypad_Handle(void);
void Key1_Action(void);
void Key2_Action(void);
void Key3_Action(void);
void Key4_Action(void);

void Key5_Action(void);
void Key6_Action(void);
void Key7_Action(void);
void Key8_Action(void);

void Key9_Action(void);
void Key10_Action(void);
void Key11_Action(void);
void Key12_Action(void);


void Key13_Action(void);
void Key14_Action(void);
void Key15_Action(void);
void Key16_Action(void);



uint8_t Check_Password(void);
void Set_Password(void);




#define Set_button_1	ON
#define Set_button_2	OFF
#define Set_button_3	OFF
#define Set_button_4	OFF

#define Button1_GPIO_PORT            	  GPIOB
#define Button1_GPIO_PIN                GPIO_PIN_12
#define Button1_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0) 

// 按键引脚定义
#define Button2_GPIO_PORT            	  GPIOC
#define Button2_GPIO_PIN                GPIO_PIN_14
#define Button2_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0) 



// 条件编译配置（与你的工程保持一致）
#if !defined(Set_button_1)
#define Set_button_1 1  // 默认启用BUTTON1
#endif

#if !defined(Set_button_2)
#define Set_button_2 1  // 默认启用BUTTON2
#endif

#if !defined(Set_button_3)
#define Set_button_3 0  // 默认禁用BUTTON3
#endif

#if !defined(Set_button_4)
#define Set_button_4 0  // 默认禁用BUTTON4
#endif

// 按键事件类型
typedef enum {
    BTN_EVENT_NONE = 0,     // 无事件
    BTN_EVENT_PRESS_DOWN,   // 按下（消抖后）
    BTN_EVENT_SHORT_CLICK,  // 短按（释放后判定）
    BTN_EVENT_LONG_HOLD,    // 长按（持续按住）
    BTN_EVENT_LONG_CLICK    // 长按后释放
} ButtonEvent;

// 按键硬件配置（根据实际电路修改）
#if(Set_button_1)
#define BUTTON1_GPIO_PORT       GPIOB
#define BUTTON1_GPIO_PIN        GPIO_PIN_12
#define BUTTON1_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

#if(Set_button_2)
#define BUTTON2_GPIO_PORT       GPIOB
#define BUTTON2_GPIO_PIN        GPIO_PIN_13
#define BUTTON2_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

#if(Set_button_3)
#define BUTTON3_GPIO_PORT       GPIOB
#define BUTTON3_GPIO_PIN        GPIO_PIN_14
#define BUTTON3_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

#if(Set_button_4)
#define BUTTON4_GPIO_PORT       GPIOB
#define BUTTON4_GPIO_PIN        GPIO_PIN_15
#define BUTTON4_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

// 函数声明
void KEY_Init(void);
ButtonEvent Button_GetEvent(uint8_t button_id);
uint8_t Button_GetState(uint8_t button_id);

extern  uint16_t key_event[5];

#endif






