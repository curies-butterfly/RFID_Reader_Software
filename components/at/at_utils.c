/*
 * Copyright (c) 20019-2020, wanweiyingchuang
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-02-18     denghengli   the first version
 */

#include "at.h"

static char send_buf[AT_CMD_MAX_LEN];
static uint16_t last_cmd_len = 0;


/**
 * dump hex format data to console device
 *
 * @param name name for hex object, it will show on log header
 * @param buf hex buffer
 * @param size buffer size
 */
void at_print_raw_cmd(const char *name, const char *buf, uint16_t size)
{
#ifdef AT_PRINT_RAW_CMD
#define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
#define WIDTH_SIZE           32

    uint16_t i, j;

    for (i = 0; i < size; i += WIDTH_SIZE)
    {
        rt_kprintf("[D/AT] %s: %04X-%04X: ", name, i, i + WIDTH_SIZE);
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                rt_kprintf("%02X ", buf[i + j]);
            }
            else
            {
                rt_kprintf("   ");
            }
            if ((j + 1) % 8 == 0)
            {
                rt_kprintf(" ");
            }
        }
        rt_kprintf("  ");
        for (j = 0; j < WIDTH_SIZE; j++)
        {
            if (i + j < size)
            {
                rt_kprintf("%c", __is_print(buf[i + j]) ? buf[i + j] : '.');
            }
        }
        rt_kprintf("\n");
    }
#endif
}

const char *at_get_last_cmd(uint16_t *cmd_size)
{
    *cmd_size = last_cmd_len;
    return send_buf;
}

static uint16_t at_vprintf(uart_port_t uart_index, const char *format, va_list args)
{
    last_cmd_len = vsnprintf(send_buf, sizeof(send_buf), format, args);

    if(last_cmd_len > sizeof(send_buf))
        last_cmd_len = sizeof(send_buf);

#ifdef AT_PRINT_RAW_CMD
    at_print_raw_cmd("sendline", send_buf, last_cmd_len);
#endif
    ESP_LOGI("AT_send","--%s--",send_buf);
    uart_write_bytes(uart_index, (unsigned char *)send_buf, last_cmd_len);
    
    return last_cmd_len;
}

uint16_t at_vprintfln(uart_port_t uart_index, const char *format, va_list args)
{
//     uint16_t len;
//    // last_cmd_len
//     last_cmd_len = vsnprintf(send_buf, sizeof(send_buf) - 2, format, args);

//     // len = at_vprintf(uart_index, format, args);

//     // uart_write_bytes(uart_index, "\r\n", 2);
//     if(last_cmd_len > sizeof(send_buf) - 2)
//         last_cmd_len = sizeof(send_buf) - 2;
//     memcpy(send_buf + last_cmd_len, "\r\n", 2);
    
//     ESP_LOGI("AT_send","----%s----",send_buf);
//     len = last_cmd_len + 2;


//     return at_vprintf(uart_index, send_buf, args);

    uint16_t len;

    len = at_vprintf(uart_index, format, args);
    
    
    uart_write_bytes(uart_index, "\r\n", 2);
    ESP_LOGI("AT_send","----send total len:%d----",len+2);
    return len + 2;
}

