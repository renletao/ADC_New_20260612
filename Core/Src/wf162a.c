#include "wf162a.h"
#include "ADS1220.h"
#include "MCP4725.h"
#include "cmsis_os.h"
#include <stdint.h>
#include <string.h>

/*
 * WF162A-11Bar pinout from the supplied datasheet:
 *   1 = GND, 2 = Vout+, 3 = VDD, 4 = Vout-.
 *
 * The new board routes these three analogue nodes to the same ADS1220 mux
 * selections already used by WF100S: 0xA1 (AIN2-AVSS), 0xB1 (AIN3-AVSS)
 * and 0x51 (AIN2-AIN3). The mapping is kept in one place so it can be
 * changed with the board schematic without touching the result protocol.
 */
/* AIN1-AIN0, gain 1, PGA bypass; the current-sense polarity is positive. */
//#define WF162A_CURRENT_CONFIG 0x61u
#define WF162A_CURRENT_CONFIG 0x01u
#define WF162A_VOUT_PLUS_CONFIG 0xA1u
#define WF162A_VOUT_MINUS_CONFIG 0xB1u
#define WF162A_DIFF_CONFIG 0x51u
#define WF162A_SETTLE_MS 150u
#define WF162A_SAMPLE_DELAY_MS 20u
#define WF162A_SAMPLE_ATTEMPTS 10u

/* Configuration register 2, bits 7:6 VREF[1:0] (datasheet table for
   configuration register 2).  Only the analog supply keeps the ADC full
   scale tracking the switched DUT rail; the internal 2.048 V reference
   cannot represent the 2.5 V half bridge of a 5 V product, it clips at
   0x7FFFFF instead.  The constants stay local to this module on purpose so
   the other product tests keep their existing ADS1220 configuration. */
#define WF162A_VREF_MASK 0xC0u
#define WF162A_VREF_INTERNAL_2048 0x00u /* 00: internal 2.048 V          */
#define WF162A_VREF_AVDD_AVSS 0xC0u     /* 11: analog supply AVDD - AVSS */

/* Every conversion is taken against the ADC analog supply, so the full scale
   always follows the switched DUT rail: the 2.5 V half bridge of a 5 V
   product stays at 50 % FS instead of saturating a 2.048 V reference.  The
   task-9 host converts each word with a fixed 3.3 V ratio, therefore every
   word is rescaled from the reference that was really in use to 3300 mV. */
#define WF162A_HOST_REFERENCE_MV 3300u

/* (AVDD - AVSS) / 4 measured against the internal 2.048 V reference.  The
   conversion code of that monitor channel equals AVDD in mV times 1024, so
   the reference actually in use can be measured instead of assumed.  This
   covers boards that power the ADS1220 from the switched DUT rail and boards
   that keep it on a fixed 3.3 V rail. */
#define WF162A_SUPPLY_MONITOR_CONFIG 0xD1u
#define WF162A_SUPPLY_CODE_PER_MV 1024u
#define WF162A_SUPPLY_MIN_MV 1800u
#define WF162A_SUPPLY_MAX_MV 5500u

/* Positive over-range is reported as 0x7FFFFF and negative over-range as
   0x800000, so a word within a few counts of full scale is a clipped sample
   and no longer a measurement. */
#define WF162A_FULL_SCALE_CODE 0x7FFFFFu
#define WF162A_SATURATION_MARGIN 16

/* WF162A defaults to 5 V.  0x201 may select 3.3 V or 5 V and the
   selection remains active until the host changes it. */
static uint16_t wf162a_configured_supply_mv = 3300u;

void WF162A_SetConfiguredSupply(uint16_t supply_mv)
{
    if (supply_mv == 3300u || supply_mv == 5000u)
        wf162a_configured_supply_mv = supply_mv;
}

static void wf162a_store_raw(uint16_t *dst, uint32_t raw)
{
    /* Product result buffers are 16-bit Modbus slots, but the existing
       protocol stores one ADC byte per slot (the same layout used by WF100S).
       Keep the upper byte of each slot clear. */
    dst[0] = (uint8_t)raw;
    dst[1] = (uint8_t)(raw >> 8);
    dst[2] = (uint8_t)(raw >> 16);
    dst[3] = (uint8_t)(raw >> 24);
}

/* Select one input pair and one reference.  Both registers are rewritten
   because the reference of the previous conversion is not the one the next
   conversion needs. */
