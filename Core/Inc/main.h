/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OWI_Pin GPIO_PIN_0
#define OWI_GPIO_Port GPIOA
#define INT_Pin GPIO_PIN_0
#define INT_GPIO_Port GPIOB
#define VDD_FB_Pin GPIO_PIN_1
#define VDD_FB_GPIO_Port GPIOB
#define LED_Pin GPIO_PIN_8
#define LED_GPIO_Port GPIOA
#define DAC_SCL_Pin GPIO_PIN_11
#define DAC_SCL_GPIO_Port GPIOA
#define DAC_SDA_Pin GPIO_PIN_12
#define DAC_SDA_GPIO_Port GPIOA
#define VPP_EN_Pin GPIO_PIN_3
#define VPP_EN_GPIO_Port GPIOB
#define I2C_SDA_Pin GPIO_PIN_4
#define I2C_SDA_GPIO_Port GPIOB
#define I2C_SCL_Pin GPIO_PIN_5
#define I2C_SCL_GPIO_Port GPIOB
#define UART_TX_Pin GPIO_PIN_6
#define UART_TX_GPIO_Port GPIOB
#define UART_RX_Pin GPIO_PIN_7
#define UART_RX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define RESET_TIM_COUNTER() __HAL_TIM_SET_COUNTER(&htim1,0)
#define GET_TIM_COUNTER 		__HAL_TIM_GET_COUNTER(&htim1)
 
#define ADDR1_READ					(uint8_t)(HAL_GPIO_ReadPin(ADDR_1_GPIO_Port, ADDR_1_Pin))
#define ADDR2_READ					(uint8_t)(HAL_GPIO_ReadPin(ADDR_2_GPIO_Port, ADDR_2_Pin))<<1
#define ADDR3_READ					(uint8_t)(HAL_GPIO_ReadPin(ADDR_3_GPIO_Port, ADDR_3_Pin))<<2
#define ADDR4_READ					(uint8_t)(HAL_GPIO_ReadPin(ADDR_4_GPIO_Port, ADDR_4_Pin))<<3
#define ADDR5_READ					(uint8_t)(HAL_GPIO_ReadPin(ADDR_5_GPIO_Port, ADDR_5_Pin))<<4
void delay_us(uint32_t us);
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
