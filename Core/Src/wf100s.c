
#include "wf100s.h"
#include "cmsis_os.h"
#include <string.h>
#include "ADS1220.h"


static int ADC_Value_100s = 0;

void AllTestFor100S(uint16_t *out, uint16_t out_lens){
  uint8_t saved_config;
  uint16_t supply_on[] = {1, TYPE1_SET_VDD, WF100S_TEST_VDD_MV & 0xFFu, WF100S_TEST_VDD_MV >> 8};
  uint16_t supply_off[] = {1, TYPE1_SET_VDD, 0, 0};
  uint16_t supply_result[1];
  if (out == NULL || out_lens < WF100S_RESULT_LENGTH) return;
  memset(out, 0, out_lens * sizeof(*out));
  saved_config = ADS1220.config[0];
	ParseSystemCmd(supply_on, 4, supply_result, 1);
	ADS1220_Init(&ADS1220);
  osDelay(150);
	do
	{
		// Working current
		ADS1220.config[0] = 0x01;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			out[0] = (uint8_t)(ADC_Value_100s);
			out[1] = (uint8_t)(ADC_Value_100s>>8);
			out[2] = (uint8_t)(ADC_Value_100s>>16);
			out[3] = (uint8_t)(ADC_Value_100s>>24);
			if(ADC_Value_100s)break;
		}

		// AIN2 voltage
		ADS1220.config[0] = 0xA1;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			out[4] = (uint8_t)(ADC_Value_100s);
			out[5] = (uint8_t)(ADC_Value_100s>>8);
			out[6] = (uint8_t)(ADC_Value_100s>>16);
			out[7] = (uint8_t)(ADC_Value_100s>>24);
			if(ADC_Value_100s)break;
		}

		// Restore current channel
		ADS1220.config[0] = 0x01;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			if(ADC_Value_100s)break;
		}

		// AIN3 voltage
		ADS1220.config[0] = 0xB1;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			out[8] = (uint8_t)(ADC_Value_100s);
			out[9] = (uint8_t)(ADC_Value_100s>>8);
			out[10] = (uint8_t)(ADC_Value_100s>>16);
			out[11] = (uint8_t)(ADC_Value_100s>>24);
			if(ADC_Value_100s)break;
		}
		// Restore current channel
		ADS1220.config[0] = 0x01;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			if(ADC_Value_100s)break;
		}

		// Differential voltage
		ADS1220.config[0] = 0x51;
		ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
		osDelay(20);
		for(uint8_t i = 0;i<10;i++)
		{
			ADC_Value_100s = GET_ADS1220_Val(&ADS1220);
			out[12] = (uint8_t)(ADC_Value_100s);
			out[13] = (uint8_t)(ADC_Value_100s>>8);
			out[14] = (uint8_t)(ADC_Value_100s>>16);
			out[15] = (uint8_t)(ADC_Value_100s>>24);
			if(ADC_Value_100s)break;
		}
	}
	while(0);
	ADS1220.config[0] = saved_config;
  ADS1220_WriteRegister(0x00, 1, &ADS1220.config[0]);
  ParseSystemCmd(supply_off, 4, supply_result, 1);
}
