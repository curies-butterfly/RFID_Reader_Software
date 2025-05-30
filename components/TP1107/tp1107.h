#ifndef TP1107__H_
#define TP1107__H_

#include <stdint.h>
#include "driver/gpio.h"  // 引入 ESP-IDF GPIO 驱动
#include "at.h"
// #include "chry_ringbuffer.h"

typedef struct at_device_tp1107 {
    char *device_name;
    char *client_name;
    uint8_t net_joined;
    int rst_pin;
    int wake_pin;
    char ESN[13];
    at_response_t at_resp;
    at_client_t client;  // 添加client成员
    void *user_data;
} at_device_tp1107;


void TP1107_Init();
void basic_io_init(at_device_tp1107 *unb_device);
void basic_hard_wake_up_operate(at_device_tp1107 *unb_device, uint8_t state);
void basic_hard_rst_operate(at_device_tp1107 *unb_device);
int unb_tp1107_rea_init(at_device_tp1107 *unb_device, const struct at_urc urc_temp[]);
int unb_tp1107_init(void);
void urc_func1(struct at_client *client, const char *data, size_t  size);
void urc_func2(struct at_client *client, const char *data, size_t  size);
// int unb_tp1107_rea_init(at_device_tp1107 *unb_device, const struct at_urc urc_temp[]);


#endif
