#include "jh1400f_driver.h"
#include "JH1400F.h"
#include "ADS1220.h"
#include "MCP4725.h"
#include "SoftIIC.h"
#include "cmsis_os.h"
#include <string.h>

#define JH1400F_CURRENT_CONFIG 0x01u
#define JH1400F_SUPPLY_MV 3300u
#define JH1400F_POWER_SETTLE_MS 150u
#define JH1400F_POLL_DELAY_MS 1u
#define JH1400F_COMMAND_RETRIES 10u
#define JH1400F_MEASURE_RETRIES 120u

static uint8_t jh1400f_write(I2CInfo *bus, uint8_t command,
                             const uint8_t *params, uint8_t param_count)
{
    uint8_t frame[4];
    uint8_t i;

    if (param_count > 2u) return JH1400F_STATUS_I2C;
    frame[0] = JH1400F_I2C_ADDRESS | IIC_WRITE;
    frame[1] = command;
    for (i = 0; i < param_count; ++i) frame[2u + i] = params[i];
    return I2C_WriteNByte(bus, frame, 2u + param_count) ?
           JH1400F_STATUS_I2C : JH1400F_STATUS_OK;
}

static uint8_t jh1400f_read(I2CInfo *bus, uint8_t *data, uint8_t count)
{
    if (!count) return JH1400F_STATUS_OK;
    return I2C_ReadNByte(bus, JH1400F_I2C_ADDRESS, data, count) ?
           JH1400F_STATUS_I2C : JH1400F_STATUS_OK;
}

static uint8_t jh1400f_wait_idle(I2CInfo *bus, uint16_t retries)
{
    uint8_t status = 0;
    uint16_t i;

    for (i = 0; i < retries; ++i) {
        if (jh1400f_read(bus, &status, 1u) != JH1400F_STATUS_OK)
            return JH1400F_STATUS_I2C;
        /* The reference WF280_OS driver treats NOR idle as BUSY=0 and
           FIFO_EMPTY=1.  Checking both bits avoids consuming stale data
           while retaining the status byte returned by every read. */
        if ((status & BUSY_OF_STATUS) == STATUS_IS_IDLE &&
            (status & FIFO_EMPTY_OF_STATUS) == FIFO_EMPTY_OF_STATUS)
            return JH1400F_STATUS_OK;
        osDelay(JH1400F_POLL_DELAY_MS);
    }
    return JH1400F_STATUS_TIMEOUT;
}

static uint8_t jh1400f_exec(I2CInfo *bus, uint8_t command,
                            const uint8_t *params, uint8_t param_count,
                            uint8_t *data, uint8_t data_count,
                            uint16_t retries)
{
    uint8_t status = jh1400f_write(bus, command, params, param_count);
    if (status != JH1400F_STATUS_OK) return status;
    osDelay(1u);
    status = jh1400f_wait_idle(bus, retries);
    if (status != JH1400F_STATUS_OK) return status;
    return jh1400f_read(bus, data, data_count);
}

static uint8_t jh1400f_reset(I2CInfo *bus)
{
    return jh1400f_exec(bus, CMD_RESET, NULL, 0u, NULL, 0u,
                        JH1400F_COMMAND_RETRIES);
}

static uint8_t jh1400f_read_otp(I2CInfo *bus, uint8_t command,
                                uint16_t *value, uint8_t *status_byte)
{
    uint8_t data[3] = {0};
    uint8_t status = jh1400f_exec(bus, command, NULL, 0u, data, 3u,
                                  JH1400F_COMMAND_RETRIES);
    if (status == JH1400F_STATUS_OK) {
        if (status_byte) *status_byte = data[0];
        if (value) *value = ((uint16_t)data[1] << 8) | data[2];
    }
    return status;
}

static uint8_t jh1400f_read_id(I2CInfo *bus, uint8_t *data)
{
    return jh1400f_exec(bus, CMD_READ_PRODUCT_ID, NULL, 0u,
                        data, 3u, JH1400F_COMMAND_RETRIES);
}

static uint8_t jh1400f_read_raw(I2CInfo *bus, uint8_t command,
                                uint8_t data[4])
{
    return jh1400f_exec(bus, command, NULL, 0u, data, 4u,
                        JH1400F_MEASURE_RETRIES);
}

static uint8_t jh1400f_read_bridge_temperature(I2CInfo *bus,
                                                uint8_t data[6],
                                                uint32_t *working_current)
{
    uint8_t params[2] = {0u, 0u};
    uint8_t status;

    status = jh1400f_write(bus, CMD_BT_MEAS, params, 2u);
    if (status != JH1400F_STATUS_OK) return status;
    /* Capture the current while the bridge/temperature conversion is active. */
    if (working_current) *working_current = 0u;
    if (working_current) {
        ADS1220.config[0] = JH1400F_CURRENT_CONFIG;
        ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
        osDelay(10u);
        *working_current = GET_ADS1220_Val(&ADS1220);
    }
    status = jh1400f_wait_idle(bus, JH1400F_MEASURE_RETRIES);
    if (status != JH1400F_STATUS_OK) return status;
    return jh1400f_read(bus, data, 6u);
}

