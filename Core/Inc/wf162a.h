#ifndef INC_WF162A_H_
#define INC_WF162A_H_

#include "stm32g0xx_hal.h"
#include "CmdHelper.h"

/* One selected supply round occupies 0..15.  A 5.0 V round keeps a second
   copy in 16..31 for hosts that read 32 registers, so the diagnostic block
   that reports the reference voltage actually used starts at 32. */
#define WF162A_SUPPLY_RESULT_LENGTH 16u
#define WF162A_DIAG_AVDD_OFFSET 32u
#define WF162A_DIAG_STATUS_OFFSET 33u
#define WF162A_DIAG_CONFIG_OFFSET 34u
#define WF162A_RESULT_LENGTH 35u
#define WF162A_CURRENT_OFFSET 0u
#define WF162A_VOUT_PLUS_OFFSET 4u
#define WF162A_VOUT_MINUS_OFFSET 8u
#define WF162A_DIFF_OFFSET 12u

/* WF162A_DIAG_STATUS_OFFSET bits. */
#define WF162A_STATUS_REFERENCE_MEASURED 0x0001u /* AVDD read from the ADS1220 monitor */
#define WF162A_STATUS_SATURATED_CURRENT 0x0002u
#define WF162A_STATUS_SATURATED_VOUT_PLUS 0x0004u
#define WF162A_STATUS_SATURATED_VOUT_MINUS 0x0008u
#define WF162A_STATUS_SATURATED_DIFF 0x0010u
#define WF162A_STATUS_REFERENCE_FAILED 0x0020u
#define WF162A_STATUS_ADC_TIMEOUT 0x0040u

void WF162A_SetConfiguredSupply(uint16_t supply_mv);
void AllTestFor162A(uint16_t *out, uint16_t out_lens);

#endif /* INC_WF162A_H_ */
