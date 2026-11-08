#include "wf280.h"
#include "ADS1220.h"
#include "MCP4725.h"
#include "SoftIIC.h"
#include "cmsis_os.h"
#include <string.h>

/*
 * WF280 protocol adapter
 * ----------------------
 *
 * The transaction shape below intentionally follows WF280_OS_BURN/Soft/Src/
 * WF280_OS.c. A command write is one START followed by 0x70|W, the command
 * byte and each parameter byte, with an ACK clock after every byte. Reads are
 * a separate START with 0x70|R and a NACK on the last byte. The generic
 * product I2C protocol cannot express the command/status polling used by
 * WF280, so this adapter owns the complete transaction while retaining the
 * new-board SoftIIC electrical driver and its bus recovery.
 */

#define WF280_I2C_ADDRESS 0x70u
#define WF280_CMD_RESET 0xBAu
#define WF280_CMD_BT_MEAS 0xACu
#define WF280_RETRY_COUNT 3u
#define WF280_NORMAL_POLL_DELAY_MS 10u
#define WF280_CMD_POLL_DELAY_MS 1u
#define WF280_OUTPUT_3V3_SETTLE_MS 20u
#define WF280_OUTPUT_2V_SETTLE_MS 200u
#define WF280_M1_SETTLE_MS 20u
#define WF280_CURRENT_CHANNEL 0x01u
#define WF280_CURRENT_SHUNT_OHM 120u
#define WF280_ADC_FULL_SCALE 8388608u
#define WF280_SHORT_CURRENT_UA 20000u

uint16_t otp1[32];
uint16_t otp2[32];

static uint8_t wf280_write(I2CInfo *bus, uint8_t command,
                           const uint8_t *params, uint8_t param_count)
{
    uint8_t frame[2u + 32u];
    uint8_t i;

    if (param_count > 32u) return WF280_STATUS_I2C;
    frame[0] = WF280_I2C_ADDRESS | IIC_WRITE;
    frame[1] = command;
    for (i = 0; i < param_count; ++i) frame[2u + i] = params[i];
    return I2C_WriteNByte(bus, frame, 2u + param_count) ?
           WF280_STATUS_I2C : WF280_STATUS_OK;
}

static uint8_t wf280_read(I2CInfo *bus, uint8_t *data, uint8_t count)
{
    if (!count) return WF280_STATUS_OK;
    return I2C_ReadNByte(bus, WF280_I2C_ADDRESS, data, count) ?
           WF280_STATUS_I2C : WF280_STATUS_OK;
}

static uint8_t wf280_wait_idle(I2CInfo *bus, uint8_t command_mode,
                               uint8_t delay_before_start, uint8_t retries)
{
    uint8_t status = 0;
    uint8_t i;

    if (delay_before_start) osDelay(delay_before_start);
    for (i = 0; i < retries; ++i) {
        if (wf280_read(bus, &status, 1u) != WF280_STATUS_OK)
            return WF280_STATUS_I2C;
        if (!command_mode) {
            if ((status & 0x80u) == 0u && (status & 0x01u) != 0u)
                return WF280_STATUS_OK;
        } else if ((status & 0x80u) == 0u &&
                   (status & 0x41u) == 0x41u) {
            return WF280_STATUS_OK;
        }
        osDelay(command_mode ? WF280_CMD_POLL_DELAY_MS :
                WF280_NORMAL_POLL_DELAY_MS);
    }
    return command_mode ? WF280_STATUS_CMD_TIMEOUT : WF280_STATUS_NOR_TIMEOUT;
}

static uint8_t wf280_set_cmd(I2CInfo *bus, uint8_t command,
                             const uint8_t *params, uint8_t param_count,
                             uint8_t command_mode, uint8_t delay_before_start)
{
    uint8_t status = wf280_write(bus, command, params, param_count);
    if (status != WF280_STATUS_OK) return status;
    return wf280_wait_idle(bus, command_mode, delay_before_start,
                           WF280_RETRY_COUNT);
}

static uint8_t wf280_get_chip_data(I2CInfo *bus, uint8_t command,
                                   uint8_t *data, uint8_t count)
{
    uint8_t status = wf280_write(bus, command, NULL, 0u);
    if (status != WF280_STATUS_OK) return status;
    status = wf280_wait_idle(bus, 0u, 0u, WF280_RETRY_COUNT);
    if (status != WF280_STATUS_OK) return status;
    return wf280_read(bus, data, count);
}

