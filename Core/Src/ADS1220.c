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
#include "ADS1220.h"

/* USER CODE BEGIN 0 */
#include "spi.h"
#include "tim.h"
#include "cmsis_os.h"
/* USER CODE END 0 */

/* USER CODE BEGIN 1 */
ADS1220_TypeDef ADS1220;
/* USER CODE END 1 */
void ADS1220_Init(ADS1220_TypeDef *ads1220)
{
//0000：增益=1（默认设置）；0010：增益=2；0100：增益=4；0110：增益=8；1000：增益=16；1010：增益=32；1100：增益=64；1110：增益=128
/*0000：AINP = AIN0，AINN = AIN1（默认设置）；0001：AINP = AIN0，AINN = AIN2；0010：AINP = AIN0，AINN = AIN3
	0011：AINP = AIN1，AINN = AIN2；0100：AINP = AIN1，AINN = AIN3；0101：AINP = AIN2，AINN = AIN3
	0110：AINP = AIN1，AINN = AIN0；0111：AINP = AIN3，AINN = AIN2；1000：AINP = AIN0，AINN = AVSS
	1001：AINP = AIN1，AINN = AVSS；1010：AINP = AIN2，AINN = AVSS；1011：AINP = AIN3，AINN = AVSS
	1100：(V(REFPx)– V(REFNx)) / 4 监视（旁路 PGA）	；1101：(AVDD– AVSS) / 4 监视（旁路 PGA）；1110：AINP 和 AINN 短接至 (AVDD + AVSS) / 2  */
	ADS1220_Write(ADS1220_RESET);
	osDelay(100);
	ADS1220_WriteRegister(0x00, 1, &ads1220->config[0]);
	ADS1220_WriteRegister(0x01, 1, &ads1220->config[1]);
	ADS1220_WriteRegister(0x02, 1, &ads1220->config[2]);
	ADS1220_WriteRegister(0x03, 1, &ads1220->config[3]);
	ads1220->state = 1;
}

void ADS1220_Write(uint8_t dat)
{
	HAL_SPI_Transmit(&hspi1,&dat, 1, 1000);
}

uint8_t ADS1220ReceiveByte()
{  
	uint8_t Result = 0;

  HAL_SPI_Receive(&hspi1, &Result, 1, 1000);
  return Result;
}

void ADS1220_WriteRegister(uint32_t StartAddress, uint32_t NumRegs, uint8_t * pData)
{
	uint32_t i;
	/* send the command byte */
	ADS1220_Write( ADS1220_WREG | (((StartAddress << 2) & 0x0c) | ((NumRegs - 1) & 0x03)));

	/* send the data bytes */
	for(i = 0; i < NumRegs; i++)
	{
		ADS1220_Write(*pData++);
	}
}

static uint8_t ADS1220_WaitForReady(void)
{
	uint32_t elapsed = 0;

	/* ADS1220 DRDY is low when a new conversion result is ready. */
	while (HAL_GPIO_ReadPin(INT_GPIO_Port, INT_Pin) == GPIO_PIN_SET)
	{
		if (elapsed++ >= ADS1220_DRDY_TIMEOUT_US) return 0;
		delay_us(1);
	}
	return 1;
}

static uint8_t ADS1220_ReadDataFrame(uint32_t *value)
{
	uint8_t tx[4] = { ADS1220_RDATA, 0, 0, 0 };
	uint8_t rx[4] = { 0, 0, 0, 0 };

	if (HAL_SPI_TransmitReceive(&hspi1, tx, rx, 4, 1000) != HAL_OK) return 0;
	*value = ((uint32_t)rx[1] << 16) |
	         ((uint32_t)rx[2] << 8) |
	         (uint32_t)rx[3];
	if (*value & 0x800000u) *value |= 0xff000000u;
	return 1;
}

uint8_t ADS1220_ReadValue(ADS1220_TypeDef *ads1220, uint32_t *value)
{
	uint32_t result;

	if (ads1220 == 0 || value == 0) return 0;

	/* A low DRDY before START means an older result is still pending. Read
	   and discard it so START cannot be followed by an immediate stale read. */
	if (HAL_GPIO_ReadPin(INT_GPIO_Port, INT_Pin) == GPIO_PIN_RESET)
	{
		if (!ADS1220_ReadDataFrame(&result))
		{
			ads1220->Data = 0;
			return 0;
		}
	}

	ADS1220_Write(ADS1220_START);
	if (!ADS1220_WaitForReady())
	{
		ads1220->Data = 0;
		return 0;
	}

	if (!ADS1220_ReadDataFrame(&result))
	{
		ads1220->Data = 0;
		return 0;
	}
	ads1220->Data = result;
	*value = result;
	return 1;
}

uint32_t GET_ADS1220_Val(ADS1220_TypeDef *ads1220)
{
	uint32_t value = 0;

	if (!ADS1220_ReadValue(ads1220, &value)) return 0;
	return value;
}
/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
