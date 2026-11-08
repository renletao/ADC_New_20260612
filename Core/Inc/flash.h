#ifndef __HAL_FLASH_H
#define __HAL_FLASH_H
 
#include "main.h"
 
 
#define __IO    volatile 
typedef __IO uint16_t vu16;
 
#define STM32_FLASH_SIZE 		64 	 	//所选STM32的FLASH容量大小(单位为K)
#define STM_SECTOR_SIZE	    2048    //2K字节
 
#define STM32_FLASH_BASE    0x08000000 		//STM32 FLASH的起始地址
 

#define FLASH_USER_START_ADDR 0x0800F800           /* 第31页 Start @ of user Flash area */
#define FLASH_USER_END_ADDR   0x0800FFFF         /* 第31页 End @ of user Flash area */
 
#define STM32FLASH_SIZE         0x00010000UL
/* FLASH起始地址 */
#define STM32FLASH_BASE         FLASH_BASE
/* FLASH结束地址 */
#define STM32FLASH_END          (STM32FLASH_BASE | STM32FLASH_SIZE)
/* FLASH页大小：1K */
#define STM32FLASH_PAGE_SIZE    FLASH_PAGE_SIZE
/* FLASH总页数 */
#define STM32FLASH_PAGE_NUM     (STM32FLASH_SIZE / STM32FLASH_PAGE_SIZE)

void Erase_Flash(uint32_t s_addr,uint32_t end_addr) ;
void Write_Flash(uint32_t start_address,uint64_t  *buf, uint16_t length);
uint32_t ST_Flash_Read(uint32_t R_Addr,uint32_t length,void *pdata);

// void KaiJ_Read_FLASH();
 
// extern uint64_t set_Flashdat;
 
#endif
