/*
 * CmdHelper.h
 *
 *  Created on: Feb 17, 2022
 *      Author: ITry
 */

#ifndef INC_CMDHELPER_H_
#define INC_CMDHELPER_H_

#include "stm32g0xx_hal.h"
#include <stddef.h>

#define TYPE1_GET_VERSION 					0x104
#define TYPE1_GET_ADC	 							0x105
#define TYPE1_SET_VDD								0x201
#define TYPE1_SET_VPP							  0x202
#define TYPE1_SET_ADC								0x107
#define TYPE1_READ_ADC							0x108
#define TYPE1_CALIB									0x109
#define TYPE1_SAVE_ADC							0x200
#define TYPE1_GET_TASK_STATUS 0x400
#define TYPE1_GET_ALLTEST_DATA 0x401
#define TYPE1_WF3050_FT_DATA 0x567
#define TYPE1_WF280_M1 0x207
#define TYPE1_WF280_OUTDATA 0x208
#define TYPE1_WF280_COEFF 0x209
#define TYPE1_WF280_2V_OUTDATA 0x210
#define TYPE1_WF280_RAIL 0x211          /* 回读实际 DUT 轨压，out[0]=状态 out[1]=mV */
#define TYPE1_WF162A_TEST 9u
#define PRODUCT_WF162A TYPE1_WF162A_TEST
#define TYPE1_WF100S_TEST 12u
#define PRODUCT_WF100S TYPE1_WF100S_TEST
#define TYPE1_JH1400F_TEST 13u
#define PRODUCT_JH1400F TYPE1_JH1400F_TEST
/* cmd[0] = 14 触发 WF280 整机异步测试。1/2/3/5/8/9/10/11/12/13 已被占用，
   11 是 WF280 的 OTP 校验，所以 WF280 的产品号取 14。 */
#define TYPE1_WF280_TEST 14
#define PRODUCT_WF280 TYPE1_WF280_TEST

/* WF183/WF2200/WF3050 use 3.3 V; WF100S analog supply uses 5 V. */
#ifndef PRODUCT_TEST_VDD_MV
#define PRODUCT_TEST_VDD_MV 3300u
#endif
#ifndef WF100S_TEST_VDD_MV
#define WF100S_TEST_VDD_MV 5000u
#endif
#define WF183DE 0u
#define WF2200 1u
#define WF100S 2u
#define WF162A 3u
#define JH1400F 4u
#define WF280 5
#define PRODUCT_RESULT_LENGTH 120u
#define CMD_ERROR_BUSY 0xF1u
#define CMD_ERROR_LENGTH 0xF2u

extern volatile uint8_t IsTaskDone; /* 1: queued/running, 0: idle/completed */
extern volatile uint8_t IsTask_mub;
extern uint16_t sendBufBy[PRODUCT_RESULT_LENGTH];
void RunPendingProductTest(void);

void Parse(const uint16_t* cmd, uint8_t cmd_lens, uint16_t* out, uint16_t out_lens);
void ParseSystemCmd(const uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens);
void CopyAndClear(uint16_t* src, uint16_t* dest, size_t len);
void ParseI2CCmd(const uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens);
void ParseSerialCmd(const uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens);
void ParseOwiCmd(const uint16_t *cmd, uint8_t cmd_lens, uint16_t *out, uint16_t out_lens);
#endif /* INC_CMDHELPER_H_ */
