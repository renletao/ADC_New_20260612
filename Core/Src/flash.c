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
#include "flash.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */
 
/**
 * @brief       获取 地址Addr 在 Flash中式第几页。根据每页大小2k 
 * @param       Addr    开头地址
 * @retval      FLASH页数
 */
static uint32_t GetPage(uint32_t Addr)
{
    uint32_t page = 0;
    page = (Addr-FLASH_BASE) / FLASH_PAGE_SIZE;
    return page;
}
 
/**
 * @brief       擦除FLASH
 * @param       s_addr    开头地址
 * @param       end_addr    结束地址
 */
 void Erase_Flash(uint32_t s_addr,uint32_t length)
{
		uint32_t FirstPage;
		uint32_t NbOfPages;
    uint32_t SectorError=0;

    FLASH_EraseInitTypeDef E_FLASH;
      /* Get the 1st page to erase */
    FirstPage = GetPage(s_addr);
  /* Get the number of pages to erase from 1st page */
    NbOfPages = GetPage(s_addr+length) - FirstPage + 1;
    E_FLASH.TypeErase = FLASH_TYPEERASE_PAGES;
    E_FLASH.Page = FirstPage;
    E_FLASH.NbPages =NbOfPages;
    if(HAL_FLASHEx_Erase(&E_FLASH,&SectorError) != HAL_OK) {
    }
}
 
 
/**
 * @brief       写Flash,写之前需要擦除整页flash，修改宏定义
 * @param       start_address    开头地址
 *  @param      end_address   结束地址
 * @param       *buf       写入数据
 * @param       length     写入字节数据个数
 */
void Write_Flash(uint32_t start_address,uint64_t  *buf, uint16_t length)
{
	uint64_t data = 0;
	uint16_t i;
	if(start_address < STM32_FLASH_BASE || (start_address >= (STM32_FLASH_BASE + STM_SECTOR_SIZE * STM32_FLASH_SIZE)))    
	{
			 //长度不在范围
		 return;
	}
	
	HAL_FLASH_Unlock();//解锁flash	 
	Erase_Flash(start_address , length);   //擦除Flash
 
	for(i=0;i<length;i++)
	{
		data = 0;
		data=buf[i];
		if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,start_address,data) != HAL_OK) 
		{
				HAL_FLASH_Lock();
				return;
		}            
		start_address += 8;
	}  
 
    HAL_FLASH_Lock();//上锁
 
}

/**
 * @brief       读Flash
 * @param       start_address    开头地址
 * @param       length     读字节个数
 * @param       *buf       读数据
*/
uint32_t ST_Flash_Read(uint32_t R_Addr,uint32_t length,void *pdata)
{
    uint32_t nread = length;
    uint8_t *pBuffer = (uint8_t *)pdata;
    const uint8_t *pAddr = (const uint8_t *)R_Addr;

    if(!pdata || R_Addr < STM32FLASH_BASE || R_Addr > STM32FLASH_END)
    {
        return 0;
    }

    while(nread >= sizeof(uint32_t) && (((uint32_t)pAddr) <= (STM32FLASH_END - 4)))
    {
        *(uint32_t *)pBuffer = *(uint32_t *)pAddr;
        pBuffer += sizeof(uint32_t);
        pAddr += sizeof(uint32_t);
        nread -= sizeof(uint32_t);
    }

    while(nread && (((uint32_t)pAddr) < STM32FLASH_END))
    {
        *pBuffer++ = *pAddr++;
        nread--;
    }
		return length - nread;
}
/* USER CODE BEGIN 2 */

/* USER CODE END 2 */





