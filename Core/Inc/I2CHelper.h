#ifndef INC_I2CHELPER_H_
#define INC_I2CHELPER_H_

#include "SoftIIC.h"
#include "SoftIIC_PP.h"

uint8_t WF2200_GetData(uint8_t *read_data, uint8_t read_len);
uint8_t PP_WF2200_GetData(uint8_t *read_data, uint8_t read_len);
/* Latest command format; product result layout is data[0..N-1], status[N]. */
uint8_t ProductI2CTransfer(const uint16_t *cmd, uint8_t cmd_len,
                          uint16_t *out, uint16_t out_len);

#endif
