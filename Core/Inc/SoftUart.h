/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    SOFTUART.h
  * @brief   This file contains all the function prototypes for
  *          the SOFTUART.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SOFTUART_H__
#define __SOFTUART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

typedef struct structUARTINFO 
{
	GPIO_TypeDef *Tx_Port;
	GPIO_TypeDef *Rx_Port;
	uint16_t Tx_Pin;
	uint16_t Rx_Pin;
	uint16_t BuadRate;
	float Save_BuadRate;
} UartInfo;

extern UartInfo uart1; 

void UART_Init(UartInfo *uart,uint32_t buadrate,GPIO_TypeDef *Uart_Tx_Port,uint16_t Uart_Tx_Pin,GPIO_TypeDef *Uart_Rx_Port,uint16_t Uart_Rx_Pin);
void UART_DeInit(UartInfo *uart);
void Soft_Uart_Send(UartInfo *uart, uint8_t *buf, uint16_t len,uint32_t timout);
void Soft_Uart_Recv(UartInfo *uart, uint8_t *buf, uint16_t len,uint32_t timout);
/* USER CODE END Private defines */

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__SOFTUART_H__ */

