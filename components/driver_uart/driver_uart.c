#include "driver_uart.h"
#include "rfidmodule.h"

static const int RX_BUF_SIZE = 2048;
static const int U2_RX_BUF_SIZE = 2048;

QueueHandle_t   RFID_EpcQueue;  //用于同步串口数据处理任务后，给RFID数据处理任务

#define TXD_PIN 41//(GPIO_NUM_16)
#define RXD_PIN 42//(GPIO_NUM_17)

#define U2_TXD_PIN 21//(GPIO_NUM_19)
#define U2_RXD_PIN 47//(GPIO_NUM_5)

void uart1_Init(void)
{
    const uart_config_t uart_config = 
    {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    RFID_EpcQueue = xQueueCreate( 20, sizeof( BaseDataFrame_t ) );
}


void uart2_Init(void)
{
    const uart_config_t uart_config = 
    {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_2, U2_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, U2_TXD_PIN, U2_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}



int uart1_SendBytes(char* data,size_t size)
{
    const int txBytes = uart_write_bytes(UART_NUM_1, data, size);
    return txBytes;
}

int uart1_SendStr(char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    return txBytes;
}


int uart2_SendBytes(char* data,size_t size)
{
    const int txBytes = uart_write_bytes(UART_NUM_2, data, size);
    return txBytes;
}

int uart2_SendStr(char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_2, data, len);
    return txBytes;
}


//Bug 需要考虑数据越界问题，i定位到帧头0x5A的位置后，i后面不一定有数据
void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    static const char *RFID_TAG = "RFID_Frame_Analyze";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    uint16_t frameLen = 0;
    uint16_t CRC_val = 0;
    while (1) 
    {
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 200 / portTICK_PERIOD_MS);
        //200 / portTICK_PERIOD_MS,这里表示接收数据等待时间，越小接收的响应速度就越好
        int i = 0;
        if (rxBytes > 0) 
        {
            data[rxBytes] = 0;
           // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            //ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
            while(i < rxBytes)
            {
                for(; i < rxBytes; i++)
                {
                    if(data[i] == 0x5A)
                    {
                        //ESP_LOGI(RFID_TAG, "找到帧头");
                        break;
                    }
                }
                if(i == rxBytes)
                {
                   // ESP_LOGI(RFID_TAG, "没有找到帧头");
                }
                else
                {  
                    frameDealFlag = 1;   //接收到了一帧数据
                    //如果是一帧正确的数据，那么data[i+3]则是协议控制字的第3个Byte，data[i+3]&0x20是协议控制字的Bit 13
                    //如果Bit 13为RS485 标志位，当为1时才有串行设备地址
                    //这里并没有先判断数据的正确性，因为必须要先确定数据的长度后才能作CRC校验。
                    if(data[i+3]&0x20)  
                    {   
                        UARTRecvFrame.DevAddr = data[i+5]; //有设备地址
                        UARTRecvFrame.DataLen = ((uint16_t)data[i+6]<<8)|((uint16_t)data[i+7]); 
                        frameLen = 7 + UARTRecvFrame.DataLen;  //帧长度          
                    }
                    else
                    {
                        UARTRecvFrame.DevAddr = 0;      //无设备地址
                        UARTRecvFrame.DataLen = ((uint16_t)data[i+5]<<8)|((uint16_t)data[i+6]);
                        frameLen = 6 + UARTRecvFrame.DataLen; //帧长度 
                    }
                    CRC_val = CRC16_CCITT(&data[i+1],frameLen); //CRC校验
                    //CRC校验成功
                    if(CRC_val == (((uint16_t)data[i+frameLen+1]<<8)|((uint16_t)data[i+frameLen+2])))
                    {
                      //  ESP_LOGI(RFID_TAG,"crc check success");
                      //  ESP_LOGI(RFID_TAG, "CRC_Value:%x",CRC_val);
                        UARTRecvFrame.CRCValu = CRC_val;
                        UARTRecvFrame.ProCtrl[0] = data[i+1];
                        UARTRecvFrame.ProCtrl[1] = data[i+2];
                        UARTRecvFrame.ProCtrl[2] = data[i+3];
                        UARTRecvFrame.ProCtrl[3] = data[i+4];
                        
                        if(UARTRecvFrame.ProCtrl[2]&0x10 && UARTRecvFrame.ProCtrl[3]==0x00) //读EPC数据帧
                        {
                            //为保证数据不丢失，为数据开放新的空间存储,EPC数据处理任务处理完后将会把空间free
                            UARTRecvFrame.pData = (uint8_t*)malloc(sizeof(uint8_t)*UARTRecvFrame.DataLen + 2);
                            memcpy(UARTRecvFrame.pData,&data[i+frameLen-UARTRecvFrame.DataLen+1],UARTRecvFrame.DataLen);
                            
                            if(RFID_EpcQueue != 0) //将EPC数据帧，发送到EPC数据帧队列
                            {
                                xQueueGenericSend( RFID_EpcQueue, ( void * ) &UARTRecvFrame, ( TickType_t ) 0, queueSEND_TO_BACK );   
                            }  
                        }

                        else  //其它数据帧，通常是配置结果的返回,数据存储在data数据
                            UARTRecvFrame.pData = &data[i+frameLen-UARTRecvFrame.DataLen+1];     
                        frameDealFlag = 2;
                    }
                    else
                        UARTRecvFrame.CRCValu = 0; //CRC校验失败           
                }
                i++;
            }
        }
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
    free(data);
}


