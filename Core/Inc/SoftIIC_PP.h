#ifndef INC_SOFTIIC_PP_H_
#define INC_SOFTIIC_PP_H_

#include "main.h"

/* Dedicated single-master reference bus. SCL is push-pull: no stretching.
   SDA is released for slave ACK/data and for the master's final NACK. */
void pp_iic_init(GPIO_TypeDef *sda_port, uint16_t sda_pin,
                 GPIO_TypeDef *scl_port, uint16_t scl_pin);
void pp_iic_start(void);
void pp_iic_stop(void);
void pp_iic_ack(void);
void pp_iic_nack(void);
void pp_iic_sendbyte(uint8_t byte);
uint8_t pp_iic_wait_ack(void);
uint8_t pp_iic_readbyte(uint8_t ack);

#endif
