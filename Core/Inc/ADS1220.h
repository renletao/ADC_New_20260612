/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ads1220.h
  * @brief   This file contains all the function prototypes for
  *          the gpio.c file
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
#ifndef __ADS1220_H__
#define __ADS1220_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

#define ADS1220_RESET 0X06
#define ADS1220_START 0X08
#define ADS1220_POWERDOWN 0X02
#define ADS1220_RDATA 0X10
#define ADS1220_RREG 0X20
#define ADS1220_WREG 0X40 

/* DRDY is active low. The timeout covers the slowest configured
   conversion rate while still allowing a disconnected ADC to recover. */
#define ADS1220_DRDY_TIMEOUT_US 300000u

typedef struct
{
	uint8_t config[4];
	volatile uint8_t state;
	volatile uint32_t Data;
} ADS1220_TypeDef;

extern ADS1220_TypeDef ADS1220;

/* USER CODE END Private defines */

void ADS1220_Init(ADS1220_TypeDef *ads1220);
void ADS1220_Write(uint8_t dat);
uint8_t ADS1220ReceiveByte(void);
void ADS1220_WriteRegister(uint32_t StartAddress, uint32_t NumRegs, uint8_t * pData);
uint8_t ADS1220_ReadValue(ADS1220_TypeDef *ads1220, uint32_t *value);
uint32_t GET_ADS1220_Val(ADS1220_TypeDef *ads1220);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ ADS1220_H__ */

