#ifndef INC_JH1400F_DRIVER_H_
#define INC_JH1400F_DRIVER_H_

#include "stm32g0xx_hal.h"

/* JHM1400F defaults to 7-bit address 0x38 when SDO is low.  The software
   I2C driver uses the on-wire 8-bit address, hence 0x70 here. */
#ifndef JH1400F_I2C_ADDRESS
#define JH1400F_I2C_ADDRESS 0x70u
#endif

/* Result slots are 16-bit Modbus registers.  The two OTP banks each have a
   status byte plus 32 16-bit values; keep the M1 status outside the normal
   bank instead of overlapping its last two values. */
#define JH1400F_RESULT_LENGTH 98u
#define JH1400F_STATUS_OK 0u
#define JH1400F_STATUS_I2C 1u
#define JH1400F_STATUS_TIMEOUT 2u
#define JH1400F_CURRENT_OFFSET 1u
#define JH1400F_WORKING_CURRENT_OFFSET 5u
#define JH1400F_PRODUCT_ID_OFFSET 9u
#define JH1400F_BRIDGE_OFFSET 12u
#define JH1400F_TEMPERATURE_OFFSET 16u
#define JH1400F_BRIDGE_TEMPERATURE_OFFSET 20u
#define JH1400F_OTP_STATUS_OFFSET 30u
#define JH1400F_OTP_OFFSET 32u
#define JH1400F_M1_STATUS_OFFSET 64u
#define JH1400F_M1_OTP_OFFSET 66u
#define JH1400F_OTP_COUNT 32u

void AllTestForJH1400F(uint16_t *out, uint16_t out_lens);

#endif /* INC_JH1400F_DRIVER_H_ */
