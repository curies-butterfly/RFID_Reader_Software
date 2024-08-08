#ifndef     ntc_temp_adc__H_
#define     ntc_temp_adc__H_

#include <stdio.h>
#include "esp_log.h"


void ntc_init(void);
uint32_t adc_get_result_average(uint32_t ch, uint32_t times);
void ntc_temp_adc_run(void);

extern float T_C;
extern int temp;

#endif