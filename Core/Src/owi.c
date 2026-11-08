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
#include "owi.h"

/* USER CODE BEGIN 0 */
#include "tim.h"
/* USER CODE END 0 */


/* USER CODE BEGIN 1 */
OWI_TypeDef OWI_Device;
/* USER CODE END 1 */


uint8_t OWI_Star(uint32_t tir_tim)
{
  OWI_OUT_H();
  delay_us(50);
	OWI_OUT_L();
	delay_us(tir_tim);
	OWI_OUT_H();
	delay_us(50);
	RESET_TIM_COUNTER();
	while(OWI_READ)	//等待起始位
	{	
		if(GET_TIM_COUNTER >= 5000)return 0;//5ms超时退出
	}	
	return 1;
}

uint8_t OWI_Read_Began(OWI_TypeDef *owi)
{
	RESET_TIM_COUNTER();
	while(!OWI_READ)
	{
		if(GET_TIM_COUNTER >=	100)return 0;
	}
	owi->star_tim_l = GET_TIM_COUNTER;//起始位低电平时间
	RESET_TIM_COUNTER();
	while(OWI_READ)
	{
		if(GET_TIM_COUNTER >= 100)return 0;
	}
	owi->star_tim_h = GET_TIM_COUNTER;//起始位高电平时间
	owi->cycle = owi->star_tim_l+owi->star_tim_h;
	return 1;
}


uint8_t OWI_Read_Bit(OWI_TypeDef *owi)
{	
	owi->bit = 0;
	for(uint8_t i = 0;i < 8;i++)
	{
		RESET_TIM_COUNTER();	
		owi->data_tim_l = 0;
		owi->data_tim_h = 0;
		while(!OWI_READ)
		{
			if(GET_TIM_COUNTER >= 100)return 0xFF;
		}
		owi->data_tim_l = GET_TIM_COUNTER;//起始位低电平时间
		RESET_TIM_COUNTER();
		while(OWI_READ)
		{
			if(GET_TIM_COUNTER >= 100)return 0xFF;
		}
		owi->data_tim_h = GET_TIM_COUNTER;//起始位低电平时间
		owi->data_ratio = (float)(owi->data_tim_l)/(float)(owi->data_tim_h);
		if(owi->data_ratio >= 1.5) owi->bit = (owi->bit<<1)|0;
		else owi->bit = (owi->bit<<1)|1;
	}
	return owi->bit;
}



uint8_t OWI_Read_Cheak(OWI_TypeDef *owi)
{

	RESET_TIM_COUNTER();
	while(!OWI_READ)
	{
		if(GET_TIM_COUNTER >= 500)return 0;
	}
	owi->cheak_tim_l = GET_TIM_COUNTER;//起始位低电平时间
	RESET_TIM_COUNTER();
	
	owi->cheak_tim_h = (owi->cycle) - (owi->cheak_tim_l);//起始位低电平时间
	owi->cheak_ratio = (float)(owi->cheak_tim_l)/(float)(owi->cheak_tim_h);
	
	if(owi->cheak_ratio >= 1.5) owi->cheak_bit = 0;
	else owi->cheak_bit = 1;
	
	while(OWI_READ)
	{
		if(GET_TIM_COUNTER >= 500)return 0;
	}

	return 1;
}


uint8_t OWI(uint8_t* pdata,uint32_t tir_tim)
{
	if(!OWI_Star(tir_tim))return 0;
	
	if(!OWI_Read_Began(&OWI_Device))return 0;
	
	pdata[0] = OWI_Read_Bit(&OWI_Device);
	
	if(!OWI_Read_Cheak(&OWI_Device))return 0;
	
	if(!OWI_Read_Began(&OWI_Device))return 0;
	
	pdata[1] = OWI_Read_Bit(&OWI_Device);
	
	if(!OWI_Read_Cheak(&OWI_Device))return 0;
	
	if(!OWI_Read_Began(&OWI_Device))return 0;
	
	pdata[2] = OWI_Read_Bit(&OWI_Device);
	
	if(!OWI_Read_Cheak(&OWI_Device))return 0;
	
	return 1;
}
/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
