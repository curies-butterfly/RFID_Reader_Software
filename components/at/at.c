/*
 * Copyright (c) 20019-2020, wanweiyingchuang
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-02-18     denghengli   the first version
 */

#include "at.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"


#define AT_RESP_END_OK_UPCASE          "OK"
#define AT_RESP_END_OK_LWCASE          "ok"
#define AT_RESP_END_ERROR              "ERROR"
#define AT_RESP_END_FAIL               "FAIL"
#define AT_END_CR_LF                   "\r\n"

static struct at_client at_client_table[AT_CLIENT_NUM_MAX]={0};
const char *DEBUG_AT="AT_debug";
const char *MODEL_Received="MODEL_Received";
QueueHandle_t uart_event_queue;

/**
 * Create response object.
 *
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return != NULL: response object
 *          = NULL: no memory
 */
at_response_t at_create_resp(uint16_t buf_size, uint16_t line_num, uint32_t timeout)
{
    at_response_t resp = NULL;

    resp = (at_response_t) pvPortMalloc(sizeof(struct at_response));
    if (resp == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT create response object failed! No memory for response object!");
        return NULL;
    }
    memset(resp, 0, sizeof(struct at_response));

    resp->buf = (char *) pvPortMalloc(buf_size);
    if (resp->buf == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT create response object failed! No memory for response buffer!");
        vPortFree(resp);
        return NULL;
    }

    resp->buf_size = buf_size;
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->timeout = timeout;
    ESP_LOGI(DEBUG_AT, "AT create response object success! Response buffer size: %d, line number: %d, timeout: %ld ms.", buf_size, line_num, timeout);
    return resp;
}

/**
 * Delete and free response object.
 *
 * @param resp response object
 */
void at_delete_resp(at_response_t resp)
{
    if (resp && resp->buf)
    {
        vPortFree(resp->buf);
    }

    if (resp)
    {
        vPortFree(resp);
        resp = NULL;
    }
}

/**
 * Set response object information
 *
 * @param resp response object
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return  != NULL: response object
 *           = NULL: no memory
 */
at_response_t at_resp_set_info(at_response_t resp, int buf_size, int line_num, uint32_t timeout)
{
    char *p_temp = NULL;
    
    assert(resp);

    if (resp->buf_size != buf_size)
    {
        resp->buf_size = buf_size;

        vPortFree(resp->buf);
        p_temp = (char *) pvPortMalloc(buf_size);
        if (p_temp == NULL)
        {
            ESP_LOGI(DEBUG_AT, "No memory for realloc response buffer size(%d).", buf_size);
            return NULL;
        }
        else
        {
            resp->buf = p_temp;
            memset(p_temp, 0, buf_size);
        }
    }

    resp->line_num = line_num;
    resp->timeout = timeout;

    return resp;
}

/**
 * Get one line AT response buffer by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 *
 * @return != NULL: response line buffer
 *          = NULL: input response line error
 */
