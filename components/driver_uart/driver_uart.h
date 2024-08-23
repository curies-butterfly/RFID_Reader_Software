#ifndef DRIVER_UART__H_
#define DRIVER_UART__H_


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "esp_system.h"
#include "esp_log.h"
#include "string.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"


extern QueueHandle_t   RFID_EpcQueue;

void uart1_Init(void);
void uart2_Init(void);
int uart1_SendBytes(char* data,size_t size);
int uart1_SendStr(char* data);
int uart2_SendBytes(char* data,size_t size);
int uart2_SendStr(char* data);

void rx_task(void *arg);

// extern SemaphoreHandle_t uart1_rx_xBinarySemaphore;//信号量创句柄

#endif
