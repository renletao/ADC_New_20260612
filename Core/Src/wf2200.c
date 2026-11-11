
#include "wf2200.h"
#include <string.h>
#include "SoftIIC.h"
#include "cmsis_os.h"
#include "I2CHelper.h"
#include "SoftIIC_PP.h"
#include "ADS1220.h"

/************WF2200****************/
  static const uint16_t iic_write_reset[] = { 3, 0, 3, 0, 0, 0, 0xDA, 0x00, 0x24 }; // Latest format: type, reserved, send_len, reserved, recv_len, reserved, address, payload
	static const uint16_t iic_write_reg_start[] = { 3, 0, 2, 0, 0, 0, 0xDA, 0xA4 }; // Latest format: type, reserved, send_len, reserved, recv_len, reserved, address, payload
	static const uint16_t iic_read_reg[] = { 3, 0, 1, 0, 4, 0, 0xDA };// Latest format: type, reserved, send_len, reserved, recv_len, reserved, address, payload
	static const uint16_t iic_write_otp_start[] = { 3, 0, 2, 0, 0, 0, 0xDA, 0xAE };
	static const uint16_t iic_read_otp[] = { 3, 0, 1, 0, 14, 0, 0xDA };
	static const uint16_t iic_write_data_cmd1[] = { 3, 0, 3, 0, 0, 0, 0xDA, 0x30, 0x0A };
  static const uint16_t iic_write_data_cmd2[] = { 3, 0, 3, 0, 0, 0, 0xDA, 0x30, 0x0B };
	static const uint16_t iic_write_data_start[] = { 3, 0, 2, 0, 0, 0, 0xDA, 0x06 };
	static const uint16_t iic_read_data[] = { 3, 0, 1, 0, 5, 0, 0xDA };
/**********************************/

 static const uint16_t sys_adc_2200[] = { 0, TYPE1_GET_ADC };
 static const uint16_t sys_vdd_off_2200[] = { 1, TYPE1_SET_VDD, 0, 0 };
 static const uint16_t sys_vdd_on_2200[] = { 1, TYPE1_SET_VDD, (PRODUCT_TEST_VDD_MV & 0xFF), (PRODUCT_TEST_VDD_MV >> 8) };

void AllTestFor2200(uint16_t *out, uint16_t out_lens){
  if (out == NULL || out_lens < WF2200_RESULT_LENGTH) return;
  memset(out, 0, out_lens * sizeof(*out));
	uint16_t _out[16]={0};
  uint8_t i;
	//oppen power
  ParseSystemCmd(sys_vdd_on_2200, 4, _out, 4);
	ADS1220_Init(&ADS1220);
  IIC_Init(&i2c1, I2C_SDA_GPIO_Port, I2C_SDA_Pin, I2C_SCL_GPIO_Port, I2C_SCL_Pin);
  osDelay(50);
  ProductI2CTransfer(iic_write_reset, sizeof(iic_write_reset) / sizeof(iic_write_reset[0]), _out, sizeof(_out) / sizeof(_out[0]));
  osDelay(100);
  //0~3 | 4~26 | 27~49 | 50~72 | 73~76
  //sleep current  ParseSystemCmd
  ParseSystemCmd(sys_adc_2200, 2, _out, 4);
  CopyAndClear(_out, &out[0], 4);
  for(i=0; i<3; i++) {
    //id          ParseI2CCmd
    ProductI2CTransfer(iic_write_reg_start, sizeof(iic_write_reg_start) / sizeof(iic_write_reg_start[0]), _out, sizeof(_out) / sizeof(_out[0]));
    ProductI2CTransfer(iic_read_reg, sizeof(iic_read_reg) / sizeof(iic_read_reg[0]), _out, sizeof(_out) / sizeof(_out[0]));
    CopyAndClear(_out, &out[4+23*i], 4);

    //otp data    ParseI2CCmd
    ProductI2CTransfer(iic_write_otp_start, sizeof(iic_write_otp_start) / sizeof(iic_write_otp_start[0]), _out, sizeof(_out) / sizeof(_out[0]));
    ProductI2CTransfer(iic_read_otp, sizeof(iic_read_otp) / sizeof(iic_read_otp[0]), _out, sizeof(_out) / sizeof(_out[0]));
    CopyAndClear(_out, &out[8+23*i], 14);

    //out data    ParseI2CCmd
    if(i==2) ProductI2CTransfer(iic_write_data_cmd2, sizeof(iic_write_data_cmd2) / sizeof(iic_write_data_cmd2[0]), _out, sizeof(_out) / sizeof(_out[0]));
    else ProductI2CTransfer(iic_write_data_cmd1, sizeof(iic_write_data_cmd1) / sizeof(iic_write_data_cmd1[0]), _out, sizeof(_out) / sizeof(_out[0]));
    osDelay(5);

    //read temp and press
    ProductI2CTransfer(iic_write_data_start, sizeof(iic_write_data_start) / sizeof(iic_write_data_start[0]), _out, sizeof(_out) / sizeof(_out[0]));
    ProductI2CTransfer(iic_read_data, sizeof(iic_read_data) / sizeof(iic_read_data[0]), _out, sizeof(_out) / sizeof(_out[0]));
    CopyAndClear(_out, &out[22+23*i], 5);
  }
  //work current     ParseSystemCmd
  ParseSystemCmd(sys_adc_2200, 2, _out, 4);
  CopyAndClear(_out, &out[73], 4);

	//Close power
	ParseSystemCmd(sys_vdd_off_2200, 4, _out, 4);

}


void WF3050FtData(uint16_t *out, uint16_t out_lens) {
  if (out == NULL || out_lens < 12) return;
  memset(out, 0, out_lens * sizeof(*out));
  uint16_t _out[16]={0};
  uint8_t i;
  uint8_t j;
  uint8_t buf[32] = {0};
	//oppen power
  ParseSystemCmd(sys_vdd_on_2200, 4, _out, 4);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  osDelay(150);

  do{
      i=0;
      //ref sensor
      pp_iic_init(GPIOB, GPIO_PIN_7, GPIOB, GPIO_PIN_6);
      if ((out[i++] = PP_WF2200_GetData(buf, 0x05)) != 0) {
       // break;
      }
      for (j = 0; j < 0x05; j++) {
        out[i++] = buf[j];
      }

      //ft sensor
      IIC_Init(&i2c1, I2C_SDA_GPIO_Port, I2C_SDA_Pin, I2C_SCL_GPIO_Port, I2C_SCL_Pin);
      if ((out[i++] = WF2200_GetData(buf, 0x05)) != 0) {
       // break;
      }
      for (j = 0; j < 0x05; j++) {
        out[i++] = buf[j];
      }
    } while(0);

    //Close power
    GPIO_InitStruct.Pin = UART_TX_Pin | UART_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(UART_TX_GPIO_Port, &GPIO_InitStruct);
	ParseSystemCmd(sys_vdd_off_2200, 4, _out, 4);
}
