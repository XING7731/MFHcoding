#include "main.h"
#include "keypad.h"

#if(key_pad_Set)

#define PASSWORD_LENGTH 6   // 密码长度

// 默认密码和初始化密码
uint16_t default_password[PASSWORD_LENGTH] = {1, 2, 3, 4, 5, 6};  // 默认密码 123456
uint16_t current_password[PASSWORD_LENGTH];  // 当前密码
uint16_t input_password[PASSWORD_LENGTH];  // 存储用户输入的密码
uint8_t input_index = 0;  // 当前输入的位置

uint8_t is_password_set = 0;  // 标志：密码是否已设置
uint8_t password_set_flag = 0;  // 设置密码的标志位，指示是否进入设置密码模式

// 按键映射表，2维
static const uint8_t Keypad_Map[KEYPAD_ROWS][KEYPAD_COLS] = {
	{KEY_reg,    KEY_7,    KEY_8,    KEY_9},
	{KEY_del,    KEY_4,    KEY_5,    KEY_6},
	{KEY_fin,    KEY_1,    KEY_2,    KEY_3},
	{KEY_set,    KEY_ent,  KEY_0,    KEY_can}
};

// 消抖变量
static uint8_t debounce_counter = 0;
static uint8_t last_key = KEY_NONE;
static uint8_t current_key = KEY_NONE;

// 回调函数数组
static KeyActionCallback keyCallbacks[16] = {NULL};

/******************** 硬件初始化 ********************/
void Keypad_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};


    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();


    GPIO_InitStruct.Pin = ROW1_PIN | ROW2_PIN | ROW3_PIN | ROW4_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ROW_PORT, &GPIO_InitStruct);


    GPIO_InitStruct.Pin = COL1_PIN | COL2_PIN | COL3_PIN | COL4_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;  
    HAL_GPIO_Init(COL_PORT, &GPIO_InitStruct);


    HAL_GPIO_WritePin(ROW_PORT, ROW1_PIN | ROW2_PIN | ROW3_PIN | ROW4_PIN, GPIO_PIN_SET);
}

/******************** 按键扫描核心逻辑 ********************/
uint8_t Keypad_Scan(void) 
{
    uint8_t row, col;
    const uint16_t ROW_PINS[4] = {ROW1_PIN, ROW2_PIN, ROW3_PIN, ROW4_PIN};
    const uint16_t COL_PINS[4] = {COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN};

    for (row = 0; row < 4; row++) {
        // 1. 拉低当前行（其他行置高）
        HAL_GPIO_WritePin(ROW_PORT, 
            ROW1_PIN | ROW2_PIN | ROW3_PIN | ROW4_PIN, 
            GPIO_PIN_SET);
        HAL_GPIO_WritePin(ROW_PORT, ROW_PINS[row], GPIO_PIN_RESET);
        
        // 2. 延时稳定（1~2us）
        for(volatile int i=0; i<50; i++);

        // 3. 读取列线状态（无需切换模式）
        for (col = 0; col < 4; col++) {
            // 连续采样3次消抖
            uint8_t sample = 0;
            for(int i=0; i<3; i++) {
                if(HAL_GPIO_ReadPin(COL_PORT, COL_PINS[col]) == GPIO_PIN_RESET) {
                    sample++;
                }
                for(volatile int j=0; j<10; j++); // 间隔约200ns
            }
            
            // 3次采样中2次为低才确认按键
            if(sample >= 2) {
                // 恢复行线状态
                HAL_GPIO_WritePin(ROW_PORT, ROW_PINS[row], GPIO_PIN_SET);
                return Keypad_Map[row][col];
            }
        }
        
        // 4. 恢复当前行
        HAL_GPIO_WritePin(ROW_PORT, ROW_PINS[row], GPIO_PIN_SET);
    }
    return KEY_NONE;
}
uint8_t key;
/******************** 带消抖的按键获取 ********************/
uint8_t Keypad_GetKey(void) {
    key = Keypad_Scan();

    if (key != KEY_NONE) {
        if (key == last_key) {
            if (++debounce_counter >= 5) {  // 消抖时间约5ms
                debounce_counter = 0;
                if (current_key != key) {
                    current_key = key;
                    return key;
                }
            }
        } else {
            debounce_counter = 0;
            last_key = key;
        }
    } else {
        debounce_counter = 0;
        last_key = KEY_NONE;
        current_key = KEY_NONE;
    }
    return KEY_NONE;
}

/******************** 回调系统 ********************/
void Keypad_RegisterCallback(uint8_t key, KeyActionCallback callback) {
    if (key < 16) keyCallbacks[key] = callback;
}

