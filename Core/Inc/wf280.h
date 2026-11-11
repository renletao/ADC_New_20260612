
#ifndef __WF280_H__
#define __WF280_H__

#include "stm32g0xx_hal.h"
#include "CmdHelper.h"

#define WF280_RESULT_LENGTH 2u
#define WF280_OUTPUT_RESULT_LENGTH 7u
#define WF280_M1_RESULT_LENGTH 5u
#define WF280_COEFF_MAX 32u

/* AllTestFor280 (cmd = 14) 的结果帧长度，占用 30000 起的 10 个输入寄存器。*/
#define WF280_TEST_RESULT_LENGTH 12

#define WF280_R_RAIL_MV    0
#define WF280_R_WORK_RAW   1   /* 工作电流原始码，低 16 位在前 */
#define WF280_R_SLEEP_RAW  3   /* 休眠电流原始码，低 16 位在前 */
#define WF280_R_RAW        5   /* 0xAC 读出的 6 字节原始数据，1 字节 1 寄存器 */
#define WF280_R_RAW_LEN    6
#define WF280_R_MEAS_ERR  11   /* I2C测量错误码（0=OK） */


/* The WF280 adapter keeps the command/status convention used by the
   WF280_OS_BURN reference program.  A zero status is success; non-zero
   values identify a transport or protection failure. */
#define WF280_STATUS_OK 0u
#define WF280_STATUS_I2C 1u
#define WF280_STATUS_NOR_TIMEOUT 2u
#define WF280_STATUS_CMD_TIMEOUT 3u
#define WF280_STATUS_SHORT 0xE1u
#define WF280_STATUS_RAIL 0xE2

#define WF280_SUPPLY_1V8_MV 1800
#define WF280_SUPPLY_3V3_MV 3300
/* 1.8 V 下满量程只有 15 mA，短路阈值必须低于它 */
#define WF280_SHORT_1V8_UA 10000u


void WF280CheckOtpReg(uint16_t *out, uint16_t out_lens);

uint8_t WF280TestOutput(uint16_t supply_mv, uint16_t measurement,
                        uint16_t settle_ms, uint16_t *out,
                        uint16_t out_lens);
uint8_t WF280ReadCoefficients(uint8_t start, uint8_t count,
                              uint16_t *out, uint16_t out_lens);
uint8_t WF280ReadM1(uint8_t count, uint16_t *out, uint16_t out_lens);

/* 读取当前 DUT 轨压。PB1 / ADC1_IN9 经 R8(1K)/R9(1k) 分压接在 VDD 上，
   所以读到的是“实际输出”而不是 DAC 设定值。返回 mV，0 表示读取失败。
   该函数只做测量，不改变供电状态，任何时候都可以调用。 */
uint16_t WF280ReadRailMv(void);

/* cmd = 14：WF280 整机综合测试。轨压由上位机 {1, 0x201} 设置，
   固件只回读实际值并按它换算电流。
   上电 → 回读轨压 → 休眠电流 + 短路判定 → 0xBA 复位 → 0xAC 测量
   → 测量中抓工作电流 → 一次读 6 字节原始数据（Status+电桥+温度，转存）
   + 1 字节测量错误码 → 恢复 ADC 配置 → 断电。 */
void AllTestFor280(uint16_t *out, uint16_t out_lens);

#endif //__WF280_H__
