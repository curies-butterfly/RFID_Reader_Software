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

// 标签个数
#define TEMP_CONT 6
#define MODBUS_RTU_ADDRESS 0x02      //modbus_rtu_address


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



typedef struct {
    uint8_t registrationPacket[3];  // 注册包 057101（3 字节）
    uint8_t slaveAddress;           // 目标设备的地址
    uint8_t functionCode;           // 功能码（例如 0x03 读取数据）
    uint8_t dataLength;             // 数据长度（不包括 CRC）
    uint8_t data[TEMP_CONT*2];               // 数据部分（包括 6 个温度标签的十六进制数据）
    uint16_t crc;                   // CRC 校验码
} ModBusRtuCmdFrameSend_t;


uint16_t calculate_crc(uint8_t *data, uint16_t length);
void build_modbus_frame(ModBusRtuCmdFrameSend_t *frame, uint8_t slaveAddress, uint8_t functionCode, uint8_t *temperatureData, size_t temperatureCount);
void hex_to_ascii(uint8_t *hex_data, size_t length, char *ascii_str);
void publish_modbus_frame(ModBusRtuCmdFrameSend_t *frame);
void populate_temperature_data(uint8_t *temperatureData,  uint8_t targetEpcIds[][3],size_t targetCount);

void modBusRtu_Init();
void modbusRtuRx_task(void *arg);   //modbusRTU数据接收任务
void modbusRtuDeal_Task(void *arg); //modbusRTU指令处理任务
void modbusRtushow_Task(void *arg); //modbus屏幕显示任务

void showEPC(const char* str,uint8_t point);

#endif