const char *at_resp_get_line(at_response_t resp, int resp_line)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = NULL;
    int line_num = 1;

    assert(resp);

    if (resp_line > resp->line_counts || resp_line <= 0)
    {
        ESP_LOGI(DEBUG_AT, "AT response get line failed! Input response line(%d) error!", resp_line);
        return NULL;
    }

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (resp_line == line_num)
        {
            resp_line_buf = resp_buf;
            /* remove the last "\r\n". '\n' has been set to '\0' in line parsing */
            if (resp_line_buf[strlen(resp_buf) - 1] == '\r')
            {
                resp_line_buf[strlen(resp_buf) - 1] = '\0';
            }
                
            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get one line AT response buffer by keyword
 *
 * @param resp response object
 * @param keyword query keyword
 *
 * @return != NULL: response line buffer
 *          = NULL: no matching data
 */
const char *at_resp_get_line_by_kw(at_response_t resp, const char *keyword)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = NULL;
    int line_num = 1;

    assert(resp);
    assert(keyword);

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (strstr(resp_buf, keyword))
        {
            resp_line_buf = resp_buf;
            /* remove the last "\r\n". '\n' has been set to '\0' in line parsing */
            if (resp_line_buf[strlen(resp_buf) - 1] == '\r')
            {
                resp_line_buf[strlen(resp_buf) - 1] = '\0';
            }
            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get and parse AT response buffer arguments by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 * @param resp_expr response buffer expression
 *
 * @return -1 : input response line number error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args(at_response_t resp, int resp_line, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = NULL;

    assert(resp);
    assert(resp_expr);

    if ((resp_line_buf = at_resp_get_line(resp, resp_line)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Get and parse AT response buffer arguments by keyword.
 *
 * @param resp response object
 * @param keyword query keyword
 * @param resp_expr response buffer expression
 *
 * @return -1 : input keyword error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args_by_kw(at_response_t resp, const char *keyword, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = NULL;

    assert(resp);
    assert(resp_expr);

    if ((resp_line_buf = at_resp_get_line_by_kw(resp, keyword)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Send commands to AT server and wait response.
 *
 * @param client current AT client object
 * @param resp AT response object, using NULL when you don't care response
 * @param cmd_expr AT commands expression
 *
 * @return 0 : success
 *        -1 : response status error
 *        -2 : wait timeout
 *        -7 : enter AT CLI mode
 * result = at_exec_cmd(resp, "AT+CIFSR");
 */
int at_obj_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...)
{
    va_list args;
    uint16_t cmd_size = 0;
    int result = RT_EOK;
    const char *cmd = NULL;

    // ESP_LOGI(DEBUG_AT, "at_obj_exec_cmd > enter!");

    if (client == NULL)
    {
        ESP_LOGI(DEBUG_AT, "at_obj_exec_cmd > input AT Client object is NULL, please create or get AT Client object!");
        return -RT_ERROR;
    }

    // ESP_LOGI(DEBUG_AT, "at_obj_exec_cmd > check AT Client object status!");


    /* check AT CLI mode */
    if (client->status == AT_STATUS_CLI && resp)
    {
        return -RT_EBUSY;
    }

    xSemaphoreTake(client->lock, portMAX_DELAY);

    client->resp_status = AT_RESP_OK;
    client->resp = resp;

    // ESP_LOGI(DEBUG_AT, "at_obj_exec_cmd >>>");

    if (resp != NULL)
    {
        resp->buf_len = 0;
        resp->line_counts = 0;
    }

    /* clear the uart receive queue */
    array_queue_clear(client->recv_q);

    /* clear the current received one line data buffer, Ignore dirty data before transmission */
    memset(client->recv_line_buf, 0x00, client->recv_line_size);
    client->recv_line_len = 0;

    /* send data */
    va_start(args, cmd_expr);
    // ESP_LOGI(DEBUG_AT, "at_obj_exec_cmd > send command!");
    ESP_LOGI(DEBUG_AT,"cmd_expr: %s",cmd_expr);
    at_vprintfln(client->uart_index, cmd_expr, args);
    va_end(args);

   
    // ESP_LOG_BUFFER_HEX(DEBUG_AT, resp->buf, resp->buf_size);

    if (resp != NULL)
    {
        if (xSemaphoreTake(client->resp_notice, resp->timeout) != pdTRUE)
        {
            cmd = at_get_last_cmd(&cmd_size);
            ESP_LOGI(DEBUG_AT, "execute command (%.*s) timeout (%lu ticks)!", cmd_size, cmd, resp->timeout);
            client->resp_status = AT_RESP_TIMEOUT;
            result = -RT_ETIMEOUT;
            goto __exit;
        }
        if (client->resp_status != AT_RESP_OK)
        {
            cmd = at_get_last_cmd(&cmd_size);
            ESP_LOGI(DEBUG_AT, "execute command (%.*s) failed!", cmd_size, cmd);
            result = -RT_ERROR;
            goto __exit;
        }
    }

__exit:
    client->resp = NULL;

    xSemaphoreGive(client->lock);

    return result;
}

/**
 * Send data to AT server, send data don't have end sign(eg: \r\n).
 *
 * @param client current AT client object
 * @param buf   send data buffer
 * @param size  send fixed data size
 *
 * @return >0: send data size
 *         =0: send failed
 */
int at_client_obj_send(at_client_t client, char *buf, int size)
{
    assert(buf);

    if (client == NULL)
    {
        ESP_LOGI(DEBUG_AT, "at_client_obj_send > input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("sendline", buf, size);
#endif

    xSemaphoreTake(client->lock, portMAX_DELAY);

    uart_write_bytes(client->uart_index, (unsigned char *)buf, size);

    xSemaphoreGive(client->lock);
    
    return size;
}

static int at_client_getchar(at_client_t client, char *ch, uint32_t timeout)
{
    /* getchar */
    while (array_queue_dequeue(client->recv_q, ch) != 0)
    {
        if (xSemaphoreTake(client->rx_notice, timeout) != pdTRUE)
        {
            return -RT_ETIMEOUT;
        }
    }

    return RT_EOK;
}

/**
 * AT client receive fixed-length data.
 *
 * @param client current AT client object
 * @param buf   receive data buffer
 * @param size  receive fixed data size
 * @param timeout  receive data timeout (ms)
 *
 * @note this function can only be used in execution function of URC data
 *
 * @return >0: receive data size
 *         =0: receive failed
 */
int at_client_obj_recv(at_client_t client, char *buf, int size, uint32_t timeout)
{
    int read_idx = 0;
    int result = RT_EOK;
    char ch;

    assert(buf);

    if (client == NULL)
    {
        ESP_LOGI(DEBUG_AT, "at_client_obj_recv > input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

    while (1)
    {
        if (read_idx < size)
        {
            result = at_client_getchar(client, &ch, timeout);
            if (result != RT_EOK)
            {
                ESP_LOGI(DEBUG_AT, "AT Client receive failed, uart device get data error(%d)", result);
                return 0;
            }

            buf[read_idx++] = ch;
        }
        else
        {
            break;
        }
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("urc_recv", buf, size);
#endif

    return read_idx;
}

/**
 *  AT client set end sign.
 *
 * @param client current AT client object
 * @param ch the end sign, can not be used when it is '\0'
 */
void at_obj_set_end_sign(at_client_t client, char ch)
{
    if (client == NULL)
    {
        ESP_LOGI(DEBUG_AT, "at_obj_set_end_sign > input AT Client object is NULL, please create or get AT Client object!");
        return;
    }

    client->end_sign = ch;
}

/**
 * set URC(Unsolicited Result Code) table
 *
 * @param client current AT client object
 * @param table URC table
 * @param size table size
 */
int at_obj_set_urc_table(at_client_t client, const struct at_urc *urc_table, int table_sz)
{
    int idx;

    if (client == NULL)
    {
        ESP_LOGI(DEBUG_AT, "at_obj_set_urc_table > input AT Client object is NULL, please create or get AT Client object!");
        return -RT_ERROR;
    }

    for (idx = 0; idx < table_sz; idx++)
    {
        assert(urc_table[idx].cmd_prefix);
        assert(urc_table[idx].cmd_suffix);
    }

    if (client->urc_table_size == 0)
    {
        client->urc_table = (struct at_urc_table *) pvPortMalloc(sizeof(struct at_urc_table));
        if (client->urc_table == NULL)
        {
            return -RT_ENOMEM;
        }

        memset(client->urc_table, 0, 1 * sizeof(struct at_urc_table));
        client->urc_table[0].urc = urc_table;
        client->urc_table[0].urc_size = table_sz;
        client->urc_table_size++;
    }
    else
    {
        struct at_urc_table *old_urc_table = NULL;
        uint16_t old_table_size = client->urc_table_size * sizeof(struct at_urc_table);

        old_urc_table = (struct at_urc_table *) pvPortMalloc(old_table_size);
        if (old_urc_table == NULL)
        {
            return -RT_ENOMEM;
        }
        memcpy(old_urc_table, client->urc_table, old_table_size);

        /* realloc urc table space */
        client->urc_table = (struct at_urc_table *) pvPortMalloc(old_table_size + sizeof(struct at_urc_table));
        if (client->urc_table == NULL)
        {
            client->urc_table = old_urc_table;
            return -RT_ENOMEM;
        }
        memcpy(client->urc_table, old_urc_table, old_table_size);
        client->urc_table[client->urc_table_size].urc = urc_table;
        client->urc_table[client->urc_table_size].urc_size = table_sz;
        client->urc_table_size++;
        
        vPortFree(old_urc_table);
    }
    ESP_LOGI(DEBUG_AT, "AT Client set URC table success!");
    return RT_EOK;
}

/**
 * get first AT client object in the table.
 *
 * @return AT client object
 */
at_client_t at_client_get_first(void)
{
    if (at_client_table[0].status != AT_STATUS_INITIALIZED)
    {
        return NULL;
    }

    return &at_client_table[0];
}

static const struct at_urc *get_urc_obj(at_client_t client)
{
    int i, j, prefix_len, suffix_len;
    int bufsz;
    char *buffer = NULL;
    const struct at_urc *urc = NULL;
    struct at_urc_table *urc_table = NULL;

    if (client->urc_table == NULL)
    {
        return NULL;
    }

    buffer = client->recv_line_buf;
    bufsz = client->recv_line_len;

    for (i = 0; i < client->urc_table_size; i++)
    {
        for (j = 0; j < client->urc_table[i].urc_size; j++)
        {
            urc_table = client->urc_table + i;
            urc = urc_table->urc + j;

            prefix_len = strlen(urc->cmd_prefix);
            suffix_len = strlen(urc->cmd_suffix);
            if (bufsz < prefix_len + suffix_len)
            {
                continue;
            }
            if ((prefix_len ? !strncmp(buffer, urc->cmd_prefix, prefix_len) : 1)
                    && (suffix_len ? !strncmp(buffer + bufsz - suffix_len, urc->cmd_suffix, suffix_len) : 1))
            {
                return urc;
            }
        }
    }

    return NULL;
}

static int at_recv_readline(at_client_t client)
{
    char ch = 0, last_ch = 0;
    char is_full = 0;

    memset(client->recv_line_buf, 0x00, client->recv_line_size);
    client->recv_line_len = 0;

    while (1)
    {    
        /* getchar */
        at_client_getchar(client, &ch, portMAX_DELAY);

        if (client->recv_line_len < client->recv_line_size)
        {
            client->recv_line_buf[client->recv_line_len++] = ch;
        }
        else
        {
            is_full = 1;
        }

        /* is newline */
        if ((ch == '\n' && last_ch == '\r') || (client->end_sign != 0 && ch == client->end_sign)
            || get_urc_obj(client))
        {
            if (is_full)
            {
                ESP_LOGI(DEBUG_AT, "read line failed. The line data length is out of buffer size(%d)!", client->recv_line_size);
                memset(client->recv_line_buf, 0x00, client->recv_line_size);
                client->recv_line_len = 0;
                return -RT_EFULL;
            }
            break;
        }
        last_ch = ch;
    }

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("recvline", client->recv_line_buf, client->recv_line_len);
#endif

    return client->recv_line_len;
}

static void client_parser(void *pvParameters)
{
    const struct at_urc *urc;
    at_client_t client = (at_client_t)pvParameters;
    
    while(1)
    {
        if (at_recv_readline(client) > 0)
        {
            if ((urc = get_urc_obj(client)) != NULL)
            {
                /* current receive is request, try to execute related operations */
                if (urc->func != NULL)
                {
                    urc->func(client, client->recv_line_buf, client->recv_line_len);
                }
            }
            else if (client->resp != NULL)
            {
                at_response_t resp = client->resp;
                char end_ch = client->recv_line_buf[client->recv_line_len - 1];
                
                /* current receive is response */
                client->recv_line_buf[client->recv_line_len - 1] = '\0';
                if (resp->buf_len + client->recv_line_len < resp->buf_size)
                {
                    /* copy response lines, separated by '\0' */
                    memcpy(resp->buf + resp->buf_len, client->recv_line_buf, client->recv_line_len);

                    /* update the current response information */
                    resp->buf_len += client->recv_line_len;
                    resp->line_counts++;
                }
                else
                {
                    client->resp_status = AT_RESP_BUFF_FULL;
                    ESP_LOGI(DEBUG_AT,"Read response buffer failed. The Response buffer size is out of buffer size(%d)!", resp->buf_size);
                }
                
                /* check response result */
                if ((client->end_sign != 0) && (end_ch == client->end_sign) && (resp->line_num == 0))
                {
                    /* get the end sign, return response state END_OK.*/
                    client->resp_status = AT_RESP_OK;
                }
                else if (memcmp(client->recv_line_buf, AT_RESP_END_OK_UPCASE, strlen(AT_RESP_END_OK_UPCASE)) == 0 && resp->line_num == 0)
                {
                    /* get the end data by response result, return response state END_OK. */
                    client->resp_status = AT_RESP_OK;
                }
                else if (memcmp(client->recv_line_buf, AT_RESP_END_OK_LWCASE, strlen(AT_RESP_END_OK_LWCASE)) == 0 && resp->line_num == 0)
                {
                    /* get the end data by response result, return response state END_OK. */
                    client->resp_status = AT_RESP_OK;
                }
                else if (strstr(client->recv_line_buf, AT_RESP_END_ERROR) || 
                        (memcmp(client->recv_line_buf, AT_RESP_END_FAIL, strlen(AT_RESP_END_FAIL)) == 0))
                {
                    client->resp_status = AT_RESP_ERROR;
                }
                else if (resp->line_counts == resp->line_num && resp->line_num)
                {
                    /* get the end data by response line, return response state END_OK.*/
                    client->resp_status = AT_RESP_OK;
                }
                else
                {
                    continue;
                }
                
                client->resp = NULL;
                xSemaphoreGive(client->resp_notice);
            }
            else
            {
                ESP_LOGI(DEBUG_AT, "unrecognized line: %.*s", client->recv_line_len, client->recv_line_buf);
            }
        }
    }
}

// static int at_client_rx_indicate(uart_port_t uart_index, char *recv_data, int recv_len)
// {
//     int idx = 0, i = 0, res;
//     BaseType_t xHigherPriorityTaskWoken;
    
//     for (idx = 0; idx < AT_CLIENT_NUM_MAX; idx++)
//     {
//         if (at_client_table[idx].uart_index == uart_index && at_client_table[idx].status == AT_STATUS_INITIALIZED)
//         {
//             for(i=0; i<recv_len; i++)
//             {
//                 if((res = array_queue_enqueue(at_client_table[idx].recv_q, &recv_data[i])) != 0)
//                     break;
//             }
//             xSemaphoreGiveFromISR(at_client_table[idx].rx_notice, &xHigherPriorityTaskWoken);
//         }
//     }
//     ESP_LOGI(DEBUG_AT, "at_client_rx_indicate: %d", recv_len);
//     return recv_len;
// }

void at_uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t data[128];
    uart_port_t uart_index = (uart_port_t)(intptr_t)arg;

    while (1) {
        // 从队列中接收事件，设置 100ms 的等待时间
        if (xQueueReceive(uart_event_queue, (void *)&event, pdMS_TO_TICKS(100))) {
            switch (event.type) {
                case UART_DATA: {
                    // 读取 UART 数据，设置 100ms 的等待时间
                    int len = uart_read_bytes(uart_index, data, sizeof(data), pdMS_TO_TICKS(100));

                    if (len > 0) {
                        // 遍历客户端表，查找匹配的客户端
                        for (int idx = 0; idx < AT_CLIENT_NUM_MAX; idx++) {
                            if (at_client_table[idx].uart_index == uart_index &&
                                at_client_table[idx].status == AT_STATUS_INITIALIZED) {

                                // 将数据放入队列
                                for (int i = 0; i < len; i++) {
                                    if (array_queue_enqueue(at_client_table[idx].recv_q, &data[i]) != 0) {
                                        ESP_LOGE(MODEL_Received, "Failed to enqueue data at index %d", i);
                                        break;  // 如果队列满了，停止处理
                                    }
                                }

                                // 释放信号量，通知接收任务
                                xSemaphoreGive(at_client_table[idx].rx_notice);
                            }
                        }
                        ESP_LOGI(MODEL_Received, "UART%d: received %d bytes", uart_index, len);
                    }
                    break;
                }

                default:
                    ESP_LOGW(MODEL_Received, "Unhandled UART event type: %d", event.type);
                    break;
            }
        }

        // 添加短暂的延时，防止空转消耗过多 CPU 时间
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


/* initialize the client object parameters */
static int at_client_para_init(at_client_t client)
{
    int result = RT_EOK;
    static int at_client_num = 0;
    char name[32] = {0};

    client->status = AT_STATUS_UNINITIALIZED;

    /* client->recv_line_buf */
    client->recv_line_len = 0;
    client->recv_line_buf = (char *) pvPortMalloc(client->recv_line_size);
    if (client->recv_line_buf == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT client initialize failed! No memory for receive buffer.");
        result = -RT_ENOMEM;
        goto __exit;
    }
    memset(client->recv_line_buf, 0, client->recv_line_size);

    /* client->lock */
    client->lock = xSemaphoreCreateMutex();
    if (client->lock == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT client initialize failed! at_client_recv_lock create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    /* client->rx_notice */
    client->rx_notice = xSemaphoreCreateBinary();
    if (client->rx_notice == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT client initialize failed! at_client_notice semaphore create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    /* client->resp_notice */
    client->resp_notice = xSemaphoreCreateBinary();
    if (client->resp_notice == NULL)
    {
        ESP_LOGI(DEBUG_AT, "AT client initialize failed! at_client_resp semaphore create failed!");
        result = -RT_ENOMEM;
        goto __exit;
    }

    /* client->urc_table */
    client->urc_table = NULL;
    client->urc_table_size = 0;

    /* client->parser */
    sprintf(name, "task_atc_%d", at_client_num);
    client->parser = NULL;
    xTaskCreate(client_parser, name, configMINIMAL_STACK_SIZE * 4, client, 11, &client->parser);
    if (client->parser == NULL)
    {
        result = -RT_ENOMEM;
        goto __exit;
    }

__exit:
    if (result != RT_EOK)
    {
        if (client->lock) vSemaphoreDelete(client->lock);
        if (client->rx_notice) vSemaphoreDelete(client->rx_notice);
        if (client->resp_notice) vSemaphoreDelete(client->resp_notice);
        if (client->recv_line_buf) vPortFree(client->recv_line_buf);
        memset(client, 0x00, sizeof(struct at_client));
    }
    else
    {
        at_client_num++;
    }

    return result;
}


// void set_uart_rx_indicate(UART_INDEX_E uart_index, int (*set_rx_ind)(UART_INDEX_E uart_index, char *recv_data, int recv_len))
// {
//     stm32_uart_handle[uart_index].rx_ind = set_rx_ind;
// }



/**
 * AT client initialize.
 *
 * @param recv_line_size the maximum number of receive buffer length
 *
 * @return !null : initialize success
 *        null : initialize failed
 */
at_client_t at_client_init(uart_port_t uart_index, uint16_t recv_line_size, uint16_t recv_queue_size)
{
    int idx = 0;
    int result = RT_EOK;
    at_client_t client = NULL;

    for (idx = 0; idx < AT_CLIENT_NUM_MAX && at_client_table[idx].status; idx++);

    if (idx >= AT_CLIENT_NUM_MAX)
    {
        ESP_LOGI(DEBUG_AT, "AT client initialize failed! Check the maximum number(%d) of AT client.", AT_CLIENT_NUM_MAX);
        result = -RT_EFULL;
        goto __exit;
    }

    client = &at_client_table[idx];
    client->recv_line_size = recv_line_size;

    /* creat uart receive queue */
    client->recv_q = array_queue_creat(recv_queue_size, sizeof(char));
    client->recv_queue_size = recv_queue_size;
    if (client->recv_q == NULL)
    {
        array_queue_destory(client->recv_q);
        goto __exit;
    }

    result = at_client_para_init(client);
    if (result != RT_EOK)
    {
        goto __exit;
    }

    
    /* init uart by DMA-RX mode */
    client->uart_index = uart_index;
    // ESP_LOGI("uart init","uart_index=%d",client->uart_index);
    // uart_config_init();//初始化串口2
    // set_uart_rx_indicate(uart_index, at_client_rx_indicate);
    //xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

    xTaskCreate(at_uart_event_task, "at_uart_event_task", 4096, (void *)UART_NUM_2, 10, NULL);


__exit:
    if (result == RT_EOK)
    {
        client->status = AT_STATUS_INITIALIZED;
        ESP_LOGI(DEBUG_AT,"AT client initialize success.");
    }
    else
    {
        ESP_LOGI(DEBUG_AT,"AT clientinitialize failed(%d).", result);
        client = NULL;
    }

    return client;
}

at_client_t at_client_get(uart_port_t uart_index)
{
    int idx = 0;

    for (idx = 0; idx < AT_CLIENT_NUM_MAX; idx++)
    {
        // ESP_LOGI(DEBUG_AT,"%d",at_client_table[idx].uart_index);
        if (at_client_table[idx].uart_index==uart_index)
        {
            return &at_client_table[idx];
        }
    }

    return NULL;
}