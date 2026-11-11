/*
 * SoftIIC.h
 *
 *  Created on: 2020年8月4日
 *      Author: ITry
 */

#ifndef INC_SOFTIIC_H_
#define INC_SOFTIIC_H_
#include "main.h"

#define IIC_WRITE 	0
#define IIC_READ 	1

/* Single-master, external pull-ups; failure status remains 1. */
#ifndef IIC_SCL_TIMEOUT_US
#define IIC_SCL_TIMEOUT_US 1000
#endif

typedef struct structI2CINFO 
{
	GPIO_TypeDef *portSda;
	GPIO_TypeDef *portScl;
	uint16_t pinSda;
	uint16_t pinScl;
	uint8_t error; /* Latched SCL timeout / stuck bus, cleared by START. */
} I2CInfo;

extern I2CInfo i2c1;
uint8_t I2C_ReadNByte(I2CInfo *info,uint8_t addr,uint8_t *readData,uint32_t lenth);
uint8_t I2C_WriteNByte(I2CInfo *info, const uint8_t *reg_data,uint32_t lenth);

void IIC_Init(I2CInfo *info,GPIO_TypeDef *SDA_PORT, uint16_t SDA_PIN, GPIO_TypeDef *SCL_PORT, uint16_t SCL_PIN);
void IIC_DeInit(I2CInfo *info);
void iic_start(I2CInfo *info);
void iic_stop(I2CInfo *info);
void iic_ack(I2CInfo *info);
void iic_nack(I2CInfo *info);
void iic_sendbyte(I2CInfo *info,uint8_t byte);
uint8_t iic_wait_ack(I2CInfo *info);
uint8_t iic_readbyte(I2CInfo *info,uint8_t ack);
void I2C_IDLE(I2CInfo *info);
#endif /* INC_SOFTIIC_H_ */
