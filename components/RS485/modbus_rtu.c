#include "modbus_rtu.h"
#include "dev_info.h"
#include "rfidnetwork.h"


static const int U2_RX_BUF_SIZE = 2048;


modBusRtuCmdFrame_t modBusRtuCmdFrame;
rfid_read_config_t rfid_dtu_read_config;
QueueHandle_t   modBusRtuCmdQueue;     //成功接收到指令后，将指令帧发送到队列，等待modbusRTU指令处理任务处理
uint8_t modbusRtuDataTAB[50][20];
SemaphoreHandle_t xBinarySemaphore;//信号量创句柄
SemaphoreHandle_t mqtt_xBinarySemaphore;//信号量创句柄

float testvalu = 22.4;

/*要点提示:
1. float和unsigned long具有相同的数据结构长度
2. union据类型里的数据存放在相同的物理空间
*/
typedef union
{
    float fdata;
    unsigned long ldata;
}FloatLongType;


// 计算 CRC 校验码的函数（CRC-16-ANSI）
uint16_t calculate_crc(uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ 0xA001; // CRC-16-ANSI
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}


// 构建 Modbus RTU 请求数据帧
void build_modbus_frame(ModBusRtuCmdFrameSend_t *frame, uint8_t slaveAddress, uint8_t functionCode, uint8_t *temperatureData, size_t temperatureCount) {
    // 设置注册包（此部分不参与 CRC 校验）
    frame->registrationPacket[0] = 0x05;
    frame->registrationPacket[1] = 0x71;
    frame->registrationPacket[2] = 0x00;

    // 设置设备地址和功能码
    frame->slaveAddress = slaveAddress;
    frame->functionCode = functionCode;

    // 设置数据长度（每个标签 2 字节）
    frame->dataLength = temperatureCount * 2;
    ESP_LOGI("Modbus", "Data Length: %d", frame->dataLength);


    // 填充数据域
    for (size_t i = 0; i < temperatureCount; i++) {
        frame->data[2 * i] = temperatureData[2 * i];     // 高字节
        frame->data[2 * i + 1] = temperatureData[2 * i + 1]; // 低字节
        ESP_LOGI("Modbus", "Data: %02x %02x", temperatureData[2 * i], temperatureData[2 * i + 1]);
    }
    

    // 计算 CRC 校验码，计算范围是 slaveAddress, functionCode, dataLength, data
    uint16_t crcLength = sizeof(frame->slaveAddress) + sizeof(frame->functionCode) + sizeof(frame->dataLength) + frame->dataLength;
    frame->crc = calculate_crc((uint8_t*)frame + sizeof(frame->registrationPacket), crcLength);  // 不包括注册包部分
    ESP_LOGI("Modbus", "CRC: %04x", frame->crc); 
    

}

void hex_to_ascii(uint8_t *hex_data, size_t length, char *ascii_str) {
    for (size_t i = 0; i < length; i++) {
        // 将每个字节转换为两位的 ASCII 字符表示
        sprintf(ascii_str + 2 * i, "%02x", hex_data[i]);
    }
    ESP_LOGI("Modbus", "ASCII: %s", ascii_str);
}

// MQTT 发布函数
void publish_modbus_frame(ModBusRtuCmdFrameSend_t *frame) {
    int total_length = sizeof(ModBusRtuCmdFrameSend_t);

    char ascii_str[total_length * 2 + 1];  // 每个字节转为两个 ASCII 字符（加上结尾的 '\0'）
    hex_to_ascii((uint8_t*)frame, total_length, ascii_str);  // 将 Hex 转换为 ASCII
    // 发布 ASCII 格式的数据
    ESP_LOGI("Modbus", "Publishing Modbus frame: %s", ascii_str);
    int msg_id = mqtt_client_publish(send_topic, ascii_str, strlen(ascii_str), 1, 0);


    // int msg_id = mqtt_client_publish(send_topic, (const char *)frame, total_length, 1, 0);

    if (msg_id >= 0) {
        ESP_LOGI("MQTT", "Published Modbus frame with msg_id=%d", msg_id);
    } else {
        ESP_LOGE("MQTT", "Failed to publish Modbus frame");
    }
}


