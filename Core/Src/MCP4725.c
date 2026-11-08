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
#include "MCP4725.h"

/* USER CODE BEGIN 0 */
#include "SoftIIC.h"
#include "cmsis_os.h"
/* USER CODE END 0 */

/* USER CODE BEGIN 1 */
I2CInfo dac_i2c;
/* USER CODE END 1 */

void MCP4725_Init(void)
{
	IIC_Init(&dac_i2c,DAC_SDA_GPIO_Port, DAC_SDA_Pin, DAC_SCL_GPIO_Port, DAC_SCL_Pin);
	osDelay(10);
	MCP4725_WriteData_Voltage(0); /* Default supply for 3.3 V products. */
}

void MCP4725_WriteData_Voltage(uint16_t Vout)   //电压单位mV
{
  uint8_t data[3];
	uint8_t temp;
	uint16_t Dn;
	Dn = ( 4096 * Vout/2) / VREF_3V3; //这里的VREF_3V3宏定义为3300
	temp = (0x0F00 & Dn) >> 8;  //12位数据。0000XXXX XXXXXXXX
	data[0] = 0xC0;
	data[1] = temp;  //12位数据。0000XXXX XXXXXXXX 
	data[2] = Dn; //这里的VREF_5V宏定义为5000
	
	I2C_WriteNByte(&dac_i2c, data,3);

	osDelay(10);	
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
