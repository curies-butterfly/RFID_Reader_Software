#ifndef LED__H_
#define LED__H_

#include "led_indicator.h"
#include "led_indicator_blink_default.h"


extern led_indicator_handle_t s_led_run_status_handle;
extern led_indicator_handle_t s_led_alm_status_handle;
extern led_indicator_handle_t s_led_net_status_handle ;

void _led_indicator_init();
void fan_gpio_init(void);
void fan_gpio_set(bool on);

#endif