void populate_temperature_data(uint8_t *temperatureData,  uint8_t targetEpcIds[][3],size_t targetCount) {
    size_t index = 0;
    EPC_Info_t  **EPC_ptr = &LTU3_Lable;
    char  *epc_data = NULL;
    // 遍历 EPC 标签列表（最多 120 个标签）
    for (int i = 0; i < 120; i++) {
       
        if (EPC_ptr[i] == NULL) {
            break;  // 如果没有更多的 EPC 标签，退出循环
        }
        ESP_LOGI("Modbus", "EPC ID: %02x %02x  %02x %02x", EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1],(uint8_t)(EPC_ptr[i]->tempe >>8),(uint8_t)(EPC_ptr[i]->epcId[1] & 0xFF));
        // 遍历所有目标 EPC ID
        bool found = false;
        for (size_t j = 0; j < targetCount; j++) {
            if (EPC_ptr[i]->epcId[0] == targetEpcIds[j][0] && 
                EPC_ptr[i]->epcId[1] == targetEpcIds[j][1]) {
                // 如果 EPC ID 匹配，存储温度数据

                ESP_LOGI("Modbus", "Found matching EPC ID: %02x %02x", EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1]);
                int16_t temp = EPC_ptr[i]->tempe;  // 获取温度数据（例如 2550 对应 25.50°C）
                // 高字节
                temperatureData[2*j] = (uint8_t)(temp >> 8);
                // 低字节
                temperatureData[2*j+1] = (uint8_t)(temp & 0xFF);
                found = true;
                break;  // 找到目标 EPC ID 后，跳出目标 EPC ID 查找循环
            }
        }

        // 如果没有找到对应的 EPC ID，则存入 0x99 0x99（表示未找到的温度数据）
        // if (!found) {
        //     temperatureData[index++] = 0x99;
        //     temperatureData[index++] = 0x99;
        // }
    }

    // 设置数据长度
    // *dataLength = index;  // 数据的总字节数
}






void modbusRtuDeal_Task(void *arg) {
    size_t dataLength = 0;
    uint8_t targetEpcIds[TEMP_CONT][3] = {
        {0x32, 0xe2},  // EPC ID 1
        {0x22, 0xa8},  // EPC ID 2
        {0x31, 0x42},   // EPC ID 3
        {0x26, 0x96},
        {0x2F, 0x9c},
        {0x15, 0xeb},
    };
    uint8_t temperatureData[TEMP_CONT*2] = {0x99};
    for (int i = 0; i < TEMP_CONT*2; i++) {
        temperatureData[i] = 0x99;
    }

    while (1) {
        // if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) != pdTRUE) {
        //     continue;
        // }

        //rugao：
        //0571 01 01 03 0c 0a0e 0a1a 0a09 9999 9999 9999 60e3 √
        //0571 02 01 03 06 0af7 0ae6 0b19 f0df √
        //0571 00 02 03 06 0994 096c 0999 017a √

        //0571 00 02 03 06 9999 9999 9999 60d9
        //0571 00 02 03 06 0950 0948 093d b11b
        //0571 00 02 03 06 094d 0948 093d 5d19
        //0571 00 02 03 06 0944 0948 0936 c0df
        //0571 00 02 03 0c 9999 9999 9999 9999 9999 9999 5925
        //0571 00 02 03 0c 0936 0925 090b 0933 9999 9999 71e5
        //0571 00 02 03 0c 0941 0927 092c 0933 0a58 0a94 5ebf
        //0571 00 02 03 0c 092f 0927 092c 0933 090e 0a94 7402
        populate_temperature_data(temperatureData, targetEpcIds, TEMP_CONT);

        ModBusRtuCmdFrameSend_t modbusFrame;
        build_modbus_frame(&modbusFrame, MODBUS_RTU_ADDRESS, 0x03, temperatureData, TEMP_CONT);

        publish_modbus_frame(&modbusFrame);

        vTaskDelay(pdMS_TO_TICKS(pushtime_count));
    }
}

