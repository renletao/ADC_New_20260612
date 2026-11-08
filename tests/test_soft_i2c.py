#!/usr/bin/env python3
"""Exercise real GPIO drivers against a timed, wired-AND I2C slave model."""
from pathlib import Path
import os
import subprocess
import tempfile
from test_port import HEADERS, ROOT

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "SoftIIC.h"
#include "SoftIIC_PP.h"
#include "I2CHelper.h"
GPIO_TypeDef fake_a, fake_b, fake_c;
static GPIO_TypeDef *sda_port, *scl_port;
static uint16_t sda_pin, scl_pin;
static unsigned mode[2], latch[2], wire_scl=1, slave_low;
static uint64_t now, rise_time, fall_time, sda_time, stretch_until;
static unsigned starts, stops, active, phase, bits, shift, read_mode, ack_clock;
static unsigned transaction_received, received_count, read_count, master_ack_count, recovery_clocks;
static unsigned nack_byte, fault_sda, fault_scl, release_after;
static unsigned stretch_us, stretch_at, clock_requests, fault_at;
static uint8_t received[4096], reply[512], master_ack[512];
enum { RECEIVE, SLAVE_ACK, SEND, MASTER_ACK };
static unsigned master_sda(void){return mode[0]==GPIO_MODE_INPUT||mode[0]==GPIO_MODE_ANALOG||latch[0];}
static unsigned sda(void){return master_sda()&&!slave_low&&!fault_sda;}
static void check_contention(void){
    assert(!(mode[0]==GPIO_MODE_OUTPUT_PP&&latch[0]&&(slave_low||fault_sda)));
}
static void send_bit(void){slave_low=!(reply[read_count]&(0x80u>>bits));}
static void rising(void){
    if (fault_sda && release_after && ++recovery_clocks >= release_after) fault_sda=0;
    if (!active) return;
    if (phase==RECEIVE){
        shift=(shift<<1)|sda();
        if (++bits==8){
            assert(received_count<sizeof received);
            received[received_count++]=(uint8_t)shift;
            if (++transaction_received==1) read_mode=shift&1u;
        }
    } else if (phase==SLAVE_ACK) ack_clock=1;
    else if (phase==SEND) ++bits;
    else {
        assert(master_ack_count<sizeof master_ack);
        master_ack[master_ack_count++]=!sda();
        ack_clock=1;
    }
}
static void falling(void){
    if (!active) return;
    if (phase==RECEIVE&&bits==8){
        phase=SLAVE_ACK;ack_clock=0;
        slave_low=received_count!=nack_byte;
    } else if (phase==SLAVE_ACK&&ack_clock){
        slave_low=0;bits=shift=0;
        if (read_mode && received_count!=nack_byte){phase=SEND;send_bit();}
        else phase=RECEIVE;
    } else if (phase==SEND){
        if (bits==8){phase=MASTER_ACK;ack_clock=0;slave_low=0;++read_count;}
        else send_bit();
    } else if (phase==MASTER_ACK&&ack_clock){
        bits=0;
        if (master_ack[master_ack_count-1]){phase=SEND;send_bit();}
        else {phase=RECEIVE;slave_low=0;shift=0;}
    }
    check_contention();
}
static void update_scl(void){
    unsigned high=latch[1]&&!fault_scl&&now>=stretch_until;
    if (high!=wire_scl){
        wire_scl=high;
        if (high){
            if (active) {assert(now-fall_time>=5);assert(now-sda_time>=1);}
            rise_time=now;rising();
        } else {
            if (active) assert(now-rise_time>=4);
            fall_time=now;falling();
        }
    }
}
static void data_change(unsigned before){
    unsigned after=sda();
    check_contention();
    if (after!=before){
        if (wire_scl){
            assert(now-rise_time>=4);
            if (!after){
                ++starts;transaction_received=0;active=1;phase=RECEIVE;bits=shift=read_mode=ack_clock=0;slave_low=0;
            } else {++stops;active=0;slave_low=0;}
        }
        sda_time=now;
    }
}
static int index_for(GPIO_TypeDef *p,uint16_t pin){
    if(p==sda_port&&pin==sda_pin)return 0;
    assert(p==scl_port&&pin==scl_pin);return 1;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState state){
    int i=index_for(p,pin);
    if(i==0){unsigned before=sda();latch[0]=state;data_change(before);}
    else {
        if(state&& !latch[1]){
            ++clock_requests;
            if(clock_requests==fault_at)fault_scl=1;
            if(stretch_us&&(!stretch_at||clock_requests==stretch_at))stretch_until=now+stretch_us;
        }
        latch[1]=state;update_scl();
    }
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin){
    int i=index_for(p,pin);update_scl();return (i?wire_scl:sda())?GPIO_PIN_SET:GPIO_PIN_RESET;
}
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *init){
    int i=index_for(p,init->Pin);unsigned before=sda();mode[i]=init->Mode;
    if(i==0)data_change(before);
}
void delay_us(uint32_t us){now+=us;update_scl();}
void osDelay(uint32_t ms){delay_us(ms*1000);}
static void reset(unsigned pp, unsigned dac){
    sda_port=scl_port=dac?GPIOA:GPIOB;
    sda_pin=dac?DAC_SDA_Pin:(pp?UART_RX_Pin:I2C_SDA_Pin);
    scl_pin=dac?DAC_SCL_Pin:(pp?UART_TX_Pin:I2C_SCL_Pin);
    mode[0]=mode[1]=GPIO_MODE_INPUT;latch[0]=latch[1]=wire_scl=1;slave_low=0;
    now=100;rise_time=fall_time=sda_time=stretch_until=0;
    starts=stops=active=phase=bits=shift=read_mode=ack_clock=0;
    received_count=read_count=master_ack_count=recovery_clocks=0;
    nack_byte=fault_sda=fault_scl=release_after=stretch_us=stretch_at=clock_requests=fault_at=0;
    memset(received,0,sizeof received);memset(master_ack,0,sizeof master_ack);
    for(unsigned i=0;i<sizeof reply;++i)reply[i]=(uint8_t)(i*37u+0xA5u);
}
static void init(I2CInfo *bus){IIC_Init(bus,sda_port,sda_pin,scl_port,scl_pin);}
static void idle(void){assert(wire_scl&&sda()&&!active);}
static void expect_read(unsigned length){
    assert(read_count==length&&master_ack_count==length);
    for(unsigned i=0;i<length;++i)assert(master_ack[i]==(i+1<length));
}
static void od_tests(void){
    I2CInfo bus;
    uint8_t data[300], write[]={0xDA,0x06,0xA5,0x00,0xFF};
    reset(0,0);init(&bus);
    assert(mode[0]==GPIO_MODE_OUTPUT_OD&&mode[1]==GPIO_MODE_OUTPUT_OD);
    assert(I2C_WriteNByte(&bus,write,sizeof write)==0);
    assert(received_count==sizeof write&&!memcmp(received,write,sizeof write));
    assert(starts==1&&stops==1);idle();
    unsigned before=starts+stops;IIC_DeInit(&bus);assert(starts+stops==before);
    for(unsigned length=1;length<=300;length=length==1?3:(length==3?300:301)){
        reset(0,0);init(&bus);
        assert(I2C_ReadNByte(&bus,0xDA,data,length)==0&&!memcmp(data,reply,length));
        assert(received_count==1&&received[0]==0xDB&&starts==1&&stops==1);
        expect_read(length);idle();
    }
    reset(0,0);init(&bus);memset(data,0x5A,sizeof data);data[0]=0xDA;
    assert(I2C_WriteNByte(&bus,data,sizeof data)==0&&received_count==sizeof data);
    assert(!memcmp(received,data,sizeof data));idle();
    for(unsigned n=1;n<=sizeof write;++n){
        reset(0,0);init(&bus);nack_byte=n;
        assert(I2C_WriteNByte(&bus,write,sizeof write)==1&&received_count==n);
        assert(starts==1&&stops==1);idle();
    }
    reset(0,0);init(&bus);nack_byte=1;memset(data,0xFF,3);
    assert(I2C_ReadNByte(&bus,0xDA,data,3)==1&&!data[0]&&!data[1]&&!data[2]);
    assert(starts==1&&stops==1);idle();
    reset(0,0);init(&bus);stretch_us=75;
    assert(I2C_ReadNByte(&bus,0xDA,data,3)==0&&!memcmp(data,reply,3));
    expect_read(3);idle();
    /* Timeouts at address, ACK, data, master ACK and STOP all propagate. */
    unsigned faults[]={1,9,10,18,37};
    for(unsigned i=0;i<sizeof faults/sizeof faults[0];++i){
        reset(0,0);init(&bus);fault_at=faults[i];memset(data,0xFF,3);
        assert(I2C_ReadNByte(&bus,0xDA,data,3)==1&&bus.error);
        assert(!data[0]&&!data[1]&&!data[2]&&now<10000);
        assert(latch[0]&&latch[1]);
    }
    reset(0,0);init(&bus);fault_scl=1;
    assert(I2C_WriteNByte(&bus,write,sizeof write)==1&&now<2000&&!received_count);
    assert(latch[0]&&latch[1]);
    /* A slave left mid-read releases SDA within nine recovery clocks. */
    reset(0,0);init(&bus);fault_sda=1;release_after=7;
    assert(I2C_WriteNByte(&bus,write,sizeof write)==0&&recovery_clocks==7);
    assert(received_count==sizeof write&&starts==1&&stops==2);idle();
    /* A real slave interrupted in SEND can briefly release SDA on a '1'
       and drive the next '0'. Finish all nine recovery clocks before STOP. */
    reset(0,0);reply[0]=0x40;init(&bus);
    iic_start(&bus);iic_sendbyte(&bus,0xDB);assert(!iic_wait_ack(&bus));
    init(&bus); /* MCU restarted while the slave still transmits. */
    assert(!I2C_WriteNByte(&bus,write,sizeof write));
    assert(received_count==sizeof write+1&&!memcmp(received+1,write,sizeof write));idle();
    reset(0,0);init(&bus);fault_sda=1;
    assert(I2C_WriteNByte(&bus,write,sizeof write)==1&&now<2000&&!received_count);
    assert(latch[0]&&latch[1]);
    fault_sda=0;fault_scl=0;active=slave_low=0;
    assert(I2C_WriteNByte(&bus,write,sizeof write)==0);idle();
    /* Repeated START remains available through the existing low-level API. */
    reset(0,0);init(&bus);iic_start(&bus);iic_sendbyte(&bus,0xDA);assert(!iic_wait_ack(&bus));
    iic_start(&bus);iic_sendbyte(&bus,0xDB);assert(!iic_wait_ack(&bus));iic_stop(&bus);
    assert(starts==2&&stops==1&&!bus.error);idle();
    reset(0,0);init(&bus);
    assert(!I2C_ReadNByte(&bus,0xDA,NULL,0)&&!I2C_WriteNByte(&bus,NULL,0)&&!starts&&!stops);
    /* Same driver controls the DAC pins and preserves address/data bytes. */
    reset(0,1);init(&bus);uint8_t dac[]={0xC0,0x08,0x00};
    assert(!I2C_WriteNByte(&bus,dac,3)&&!memcmp(received,dac,3));idle();
    puts("PASS: open-drain byte timing, ACK/NACK, 300-byte transfers, stretching, timeout/recovery, DAC pins");
}
static void pp_tests(void){
    uint8_t write[]={0xDA,0x06,0xA5,0x00,0xFF}, data[3];
    reset(1,0);pp_iic_init(sda_port,sda_pin,scl_port,scl_pin);pp_iic_start();
    for(unsigned i=0;i<sizeof write;++i){pp_iic_sendbyte(write[i]);assert(!pp_iic_wait_ack());}
    pp_iic_stop();assert(received_count==sizeof write&&!memcmp(received,write,sizeof write));
    assert(starts==1&&stops==1);idle();
    reset(1,0);reply[1]=0x00;reply[2]=0xFF;
    pp_iic_init(sda_port,sda_pin,scl_port,scl_pin);pp_iic_start();
    pp_iic_sendbyte(0xDB);assert(!pp_iic_wait_ack());
    for(unsigned i=0;i<3;++i)data[i]=pp_iic_readbyte(i+1<3);
    pp_iic_stop();assert(!memcmp(data,reply,3));expect_read(3);
    assert(starts==1&&stops==1);idle();
    reset(1,0);pp_iic_init(sda_port,sda_pin,scl_port,scl_pin);nack_byte=1;
    pp_iic_start();pp_iic_sendbyte(0xDA);assert(pp_iic_wait_ack()==1);pp_iic_stop();
    assert(starts==1&&stops==1);idle();
    puts("PASS: push-pull byte timing, full ACK clock, STOP setup, SDA handoff without contention");
}
static void integration_tests(void){
    uint8_t data[5];uint16_t out[8];
    uint16_t cmd[]={3,0,3,0,0,0,0xDA,0x30,0x0A};
    reset(0,0);assert(ProductI2CTransfer(cmd,9,out,8)==0&&out[0]==0);
    assert(received_count==3&&received[1]==0x30&&received[2]==0x0A);idle();
    uint16_t read_cmd[]={3,0,1,0,5,0,0xDA};
    reset(0,0);assert(!ProductI2CTransfer(read_cmd,7,out,8)&&!out[5]);
    for(unsigned i=0;i<5;++i)assert(out[i]==reply[i]);expect_read(5);idle();
    reset(0,0);fault_at=10;assert(ProductI2CTransfer(read_cmd,7,out,8)==1&&out[5]==1);
    for(unsigned i=0;i<5;++i)assert(!out[i]);
    /* Trigger/select/read sequences run through both real driver variants. */
    reset(0,0);assert(!WF2200_GetData(data,5)&&!memcmp(data,reply,5));
    assert(starts==3&&stops==3&&received_count==6);expect_read(5);idle();
    reset(0,0);fault_at=60;assert(WF2200_GetData(data,5)==1);
    for(unsigned i=0;i<5;++i)assert(!data[i]);
    reset(1,0);assert(!PP_WF2200_GetData(data,5)&&!memcmp(data,reply,5));
    assert(starts==3&&stops==3&&received_count==6);expect_read(5);idle();
    puts("PASS: actual product adapter and WF3050/WF2200 acquisition, including read timeout status");
}
int main(void){od_tests();pp_tests();integration_tests();return 0;}
'''

DELAY_HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t elapsed, reads, jump_read, jump_us;
static uint16_t timer;
static uint32_t counter_get(void){
    unsigned advance=(++reads==jump_read)?jump_us:1;
    elapsed+=advance;timer=(uint16_t)(timer+advance);return timer;
}
#define __HAL_TIM_GET_COUNTER(t) counter_get()
/* Exact production function from main.c is inserted here. */
%s
static void check(uint32_t us,uint16_t initial,unsigned interrupt){
    timer=initial;elapsed=reads=0;jump_read=interrupt?3:0;jump_us=100;
    delay_us(us);
    assert(elapsed>=us&&elapsed<=us+101);
}
int main(void){
    check(0,0,0);check(1,0,0);check(20,0,0);check(20,65530,0);
    check(20,65500,1);check(70000,65000,0);
    puts("PASS: actual microsecond delay, wraparound, interrupt deadline crossing and long delays");
    return 0;
}
'''

def run():
    with tempfile.TemporaryDirectory(prefix="adc-i2c-test-") as directory:
        tmp = Path(directory)
        for name, text in HEADERS.items():
            (tmp / name).write_text(text)
        source = tmp / "i2c_slave.c"
        source.write_text(HARNESS)
        binary = tmp / "i2c_slave"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-I" + str(tmp), "-I" + str(ROOT / "Core/Inc"),
                        str(source), *(str(ROOT / "Core/Src" / (name + ".c"))
                                       for name in ["SoftIIC", "SoftIIC_PP", "I2CHelper"]),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=20)
        main = (ROOT / "Core/Src/main.c").read_text(encoding="latin-1")
        start = main.index("void delay_us(uint32_t us)")
        end = main.index("\n}\n", start) + 2
        delay_source = tmp / "delay.c"
        delay_source.write_text(DELAY_HARNESS % main[start:end])
        delay_binary = tmp / "delay"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", str(delay_source), "-o", str(delay_binary)], check=True)
        subprocess.run([str(delay_binary)], check=True, timeout=20)

if __name__ == "__main__":
    run()
