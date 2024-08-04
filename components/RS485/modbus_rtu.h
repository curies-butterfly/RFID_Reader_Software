#ifndef MODBUS_RTU_H__
#define MODBUS_RTU_H__

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
#include "rfidcommon.h"
#include "driver_uart.h"
#include "sht30.h"
#include "rfidmodule.h"


extern uint8_t modbusRtuDataTAB[50][20];

//modBUSRTU 指令帧结构
typedef struct
{
    uint8_t addr;   //从机地址，一个字节 
    uint8_t funcCode; //功能码，  一个字节 
    uint16_t regStart; //寄存器开始地址，2个字节，（高在前，低在后）
    uint16_t dataNum; //要读取的寄存器数目
    uint16_t crcValue; //CRC校验值;

}modBusRtuCmdFrame_t;

//modBUSRTU 控制读写器数据帧结构体
typedef struct
{
    uint8_t addr;   //从机地址，一个字节 
    uint8_t funcCode; //功能码，  一个字节 
    uint8_t read_on_off; 
    uint8_t read_mode; 
    uint8_t ant_sel;
    uint32_t interval_time;
    uint16_t crcValue; //CRC校验值;

}modBusRtuRFIDCmdFrame_t;


void modBusRtu_Init();
void modbusRtuRx_task(void *arg);   //modbusRTU数据接收任务
void modbusRtuDeal_Task(void *arg); //modbusRTU指令处理任务


#endif