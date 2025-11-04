#ifndef RFIDMODULE__H_
#define RFIDMODULE__H_

#include "driver_uart.h"
#include "rfidcommon.h"
#include "modbus_rtu.h"

#define ALPHA 0.95f // α=0.2， 一阶低通滤波



//基本数据帧
typedef struct 
{
    uint8_t  ProCtrl[4];    //协议控制字
    uint8_t  DevAddr;       //串行设备地址，为0则表示没有串行设备地址信息
    uint16_t DataLen;       //数据长度
    uint8_t  *pData;        //数据
    uint16_t CRCValu;       //CRC 校验
}BaseDataFrame_t;
extern BaseDataFrame_t  UARTRecvFrame;

extern uint8_t frameDealFlag; 

//频段列表结构体
typedef struct 
{
    uint8_t num;        //支持的频段数目
    uint8_t*  List;     //支持的频段
}FreqBandList_t;

//协议列表结构体
typedef struct 
{
    uint8_t num;        //支持的协议数目
    uint8_t*  List;     //支持的协议
}ProtocolList_t;

//RFID 模组读写能力结构体
typedef struct 
{
    uint8_t minPower;           //最小发射功率,0~36，单位 dBm，步进 1dB
    uint8_t maxPower;           //最大发射功率,0~36，单位 dBm，步进 1dB
    uint8_t antNum;             //天线数目,读写器支持的天线端口数
    FreqBandList_t FreqBandList; //频段列表,
    ProtocolList_t ProtocolList; //RFID 协议列表
}CapacityInfo_t;



//系统所有天线功率信息
typedef struct 
{
    uint8_t antNum;     //天线数目
    uint8_t power[64];  //每个天线的信号值
}PoweInfo_t; 


//读写器单个天线功率
typedef struct 
{
    uint8_t AntennaNo;//天线号
    uint8_t Power;//功率 0~36，单位 dBm，步进 1dB
}Dictionary;


//读LTU3 温度标签信息
typedef struct 
{
    uint8_t epcId[3];       // EPCID
    int16_t tempe;          // 温度数据 
    uint8_t antID;          //天线号
    uint8_t rssi;           //信号质量
    uint8_t del_flag;       //
    float last_temp;        //上一次发送的温度值
    float filtered_tempe;   // 滤波后的温度值
}EPC_Info_t;

//读写器工件频率
typedef struct
{
    uint8_t autoSet;
    uint16_t freqNum;
    uint8_t *freqList;
}WorkFreq_t;

typedef enum
{
    RFID_READ_NULL = 0,
    RFID_READ_ON,
    RFID_READ_OFF,
}rfid_read_on_off_t;


typedef enum
{
    RFID_READ_MODE_NULL = 0,
    RFID_READ_MODE_ONCE,
    RFID_READ_MODE_CONTINUOUS,
}rfid_read_mode_t;


typedef struct 
{
    rfid_read_on_off_t rfid_read_on_off;
    rfid_read_mode_t rfid_read_mode;
    uint8_t ant_sel;
    uint32_t read_interval_time;
}rfid_read_config_t;

extern uint8_t type_epc;

typedef enum {
    TAG_TYPE_UNKNOWN = 0,
    TAG_TYPE_YH,
    TAG_TYPE_XY
} TagType;//标签类型 0:未知 1:悦和标签 2:星沿标签 
extern TagType tagType;


#define  RFID_SendBytes(data,size) uart1_SendBytes(data,size) //串口数据发送接口

extern EPC_Info_t  *LTU3_Lable[];  //
extern uint16_t    epcCnt;
extern uint16_t    epc_read_speed;
extern char *FreqTab[];
extern char *ProtocolTab[];
// extern rfid_read_config_t rfid_read_config;   不使用全局变量

void ClearRecvFlag();               //清除接收标志
result_t WaitFlag(uint16_t waitTime);   //等待数据接收
void RFID_SendBaseFrame(BaseDataFrame_t BaseDataFrame);
void RFID_SendBaseFrameTest();
void RFID_SendCmdStop();
result_t RFID_StopRead();
result_t RFID_GetCapacity(CapacityInfo_t *CapacityInfo);
void RFID_ShowCapacityInfo(CapacityInfo_t CapacityInfo);
result_t RFID_SetPower(Dictionary DicPower, uint8_t cfgMake, uint8_t ParamSave);
result_t RFID_GetPower(PoweInfo_t *PoweInfo);
void RFID_ShowPower(PoweInfo_t PoweInfo);
result_t RFID_GetFreqRanger(uint8_t *FreqRang);
result_t RFID_GetWorkFreq(WorkFreq_t *WorkFreq);

void RFID_SendReadEpcCmd(uint8_t ant,uint8_t mode);
void RFID_ShowEpc(EPC_Info_t  **EPC_ptr);
result_t RFID_ReadEPC(rfid_read_config_t rfid_read_config);

void RFID_ReadEpcTask(void *arg);

extern SemaphoreHandle_t xBinarySemaphore;
extern SemaphoreHandle_t mqtt_xBinarySemaphore;
extern SemaphoreHandle_t xBinarySemaphore_clear;

TagType get_tag_type(uint8_t *data, uint16_t dataLen);
double calculate_temperature(uint16_t adc_raw, uint16_t cali_raw);
void handle_tag_xy(BaseDataFrame_t *frame, bool *flag_ptr);
void handle_tag_yh(BaseDataFrame_t *frame, bool *flag_ptr);
void rfidModuleInit();
void ctrl_rfid_mode(uint8_t mode);

#endif