static void wf280_save_adc_config(uint8_t saved[4])
{
    memcpy(saved, ADS1220.config, 4u);
}

static void wf280_restore_adc_config(const uint8_t saved[4])
{
    memcpy(ADS1220.config, saved, 4u);
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
}

static uint32_t wf280_read_current(void)
{
    ADS1220.config[0] = WF280_CURRENT_CHANNEL;
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
    osDelay(10u);
    return GET_ADS1220_Val(&ADS1220);
}

static uint8_t wf280_is_short(uint32_t raw, uint16_t supply_mv)
{
    uint64_t current_ua;
    if (!supply_mv || (raw & 0x80000000u)) return 0u;
    current_ua = (uint64_t)raw * supply_mv * 1000u;
    current_ua /= (uint64_t)WF280_ADC_FULL_SCALE * WF280_CURRENT_SHUNT_OHM;
    return current_ua >= WF280_SHORT_CURRENT_UA;
}

static void wf280_power_off(void)
{
    MCP4725_WriteData_Voltage(0u);
}

static void wf280_open(I2CInfo *bus)
{
    IIC_Init(bus, I2C_SDA_GPIO_Port, I2C_SDA_Pin,
             I2C_SCL_GPIO_Port, I2C_SCL_Pin);
}

static void wf280_close(I2CInfo *bus)
{
    IIC_DeInit(bus);
}

/* Keep the legacy OTP command (cmd[0] == 11) available. It now uses the
   same direct command/read transaction as the new WF280 test commands and
   propagates NACK/timeout instead of comparing stale bytes. */
void WF280CheckOtpReg(uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t i;
    uint8_t data[3] = {0};
    uint8_t status;

    if (!out || out_lens < WF280_RESULT_LENGTH) return;
    memset(out, 0, out_lens * sizeof(*out));
    memset(otp1, 0, sizeof(otp1));
    memset(otp2, 0, sizeof(otp2));
    wf280_open(&bus);
    for (i = 0; i < 32u; ++i) {
        status = wf280_get_chip_data(&bus, i, data, 3u);
        if (status != WF280_STATUS_OK) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
        otp1[i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(2u);
    }
    for (i = 0; i < 32u; ++i) {
        status = wf280_get_chip_data(&bus, (uint8_t)(0x80u + i), data, 3u);
        if (status != WF280_STATUS_OK) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
        otp2[i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(2u);
        if (otp2[i] != otp1[i]) {
            out[0] = 0xFFu; out[1] = i + 1u; wf280_close(&bus); return;
        }
    }
    wf280_close(&bus);
    out[0] = 1u;
    out[1] = 0u;
}

/* Run the 0xAC bridge/temperature measurement in either 3.3 V or 2.0 V
   mode. Results are {status, six raw bytes}; the supply is always removed
   on every exit, including short-circuit and I2C failure paths. */
uint8_t WF280TestOutput(uint16_t supply_mv, uint16_t measurement,
                        uint16_t settle_ms, uint16_t *out,
                        uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t params[2];
    uint8_t data[6] = {0};
    uint8_t status = WF280_STATUS_OK;
    uint32_t current;

    if (!out || out_lens < WF280_OUTPUT_RESULT_LENGTH ||
        (supply_mv != 2000u && supply_mv != 3300u))
        return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    MCP4725_WriteData_Voltage(supply_mv);
    /* The reference firmware gives the 2.0 V/M1 path substantially longer
       rail settling time before its short-circuit/current sample. */
    osDelay(supply_mv == 2000u ? WF280_OUTPUT_2V_SETTLE_MS :
            WF280_OUTPUT_3V3_SETTLE_MS);
    current = wf280_read_current();
    if (wf280_is_short(current, supply_mv)) {
        out[0] = WF280_STATUS_SHORT;
        out[1] = (uint8_t)current;
        out[2] = (uint8_t)(current >> 8);
        out[3] = (uint8_t)(current >> 16);
        out[4] = (uint8_t)(current >> 24);
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }

    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        params[0] = (uint8_t)(measurement >> 8);
        params[1] = (uint8_t)measurement;
        status = wf280_write(&bus, WF280_CMD_BT_MEAS, params, 2u);
    }
    if (status == WF280_STATUS_OK) {
        osDelay(settle_ms);
        status = wf280_wait_idle(&bus, 0u, 2u, WF280_RETRY_COUNT);
    }
    if (status == WF280_STATUS_OK) status = wf280_read(&bus, data, 6u);
    wf280_close(&bus);
    out[0] = status;
    if (status == WF280_STATUS_OK) {
        uint8_t i;
        for (i = 0; i < 6u; ++i) out[i + 1u] = data[i];
    }
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
    return status;
}

/* Read the normal (0x00..0x1F) or M1 (0x80..0x9F) coefficient bank. */
uint8_t WF280ReadCoefficients(uint8_t start, uint8_t count,
                              uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t data[3] = {0};
    uint8_t i;
    uint8_t status = WF280_STATUS_OK;
    uint16_t supply_mv;

    if (!out || !count || count > WF280_COEFF_MAX ||
        out_lens < (uint16_t)(count + 1u)) return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    supply_mv = (start & 0x80u) ? 2000u : 3300u;
    MCP4725_WriteData_Voltage(supply_mv);
    osDelay(100u);
		//
    if (wf280_is_short(wf280_read_current(), supply_mv)) {
        out[0] = WF280_STATUS_SHORT;
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }
    wf280_open(&bus);
    /* The reference program resets the device before every coefficient
       sequence.  Keep the command/NOR state deterministic when a host
       requests coefficients after an earlier measurement. */
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) osDelay(10u);
    for (i = 0; i < count; ++i) {
        if (status != WF280_STATUS_OK) break;
        status = wf280_get_chip_data(&bus, (uint8_t)(start + i), data, 3u);
        if (status != WF280_STATUS_OK) break;
        out[i + 1u] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        osDelay(5u);
    }
    wf280_close(&bus);
    out[0] = status;
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
    return status;
}

