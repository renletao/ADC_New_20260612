/**
  ******************************************************************************
  * @file    py32f002bxx_Start_Kit.c
  * @author  MCU Application Team
  * @brief   This file provides set of firmware functions to manage Leds, 
  *          push-button available on Start Kit.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "mb.h"
#include <string.h>
#include "mbutils.h"
#include "CmdHelper.h"

#define REG_INPUT_START 30001
#define SENDBUF_LENS 120
#define REG_HOLDING_START 40001
#define RECVBUF_LENS 64
uint16_t recvBuf[RECVBUF_LENS];
uint16_t sendBuf[SENDBUF_LENS]; 

/**
  * @}
  */
eMBErrorCode eMBRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs) {
	eMBErrorCode eStatus = MB_ENOERR;
	int i = 0;
	if ((usAddress/* >=*/== REG_INPUT_START) && (usAddress + usNRegs <= REG_INPUT_START + SENDBUF_LENS)) {
		i = (int) (usAddress - REG_INPUT_START);
		while (usNRegs > 0) {
			*pucRegBuffer++ = (uint8_t) (sendBuf[i] >> 8);
			*pucRegBuffer++ = (uint8_t) (sendBuf[i] & 0xFF);
			i++;
			usNRegs--;
		}
	}
	else {
		eStatus = MB_ENOREG;
	}
	return eStatus;
}

/**
  * @}
  */
eMBErrorCode eMBRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs, eMBRegisterMode eMode) {
	eMBErrorCode eStatus = MB_ENOERR;
	int i = 0;
	if ((usAddress /*>=*/== REG_HOLDING_START) && (usAddress + usNRegs <= REG_HOLDING_START + RECVBUF_LENS)) {
		i = (int) (usAddress - REG_HOLDING_START);
		if (eMode == MB_REG_READ) {
			while (usNRegs > 0) {
				*pucRegBuffer++ = (UCHAR) (recvBuf[i] >> 8);
				*pucRegBuffer++ = (UCHAR) (recvBuf[i] & 0xFF);
				i++;
				usNRegs--;
			}
		} else if (eMode == MB_REG_WRITE) {
			while (usNRegs > 0) {
				recvBuf[i] = *pucRegBuffer++ << 8;
				recvBuf[i] |= *pucRegBuffer++;
				i++;
				usNRegs--;
			}
			memset(sendBuf, 0, sizeof(sendBuf));
			Parse(recvBuf, i, sendBuf, SENDBUF_LENS);
		}
	} else {
		eStatus = MB_ENOREG;
	}
	return eStatus;
}

eMBErrorCode eMBRegCoilsCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNCoils, eMBRegisterMode eMode) {
	return MB_ENOERR;
}

eMBErrorCode eMBRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNDiscrete) {
	return MB_ENOERR;
}

/**
  * @}
  */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