void Keypad_HandleKeyPress(uint8_t key) {
    if (key != KEY_NONE && keyCallbacks[key] != NULL) {
        keyCallbacks[key]();
    }
		Set_Password();
		if(Check_Password())
		{
			
		}	
}




//按键处理函数
void Key1_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 1;
    }
}
void Key2_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 2;
    }
}
void Key3_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 3;
    }
}
void Key4_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 4;
    }
}
void Key5_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 5;
    }
}
void Key6_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 6;
    }
}
void Key7_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 7;
    }
}
void Key8_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 8;
    }
}
void Key9_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 9;
    }
}
void Key10_Action(void) { 
    if (password_set_flag == 1 && input_index < PASSWORD_LENGTH) {
        input_password[input_index++] = 0;
    }
}

//注册卡片
void Key11_Action(void) 
{ 
    if(is_password_set == 0 && password_set_flag == 0) 
		{  // 密码未设置并且尚未进入设置阶段
        password_set_flag = 1;  // 进入设置密码模式
        input_index = 0;  // 重置输入
        memset(input_password, 0, sizeof(input_password));  // 清空输入的密码
        // 提示用户开始设置密码
    }
}

//删除卡号
void Key12_Action(void) 
{ 
    if(is_password_set == 0 && password_set_flag == 0)
		{
        password_set_flag = 1;
        input_index = 0;
        memset(input_password, 0, sizeof(input_password));
    }
}

//录入指纹
void Key13_Action(void)
{ 
    if (is_password_set == 0 && password_set_flag == 0) {
        password_set_flag = 1;
        input_index = 0;
        memset(input_password, 0, sizeof(input_password));
    }
}

//修改密码
void Key14_Action(void) { 
    if (is_password_set == 0 && password_set_flag == 0) {
        password_set_flag = 1;
        input_index = 0;
        memset(input_password, 0, sizeof(input_password));
    }
}

// 确认按键
void Key15_Action(void) 
{
    if (password_set_flag == 1 && input_index == PASSWORD_LENGTH) {
        Set_Password();  // 设置密码
    } else {
        // 提示密码未设置完全，用户需要输入6位
    }
}

// 删除按键
void Key16_Action(void) 
{
    if(input_index > 0)
		{
			input_index--;  // 删除最后输入的数字
			input_password[input_index] = 0;  // 清除已删除的数字
    }
}



// 设置密码
void Set_Password(void) 
{
    if (input_index == PASSWORD_LENGTH) 
		{
        memcpy(current_password, input_password, PASSWORD_LENGTH * sizeof(uint16_t));  // 设置密码
        is_password_set = 1;  // 设置密码标志
        password_set_flag = 0;  // 退出设置密码模式
        // 提示密码设置成功
    }
}

// 密码验证函数
uint8_t Check_Password(void) 
{
    // 如果密码未设置，使用默认密码
    uint16_t* password_to_check = (is_password_set) ? current_password : default_password;

    // 比较用户输入的密码与正确的密码
    for (uint8_t i = 0; i < PASSWORD_LENGTH; i++) 
	  {
        if (input_password[i] != password_to_check[i]) {
            return 0;  // 密码错误
        }
    }
    return 1;  // 密码正确
}


void KEY_Init(void)
{
	  Keypad_Init();
    Keypad_RegisterCallback(KEY_1, Key1_Action);
    Keypad_RegisterCallback(KEY_2, Key2_Action);
		Keypad_RegisterCallback(KEY_3, Key3_Action);	
		Keypad_RegisterCallback(KEY_4, Key4_Action);	

    Keypad_RegisterCallback(KEY_5, Key5_Action);
    Keypad_RegisterCallback(KEY_6, Key6_Action);
		Keypad_RegisterCallback(KEY_7, Key7_Action);	
		Keypad_RegisterCallback(KEY_8, Key8_Action);	

    Keypad_RegisterCallback(KEY_9, Key9_Action);
    Keypad_RegisterCallback(KEY_0, Key10_Action);
		Keypad_RegisterCallback(KEY_reg, Key11_Action);	  //注册
		Keypad_RegisterCallback(KEY_del, Key12_Action);	  //删除卡号

    Keypad_RegisterCallback(KEY_fin, Key13_Action);   //指纹
    Keypad_RegisterCallback(KEY_set, Key14_Action);   //修改密码
		Keypad_RegisterCallback(KEY_ent, Key15_Action);	  //确认
		Keypad_RegisterCallback(KEY_can, Key16_Action);		//取消
	
}	
  


