#include "I2CHelper.h"
#include "cmsis_os.h"
#include <string.h>
#include "CmdHelper.h"

#define WF2200_I2C_ADDRESS 0xDAu

uint8_t ProductI2CTransfer(const uint16_t *cmd, uint8_t cmd_len,
                          uint16_t *out, uint16_t out_len)
{
    uint8_t send_data[64];
    uint8_t read_data[64] = {0};
    uint8_t err = 0;
    uint16_t i, send_len, read_len;
    if (!cmd || cmd_len < 7u || !out || !out_len) return CMD_ERROR_LENGTH;
    send_len = cmd[2]; read_len = cmd[4];
    if (!send_len || send_len > 64u || read_len > 64u ||
        cmd_len < 6u + send_len || read_len >= out_len) return CMD_ERROR_LENGTH;
    memset(out, 0, out_len * sizeof(*out));
    for (i = 0; i < send_len; ++i) send_data[i] = (uint8_t)cmd[i + 6u];
    IIC_Init(&i2c1, I2C_SDA_GPIO_Port, I2C_SDA_Pin, I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    /* A product read follows its separate register-select write. */
    if (send_len > 1u || !read_len) err = I2C_WriteNByte(&i2c1, send_data, send_len);
    if (!err && read_len) err = I2C_ReadNByte(&i2c1, send_data[0], read_data, read_len);
    for (i = 0; i < read_len; ++i) out[i] = read_data[i];
    out[read_len] = err;
    IIC_DeInit(&i2c1);
    return err;
}

static uint8_t product_write(I2CInfo *bus, uint8_t command,
                             const uint8_t *data, uint8_t len)
{
    uint8_t i;
    uint8_t err;
    iic_start(bus);
    iic_sendbyte(bus, WF2200_I2C_ADDRESS | IIC_WRITE);
    if (iic_wait_ack(bus)) { iic_stop(bus); return 1; }
    iic_sendbyte(bus, command);
    if (iic_wait_ack(bus)) { iic_stop(bus); return 1; }
    for (i = 0; i < len; ++i) {
        iic_sendbyte(bus, data[i]);
        err = iic_wait_ack(bus);
        if (err) { iic_stop(bus); return err; }
    }
    iic_stop(bus);
    return bus->error;
}

static uint8_t product_read(I2CInfo *bus, uint8_t *data, uint8_t len)
{
    return I2C_ReadNByte(bus, WF2200_I2C_ADDRESS, data, len);
}

static uint8_t pp_product_write(uint8_t command, const uint8_t *data, uint8_t len)
{
    uint8_t i;
    pp_iic_start();
    pp_iic_sendbyte(WF2200_I2C_ADDRESS | IIC_WRITE);
    if (pp_iic_wait_ack()) { pp_iic_stop(); return 1; }
    pp_iic_sendbyte(command);
    if (pp_iic_wait_ack()) { pp_iic_stop(); return 1; }
    for (i = 0; i < len; ++i) {
        pp_iic_sendbyte(data[i]);
        if (pp_iic_wait_ack()) { pp_iic_stop(); return 1; }
    }
    pp_iic_stop();
    return 0;
}

static uint8_t pp_product_read(uint8_t *data, uint8_t len)
{
    uint8_t i;
    pp_iic_start();
    pp_iic_sendbyte(WF2200_I2C_ADDRESS | IIC_READ);
    if (pp_iic_wait_ack()) { pp_iic_stop(); return 1; }
    for (i = 0; i < len; ++i) data[i] = pp_iic_readbyte(i + 1u < len);
    pp_iic_stop();
    return 0;
}

static uint8_t get_data(I2CInfo *bus, uint8_t *data, uint8_t len, uint8_t push_pull)
{
    uint8_t trigger = 0x0A;
    uint8_t err;
    memset(data, 0, len);
    if (push_pull) {
        err = pp_product_write(0x30, &trigger, 1);
        if (!err) osDelay(10);
        if (!err) err = pp_product_write(0x06, NULL, 0);
        if (!err) osDelay(2);
        if (!err) err = pp_product_read(data, len);
    } else {
        err = product_write(bus, 0x30, &trigger, 1);
        if (!err) osDelay(10);
        if (!err) err = product_write(bus, 0x06, NULL, 0);
        if (!err) osDelay(2);
        if (!err) err = product_read(bus, data, len);
    }
    return err;
}

uint8_t WF2200_GetData(uint8_t *read_data, uint8_t read_len)
{
    I2CInfo bus;
    IIC_Init(&bus, I2C_SDA_GPIO_Port, I2C_SDA_Pin, I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    uint8_t err = get_data(&bus, read_data, read_len, 0);
    IIC_DeInit(&bus);
    return err;
}

uint8_t PP_WF2200_GetData(uint8_t *read_data, uint8_t read_len)
{
    pp_iic_init(GPIOB, UART_RX_Pin, GPIOB, UART_TX_Pin);
    return get_data(NULL, read_data, read_len, 1);
}