/* M1 mode returns standby/work current followed by the 0x80 coefficient
   bank. Current occupies four byte-valued Modbus registers, matching the
   reference program's ADC[0]/ADC[3] fields. */
uint8_t WF280ReadM1(uint8_t count, uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t data[3] = {0};
    uint8_t i;
    uint8_t status = WF280_STATUS_OK;
    uint32_t standby = 0u;
    uint32_t working = 0u;

    if (!out || !count || count > WF280_COEFF_MAX ||
        out_lens < (uint16_t)(5u + count)) return WF280_STATUS_I2C;
    memset(out, 0, out_lens * sizeof(*out));
    wf280_save_adc_config(saved_config);
    /* The 0x80..0x9F bank is the M1/2.0 V calibration bank in the
       reference firmware.  The new board selects that rail with the DAC. */
    MCP4725_WriteData_Voltage(2000u);
    osDelay(WF280_M1_SETTLE_MS);
    standby = wf280_read_current();
    if (wf280_is_short(standby, 2000u)) {
        out[0] = WF280_STATUS_SHORT;
        out[1] = (uint8_t)standby;
        out[2] = (uint8_t)(standby >> 8);
        wf280_restore_adc_config(saved_config);
        wf280_power_off();
        return WF280_STATUS_SHORT;
    }
    wf280_open(&bus);
    status = wf280_set_cmd(&bus, WF280_CMD_RESET, NULL, 0u, 0u, 10u);
    if (status == WF280_STATUS_OK) {
        osDelay(50u);
        working = wf280_read_current();
        /* Preserve the reference host layout: each result register carries
           one byte (ADC[0] low/high, then ADC[3] low/high). */
        out[1] = (uint8_t)standby;
        out[2] = (uint8_t)(standby >> 8);
        out[3] = (uint8_t)working;
        out[4] = (uint8_t)(working >> 8);
    }
    for (i = 0; status == WF280_STATUS_OK && i < count; ++i) {
        status = wf280_get_chip_data(&bus, (uint8_t)(0x80u + i), data, 3u);
        if (status == WF280_STATUS_OK)
            out[5u + i] = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
    }
    wf280_close(&bus);
    out[0] = status;
    wf280_restore_adc_config(saved_config);
    wf280_power_off();
    return status;
}