static void wf162a_apply_input(uint8_t config, uint8_t vref_bits)
{
    ADS1220.config[0] = config;
    ADS1220.config[2] = (uint8_t)((ADS1220.config[2] & (uint8_t)~WF162A_VREF_MASK) |
                                  vref_bits);
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
    ADS1220_WriteRegister(2u, 1u, &ADS1220.config[2]);
}

static uint8_t wf162a_read_code(int32_t *value)
{
    uint32_t raw;
    uint8_t attempt;

    /* A disconnected ADC or conversion timeout is retried, while a valid
       zero code is accepted because ADS1220_ReadValue returns a separate
       success flag. */
    for (attempt = 0u; attempt < WF162A_SAMPLE_ATTEMPTS; ++attempt) {
        if (ADS1220_ReadValue(&ADS1220, &raw)) {
            *value = (int32_t)raw;
            return 1u;
        }
    }
    *value = 0;
    return 0u;
}

static int32_t wf162a_median3(int32_t a, int32_t b, int32_t c)
{
    if (a > b) {
        int32_t t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        int32_t t = b;
        b = c;
        c = t;
    }
    if (a > b) b = a;
    return b;
}

/* Change the input/reference, discard the first conversion after the
   change, then use the median of three valid conversions.  The median
   suppresses one stale or disturbed conversion without changing the result
   format used by the host. */
static uint8_t wf162a_sample(uint8_t config, uint8_t vref_bits, int32_t *value)
{
    int32_t sample_a;
    int32_t sample_b;
    int32_t sample_c;

    wf162a_apply_input(config, vref_bits);
    osDelay(WF162A_SAMPLE_DELAY_MS);
    (void)GET_ADS1220_Val(&ADS1220);
    if (!wf162a_read_code(&sample_a) ||
        !wf162a_read_code(&sample_b) ||
        !wf162a_read_code(&sample_c)) {
        *value = 0;
        return 0u;
    }
    *value = wf162a_median3(sample_a, sample_b, sample_c);
    return 1u;
}

/* Rescale a word taken against reference_mv to the reference the host
   assumes, keeping signed current and differential results intact. */
static uint32_t wf162a_rescale(uint32_t raw, uint16_t reference_mv)
{
    int64_t value;

    if (reference_mv == 0u) return raw;
    value = (int64_t)(int32_t)raw * (int64_t)reference_mv /
            (int64_t)WF162A_HOST_REFERENCE_MV;
    if (value > (int64_t)INT32_MAX) value = (int64_t)INT32_MAX;
    if (value < (int64_t)INT32_MIN) value = (int64_t)INT32_MIN;
    return (uint32_t)(int32_t)value;
}

static uint8_t wf162a_saturated(uint32_t raw)
{
    int32_t code = (int32_t)raw;
    int32_t magnitude = (code < 0) ? -code : code;

    return (magnitude >= ((int32_t)WF162A_FULL_SCALE_CODE - WF162A_SATURATION_MARGIN)) ?
           1u : 0u;
}

/* Measure the ADC analog supply.  The requested DUT supply is retained only
   as a diagnostic fallback; callers must reject the round when the monitor
   failed instead of treating that fallback as a measured reference. */
static uint16_t wf162a_measure_reference_mv(uint16_t fallback_mv, uint16_t *status)
{
    int32_t code;
    uint32_t mv;

    if (!wf162a_sample(WF162A_SUPPLY_MONITOR_CONFIG,
                       WF162A_VREF_INTERNAL_2048, &code)) {
        *status |= WF162A_STATUS_REFERENCE_FAILED | WF162A_STATUS_ADC_TIMEOUT;
        return fallback_mv;
    }
    if (code < 0) {
        *status |= WF162A_STATUS_REFERENCE_FAILED;
        return fallback_mv;
    }
    mv = ((uint32_t)code + (WF162A_SUPPLY_CODE_PER_MV / 2u)) /
         WF162A_SUPPLY_CODE_PER_MV;
    if (mv < WF162A_SUPPLY_MIN_MV || mv > WF162A_SUPPLY_MAX_MV) {
        *status |= WF162A_STATUS_REFERENCE_FAILED;
        return fallback_mv;
    }
    *status |= WF162A_STATUS_REFERENCE_MEASURED;
    return (uint16_t)mv;
}

