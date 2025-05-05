/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-03-30     chenyong     first version
 * 2018-08-17     chenyong     multiple client support
 * 2022-02-17     denghengli   ported to freeRTos  
 */
#ifndef __AT_H__
#define __AT_H__

#include "at_utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RT-Thread error code definitions */
#define RT_EOK                          0               /**< There is no error */
#define RT_ERROR                        1               /**< A generic error happens */
#define RT_ETIMEOUT                     2               /**< Timed out */
#define RT_EFULL                        3               /**< The resource is full */
#define RT_EEMPTY                       4               /**< The resource is empty */
#define RT_ENOMEM                       5               /**< No memory */
#define RT_ENOSYS                       6               /**< No system */
#define RT_EBUSY                        7               /**< Busy */
#define RT_EIO                          8               /**< IO error */
#define RT_EINTR                        9               /**< Interrupted system call */
#define RT_EINVAL                       10              /**< Invalid argument */

/* the maximum number of supported AT clients */
#define AT_CLIENT_NUM_MAX              2
/* the maximum len of AT CMD */
#define AT_CMD_MAX_LEN                 256


extern QueueHandle_t uart_event_queue;

enum at_status
{
    AT_STATUS_UNINITIALIZED = 0,
    AT_STATUS_INITIALIZED,
    AT_STATUS_CLI,
};
typedef enum at_status at_status_t;

enum at_resp_status
{
     AT_RESP_OK = 0,                   /* AT response end is OK */
     AT_RESP_ERROR = -1,               /* AT response end is ERROR */
     AT_RESP_TIMEOUT = -2,             /* AT response is timeout */
     AT_RESP_BUFF_FULL= -3,            /* AT response buffer is full */
};
typedef enum at_resp_status at_resp_status_t;


////
typedef enum {
    UART_INDEX_0,  // ESP32 UART0
    UART_INDEX_1,  // ESP32 UART1
    UART_INDEX_2,  // ESP32 UART2
    UART_INDEX_ALL
} UART_INDEX_E;

typedef int (*uart_rx_callback_t)(UART_INDEX_E uart_index, char *recv_data, int recv_len);

typedef struct {
    uart_port_t uart_port;
    QueueHandle_t uart_queue;
    uart_rx_callback_t rx_callback;
    TaskHandle_t task_handle;
} esp32_uart_t;

static esp32_uart_t esp32_uart_handles[UART_INDEX_ALL];
////

struct at_response
{
    /* response buffer */
    char *buf;
    /* the maximum response buffer size, it set by `at_create_resp()` function */
    uint16_t buf_size;
    /* the length of current response buffer */
    uint16_t buf_len;
    /* the number of setting response lines, it set by `at_create_resp()` function
     * == 0: the response data will auto return when received 'OK' or 'ERROR'
     * != 0: the response data will return when received setting lines number data */
    uint16_t line_num;
    /* the count of received response lines */
    uint16_t line_counts;
    /* the maximum response time */
    uint32_t timeout;
};
typedef struct at_response *at_response_t;

struct at_client;

/* URC(Unsolicited Result Code) object, such as: 'RING', 'READY' request by AT server */
struct at_urc
{
    const char *cmd_prefix;
    const char *cmd_suffix;
    void (*func)(struct at_client *client, const char *data, uint16_t size);
};
typedef struct at_urc *at_urc_t;

struct at_urc_table
{
    size_t urc_size;
    const struct at_urc *urc;
};
typedef struct at_urc *at_urc_table_t;

struct at_client
{    
    uart_port_t device;
    at_status_t status;
    char end_sign;

    /* the current received one line data buffer */
    char *recv_line_buf;
    /* The length of the currently received one line data */
    uint16_t recv_line_len;
    /* The maximum supported receive one line data length */
    uint16_t recv_line_size;
    SemaphoreHandle_t  rx_notice;
    SemaphoreHandle_t  lock;

    at_response_t resp;
    SemaphoreHandle_t resp_notice;
    at_resp_status_t resp_status;

    struct at_urc_table *urc_table;
    uint16_t urc_table_size;

    /* uart receive queue */
    struct array_queue *recv_q;
    /* The maximum supported receive data length */
    uint16_t recv_queue_size;

    /* uart receive complete deley timer */
    uart_port_t uart_index;

    /* handle task */
    TaskHandle_t parser;
};
typedef struct at_client *at_client_t;

/* get AT client object */
at_client_t at_client_get_first(void);
/* AT client initialize and start*/
at_client_t at_client_init(uart_port_t uart_index, uint16_t recv_line_size, uint16_t recv_queue_size);


void set_uart_rx_indicate(UART_INDEX_E uart_index, uart_rx_callback_t rx_callback);

/* AT client send or receive data */
int at_client_obj_send(at_client_t client, char *buf, int size);
int at_client_obj_recv(at_client_t client, char *buf, int size, uint32_t timeout);
/* AT client send commands to AT server and waiter response */
int at_obj_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...);

/* set AT client a line end sign */
void at_obj_set_end_sign(at_client_t client, char ch);
/* Set URC(Unsolicited Result Code) table */
int at_obj_set_urc_table(at_client_t client, const struct at_urc * table, int size);
at_client_t at_client_get(uart_port_t uart_index);

/* AT response object create and delete */
at_response_t at_create_resp(uint16_t buf_size, uint16_t line_num, uint32_t timeout);
void at_delete_resp(at_response_t resp);
at_response_t at_resp_set_info(at_response_t resp, int buf_size, int line_num, uint32_t timeout);

/* AT response line buffer get and parse response buffer arguments */
const char *at_resp_get_line(at_response_t resp, int resp_line);
const char *at_resp_get_line_by_kw(at_response_t resp, const char *keyword);
int at_resp_parse_line_args(at_response_t resp, int resp_line, const char *resp_expr, ...);
int at_resp_parse_line_args_by_kw(at_response_t resp, const char *keyword, const char *resp_expr, ...);

/* ========================== single AT client function ============================ */
/**
 * NOTE: These functions can be used directly when there is only one AT client.
 * If there are multiple AT Client in the program, these functions can operate on the first initialized AT client.
 */
#define at_exec_cmd(resp, ...)                   at_obj_exec_cmd(at_client_get_first(), resp, __VA_ARGS__)
#define at_client_wait_connect(timeout)          at_client_obj_wait_connect(at_client_get_first(), timeout)
#define at_client_send(buf, size)                at_client_obj_send(at_client_get_first(), buf, size)
#define at_client_recv(buf, size, timeout)       at_client_obj_recv(at_client_get_first(), buf, size, timeout)
#define at_set_end_sign(ch)                      at_obj_set_end_sign(at_client_get_first(), ch)
#define at_set_urc_table(urc_table, table_sz)    at_obj_set_urc_table(at_client_get_first(), urc_table, table_sz)

#ifdef __cplusplus
}
#endif

#endif
