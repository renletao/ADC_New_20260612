
#ifndef __WF280_H__
#define __WF280_H__

#include "stm32g0xx_hal.h"
#include "CmdHelper.h"

#define WF280_RESULT_LENGTH 2u
#define WF280_OUTPUT_RESULT_LENGTH 7u
#define WF280_M1_RESULT_LENGTH 5u
#define WF280_COEFF_MAX 32u

/* AllTestFor280 (cmd = 14) 的结果帧长度，占用 30001 起的 96 个输入寄存器。 */
#define WF280_TEST_RESULT_LENGTH 96

/* 结果帧偏移。0..23 与旧版完全一致，上位机老代码不用改；
   24 起是扩展区：电桥(压力)/温度单独测量 + 两个 bank 的全量系数。 */
#define WF280_R_STATUS       0   /* 0 = OK，其余见 WF280_STATUS_*        */
#define WF280_R_RAIL_MV      1   /* STM32 ADC 回读的实测轨压 (mV)        */
#define WF280_R_RAIL_SET     2   /* 固定 0：轨压由上位机 0x201 设定，固件不回报 */
#define WF280_R_RAIL_FLAG    3   /* 见 WF280_RAIL_FLAG_*                 */
#define WF280_R_WORK_UA      4   /* 工作电流 (uA，按实测轨压换算)        */
#define WF280_R_SLEEP_UA     5   /* 休眠电流 (uA，按实测轨压换算)        */
#define WF280_R_WORK_CODE    6   /* 工作电流原始码，4 字节小端           */
#define WF280_R_SLEEP_CODE  10   /* 休眠电流原始码，4 字节小端           */
#define WF280_R_BT_DATA     14   /* 0xAC 测量返回的 6 个原始字节         */
#define WF280_R_COEFF       20   /* 正常 bank 前 2 路（兼容旧帧）        */
#define WF280_R_M1_COEFF    22   /* M1 bank 前 2 路（兼容旧帧）          */
#define WF280_R_BRIDGE      24   /* 0xAD 电桥(压力) 24 位原始码，4 字节小端 */
#define WF280_R_TEMP        28   /* 0xAE 温度 24 位原始码，4 字节小端       */
#define WF280_R_COEFF_ALL   32   /* 正常 bank 全 32 路，16 位/寄存器     */
#define WF280_R_M1_COEFF_ALL 64  /* M1 bank 全 32 路，16 位/寄存器       */

/* WF280_R_RAIL_FLAG 位定义 */
#define WF280_RAIL_FLAG_MEASURED 0x01  /* ADC 读到了有效值       */
#define WF280_RAIL_FLAG_DEVIATED 0x02  /* 与设定值偏差超过 10 %  */
#define WF280_RAIL_FLAG_UNDER    0x04  /* 低于 1500 mV，轨没起来 */

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

/* cmd = 14：WF280 整机综合测试（照 WF183 的流程）。轨压由上位机
   {1, 0x201} 设置，固件只回读实际值并按它换算电流。
   上电 → 回读轨压 → 休眠电流 → 短路判定 → 0xBA 复位 → 0xAC 测量
   → 测量中抓工作电流 → 0xAD 压力 / 0xAE 温度 → 两个 bank 各 32 路系数
   → 恢复 ADC 配置 → 断电。 */
void AllTestFor280(uint16_t *out, uint16_t out_lens);

#endif //__WF280_H__