void Float_to_Byte(float f,unsigned char byte[],char N)
{
    FloatLongType fl;
    fl.fdata=f;
    byte[N+0]=(unsigned char)fl.ldata;
    byte[N+1]=(unsigned char)(fl.ldata>>8);
    byte[N+2]=(unsigned char)(fl.ldata>>16);
    byte[N+3]=(unsigned char)(fl.ldata>>24);
}

// uint16_t calculate_crc(uint8_t *data, int length) 
// {
//     uint16_t crc = 0xFFFF;
//     int i, j;

//     for (i = 0; i < length; i++) {
//         crc ^= (uint16_t)data[i];
//         for (j = 0; j < 8; j++) {
//             if (crc & 1) {
//                 crc = (crc >> 1) ^ 0xA001;
//             } else {
//                 crc >>= 1;
//             }
//         }
//     }
//     return (crc)<<8|(crc)>>8;
// }


void modBusRtu_Init()
{
    modBusRtuCmdQueue = xQueueCreate( 20, sizeof(modBusRtuCmdFrame_t));
        // 创建二元信号量
    xBinarySemaphore = xSemaphoreCreateBinary();
    if ( xBinarySemaphore == NULL)
    {
        // printf("创建信号量失败\r\n");
        return;
    }
}


void modbusRtuRx_task(void *arg)
{
    static const char *RX_TASK_TAG = "ModBusRtuRX_TASK";
    static const char *RFID_TAG = "RFID_Frame_Analyze";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(U2_RX_BUF_SIZE+1);
    uint16_t frameLen = 0;
    uint16_t CRC_val = 0;

    while (1) 
    {
        const int rxBytes = uart_read_bytes(UART_NUM_2, data, U2_RX_BUF_SIZE, 200 / portTICK_PERIOD_MS);
        //200 / portTICK_PERIOD_MS,这里表示接收数据等待时间，越小接收的响应速度就越好
        int i = 0;
        if (rxBytes > 0) 
        {
            data[rxBytes] = 0;
            // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            // ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
            //if(rxBytes == 8)//读数据指令（16位数据模式），长度为固定的8 Byte
            // 01 03 01 02 0f 00 00 03 e8 cec1 crc0
            if(1)
            {
                CRC_val = calculate_crc(data,9); //CRC校验
                ESP_LOGI(RX_TASK_TAG,"CRC_val:%x",CRC_val);
                ESP_LOGI(RX_TASK_TAG,"%x",(((uint16_t)data[9]<<8)|((uint16_t)data[10])));
                if(CRC_val == (((uint16_t)data[9]<<8)|((uint16_t)data[10])))
                {
                    ESP_LOGI(RX_TASK_TAG,"crc check success");

                    modBusRtuCmdFrame.crcValue = CRC_val;//crc校验
                    modBusRtuCmdFrame.addr = data[0];//485 地址
                    modBusRtuCmdFrame.funcCode= data[1];//功能码
                    if (0x01==data[2])
                    {
                        rfid_dtu_read_config.rfid_read_on_off = RFID_READ_ON;
                    }else if(0x02==data[2])
                    {
                        rfid_dtu_read_config.rfid_read_on_off = RFID_READ_OFF;
                    }else
                    {
                        rfid_dtu_read_config.rfid_read_on_off = RFID_READ_NULL;
                    }

                    if (0x01==data[3])
                    {
                        rfid_dtu_read_config.rfid_read_mode = RFID_READ_MODE_ONCE;
                    }else if(0x02==data[3])
                    {
                        rfid_dtu_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;
                    }else
                    {
                        rfid_dtu_read_config.rfid_read_mode = RFID_READ_MODE_NULL;
                    }

                    // modBusRtuRFIDCmdFrame.read_mode=data[3];
                    rfid_dtu_read_config.ant_sel=data[4];
                    rfid_dtu_read_config.read_interval_time=(((uint32_t)data[5]<<24)|((uint32_t)data[6]<<16)|((uint32_t)data[7]<<8)|((uint32_t)data[8]));;
                    RFID_ReadEPC(rfid_dtu_read_config);

                    xQueueGenericSend( modBusRtuCmdQueue, ( void * ) &modBusRtuCmdFrame, ( TickType_t ) 0, queueSEND_TO_BACK );

                    vTaskDelay(100/portTICK_PERIOD_MS);

                    // modBusRtuCmdFrame.funcCode = data[1];
                    // modBusRtuCmdFrame.regStart = (((uint16_t)data[2]<<8)|((uint16_t)data[3])); 
                    // modBusRtuCmdFrame.dataNum = (((uint16_t)data[4]<<8)|((uint16_t)data[5]));

                    // ESP_LOGI(RX_TASK_TAG,"crcValue:%x",modBusRtuCmdFrame.crcValue);
                    // ESP_LOGI(RX_TASK_TAG,"addr:%x",modBusRtuCmdFrame.addr);
                    // ESP_LOGI(RX_TASK_TAG,"funcCode:%d",modBusRtuCmdFrame.funcCode);
                    // ESP_LOGI(RX_TASK_TAG,"regStart:%d",modBusRtuCmdFrame.regStart);
                    // ESP_LOGI(RX_TASK_TAG,"dataNum:%d",modBusRtuCmdFrame.dataNum);
                }
                else
                {
                    ESP_LOGI(RX_TASK_TAG,"crc check error");
                }
            }
        }
        // vTaskDelay(200/portTICK_PERIOD_MS);
    }
    free(data);
}

