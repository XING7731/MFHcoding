#ifndef _USART_H
#define _USART_H

#include "main.h"


#define USART_REC_LEN  			512  	  //定义最大接收字节数 100
#define PORTn  			        4  	    //定义通道个数
#define TX_TIMEOUT          ((uint32_t)100)	  


extern UART_HandleTypeDef UART1_Handler,UART2_Handler,UART3_Handler; //UART1句柄
extern u16 UART1_Rx_cnt;
extern u16 UART2_Rx_cnt;
extern u16 UART3_Rx_cnt;

#define RXBUFFERSIZE   1 //缓存大小

extern u8 USART1Buf[RXBUFFERSIZE];//HAL库USART接收Buffer
extern u8 USART2Buf[RXBUFFERSIZE];//HAL库USART接收Buffer
extern u8 USART3Buf[RXBUFFERSIZE];//HAL库USART接收Buffer

extern u8 U1Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
extern u8 U2Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
extern u8 U3Buffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
extern u8 JWBuffer[USART_REC_LEN];//HAL库使用的串口接收缓冲
extern uint16_t CO2;
extern uint16_t co2;
#define S_RX_BUF_SIZE		30
#define S_TX_BUF_SIZE		128

void Serial_Printf(const char *fmt, ...);
void UART_Setinit(uint32_t bound1,uint32_t bound2,uint32_t bound3);
void Uart_Init(void);
void Serial_PutString(char *p_string);
void Serial_PutHex(uint8_t *data, uint16_t len);
void send_message(void);

void USART1_send(u8* buf,u8 len);
void USART2_send(u8* buf,u8 len);
void USART3_send(u8* buf,u8 len);	
#endif
