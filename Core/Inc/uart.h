#ifndef __uart_H__
#define __uart_H__
#ifdef __cplusplus
extern "C" {
#endif
/* Includes ------------------------------------------------------------------*/
#include "main.h"

typedef struct 
{
  GPIO_TypeDef* TxPort;
	uint16_t TxPin;
	GPIO_TypeDef* RxPort;
	uint16_t RxPin;
	volatile uint32_t BaudRate;
	volatile uint8_t Tx_Buff;
	volatile uint8_t Rx_Buff;
	volatile uint8_t rx_flag;
} UART_Config;

extern UART_Config uart1;

void MX_UART_Init(void);
uint8_t UART_MspInit(UART_Config* uart);
void UART_Receive_bit(UART_Config* uart);
void UART_Transmit_bit(UART_Config* uart);
void UART_Transmit(UART_Config* uart,uint8_t *buf, uint8_t len);
void delay_us(uint16_t us);
#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