// void modbusRtuDeal_Task(void *arg)
// {
//     modBusRtuCmdFrame_t modBusRtuCmdFrame_task; 
//     static const char *RFID_DTU_DEAL_TAG = "RFID_DTU_DEAL_TAG";
//     char *testdata = "0abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLM";
//     uint8_t sendbuf[200];
//     // char sendbuf[200];
//     while(1)
//     {
//         uint8_t index = 0;
//         if(xSemaphoreTake(xBinarySemaphore, portMAX_DELAY)!= pdTRUE)
//             continue;
//         if(xQueueReceive( modBusRtuCmdQueue, &(modBusRtuCmdFrame_task), ( TickType_t )10))
//         {     
//             memset(sendbuf, '\0', sizeof(sendbuf));
//             if(modBusRtuCmdFrame_task.addr == MODBUS_RTU_ADDRESS)
//             {
//                 if(modBusRtuCmdFrame_task.funcCode == 0x03)
//                 {
//                     EPC_Info_t  **EPC_ptr = &LTU3_Lable; 
//                     unsigned char signpkg[3]= {0x05,0x71,0x10};  //注册包 三个字节


//                     for (size_t i = 0; i < epcCnt; i++)
//                     {
//                         memcpy(sendbuf, signpkg, sizeof(signpkg));
//                         index=3;
//                         sendbuf[index++] = modBusRtuCmdFrame_task.addr;  //地址 一个字节
//                         sendbuf[index++] = modBusRtuCmdFrame_task.funcCode;//功能码 一个字节
//                         sendbuf[index++] = EPC_ptr[i]->epcId[0];//epcID 高位 
//                         sendbuf[index++] = EPC_ptr[i]->epcId[1];//epcID 低位

