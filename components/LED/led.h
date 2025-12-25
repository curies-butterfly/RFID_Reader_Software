#ifndef LED__H_
#define LED__H_

#include "led_indicator.h"
#include "led_indicator_blink_default.h"

typedef enum {
    NET_MODE_4G,
    NET_MODE_ETH,
    NET_MODE_LORA,
} net_mode_t;


extern led_indicator_handle_t s_led_run_status_handle;
extern led_indicator_handle_t s_led_alm_status_handle;
extern led_indicator_handle_t s_led_net_status_handle ;

void _led_indicator_init();
void fan_gpio_init(void);
void fan_gpio_set(bool on);
void led_net_set_mode(net_mode_t mode);


#endif