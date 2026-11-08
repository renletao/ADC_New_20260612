#include "uart.h"
#include "tim.h"
#include "stdio.h"
#include <stdarg.h>

#define BuadRate_9600	104

UART_Config uart1;

void MX_UART_Init(void) 
{
	uart1.BaudRate = BuadRate_9600;
	uart1.RxPin = UART_RX_Pin;
	uart1.RxPort = UART_RX_GPIO_Port;
	uart1.TxPin = UART_TX_Pin;
	uart1.TxPort = UART_TX_GPIO_Port;
	UART_MspInit(&uart1);
	HAL_TIM_Base_Start(&htim2);//打开定时器
}

//====== 初始化函数 ======//
uint8_t UART_MspInit(UART_Config* uart) 
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
	// TX引脚配置
	GPIO_InitStruct.Pin = uart->TxPin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(uart->TxPort, &GPIO_InitStruct);
	// RX引脚配置
	GPIO_InitStruct.Pin = uart->RxPin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(uart->RxPort, &GPIO_InitStruct);
  // 初始状态置为高电平
  HAL_GPIO_WritePin(uart->TxPort, uart->TxPin, GPIO_PIN_SET);
	// 启动接收
	return 1; // 初始化成功
}

//====== 接收开始 ======//
void UART_Receive_bit(UART_Config* uart)
{
	uart->rx_flag = 0;  // 重置接收标志
	__HAL_GPIO_EXTI_CLEAR_FALLING_IT(uart->RxPin); // 确保中断清除
	HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

//====== 单字节发送函数 ======//
void UART_Transmit_bit(UART_Config* uart)
{
	__disable_irq(); // 禁用中断保证时序
	// 发送起始位（低电平）
	HAL_GPIO_WritePin(uart->TxPort, uart->TxPin, GPIO_PIN_RESET);
	delay_us(uart->BaudRate);
	// 发送8位数据（LSB优先）
	for(uint8_t i=0; i<8; i++) 
	{
		if(uart->Tx_Buff & 0x01) 
		{
			HAL_GPIO_WritePin(uart->TxPort, uart->TxPin, GPIO_PIN_SET);
		} 
		else 
		{
			HAL_GPIO_WritePin(uart->TxPort, uart->TxPin, GPIO_PIN_RESET);
		}
		uart->Tx_Buff >>= 1;
		delay_us(uart->BaudRate);
	}
	// 发送停止位（高电平）
	HAL_GPIO_WritePin(uart->TxPort, uart->TxPin, GPIO_PIN_SET);
	delay_us(uart->BaudRate);
	__enable_irq(); // 重新启用中断
}

//====== 多字节发送函数 ======//
void UART_Transmit(UART_Config* uart,uint8_t *buf, uint8_t len)
{
	for(uint8_t i = 0; i < len; i++)
	{
		uart->Tx_Buff = buf[i];
		UART_Transmit_bit(uart);
	}
}

//====== 外部中断处理（RX引脚） ======//
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)  // 根据实际引脚确定中断号
{		
	HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
	if(GPIO_Pin == uart1.RxPin)
	{
		// 检测起始位
		if(HAL_GPIO_ReadPin(uart1.RxPort, uart1.RxPin) == GPIO_PIN_RESET)
		{
			uint8_t data = 0;
			delay_us(uart1.BaudRate	+ uart1.BaudRate/2); // 跳到位中间采样
			// 采样8位数据
			for(int i=0; i<8; i++) 
			{
				if(HAL_GPIO_ReadPin(uart1.RxPort, uart1.RxPin)) 
				{
					data |= (1 << i);
				}
				delay_us(uart1.BaudRate);
			}
			// 跳过停止位
			delay_us(uart1.BaudRate);
			uart1.Rx_Buff = data;
			uart1.rx_flag = 1;  // 设置接收标志
		}
	}
}
