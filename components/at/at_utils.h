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
#ifndef __AT_UTILS_H__
#define __AT_UTILS_H__

#include  <stdarg.h>
#include  <stdlib.h>
#include  <stdio.h>
#include  <math.h>
#include  <time.h>
#include  <string.h>
#include  <stddef.h>
#include  <stdint.h>
#include  <assert.h>

#include  "algo_array_queue.h"
#include  "driver/uart.h"

void at_print_raw_cmd(const char *name, const char *buf, uint16_t size);
const char *at_get_last_cmd(uint16_t *cmd_size);
uint16_t at_vprintfln(uart_port_t uart_index, const char *format, va_list args);

#endif

