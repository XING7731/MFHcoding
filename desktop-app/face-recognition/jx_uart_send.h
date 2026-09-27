#ifndef JX_UART_SEND_H_
#define JX_UART_SEND_H_

#ifdef __cplusplus
extern "C" {
#endif

// 串口发送消息最大长度
#define UART_SEND_MAX      32
#define UART_MSG_HEAD_LEN  2
#define UART_MSG_FOOT_LEN  2

// 串口发送消息号
#define U_MSG_Tem_In_Play      1
#define U_MSG_Tem_Out_Play      2
#define U_MSG_Hum_In_Play      3
#define U_MSG_Hum_Out_Play      4
#define U_MSG_Smoke_Play      5
#define U_MSG_Light_Play      6
#define U_MSG_Person_Play      7


// action: Tem_In_Play
void _uart_Tem_In_Play(int Tem_In);

// action: Tem_Out_Play
void _uart_Tem_Out_Play(int Tem_Out_L, int Tem_Out_H);

// action: Hum_In_Play
void _uart_Hum_In_Play(int Hum_In);

// action: Hum_Out_Play
void _uart_Hum_Out_Play(int Hum_Out);

// action: Smoke_Play
void _uart_Smoke_Play(int Smoke_Value);

// action: Light_Play
void _uart_Light_Play(int Light_Value);

// action: Person_Play
void _uart_Person_Play(int Person_Value);

#ifdef __cplusplus
}
#endif

#endif /*JX_UART_SEND_H_*/