static uint32_t jh1400f_read_current(void)
{
    ADS1220.config[0] = JH1400F_CURRENT_CONFIG;
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
    osDelay(10u);
    return GET_ADS1220_Val(&ADS1220);
}

static void jh1400f_store_u32(uint16_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void jh1400f_store_bytes(uint16_t *out, const uint8_t *data,
                                uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; ++i) out[i] = data[i];
}

void AllTestForJH1400F(uint16_t *out, uint16_t out_lens)
{
    I2CInfo bus;
    uint8_t saved_config[4];
    uint8_t id[3] = {0};
    uint8_t bridge[4] = {0};
    uint8_t temperature[4] = {0};
    uint8_t bridge_temperature[6] = {0};
    uint8_t status_byte = 0;
    uint8_t status = JH1400F_STATUS_OK;
    uint8_t bus_open = 0u;
    uint8_t i;
    uint16_t value;
    uint32_t standby_current;
    uint32_t working_current = 0u;

    if (!out || out_lens < JH1400F_RESULT_LENGTH) return;
    memset(out, 0, out_lens * sizeof(*out));
    memcpy(saved_config, ADS1220.config, sizeof(saved_config));

    MCP4725_WriteData_Voltage(JH1400F_SUPPLY_MV);
    osDelay(JH1400F_POWER_SETTLE_MS);
    standby_current = jh1400f_read_current();
    jh1400f_store_u32(&out[JH1400F_CURRENT_OFFSET], standby_current);

    IIC_Init(&bus, I2C_SDA_GPIO_Port, I2C_SDA_Pin,
             I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    bus_open = 1u;

    status = jh1400f_reset(&bus);
    if (status != JH1400F_STATUS_OK) goto cleanup;

    status = jh1400f_read_id(&bus, id);
    out[JH1400F_PRODUCT_ID_OFFSET] = status;
    if (status == JH1400F_STATUS_OK)
        jh1400f_store_bytes(&out[JH1400F_PRODUCT_ID_OFFSET + 1u], &id[1], 2u);
    if (status != JH1400F_STATUS_OK) goto cleanup;

    status = jh1400f_read_bridge_temperature(&bus, bridge_temperature,
                                             &working_current);
    jh1400f_store_u32(&out[JH1400F_WORKING_CURRENT_OFFSET], working_current);
    out[JH1400F_BRIDGE_TEMPERATURE_OFFSET] = status;
    if (status == JH1400F_STATUS_OK)
        jh1400f_store_bytes(&out[JH1400F_BRIDGE_TEMPERATURE_OFFSET + 1u],
                            &bridge_temperature[1], 5u);
    if (status != JH1400F_STATUS_OK) goto cleanup;

    status = jh1400f_read_raw(&bus, CMD_B_MEAS, bridge);
    out[JH1400F_BRIDGE_OFFSET] = status;
    if (status == JH1400F_STATUS_OK)
        jh1400f_store_bytes(&out[JH1400F_BRIDGE_OFFSET + 1u], &bridge[1], 3u);
    if (status != JH1400F_STATUS_OK) goto cleanup;

    status = jh1400f_read_raw(&bus, CMD_T_MEAS, temperature);
    out[JH1400F_TEMPERATURE_OFFSET] = status;
    if (status == JH1400F_STATUS_OK)
        jh1400f_store_bytes(&out[JH1400F_TEMPERATURE_OFFSET + 1u],
                            &temperature[1], 3u);
    if (status != JH1400F_STATUS_OK) goto cleanup;

    for (i = 0; i < JH1400F_OTP_COUNT; ++i) {
        status = jh1400f_read_otp(&bus, i, &value, &status_byte);
        if (status != JH1400F_STATUS_OK) break;
        out[JH1400F_OTP_OFFSET + i] = value;
    }
    out[JH1400F_OTP_STATUS_OFFSET] = status;
    out[JH1400F_OTP_STATUS_OFFSET + 1u] = status_byte;
    if (status != JH1400F_STATUS_OK) goto cleanup;

    for (i = 0; i < JH1400F_OTP_COUNT; ++i) {
        status = jh1400f_read_otp(&bus, (uint8_t)(0x80u + i), &value,
                                  &status_byte);
        if (status != JH1400F_STATUS_OK) break;
        out[JH1400F_M1_OTP_OFFSET + i] = value;
    }
    out[JH1400F_M1_STATUS_OFFSET] = status;
    out[JH1400F_M1_STATUS_OFFSET + 1u] = status_byte;

cleanup:
    if (bus_open) IIC_DeInit(&bus);
    memcpy(ADS1220.config, saved_config, sizeof(saved_config));
    ADS1220_WriteRegister(0u, 1u, &ADS1220.config[0]);
    MCP4725_WriteData_Voltage(0u);
    out[0] = status;
}
