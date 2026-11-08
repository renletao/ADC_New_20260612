/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    OWI.h
  * @brief   This file contains all the function prototypes for
  *          the OWI.c file
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
#ifndef __OWI_H__
#define __OWI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */
#define OWI_OUT_H() 					HAL_GPIO_WritePin(OWI_GPIO_Port, OWI_Pin, GPIO_PIN_SET)
#define OWI_OUT_L() 					HAL_GPIO_WritePin(OWI_GPIO_Port, OWI_Pin, GPIO_PIN_RESET)
#define OWI_READ							HAL_GPIO_ReadPin(OWI_GPIO_Port, OWI_Pin)


typedef struct
{	
	 uint8_t bit;
	 uint8_t cheak_bit;
	 float data_ratio;
	 float cheak_ratio;
	 uint32_t cycle;
	 uint32_t star_tim_l;
	 uint32_t star_tim_h;
	 uint8_t data_tim_l;
	 uint8_t data_tim_h;
	 uint8_t cheak_tim_h;
	 uint8_t cheak_tim_l;
} OWI_TypeDef;

extern OWI_TypeDef OWI_Device;
/* USER CODE END Private defines */
uint8_t OWI_Read_Began(OWI_TypeDef *OWI);
uint8_t OWI(uint8_t* pdata,uint32_t tir_tim);
/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__OWI_H__ */

