#include "led.h"

#define LED_RUN_STATUS_GPIO                 CONFIG_LED_RUN_STATUS_GPIO
#define LED_ALM_STATUS_GPIO                 CONFIG_LED_ALM_STATUS_GPIO
#define LED_NET_STATUS_GPIO                 CONFIG_LED_NET_STATUS_GPIO

#define FAN_GPIO                            CONFIG_FAN_CTRL_GPIO
#define LED_ACTIVE_LEVEL                    1

led_indicator_handle_t s_led_run_status_handle = NULL;
led_indicator_handle_t s_led_alm_status_handle = NULL;
led_indicator_handle_t s_led_net_status_handle = NULL;

void _led_indicator_init()
{
    led_indicator_gpio_config_t led_indicator_gpio_config = {
        .is_active_level_high = LED_ACTIVE_LEVEL,
    };

    led_indicator_config_t led_config = {
        .led_indicator_gpio_config = &led_indicator_gpio_config,
        .mode = LED_GPIO_MODE,
    };

    if (LED_RUN_STATUS_GPIO) {
        led_indicator_gpio_config.gpio_num = LED_RUN_STATUS_GPIO;
        s_led_run_status_handle = led_indicator_create(&led_config);
        assert(s_led_run_status_handle != NULL);
    }
    if (LED_ALM_STATUS_GPIO) {
        led_indicator_gpio_config.gpio_num = LED_ALM_STATUS_GPIO;
        s_led_alm_status_handle = led_indicator_create(&led_config);
        assert(s_led_alm_status_handle != NULL);
        // led_indicator_stop(s_led_alm_status_handle, BLINK_CONNECTED);
        // led_indicator_start(s_led_alm_status_handle, BLINK_CONNECTING);
    }
    if (LED_NET_STATUS_GPIO) {
        led_indicator_gpio_config.gpio_num = LED_NET_STATUS_GPIO;
        s_led_net_status_handle = led_indicator_create(&led_config);
        assert(s_led_net_status_handle != NULL);
        led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTED);
        led_indicator_start(s_led_net_status_handle, BLINK_CONNECTING);
    }
}


void fan_gpio_init(void)
{
    // 配置 LED 引脚为输出模式
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << FAN_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

void fan_gpio_set(bool on)
{
    gpio_set_level(FAN_GPIO, on ? 1 : 0);
}





