#ifndef __SPI_h__
#define __SPI_h__
#include "main.h"
#include "spi.h"


/* SPI总线速度设置 */
#define SPI_SPEED_2         0
#define SPI_SPEED_4         1
#define SPI_SPEED_8         2
#define SPI_SPEED_16        3
#define SPI_SPEED_32        4
#define SPI_SPEED_64        5
#define SPI_SPEED_128       6
#define SPI_SPEED_256       7



/* 引脚 定义 */ 


#define SPI2_SCK_GPIO_PORT            	  GPIOB
#define SPI2_SCK_GPIO_PIN                 GPIO_PIN_13
#define SPI2_SCK_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define SPI2_MISO_GPIO_PORT               GPIOB
#define SPI2_MISO_GPIO_PIN                GPIO_PIN_14
#define SPI2_MISO_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define SPI2_MOSI_GPIO_PORT               GPIOB
#define SPI2_MOSI_GPIO_PIN                GPIO_PIN_15
#define SPI2_MOSI_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define SPI2_CS_GPIO_PORT                	GPIOB
#define SPI2_CS_GPIO_PIN                 	GPIO_PIN_12
#define SPI2_CS_GPIO_CLK_ENABLE()        	do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */
/******************************************************************************************/
#define SPI2_CS(x)  	  do{ x ? \
												HAL_GPIO_WritePin(SPI2_CS_GPIO_PORT, SPI2_CS_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI2_CS_GPIO_PORT, SPI2_CS_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)  

#define SPI2_SCK(x)   	do{ x ? \
												HAL_GPIO_WritePin(SPI2_SCK_GPIO_PORT, SPI2_SCK_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI2_SCK_GPIO_PORT, SPI2_SCK_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)      
 
#define SPI2_MOSI(x)    do{ x ? \
												HAL_GPIO_WritePin(SPI2_MOSI_GPIO_PORT, SPI2_MOSI_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI2_MOSI_GPIO_PORT, SPI2_MOSI_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0) 

#define SPI2_MISO    		HAL_GPIO_ReadPin(SPI2_MISO_GPIO_PORT, SPI2_MISO_GPIO_PIN) 
/******************************************************************************************/	
#define SPI3_CS_GPIO_PORT                	GPIOA
#define SPI3_CS_GPIO_PIN                 	GPIO_PIN_15
#define SPI3_CS_GPIO_CLK_ENABLE()        	do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */
													
#define SPI3_SCK_GPIO_PORT            	  GPIOA
#define SPI3_SCK_GPIO_PIN                 GPIO_PIN_15
#define SPI3_SCK_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define SPI3_MISO_GPIO_PORT               GPIOA
#define SPI3_MISO_GPIO_PIN                GPIO_PIN_15
#define SPI3_MISO_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define SPI3_MOSI_GPIO_PORT               GPIOA
#define SPI3_MOSI_GPIO_PIN                GPIO_PIN_3
#define SPI3_MOSI_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

											
											
/******************************************************************************************/													
												
#define SPI3_CS(x)  	  do{ x ? \
												HAL_GPIO_WritePin(SPI3_CS_GPIO_PORT, SPI3_CS_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI3_CS_GPIO_PORT, SPI3_CS_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)  
												
#define SPI3_SCK(x)   	do{ x ? \
												HAL_GPIO_WritePin(SPI3_SCK_GPIO_PORT, SPI3_SCK_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI3_SCK_GPIO_PORT, SPI3_SCK_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0)       

#define SPI3_MOSI(x)    do{ x ? \
												HAL_GPIO_WritePin(SPI3_MOSI_GPIO_PORT, SPI3_MOSI_GPIO_PIN, GPIO_PIN_SET) : \
												HAL_GPIO_WritePin(SPI3_MOSI_GPIO_PORT, SPI3_MOSI_GPIO_PIN, GPIO_PIN_RESET); \
												}while(0) 

#define SPI3_MISO    		HAL_GPIO_ReadPin(SPI3_MISO_GPIO_PORT, SPI3_MISO_GPIO_PIN)  


//#define SIMULATE_DELAY_US __nop()
#define SIMULATE_DELAY_US delay_us(1);



void MX_SPIx_Init(void);
uint8_t spi3_read_write_byte(uint8_t txdata);												
uint8_t spi2_read_write_byte(uint8_t txdata);
uint8_t spi1_read_write_byte(uint8_t txdata);
uint8_t spi2_read_write_3byte(uint8_t txdata);
void spix_set_speed(uint8_t speed);
void spi1_set_speed(uint8_t speed);
void spi2_set_speed(uint8_t speed);
void spi3_set_speed(uint8_t speed);	
void simulate_spi1_write_byte(uint8_t data);
uint8_t simulate_spi1_read_byte(void);		

void simulate_spi2_write_byte(uint8_t data);
uint8_t simulate_spi2_read_byte(void);	

void simulate_spi3_write_byte(uint8_t data);
uint8_t simulate_spi3_read_byte(void);	

void MX_SPI1_Init(void);
void MX_SPI2_Init(void);
void MX_SPI3_Init(void);

#endif





