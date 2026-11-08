#ifndef __WF2200_H__
#define __WF2200_H__

#include "stm32g0xx_hal.h"
#include "CmdHelper.h"

#define WF2200_RESULT_LENGTH 77u

void AllTestFor2200(uint16_t *out, uint16_t out_lens);
void WF3050FtData(uint16_t *out, uint16_t out_lens);

#endif //__WF2200_H__
