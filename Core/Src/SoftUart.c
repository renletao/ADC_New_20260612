/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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

/* Includes ------------------------------------------------------------------*/
#include "SoftUart.h"

/* USER CODE BEGIN 0 */
#include "tim.h"
/* USER CODE END 0 */


/* USER CODE BEGIN 1 */
UartInfo uart1; 
/* USER CODE END 1 */

/**
 *IO初始化
 */
void UART_MspInit(GPIO_TypeDef *Uart_Tx_Port, uint16_t Uart_Tx_Pin, GPIO_TypeDef *Uart_Rx_Port, uint16_t Uart_Rx_Pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	HAL_GPIO_WritePin(Uart_Tx_Port, Uart_Tx_Pin, GPIO_PIN_SET);
	  
	GPIO_InitStruct.Pin = Uart_Tx_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Uart_Tx_Port, &GPIO_InitStruct);
	
	GPIO_InitStruct.Pin = Uart_Rx_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(Uart_Rx_Port, &GPIO_InitStruct);
}
/**
 *IO取消初始化
 */
void UART_DeMspInit(GPIO_TypeDef *Uart_Tx_Port, uint16_t Uart_Tx_Pin, GPIO_TypeDef *Uart_Rx_Port, uint16_t Uart_Rx_Pin)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	HAL_GPIO_WritePin(Uart_Tx_Port, Uart_Tx_Pin, GPIO_PIN_SET);
	  
	GPIO_InitStruct.Pin = Uart_Tx_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Uart_Tx_Port, &GPIO_InitStruct);
	
	GPIO_InitStruct.Pin = Uart_Rx_Pin;
  HAL_GPIO_Init(Uart_Rx_Port, &GPIO_InitStruct);
}
/**
 *uart初始化
 */
void UART_Init(UartInfo *uart,uint32_t buadrate,GPIO_TypeDef *Uart_Tx_Port,uint16_t Uart_Tx_Pin,GPIO_TypeDef *Uart_Rx_Port,uint16_t Uart_Rx_Pin)
{
	uart->Tx_Port = Uart_Tx_Port;
	uart->Rx_Port = Uart_Rx_Port;
	uart->Tx_Pin = Uart_Tx_Pin;
	uart->Rx_Pin = Uart_Rx_Pin;
	uart->BuadRate = 1000000/buadrate;
	uart->Save_BuadRate = 0;
	UART_MspInit(uart->Tx_Port,uart->Tx_Pin,uart->Rx_Port,uart->Rx_Pin);
}

/**
 *uart取消初始化
 */
void UART_DeInit(UartInfo *uart)
{
	UART_DeMspInit(uart->Tx_Port,uart->Tx_Pin,uart->Rx_Port,uart->Rx_Pin);
	uart->Tx_Port = NULL;
	uart->Rx_Port = NULL;
	uart->Tx_Pin = 0;
	uart->Rx_Pin = 0;
	uart->BuadRate = 0;
}

/**
 *uart发送1byte数据
 */
void Uart_Send_Bit(UartInfo *uart,uint8_t Data,uint32_t timout)
{
	uart->Tx_Port->BRR = (uint32_t)uart->Tx_Pin;
	delay_us(uart->BuadRate);
	for(uint8_t  i = 0; i < 8; i++)
	{
		if(Data&0x01) uart->Tx_Port->BSRR = (uint32_t)uart->Tx_Pin;
		else uart->Tx_Port->BRR = (uint32_t)uart->Tx_Pin;
		delay_us(uart->BuadRate);
		Data = Data>>1;
	}
	uart->Tx_Port->BSRR = (uint32_t)uart->Tx_Pin;
	delay_us(uart->BuadRate);
}

/**
 *等待需要电平
 */
uint16_t Wiat_For_Level(UartInfo *uart,uint8_t level,uint32_t timout)
{	
	uint32_t Count = 0;
	__HAL_TIM_SetCounter(&htim2,0);	
	while(HAL_GPIO_ReadPin(uart->Rx_Port,uart->Rx_Pin) != level)
	{
		Count = __HAL_TIM_GET_COUNTER(&htim2);
		if(Count >= timout)break;
	}
	return Count;
}

/**
 *读取多次电平
 */
uint16_t Read_Level(UartInfo *uart,uint32_t timout)
{
	uint32_t level = 0;
	uint32_t Count = 0;
	__HAL_TIM_SetCounter(&htim2,0);
	while(__HAL_TIM_GET_COUNTER(&htim2) <= timout)
	{
		Count++;
		if(HAL_GPIO_ReadPin(uart->Rx_Port,uart->Rx_Pin))level++; 
	}
	if(level >= Count/2)return 1;
	else return 0;
}

/**
 *uart读取1byte数据
 */
uint8_t Soft_Uart_Read_Bit(UartInfo *uart,uint32_t timout)
{
	uint8_t data = 0;
	uint8_t Count = 0;
	uint32_t Tim = 0;
	Wiat_For_Level(uart,GPIO_PIN_RESET,timout);
    if (HAL_GPIO_ReadPin(uart->Rx_Port, uart->Rx_Pin) != GPIO_PIN_RESET) return 0;		//等待低电平开始
	//判断波特率
	if(uart->Save_BuadRate == 0) 
	{
		Tim = Wiat_For_Level(uart,GPIO_PIN_SET,timout);
		if (HAL_GPIO_ReadPin(uart->Rx_Port, uart->Rx_Pin) != GPIO_PIN_SET) return 0;
        if (!uart->BuadRate || Tim < (uint32_t)uart->BuadRate / 2 ||
            Tim > 9 * (uint32_t)uart->BuadRate + (uint32_t)uart->BuadRate / 2) return 0;
        Count = Tim/uart->BuadRate + ((Tim % uart->BuadRate)>=uart->BuadRate/2);
        if (Count < 1 || Count > 9) return 0;
		data >>= Count - 1;
		uart->Save_BuadRate = (float)Tim/Count;
	}
	//第二次读取直接跳过判断波特率
	else
	{
		Count = 1;
		delay_us(uart->BuadRate);
	}
	//读数据
	for(uint8_t i = Count - 1; i < 8;i++)
	{
		data >>= 1;
		if(Read_Level(uart,uart->BuadRate) ==1)
		{
			data |= 0x80;
		}
	}
	Wiat_For_Level(uart,GPIO_PIN_SET,uart->BuadRate);		//等待从机释放
	return data;
}

/**
 *uart发送多字节
 */
void Soft_Uart_Send(UartInfo *uart, uint8_t *buf, uint16_t len,uint32_t timout)
{
	timout *= 1000; 
	for(uint16_t i= 0; i < len; i++)
	{
		Uart_Send_Bit(uart,buf[i],timout);
	}
}

/**
 *uart读取多字节
 */
void Soft_Uart_Recv(UartInfo *uart, uint8_t *buf, uint16_t len,uint32_t timout)
{
	timout *= 1000; 
	for(uint16_t i= 0; i < len; i++)
	{
		buf[i] = Soft_Uart_Read_Bit(uart,timout);
	}
}
/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