#endif



#if(button_Set)


// 消抖和长按时间配置（单位：ms）
#define DEBOUNCE_TIME       15
#define SHORT_CLICK_TIME    300
#define LONG_HOLD_TIME      2000

// 按键状态机状态
typedef enum {
    BTN_STATE_RELEASED = 0,
    BTN_STATE_DEBOUNCE,
    BTN_STATE_PRESSED,
    BTN_STATE_WAIT_LONG
} ButtonState;

// 按键控制块
typedef struct {
    ButtonState state;
    uint32_t press_tick;
    uint8_t long_triggered;
} ButtonCB;

// 静态变量（每个按键独立状态）
static ButtonCB button_cb[4] = {0}; // 支持最多4个按键

// 初始化所有启用的按键
void KEY_Init(void) 
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
#if(Set_button_1)
    Button1_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = Button1_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP; // 关键修改：上拉输入
    HAL_GPIO_Init(Button1_GPIO_PORT, &GPIO_InitStruct);
#endif

#if(Set_button_2)
    BUTTON2_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = BUTTON2_GPIO_PIN;
    HAL_GPIO_Init(BUTTON2_GPIO_PORT, &GPIO_InitStruct);
#endif

#if(Set_button_3)
    BUTTON3_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = BUTTON3_GPIO_PIN;
    HAL_GPIO_Init(BUTTON3_GPIO_PORT, &GPIO_InitStruct);
#endif

#if(Set_button_4)
    BUTTON4_GPIO_CLK_ENABLE();
    GPIO_InitStruct.Pin = BUTTON4_GPIO_PIN;
    HAL_GPIO_Init(BUTTON4_GPIO_PORT, &GPIO_InitStruct);
#endif
}

// 获取按键物理状态（0=释放，1=按下）
uint8_t Button_GetState(uint8_t button_id)
{
    GPIO_PinState pin_state;
    
    switch(button_id) {
#if(Set_button_1)
        case 1:
            pin_state = HAL_GPIO_ReadPin(Button1_GPIO_PORT, Button1_GPIO_PIN);
            return (pin_state == GPIO_PIN_RESET) ? 1 : 0;
#endif
#if(Set_button_2)
        case 2:
            pin_state = HAL_GPIO_ReadPin(BUTTON2_GPIO_PORT, BUTTON2_GPIO_PIN);
            return (pin_state == GPIO_PIN_RESET) ? 1 : 0;
#endif
#if(Set_button_3)
        case 3:
            pin_state = HAL_GPIO_ReadPin(BUTTON3_GPIO_PORT, BUTTON3_GPIO_PIN);
            return (pin_state == GPIO_PIN_RESET) ? 1 : 0;
#endif
#if(Set_button_4)
        case 4:
            pin_state = HAL_GPIO_ReadPin(BUTTON4_GPIO_PORT, BUTTON4_GPIO_PIN);
            return (pin_state == GPIO_PIN_RESET) ? 1 : 0;
#endif
        default:
            return 0;
    }
}

// 核心状态机处理函数
ButtonEvent Button_GetEvent(uint8_t button_id) 
{
    if(button_id == 0 || button_id > 4) return BTN_EVENT_NONE;
    
    uint8_t idx = button_id - 1;
    ButtonCB* cb = &button_cb[idx];
    uint32_t now = HAL_GetTick();
    uint8_t is_pressed = Button_GetState(button_id);

    switch(cb->state) {
        case BTN_STATE_RELEASED:
            if(is_pressed) {
                cb->state = BTN_STATE_DEBOUNCE;
                cb->press_tick = now;
            }
            break;

        case BTN_STATE_DEBOUNCE:
            if((now - cb->press_tick) >= DEBOUNCE_TIME) {
                if(is_pressed) {
                    cb->state = BTN_STATE_PRESSED;
                    return BTN_EVENT_PRESS_DOWN;
                } else {
                    cb->state = BTN_STATE_RELEASED;
                }
            }
            break;

        case BTN_STATE_PRESSED:
            if(!is_pressed) {
                cb->state = BTN_STATE_RELEASED;
                return ((now - cb->press_tick) < SHORT_CLICK_TIME) ? 
                       BTN_EVENT_SHORT_CLICK : BTN_EVENT_LONG_CLICK;
            } 
            else if((now - cb->press_tick) >= LONG_HOLD_TIME) {
                cb->state = BTN_STATE_WAIT_LONG;
                cb->long_triggered = 1;
                return BTN_EVENT_LONG_HOLD;
            }
            break;

        case BTN_STATE_WAIT_LONG:
            if(!is_pressed) {
                cb->state = BTN_STATE_RELEASED;
                cb->long_triggered = 0;
                return BTN_EVENT_LONG_CLICK;
            }
            break;
    }

    return BTN_EVENT_NONE;
}


