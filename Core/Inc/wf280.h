
#ifndef __WF280_H__
#define __WF280_H__

#include "stm32g0xx_hal.h"
#include "CmdHelper.h"

#define WF280_RESULT_LENGTH 2u
#define WF280_OUTPUT_RESULT_LENGTH 7u
#define WF280_M1_RESULT_LENGTH 5u
#define WF280_COEFF_MAX 32u

/* The WF280 adapter keeps the command/status convention used by the
   WF280_OS_BURN reference program.  A zero status is success; non-zero
   values identify a transport or protection failure. */
#define WF280_STATUS_OK 0u
#define WF280_STATUS_I2C 1u
#define WF280_STATUS_NOR_TIMEOUT 2u
#define WF280_STATUS_CMD_TIMEOUT 3u
#define WF280_STATUS_SHORT 0xE1u

void WF280CheckOtpReg(uint16_t *out, uint16_t out_lens);
uint8_t WF280TestOutput(uint16_t supply_mv, uint16_t measurement,
                        uint16_t settle_ms, uint16_t *out,
                        uint16_t out_lens);
uint8_t WF280ReadCoefficients(uint8_t start, uint8_t count,
                              uint16_t *out, uint16_t out_lens);
uint8_t WF280ReadM1(uint8_t count, uint16_t *out, uint16_t out_lens);

#endif //__WF280_H__
