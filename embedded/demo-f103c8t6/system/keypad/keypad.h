
#ifndef __KEYPAD_H__
#define __KEYPAD_H__
#include "main.h"


#define button_Set	 ON

/******************************************************************************************/
/* 引脚 定义 */

#define KEY0_GPIO_PORT                  GPIOB
#define KEY0_GPIO_PIN                   GPIO_PIN_4
#define KEY0_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define KEY1_GPIO_PORT                  GPIOB
#define KEY1_GPIO_PIN                   GPIO_PIN_5
#define KEY1_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define KEY2_GPIO_PORT                  GPIOB
#define KEY2_GPIO_PIN                   GPIO_PIN_6
#define KEY2_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */

#define KEY3_GPIO_PORT                  GPIOB
#define KEY3_GPIO_PIN                   GPIO_PIN_3
#define KEY3_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PE口时钟使能 */


/******************************************************************************************/

#define KEY0        HAL_GPIO_ReadPin(KEY0_GPIO_PORT, KEY0_GPIO_PIN)     /* 读取KEY0引脚 */
#define KEY1        HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN)     /* 读取KEY1引脚 */
#define KEY2        HAL_GPIO_ReadPin(KEY2_GPIO_PORT, KEY2_GPIO_PIN)     /* 读取KEY2引脚 */
#define KEY3        HAL_GPIO_ReadPin(KEY3_GPIO_PORT, KEY3_GPIO_PIN)     /* 读取KEY3引脚 */

void KEY_Init(void);                /* 按键初始化函数 */
void KeyScanHandle(void);  //按键扫描
void KeyScan(void);//获取当前按键值
void Display(void);
void DISplaysetvalue(void);
void Displaysetvalue(void);
void CotorFun(void);
void DisplayTime(void);//显示时间函数
extern uint8_t setn,keyNum[4];
extern char display[16];//显示存储数组
extern uint16_t pay1,pay2,pay3,cardNum,seccost;
extern uint8_t inSec1,inSec2,inSec3,outSec1,outSec2,outSec3;//出入库时间,秒
extern uint8_t inMin1,inMin2,inMin3,outMin1,outMin2,outMin3;//出入库时间,分
extern uint8_t inHour1,inHour2,inHour3,outHour1,outHour2,outHour3;//出入库时间,时
extern uint8_t car1,car2,car3,space1Flag,space2Flag,space3Flag;
extern uint8_t Register;//注册卡标志
extern uint8_t Delete;//删除卡标志
extern uint8_t InitDisplay;//返回主页面标志
extern uint8_t warningFlag,doorFlag;
extern uint8_t ucArray1_ID[16] ;//存放IC卡号
extern uint8_t ucArray2_ID[16] ;//存放IC卡号
extern uint8_t ucArray3_ID[16] ;//存放IC卡号

#endif







