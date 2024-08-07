#include "modbus_rtu.h"

static const int U2_RX_BUF_SIZE = 2048;

uint8_t MODBUS_RTU_ADDRESS = 0x01;      //modbus_rtu_address
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

void Float_to_Byte(float f,unsigned char byte[],char N)
{
    FloatLongType fl;
    fl.fdata=f;
    byte[N+0]=(unsigned char)fl.ldata;
    byte[N+1]=(unsigned char)(fl.ldata>>8);
    byte[N+2]=(unsigned char)(fl.ldata>>16);
    byte[N+3]=(unsigned char)(fl.ldata>>24);
}

uint16_t calculate_crc(uint8_t *data, int length) 
{
    uint16_t crc = 0xFFFF;
    int i, j;

    for (i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return (crc)<<8|(crc)>>8;
}


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

void modbusRtuDeal_Task(void *arg)
{
    modBusRtuCmdFrame_t modBusRtuCmdFrame_task; 
    static const char *RFID_DTU_DEAL_TAG = "RFID_DTU_DEAL_TAG";
    char *testdata = "0abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLM";
    uint8_t sendbuf[200];
    // char sendbuf[200];
    while(1)
    {
        uint8_t index = 0;
        if(xSemaphoreTake(xBinarySemaphore, portMAX_DELAY)!= pdTRUE)
            continue;
        if(xQueueReceive( modBusRtuCmdQueue, &(modBusRtuCmdFrame_task), ( TickType_t )10))
        {     
            memset(sendbuf, '\0', sizeof(sendbuf));
            if(modBusRtuCmdFrame_task.addr == MODBUS_RTU_ADDRESS)
            {
                if(modBusRtuCmdFrame_task.funcCode == 0x03)
                {
                    EPC_Info_t  **EPC_ptr = &LTU3_Lable; 
                    unsigned char signpkg[3]= {0x05,0x71,0x10};  //注册包 三个字节


                    for (size_t i = 0; i < epcCnt; i++)
                    {
                        memcpy(sendbuf, signpkg, sizeof(signpkg));
                        index=3;
                        sendbuf[index++] = modBusRtuCmdFrame_task.addr;  //地址 一个字节
                        sendbuf[index++] = modBusRtuCmdFrame_task.funcCode;//功能码 一个字节
                        sendbuf[index++] = EPC_ptr[i]->epcId[0];//epcID 高位 
                        sendbuf[index++] = EPC_ptr[i]->epcId[1];//epcID 低位

                        Float_to_Byte((float)(EPC_ptr[i]->tempe/100.0),sendbuf,index);
                        index += 4;
                        sendbuf[index++] = EPC_ptr[i]->antID;
                        sendbuf[index++] = EPC_ptr[i]->rssi;
                        uart2_SendStr((char*)sendbuf); 
                        memset(sendbuf, '\0', sizeof(sendbuf));
                        index=0;
                    }
                    
                    

                    // EPC_Info_t  **EPC_ptr = &LTU3_Lable; 
                    // char  head_data[50] = {'\0'};
                    // sprintf(head_data,"addr:%d func:%d epccnt:%d,", modBusRtuCmdFrame_task.addr,modBusRtuCmdFrame_task.funcCode,epcCnt); 
                    // strcat(sendbuf,head_data);

                    // char  one_epc_data[50] = {'\0'};
                    // for(int i = 0; i < epcCnt; i++)
                    // {
                    //     if(EPC_ptr[i] == NULL)
                    //         break;
                    //     memset(one_epc_data,0,sizeof(one_epc_data));
                    //     sprintf(one_epc_data,"{\"epc\":\"%x%x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",       
                    //     EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0,EPC_ptr[i]->antID,EPC_ptr[i]->rssi); 
                    //     strcat(sendbuf,one_epc_data);
                    //     if(EPC_ptr[i]->epcId[0]!=0)
                    //         strcat(sendbuf,",");
                    // }
                    // uart2_SendStr((char*)sendbuf); 



                    // uint8_t dataLen = 0;
                    // dataLen = 2 * modBusRtuCmdFrame_task.dataNum; 
                    // sendbuf[index++] = modBusRtuCmdFrame_task.addr;         //设备地址
                    // sendbuf[index++] = modBusRtuCmdFrame_task.funcCode;     //功能码
                    // sendbuf[index++] = dataLen;                              //要发送的字节数目，1个地址有2Byte数据
                    // Float_to_Byte((float)sht30_data.Temperature,sendbuf,index);
                    // index += 4;
                    // Float_to_Byte(LTU3_Lable[0]->tempe/100.0,sendbuf,index);
                    // index += 4;
                    // Float_to_Byte((float)sht30_data.Humidity,sendbuf,index);
                    // index += 4;
                    // Float_to_Byte(1.1,sendbuf,index);
                    // index += 4;
                    
                    // // memcpy(&sendbuf[index],modbusRtuDataTAB[modBusRtuCmdFrame_task.regStart - 0x01],dataLen);
                    // // index += dataLen;
                    // uint16_t CRC_val = calculate_crc(sendbuf,index); //CRC校验
                    // sendbuf[index++] = CRC_val>>8;
                    // sendbuf[index++] = CRC_val&0x00FF;
                    // uart2_SendStr((char*)sendbuf);    



                }
            }    

            
        }
        vTaskDelay(200/portTICK_PERIOD_MS);
    }
}
