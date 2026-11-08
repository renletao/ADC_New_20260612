#include "SoftIIC_PP.h"

static GPIO_TypeDef *pp_sda_port;
static GPIO_TypeDef *pp_scl_port;
static uint16_t pp_sda_pin;
static uint16_t pp_scl_pin;

static void pp_sda_out(GPIO_PinState level)
{
    GPIO_InitTypeDef init = {0};
    /* Preload the requested level before enabling the push-pull output. */
    HAL_GPIO_WritePin(pp_sda_port, pp_sda_pin, level);
    init.Pin = pp_sda_pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pp_sda_port, &init);
}

static void pp_sda_in(void)
{
    GPIO_InitTypeDef init = {0};
    init.Pin = pp_sda_pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(pp_sda_port, &init);
}

static void pp_scl_out(void)
{
    GPIO_InitTypeDef init = {0};
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    init.Pin = pp_scl_pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pp_scl_port, &init);
}

void pp_iic_init(GPIO_TypeDef *sda_port, uint16_t sda_pin,
                 GPIO_TypeDef *scl_port, uint16_t scl_pin)
{
    pp_sda_port = sda_port;
    pp_scl_port = scl_port;
    pp_sda_pin = sda_pin;
    pp_scl_pin = scl_pin;
    pp_scl_out();
    pp_sda_out(GPIO_PIN_SET);
}

void pp_iic_start(void)
{
    /* SDA changes only while SCL is low until the actual START edge. */
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    delay_us(10);
    pp_sda_out(GPIO_PIN_SET);
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    delay_us(20);
    HAL_GPIO_WritePin(pp_sda_port, pp_sda_pin, GPIO_PIN_RESET);
    delay_us(20);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    delay_us(10);
}

void pp_iic_stop(void)
{
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    delay_us(10);
    pp_sda_out(GPIO_PIN_RESET);
    delay_us(20);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    delay_us(20);
    HAL_GPIO_WritePin(pp_sda_port, pp_sda_pin, GPIO_PIN_SET);
    delay_us(20);
}

void pp_iic_sendbyte(uint8_t byte)
{
    uint8_t i;
    uint8_t last_high = byte & 1u;
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    pp_sda_out((byte & 0x80u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    for (i = 0; i < 8; ++i) {
        HAL_GPIO_WritePin(pp_sda_port, pp_sda_pin,
                         (byte & 0x80u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        byte <<= 1;
        delay_us(5);
        HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
        delay_us(10);
        /* Release a high final bit before the slave can assert its ACK.
           A low final bit stays low until the falling edge, then releases. */
        if (i == 7 && last_high) pp_sda_in();
        HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
        if (i == 7) pp_sda_in();
        delay_us(5);
    }
}

uint8_t pp_iic_wait_ack(void)
{
    uint8_t nack;
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    pp_sda_in();
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    /* ACK is sampled in a complete ninth clock, even when already low. */
    delay_us(10);
    nack = HAL_GPIO_ReadPin(pp_sda_port, pp_sda_pin) == GPIO_PIN_SET;
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    delay_us(10);
    return nack;
}

void pp_iic_ack(void)
{
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    pp_sda_out(GPIO_PIN_RESET);
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    /* The slave may drive the next data bit after this falling edge. */
    pp_sda_in();
    delay_us(10);
}

void pp_iic_nack(void)
{
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    pp_sda_in();
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
    delay_us(10);
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    delay_us(10);
}

uint8_t pp_iic_readbyte(uint8_t ack)
{
    uint8_t i;
    uint8_t value = 0;
    HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
    pp_sda_in();
    delay_us(10);
    for (i = 0; i < 8; ++i) {
        HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_SET);
        delay_us(10);
        value = (uint8_t)((value << 1) |
                 (HAL_GPIO_ReadPin(pp_sda_port, pp_sda_pin) == GPIO_PIN_SET));
        HAL_GPIO_WritePin(pp_scl_port, pp_scl_pin, GPIO_PIN_RESET);
        delay_us(10);
    }
    if (ack) pp_iic_ack(); else pp_iic_nack();
    return value;
}