//                         Float_to_Byte((float)(EPC_ptr[i]->tempe/100.0),sendbuf,index);
//                         index += 4;
//                         sendbuf[index++] = EPC_ptr[i]->antID;
//                         sendbuf[index++] = EPC_ptr[i]->rssi;
//                         uart2_SendStr((char*)sendbuf); 
//                         memset(sendbuf, '\0', sizeof(sendbuf));
//                         index=0;
//                     }
                    
//                     // EPC_Info_t  **EPC_ptr = &LTU3_Lable; 
//                     // char  head_data[50] = {'\0'};
//                     // sprintf(head_data,"addr:%d func:%d epccnt:%d,", modBusRtuCmdFrame_task.addr,modBusRtuCmdFrame_task.funcCode,epcCnt); 
//                     // strcat(sendbuf,head_data);

//                     // char  one_epc_data[50] = {'\0'};
//                     // for(int i = 0; i < epcCnt; i++)
//                     // {
//                     //     if(EPC_ptr[i] == NULL)
//                     //         break;
//                     //     memset(one_epc_data,0,sizeof(one_epc_data));
//                     //     sprintf(one_epc_data,"{\"epc\":\"%x%x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",       
//                     //     EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0,EPC_ptr[i]->antID,EPC_ptr[i]->rssi); 
//                     //     strcat(sendbuf,one_epc_data);
//                     //     if(EPC_ptr[i]->epcId[0]!=0)
//                     //         strcat(sendbuf,",");
//                     // }
//                     // uart2_SendStr((char*)sendbuf); 



//                     // uint8_t dataLen = 0;
//                     // dataLen = 2 * modBusRtuCmdFrame_task.dataNum; 
//                     // sendbuf[index++] = modBusRtuCmdFrame_task.addr;         //设备地址
//                     // sendbuf[index++] = modBusRtuCmdFrame_task.funcCode;     //功能码
//                     // sendbuf[index++] = dataLen;                              //要发送的字节数目，1个地址有2Byte数据
//                     // Float_to_Byte((float)sht30_data.Temperature,sendbuf,index);
//                     // index += 4;
//                     // Float_to_Byte(LTU3_Lable[0]->tempe/100.0,sendbuf,index);
//                     // index += 4;
//                     // Float_to_Byte((float)sht30_data.Humidity,sendbuf,index);
//                     // index += 4;
//                     // Float_to_Byte(1.1,sendbuf,index);
//                     // index += 4;
                    
//                     // // memcpy(&sendbuf[index],modbusRtuDataTAB[modBusRtuCmdFrame_task.regStart - 0x01],dataLen);
//                     // // index += dataLen;
//                     // uint16_t CRC_val = calculate_crc(sendbuf,index); //CRC校验
//                     // sendbuf[index++] = CRC_val>>8;
//                     // sendbuf[index++] = CRC_val&0x00FF;
//                     // uart2_SendStr((char*)sendbuf);    

//                 }
//             }    

            
//         }
//         vTaskDelay(200/portTICK_PERIOD_MS);
//     }
// }