uint16_t key_event[5];
void Process_Button_Events(void)
{
#if(Set_button_1)
    ButtonEvent evt1 = Button_GetEvent(1);
    if(evt1 == BTN_EVENT_SHORT_CLICK)
		{
			key_event[1]++;
			if(key_event[1]>=2)key_event[1]=0;
		}			
//    if(evt1 == BTN_EVENT_LONG_HOLD) ;
#endif

#if(Set_button_2)
    ButtonEvent evt2 = Button_GetEvent(2);
    // 处理BUTTON2事件...
#endif
}

#endif




void Keypad_Handle(void)
{
#if(key_pad_Set)	
	Keypad_HandleKeyPress(Keypad_GetKey());	
#endif	
#if(button_Set)	
	  Process_Button_Events();
#endif	
}	



#if(1)	
//废案，测试正常，可以使用，暂无扩展功能舍弃
/**
 * @brief 检测按键是否按下（低电平有效，带消抖）
 * @return 1: 按下, 0: 未按下
 */
uint8_t Key_Scan(void) 
{
    static uint8_t key_up = 1;  // 按键松开标志

    if (key_up && (HAL_GPIO_ReadPin(Button1_GPIO_PORT, Button1_GPIO_PIN) == GPIO_PIN_RESET)) 
			{
        HAL_Delay(10);  // 消抖延时（10ms）
        key_up = 0;      // 标记按键已按下
        if (HAL_GPIO_ReadPin(Button1_GPIO_PORT, Button1_GPIO_PIN) == GPIO_PIN_RESET) 
				{
            return 1;    // 确认按下
        }
      } 
			else if (HAL_GPIO_ReadPin(Button1_GPIO_PORT, Button1_GPIO_PIN) == GPIO_PIN_SET)
				{
        key_up = 1;      // 按键已松开
        }
    return 0;
}




// 获取当前按键状态（带消抖）
uint8_t Button_GetState(void)
{
	uint8_t state = HAL_GPIO_ReadPin(Button1_GPIO_PORT, Button1_GPIO_PIN);
	
	if(state != last_state) 
	{
		debounce_counter++;
		if(debounce_counter >= 5) {  // 消抖时间约5ms
			debounce_counter = 0;
			last_state = state;
			current_state = state;
		}
	}else{
		debounce_counter = 0;
	}
	
	return current_state;
}

// 获取按键事件（按下、释放、短按、长按）
uint8_t Button_GetEvent(void)
{
	static uint8_t last_btn_state = BUTTON_RELEASED;
	uint8_t btn_state = Button_GetState();
	uint8_t event = BTN_EVENT_NONE;
	
	// 状态变化检测
	if (btn_state != last_btn_state) {
		if (btn_state == BUTTON_PRESSED) {
			// 按键按下
			event = BTN_EVENT_PRESSED;
			press_start_time = HAL_GetTick();
			long_press_detected = 0;
		} else {
			// 按键释放
			event = BTN_EVENT_RELEASED;
			if (!long_press_detected && (HAL_GetTick() - press_start_time < 1000)) {
				event = BTN_EVENT_SHORT_PRESS;
			}
		}
		last_btn_state = btn_state;
	} 
	// 长按检测
	else if (btn_state == BUTTON_PRESSED && !long_press_detected) {
		if (HAL_GetTick() - press_start_time >= 1000) {  // 长按1秒
			event = BTN_EVENT_LONG_PRESS;
			long_press_detected = 1;
		}
	}
	
	return event;
}

uint16_t key_x;
/**
 * @brief 处理按键事件
 * @param btn_event 按键事件类型
 */
void Handle_ButtonEvent(uint8_t btn_event)
{
    switch (btn_event) {
        case BTN_EVENT_PRESSED:  // 按键按下事件处理
						key_x=1;
            break;
        
        case BTN_EVENT_RELEASED:	// 按键释放事件处理
            key_x=2;
            break;
        
        case BTN_EVENT_SHORT_PRESS: // 短按事件处理
             key_x=3;
            
            break;
        
        case BTN_EVENT_LONG_PRESS:	// 长按事件处理
             key_x=4;
            // 可以在这里添加进入配置模式等操作
            break;
        
        default:
            break;
    }
}
#endif	