static void wf162a_measure_channel(uint16_t *dst, uint8_t config, uint16_t reference_mv,
                                   uint16_t saturation_flag, uint16_t *status)
{
    int32_t raw;

    if (!wf162a_sample(config, WF162A_VREF_AVDD_AVSS, &raw)) {
        *status |= WF162A_STATUS_ADC_TIMEOUT;
        wf162a_store_raw(dst, 0u);
        return;
    }
    if (wf162a_saturated((uint32_t)raw)) *status |= saturation_flag;
    wf162a_store_raw(dst, wf162a_rescale((uint32_t)raw, reference_mv));
}

static void wf162a_run_supply(uint16_t supply_mv, uint16_t *result)
{
    uint16_t status = 0u;
    uint16_t reference_mv;

    /* Keep the selected rail valid even when cmd=9 is sent without a
       preceding 0x201 write.  This makes the default 5 V mode usable. */
    MCP4725_WriteData_Voltage(supply_mv);
    osDelay(WF162A_SETTLE_MS);

    reference_mv = wf162a_measure_reference_mv(supply_mv, &status);
    if (status & WF162A_STATUS_REFERENCE_FAILED) {
        result[WF162A_DIAG_AVDD_OFFSET] = reference_mv;
        result[WF162A_DIAG_STATUS_OFFSET] = status;
        result[WF162A_DIAG_CONFIG_OFFSET] = ADS1220.config[2];
        return;
    }
    wf162a_measure_channel(result + WF162A_CURRENT_OFFSET, WF162A_CURRENT_CONFIG,
                           reference_mv, WF162A_STATUS_SATURATED_CURRENT, &status);
    wf162a_measure_channel(result + WF162A_VOUT_PLUS_OFFSET, WF162A_VOUT_PLUS_CONFIG,
                           reference_mv, WF162A_STATUS_SATURATED_VOUT_PLUS, &status);
    wf162a_measure_channel(result + WF162A_VOUT_MINUS_OFFSET, WF162A_VOUT_MINUS_CONFIG,
                           reference_mv, WF162A_STATUS_SATURATED_VOUT_MINUS, &status);
    wf162a_measure_channel(result + WF162A_DIFF_OFFSET, WF162A_DIFF_CONFIG,
                           reference_mv, WF162A_STATUS_SATURATED_DIFF, &status);

    /* Report what the words were converted with, so a host or a bench
       measurement can confirm the reference without a scope. */
    result[WF162A_DIAG_AVDD_OFFSET] = reference_mv;
    result[WF162A_DIAG_STATUS_OFFSET] = status;
    result[WF162A_DIAG_CONFIG_OFFSET] = ADS1220.config[2];
}

void AllTestFor162A(uint16_t *out, uint16_t out_lens)
{
    uint8_t saved_config[4];
    uint16_t *selected_result;

    if (!out || out_lens < WF162A_RESULT_LENGTH) return;
    memset(out, 0, out_lens * sizeof(*out));
    memcpy(saved_config, ADS1220.config, sizeof(saved_config));
    /* Keep the established task-9 ADC lifecycle.  VDD remains active between
       rounds, so a full ADS1220 reset is unnecessary here and would disturb
       the conversion state used by the next sample. */
    /* The host may select 3.3 V or 5.0 V with command 0x201 before cmd=9.
       The existing upper-computer task-9 reader requests only the first
       16 registers, so every selected round must be exposed at out[0..15].
       Keep a copy in the 5 V bank as well for callers that read the extended
       32-register layout. */
    selected_result = &out[0];
    wf162a_run_supply(wf162a_configured_supply_mv, selected_result);
    if (wf162a_configured_supply_mv == 5000u) {
        memcpy(&out[WF162A_SUPPLY_RESULT_LENGTH], selected_result,
               WF162A_SUPPLY_RESULT_LENGTH * sizeof(*selected_result));
    }
    /* Register 2 is rewritten during the round, so all four registers are
       restored together instead of only the mux byte. */
    memcpy(ADS1220.config, saved_config, sizeof(saved_config));
    ADS1220_WriteRegister(0u, 4u, &ADS1220.config[0]);
    /* Keep VDD on for repeated task-9 measurements.  The host can explicitly
       switch it off with {1, 0x201, 0, 0}; other product tests keep their
       existing power lifecycle. */
}