//屏显线程
void modbusRtushow_Task(void *arg)
{
    static const char *RFID_DTU_DEAL_TAG = "RFID_DTU_DEAL_TAG";
    char ant1_epc485sendid[50] = {'\0'};
    char ant2_epc485sendid[50] = {'\0'};
    char ant3_epc485sendid[50] = {'\0'};
    char ant4_epc485sendid[50] = {'\0'};
    char ant1_allEPCData[1024] = {'\0'};  // 用于拼接所有天线数据
    char ant2_allEPCData[1024] = {'\0'};  // 用于拼接所有天线数据
    char ant3_allEPCData[1024] = {'\0'};  // 用于拼接所有天线数据
    char ant4_allEPCData[1024] = {'\0'};  // 用于拼接所有天线数据

    // char formattedData[1050] = {'\0'}; // 最终发送的数据（包含引号）

    while (1) {
        if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) != pdTRUE)
            continue;

        EPC_Info_t **EPC_ptr = &LTU3_Lable;
        ant1_allEPCData[0] = '\0';  // 清空之前的拼接数据
        ant2_allEPCData[0] = '\0';  // 清空之前的拼接数据
        ant3_allEPCData[0] = '\0';  // 清空之前的拼接数据
        ant4_allEPCData[0] = '\0';  // 清空之前的拼接数据

        for (int i = 0; i < 120; i++) {
            if (EPC_ptr[i] == NULL)
                break;

            memset(ant1_epc485sendid, 0, sizeof(ant1_epc485sendid));
            memset(ant2_epc485sendid, 0, sizeof(ant2_epc485sendid));
            memset(ant3_epc485sendid, 0, sizeof(ant3_epc485sendid));
            memset(ant4_epc485sendid, 0, sizeof(ant4_epc485sendid));

            switch (EPC_ptr[i]->antID) {
                case 1:
                    // 格式化标签数据（ID和温度）
                    snprintf(ant1_epc485sendid, sizeof(ant1_epc485sendid), "ID:%02x%02x temp:%2.1f", 
                            EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], EPC_ptr[i]->tempe / 100.0);
                    break;
                case 2:
                    // 天线2的数据处理
                    snprintf(ant2_epc485sendid, sizeof(ant2_epc485sendid), "ID:%02x%02x temp:%2.1f", 
                            EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], EPC_ptr[i]->tempe / 100.0);
                    break;
                case 3:
                    // 天线3的数据处理
                    snprintf(ant3_epc485sendid, sizeof(ant3_epc485sendid), "ID:%02x%02x temp:%2.1f", 
                            EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], EPC_ptr[i]->tempe / 100.0);
                    break;
                case 4:
                    // 天线4的数据处理
                    snprintf(ant4_epc485sendid, sizeof(ant4_epc485sendid), "ID:%02x%02x temp:%2.1f", 
                            EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], EPC_ptr[i]->tempe / 100.0);
                    break;
                default:
                    break;
            }

            // 拼接每条标签数据，加上换行符
            if (strlen(ant1_epc485sendid) > 0) {
                strcat(ant1_allEPCData, ant1_epc485sendid);
                strcat(ant1_allEPCData, "\r\n");  // 添加换行符
            }
            // 拼接每条标签数据，加上换行符
            if (strlen(ant2_epc485sendid) > 0) {
                strcat(ant2_allEPCData, ant2_epc485sendid);
                strcat(ant2_allEPCData, "\r\n");  // 添加换行符
            }
            // 拼接每条标签数据，加上换行符
            if (strlen(ant3_epc485sendid) > 0) {
                strcat(ant3_allEPCData, ant3_epc485sendid);
                strcat(ant3_allEPCData, "\r\n");  // 添加换行符
            }
            // 拼接每条标签数据，加上换行符
            if (strlen(ant4_epc485sendid) > 0) {
                strcat(ant4_allEPCData, ant4_epc485sendid);
                strcat(ant4_allEPCData, "\r\n");  // 添加换行符
            }

        }

        // 如果有数据，添加一对最外层引号并发送
        if (strlen(ant1_allEPCData) > 0) {
            // snprintf(formattedData, sizeof(formattedData), "\"%s\"", allEPCData); // 最外层添加引号
            showEPC(ant1_allEPCData,0x31); // 调用 showEPC 发送数据
        }
        if (strlen(ant2_allEPCData) > 0) {
            // snprintf(formattedData, sizeof(formattedData), "\"%s\"", allEPCData); // 最外层添加引号
            showEPC(ant2_allEPCData,0x33); // 调用 showEPC 发送数据
        }
        if (strlen(ant3_allEPCData) > 0) {
            // snprintf(formattedData, sizeof(formattedData), "\"%s\"", allEPCData); // 最外层添加引号
            showEPC(ant3_allEPCData,0x35); // 调用 showEPC 发送数据
        }
        if (strlen(ant4_allEPCData) > 0) {
            // snprintf(formattedData, sizeof(formattedData), "\"%s\"", allEPCData); // 最外层添加引号
            showEPC(ant4_allEPCData,0x37); // 调用 showEPC 发送数据
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    // modBusRtuCmdFrame_t modBusRtuCmdFrame_task; 
    // static const char *RFID_DTU_DEAL_TAG = "RFID_DTU_DEAL_TAG";
    // char *testdata = "abcpqrstuvwxyzABCDEF";//defghijklmnoHIJKLMG

    // // uint8_t sendbuf[200];
    // uint8_t sendtemp_test[]={0x6D,0x61,0x69,0x6E,0x2E,0x78,0x30,0x2E,0x76,0x61,0x6c,0x3D,0x31,0x30,0x30,0xFF,0xFF,0xFF};
    // uint8_t sendbuf2[50];
    // char  one_epc_data[50] = {'\0'};
    // // char sendbuf[200];
    // while(1)
    // {
    //     uint8_t index = 0;
    //     if(xSemaphoreTake(xBinarySemaphore, portMAX_DELAY)!= pdTRUE)
    //         continue;
        
    //     EPC_Info_t  **EPC_ptr = &LTU3_Lable;
    //     char epc485sendid[18];//id+temp

    //     for(int i = 0; i < 120; i++)
    //     {
    //         if(EPC_ptr[i] == NULL)
    //             break;
    //         memset(epc485sendid,0,sizeof(epc485sendid));
    //         switch (EPC_ptr[i]->antID)
    //         {
    //             case 1:
    //                 /* code */
    //                 snprintf(epc485sendid, sizeof(epc485sendid),"ID:%02x%02x temp:%2.1f", EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0);
    //                 // printf("id:%s\n",epc485sendid);
    //                 showEPC(epc485sendid);
    //                 // uart2_SendStr((char*)"\r\n"); 
    //                 // uart2_SendStr((char*)sendtemp_test);

    //                 break;
    //             case 2:
    //                 /* code */
    //                 break;
    //             case 3:
    //                 /* code */
    //                 break;
    //             case 4:
    //                 /* code */
    //                 break;
    //             default:
    //                 break;
    //         }           
    //     }
     
    //     vTaskDelay(200/portTICK_PERIOD_MS);
    // }



}


