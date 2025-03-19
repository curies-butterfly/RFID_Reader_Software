#ifndef TP1107__H_
#define TP1107__H_

#include <stdint.h>
#include "driver/gpio.h"  // 引入 ESP-IDF GPIO 驱动
#include "chry_ringbuffer.h"

typedef struct
{
    gpio_num_t  wake_pin;
    gpio_num_t  reset_pin;
    gpio_num_t  read_pin;

    volatile uint8_t byte_buffer;
    volatile uint8_t cmd_rx_flag;
    uint8_t *cmd_rx_buffer;
    volatile uint8_t cmd_rx_buffer_tail;

    chry_ringbuffer_t rb_read;
    uint8_t *rb_buf;
    uint16_t rb_buf_size;

    chry_ringbuffer_t rb_msg;
    uint8_t *rb_buf_msg;

    uint16_t cmd_buf_size;

    volatile uint8_t recv_timeout;
    // tp1107_result_t result;//

    uint8_t *message_rx_buffer;
    //UART_HandleTypeDef *uart_handle;
} TP1107_HandleTypedef;

uint8_t TP1107_ATGTIMR(TP1107_HandleTypedef *ins);

#endif
