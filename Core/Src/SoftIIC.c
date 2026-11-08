/* Single-master software I2C. SDA/SCL need external pull-ups. */
#include "SoftIIC.h"
#include <string.h>

I2CInfo i2c1;

static void release_bus(I2CInfo *info)
{
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_SET);
}

/* Releasing an open-drain output does not mean the wire is already high. */
static uint8_t scl_high(I2CInfo *info)
{
    uint32_t elapsed = 0;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_SET);
    while (HAL_GPIO_ReadPin(info->portScl, info->pinScl) == GPIO_PIN_RESET) {
        if (elapsed++ >= IIC_SCL_TIMEOUT_US) {
            info->error = 1;
            release_bus(info);
            return 1;
        }
        delay_us(1);
    }
    return 0;
}

void IIC_MspInit(GPIO_TypeDef *SDA_PORT, uint16_t SDA_PIN,
                 GPIO_TypeDef *SCL_PORT, uint16_t SCL_PIN)
{
    GPIO_InitTypeDef init = {0};
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    init.Pin = SDA_PIN;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SDA_PORT, &init);
    init.Pin = SCL_PIN;
    HAL_GPIO_Init(SCL_PORT, &init);
}

void IIC_DeMspInit(GPIO_TypeDef *SDA_PORT, uint16_t SDA_PIN,
                   GPIO_TypeDef *SCL_PORT, uint16_t SCL_PIN)
{
    GPIO_InitTypeDef init = {0};
    /* Disconnect without first pulling the idle bus low. */
    init.Pin = SDA_PIN;
    init.Mode = GPIO_MODE_ANALOG;
    init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SDA_PORT, &init);
    init.Pin = SCL_PIN;
    HAL_GPIO_Init(SCL_PORT, &init);
}

void IIC_Init(I2CInfo *info, GPIO_TypeDef *SDA_PORT, uint16_t SDA_PIN,
              GPIO_TypeDef *SCL_PORT, uint16_t SCL_PIN)
{
    info->portSda = SDA_PORT;
    info->portScl = SCL_PORT;
    info->pinSda = SDA_PIN;
    info->pinScl = SCL_PIN;
    info->error = 0;
    IIC_MspInit(SDA_PORT, SDA_PIN, SCL_PORT, SCL_PIN);
}

void IIC_DeInit(I2CInfo *info)
{
    IIC_DeMspInit(info->portSda, info->pinSda, info->portScl, info->pinScl);
    info->portSda = NULL;
    info->portScl = NULL;
    info->pinSda = 0;
    info->pinScl = 0;
}

void iic_stop(I2CInfo *info)
{
    if (info->error) { release_bus(info); return; }
    /* Establish SDA low while SCL is low, then generate exactly one STOP. */
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    delay_us(10);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_RESET);
    delay_us(20);
    if (scl_high(info)) return;
    delay_us(20);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
    delay_us(20);
    if (HAL_GPIO_ReadPin(info->portSda, info->pinSda) == GPIO_PIN_RESET)
        info->error = 1;
}

void iic_start(I2CInfo *info)
{
    uint8_t pulse;
    info->error = 0;
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
    delay_us(20);
    if (scl_high(info)) return;
    delay_us(20);
    /* A reset in the middle of a read can leave a slave holding SDA low.
       Clock out at most nine bits and terminate it before a new request. */
    if (HAL_GPIO_ReadPin(info->portSda, info->pinSda) == GPIO_PIN_RESET) {
        for (pulse = 0; pulse < 9; ++pulse) {
            HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
            delay_us(20);
            if (scl_high(info)) return;
            delay_us(20);
        }
        iic_stop(info);
        if (info->error) return;
    }
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_RESET);
    delay_us(20);
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    delay_us(20);
}

uint8_t iic_wait_ack(I2CInfo *info)
{
    uint8_t nack;
    if (info->error) return 1;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
    delay_us(20);
    if (scl_high(info)) return 1;
    delay_us(20);
    nack = HAL_GPIO_ReadPin(info->portSda, info->pinSda) == GPIO_PIN_SET;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    delay_us(20);
    /* The transaction owner emits STOP, including on NACK. */
    return nack;
}

static void master_ack(I2CInfo *info, GPIO_PinState level)
{
    if (info->error) return;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, level);
    delay_us(20);
    if (scl_high(info)) return;
    delay_us(20);
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    delay_us(20);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
}

void iic_ack(I2CInfo *info) { master_ack(info, GPIO_PIN_RESET); }
void iic_nack(I2CInfo *info) { master_ack(info, GPIO_PIN_SET); }

void iic_sendbyte(I2CInfo *info, uint8_t byte)
{
    uint8_t bit;
    if (info->error) return;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    for (bit = 0; bit < 8; ++bit) {
        HAL_GPIO_WritePin(info->portSda, info->pinSda,
                         (byte & 0x80u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        byte <<= 1;
        delay_us(10);
        if (scl_high(info)) return;
        delay_us(20);
        HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
        delay_us(10);
    }
}

uint8_t iic_readbyte(I2CInfo *info, uint8_t ack)
{
    uint8_t bit, value = 0;
    if (info->error) return 0;
    HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(info->portSda, info->pinSda, GPIO_PIN_SET);
    delay_us(20);
    for (bit = 0; bit < 8; ++bit) {
        if (scl_high(info)) return 0;
        delay_us(10);
        value = (uint8_t)((value << 1) |
                (HAL_GPIO_ReadPin(info->portSda, info->pinSda) == GPIO_PIN_SET));
        delay_us(10);
        HAL_GPIO_WritePin(info->portScl, info->pinScl, GPIO_PIN_RESET);
        delay_us(20);
    }
    if (ack) iic_ack(info); else iic_nack(info);
    return info->error ? 0 : value;
}

/* Addresses are the existing 8-bit bus addresses, not 7-bit addresses. */
uint8_t I2C_ReadNByte(I2CInfo *info, uint8_t addr, uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t err;
    if (!length) return 0;
    memset(data, 0, length);
    iic_start(info);
    iic_sendbyte(info, addr | IIC_READ);
    err = iic_wait_ack(info);
    if (!err) {
        for (i = 0; i < length; ++i) {
            data[i] = iic_readbyte(info, i + 1u < length);
            if (info->error) break;
        }
    }
    iic_stop(info);
    err = err || info->error;
    if (err) memset(data, 0, length);
    return err;
}

/* The first element is the on-wire address; following elements are payload. */
uint8_t I2C_WriteNByte(I2CInfo *info, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t err = 0;
    if (!length) return 0;
    iic_start(info);
    for (i = 0; i < length; ++i) {
        iic_sendbyte(info, data[i]);
        err = iic_wait_ack(info);
        if (err) break;
    }
    iic_stop(info);
    return err || info->error;
}

void I2C_IDLE(I2CInfo *info) { IIC_DeInit(info); }