void showEPC(const char* str,uint8_t point) {
    uint8_t sendbuf1[] = {0x70, 0x61, 0x67, 0x65, 0x30, 0x2E, 0x74, 0x31, 0x2E, 0x74, 0x78, 0x74, 0x3D, 0x22};
    //                      p     a     g     e     0     .     t     1     .     t     x     t     =     "
    sendbuf1[7]=point;
    unsigned char hexArray[1024] = {0};  // 增加大小以避免溢出
    int length = strlen(str);          
    // printf("%d\n", length);
    for (int i = 0; i < length; i++) {
        hexArray[i] = (unsigned char)str[i];
    }
    hexArray[length++] = 0x22;  // 添加 0x22 (引号)
    hexArray[length++] = 0xFF;  // 添加 0xFF
    hexArray[length++] = 0xFF;  // 再添加 0xFF
    hexArray[length++] = 0xFF;  // 0xFF 
    hexArray[length++] = 0x00;  // 
    // 计算新数组的总长度
    int sendbuf1_len = sizeof(sendbuf1); // sendbuf1 的长度
    int hexArray_len = length;       // hexArray 的长度
    int total_len = sendbuf1_len + hexArray_len;
    // 创建新数组以存储合并后的数据
    unsigned char combinedArray[total_len];
    // 复制 sendbuf1 到新数组
    memcpy(combinedArray, sendbuf1, sendbuf1_len);
    // 复制 hexArray 到新数组
    memcpy(combinedArray + sendbuf1_len, hexArray, hexArray_len);
    // 发送合并后的数组
    uart2_SendStr((char*)combinedArray);

}


