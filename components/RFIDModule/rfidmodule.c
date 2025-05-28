#include "rfidmodule.h"
#include "string.h"
#include "stdlib.h"
#include "stdint.h"
#include "freertos/semphr.h"//信号量头文件
#include <stdbool.h>
#include "rfidnetwork.h"
#include <math.h>

static const char *TAG = "RFID_Del";

BaseDataFrame_t  UARTRecvFrame;
EPC_Info_t  *LTU3_Lable[120] = {NULL};  //
uint16_t    epcCnt = 0;
uint16_t    epc_read_speed = 0;
uint8_t frameDealFlag = 0;         //帧处理flag,当为 1 时表示成接收了一帧数据,
                                   //当为 2 时表示成功接收并处理了一帧数据(CRC校验成功) ,处理好的数据放在UARTRecvFrame

char *FreqTab[40] = {"国标 920~925MHz","国标 840~845MHz","国标 840~845MHz 和 920~925MHz",\
                   "FCC,902~928MHz","ETSI, 866~868MHz","JP, 916.8~920.4 MHz",\
                   "TW, 922.25~927.75 MHz","ID, 923.125~925.125 MHz","RUS, 866.6~867.4 MHz",\
                   "TEST,802.75~998.75MHz,间隔1MHz","JP_LBT,916.8~920.8MHz日本带 LBT 的频段"};

char *ProtocolTab[40] ={"ISO18000-6C/EPC C1G2","ISO18000-6B","国标 GB/T 29768-2013","国军标 GJB 7383.1-2011"};

#define P1 0.000181
#define P2 -0.0079
#define P3 1.81
#define P4 -67.7

rfid_read_config_t rfid_read_config = {
    .rfid_read_on_off = RFID_READ_ON,
    .rfid_read_mode = RFID_READ_MODE_CONTINUOUS,
    .ant_sel =0x01,
    .read_interval_time = 1000,
};

#define EXAMPLE_ESP_Label_Mode      CONFIG_Label_Mode
uint8_t type_epc;
/*****************************************************
函数名称：void ClearRecvFlag()
函数功能：清除接收标志
入口参数：无
返回值：无
注：
*********************************************************/
void ClearRecvFlag()              
{
    frameDealFlag = 0;
}

/*****************************************************
函数名称：void WaitFlag(uint8_t type, uint16_t waitTime)   
函数功能：等待数据接收
入口参数：
返回值：无
注：
******************uint8_t type, ***************************************/
result_t WaitFlag(uint16_t waitTime) 
{
    while(waitTime--)
    {
        if(frameDealFlag == 2)
            return  Ok; 
        // if(frameDealFlag == 1)
        //     return  Error; 
        vTaskDelay(100/portTICK_PERIOD_MS);       
    }
    return ErrorTimeout;
}  


/*****************************************************
函数名称：void RFID_SendBaseFrame(BaseDataFrame_t BaseDataFrame)
函数功能：读写器发送一个基本格式数据帧
入口参数：pProCtrl：4个字节的协议控制字的地址
         pDevAddr：串行设备地址
         DataLen : 指示数据内容字节总长度
         pData   : 上位机指令信息内容
返 回 值：无
注：
*********************************************************/
void RFID_SendBaseFrame(BaseDataFrame_t BaseDataFrame)
{
    uint16_t framLen = sizeof(uint8_t)*(BaseDataFrame.DataLen + 15);  //数据帧总长度，有4个Byte的冗余
    uint8_t *SendBuff = (uint8_t*)malloc(framLen);      //为数据帧申请内存
    uint16_t index = 0;
    uint8_t  uint8Type[3] = {'\0'};
    uint16_t crcValue = 0;
//  printf("framLen:%d\r\n",framLen);
    for(int i = 0 ; i < framLen ; i++)                      //初始化缓存
    {
        SendBuff[i] = '\0';
    }
    SendBuff[index++] = 0x5A;               //帧头 1个Byte 0x5A
    for(int i = 0 ; i < 4; i++)             //协议控制字 4个Byte 
    {
        SendBuff[index++] = BaseDataFrame.ProCtrl[i];
    }
//    printf("\r\n");
//  SendBuff[index++] = pDevAddr;           //串行设备地址 1个Byte

    Uint16toByte(BaseDataFrame.DataLen,uint8Type);        //数据长度 2个Byte
    SendBuff[index++] = uint8Type[0];
    SendBuff[index++] = uint8Type[1];
    // printf("uint8Type[0]:%d,uint8Type[1]:%d\r\n",uint8Type[0],uint8Type[1]);

    for(int i = 0 ; i < BaseDataFrame.DataLen;i++)
    {
        // if((BaseDataFrame.pData+i) == NULL)
        // {
        //     printf("pData地址错误\r\n");
        //     return;
        // }
        SendBuff[index++] = BaseDataFrame.pData[i];
    }

    //除帧头外的数据的 CRC16 校验和
    crcValue = CRC16_CCITT(SendBuff+1,index-1);
    Uint16toByte(crcValue,uint8Type); 
    SendBuff[index++] = uint8Type[0];
    SendBuff[index++] = uint8Type[1];

    // ESP_LOGE("EPC_READ!!!:%x",(char*)SendBuff);
     // 打印SendBuff内容
    //  printf("SendBuff Content (Hex):!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ");
    //  for(int i = 0; i < index; i++) {
    //      printf("%02X ", SendBuff[i]);  // 以16进制格式打印每个字节
    //  }
    //  printf("\n");
    RFID_SendBytes((char*)SendBuff,(size_t)index);
    free(SendBuff); 
}

/*****************************************************
函数名称：void RFID_SendBaseFrameTest()
函数功能：基本格式数据帧发送测试
入口参数：
返回值：无
注：
*********************************************************/
void RFID_SendBaseFrameTest()
{
    BaseDataFrame_t TestDataFrame;
    TestDataFrame.ProCtrl[0] = 0x00;
    TestDataFrame.ProCtrl[1] = 0x01;
    TestDataFrame.ProCtrl[2] = 0x02;
    TestDataFrame.ProCtrl[3] = 0x10;
    TestDataFrame.DevAddr = 0;
    TestDataFrame.DataLen = 0;
    TestDataFrame.pData = NULL;
    RFID_SendBaseFrame(TestDataFrame);
}

// 2025-04-29 19:25:18.325 -[Info]- send-   [MsgBaseStop]-        [5A000102FF0000885A]
// 2025-04-29 19:25:18.326 -[Info]- send-   [MsgBaseInventoryEpc]-[5A0001021000050000000F01D788]
// 2025-04-29 19:25:18.369 -[Info]- receive-[MsgBaseInventoryEpc]-[5A0001021000010029B5]

/*****************************************************
函数名称：void RFID_SendCmdStop()
函数功能：向RFID 模组发送停止命令
入口参数：
返回值：无
注：
*********************************************************/
void RFID_SendCmdStop()
{
    BaseDataFrame_t TestDataFrame;
    TestDataFrame.ProCtrl[0] = 0x00;
    TestDataFrame.ProCtrl[1] = 0x01;
    TestDataFrame.ProCtrl[2] = 0x02;
    TestDataFrame.ProCtrl[3] = 0xff;
    TestDataFrame.DevAddr = 0;
    TestDataFrame.DataLen = 0;
    TestDataFrame.pData = NULL;
    RFID_SendBaseFrame(TestDataFrame);
}


/*****************************************************
函数名称：void RFID_SendCmdStop()
函数功能：RFID模组停止读数据
入口参数：无
返回值：  操作结果
注：
*********************************************************/
result_t RFID_StopRead()
{
    result_t ret;
    ClearRecvFlag();
    RFID_SendCmdStop();
    ret = WaitFlag(20);
    if(ret != Ok)
        return ret;
    if(UARTRecvFrame.ProCtrl[3] == 0xFF && UARTRecvFrame.pData[0] == 0x00)
    {
        printf("RFID Stop Read: ok\r\n");
        return Ok;
    }
    printf("RFID Stop Read :Failed\r\n");
    return Error;  
}


/*****************************************************
函数名称：result_t RFID_SetPower(Dictionary DicPower, uint8_t cfgMake, uint8_t ParamSave)
函数功能：用于对读写器当前的功率进行配置
入口参数：DicPower:天线参数结构体
         cfgMake: 配置标记。0， 同时配置读写功率    1， 只配置读功率    2， 只配置写功率  
         ParamSave: 参数持久化  0， 掉电不保存     1， 掉电保存
返回值：  操作结果
注：
*********************************************************/
result_t RFID_SetPower(Dictionary DicPower, uint8_t cfgMake, uint8_t ParamSave)
{
    result_t ret;
    BaseDataFrame_t TestDataFrame;
    uint8_t DicPower_cfg[10] = {'\0'};
    DicPower_cfg[0] = DicPower.AntennaNo;
    DicPower_cfg[1] = DicPower.Power;
    /* Bug 记录
       按照手册上的格式加入配置标记和参数持久化 参数，配置会不成功，初步定位为RFID模组问题
     */
    // DicPower_cfg[2] = 0xFE;          
    // DicPower_cfg[3] = cfgMake;
    // DicPower_cfg[4] = 0xFF;
    // if(ParamSave)
    //     DicPower_cfg[5] = 0x01;
    // else
    //     DicPower_cfg[5] = 0x00;
    TestDataFrame.ProCtrl[0] = 0x00;
    TestDataFrame.ProCtrl[1] = 0x01;
    TestDataFrame.ProCtrl[2] = 0x02;
    TestDataFrame.ProCtrl[3] = 0x01;
    TestDataFrame.DevAddr = 0;
    TestDataFrame.DataLen = 2;
    TestDataFrame.pData = DicPower_cfg;
    ClearRecvFlag();
    RFID_SendBaseFrame(TestDataFrame);
    ret = WaitFlag(100);
    if(ret != Ok)
        return ret;

    if(UARTRecvFrame.ProCtrl[3] == 0x01)
    {
        if(UARTRecvFrame.pData[0] == 0x00)
        {
            printf("Set Power ok,ant No:%d,SetValue:%d\r\n",DicPower.AntennaNo,DicPower.Power);
            return Ok;
        }
        else if(UARTRecvFrame.pData[0] == 0x01)
        {
            printf("Port Hardware no't supported\r\n");
            return ErrorAccessRights;
        }
        else if(UARTRecvFrame.pData[0] == 0x02)
        {
            printf("Power Hardware no't supported\r\n");
            return ErrorAccessRights;
        }
        else
        {
            printf("Failed to save\r\n");
            return ErrorAccessRights;
        }
    }
    return Error;
}

/*****************************************************
函数名称：result_t RFID_GetPower(PoweInfo_t *PoweInfo)
函数功能：读取RFID模组天线的功率
入口参数：天线功率数据消息结构体指针
返回值：  操作结果
注：
*********************************************************/
result_t RFID_GetPower(PoweInfo_t *PoweInfo)
{
    result_t ret;
    BaseDataFrame_t DataFrame;
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x02;
    DataFrame.DevAddr = 0;
    DataFrame.DataLen = 0;
    DataFrame.pData = NULL;
    ClearRecvFlag();
    RFID_SendBaseFrame(DataFrame);
    ret = WaitFlag(100);
    if(ret != Ok)
        return ret;
    if(UARTRecvFrame.ProCtrl[3] == 0x02)
    {   
        PoweInfo->antNum = UARTRecvFrame.DataLen/2;
        for(int i = 0; i < PoweInfo->antNum; i++)
        {
            PoweInfo->power[i] = UARTRecvFrame.pData[2*i+1];
        } 
        return Ok;
    }
    return Error;
}


/*****************************************************
函数名称：void RFID_ShowPower(PoweInfo_t PoweInfo);
函数功能：显示RFID模组天线的功率信息
入口参数：天线功率数据消息结构体
返回值：  无
注：
*********************************************************/
void RFID_ShowPower(PoweInfo_t PoweInfo)
{
    printf("\r\n天线数目:%d\r\n",PoweInfo.antNum);
    for(int i = 0; i < PoweInfo.antNum; i++)
        printf("天线%d功率:%ddBm\r\n",i+1,PoweInfo.power[i]);
}


/*****************************************************
函数名称：result_t RFID_GetCapacity(CapacityInfo_t *CapacityInfo)
函数功能：查询RFID模组的读写能力
入口参数：读写能力结构体指针
返回值：  无
注：
*********************************************************/
result_t RFID_GetCapacity(CapacityInfo_t *CapacityInfo)
{
    result_t ret = Error;
    BaseDataFrame_t DataFrame;
    int i = 0;
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x00;
    DataFrame.DevAddr = 0;
    DataFrame.DataLen = 0;
    DataFrame.pData = NULL;
    ClearRecvFlag();
    RFID_SendBaseFrame(DataFrame);
    ret = WaitFlag(100);
    if(ret != Ok)
        return ret;
    if(UARTRecvFrame.ProCtrl[3] == 0x00)    //查询读写器 RFID 能力 MID = 0x00
    {
        //printf("读写器能力查询：数据长度:%d\r\n",UARTRecvFrame.DataLen);
        CapacityInfo->minPower = UARTRecvFrame.pData[i++];
        CapacityInfo->maxPower = UARTRecvFrame.pData[i++];
        CapacityInfo->antNum = UARTRecvFrame.pData[i++]; 
        CapacityInfo->FreqBandList.num = ((int16_t)UARTRecvFrame.pData[i]<<8|(int16_t)UARTRecvFrame.pData[i+1]);
        i++;
        CapacityInfo->FreqBandList.List = (uint8_t*)malloc(sizeof(uint8_t)*CapacityInfo->FreqBandList.num);
        memcpy(CapacityInfo->FreqBandList.List,&UARTRecvFrame.pData[i+1],CapacityInfo->FreqBandList.num);
        i += CapacityInfo->FreqBandList.num+1;
        CapacityInfo->ProtocolList.num = ((int16_t)UARTRecvFrame.pData[i]<<8|(int16_t)UARTRecvFrame.pData[i+1]);
        i+=2;
        CapacityInfo->ProtocolList.List = (uint8_t*)malloc(sizeof(uint8_t)*CapacityInfo->ProtocolList.num);
        memcpy(CapacityInfo->ProtocolList.List,&UARTRecvFrame.pData[i],CapacityInfo->ProtocolList.num);   
    }
    else
        return Error; 
    return ret;
}


/*****************************************************
函数名称：void RFID_ShowCapacityInfo(CapacityInfo_t CapacityInfo)
函数功能：显示RFID模组的读写能力
入口参数：读写能力结构体指针
返回值：  无
注：
*********************************************************/
void RFID_ShowCapacityInfo(CapacityInfo_t CapacityInfo)
{
    printf("-------Reader RFID capability show---------\r\n");
    printf("\tMinimum transmitting power:%ddb\r\n", CapacityInfo.minPower);
    printf("\tMaximum transmitting power:%ddb\r\n", CapacityInfo.maxPower);
    printf("\tNumber of antennas:%d\r\n", CapacityInfo.antNum);
    printf("\tSupported band list:\r\n");
    for(int i=0; i<CapacityInfo.FreqBandList.num; i++)
    {
        printf("\t\tband%d,code:%d,%s\r\n",i+1,CapacityInfo.FreqBandList.List[i],FreqTab[i]);
    }
    printf("\tSupports RFID protocol columns:\r\n");
    for(int i=0; i<CapacityInfo.ProtocolList.num; i++)
    {
        printf("\t\tprotocol%d,code:%d,%s\r\n",i+1,CapacityInfo.ProtocolList.List[i],ProtocolTab[i]);
        }
}


/*****************************************************
函数名称：result_t RFID_GetFreqRanger(uint8_t *FreqRang)
函数功能：读取RFID模组的RF频段
入口参数：读取结果存放指针
返回值：  操作结果
注：
*********************************************************/
result_t RFID_GetFreqRanger(uint8_t *FreqRang)
{
    result_t ret;
    BaseDataFrame_t DataFrame;
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x04;
    DataFrame.DevAddr = 0;
    DataFrame.DataLen = 0;
    DataFrame.pData = NULL;
    ClearRecvFlag();
    RFID_SendBaseFrame(DataFrame);
    ret = WaitFlag(100);
    if(ret != Ok)
        return ret;
    if(UARTRecvFrame.ProCtrl[3] == 0x04)
    {   
        *FreqRang = UARTRecvFrame.pData[0];
        return Ok;
    }
    return Error;
}



/*****************************************************
函数名称：result_t RFID_GetWorkFreq(WorkFreq_t *WorkFreq_t)
函数功能：读取RFID模组的工作频率
入口参数：读取结果存放指针
返回值：  操作结果
注：
*********************************************************/
result_t RFID_GetWorkFreq(WorkFreq_t *WorkFreq)
{
    result_t ret = Error;
    BaseDataFrame_t DataFrame;
    int i  = 0;
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x06;
    DataFrame.DevAddr = 0;
    DataFrame.DataLen = 0;
    DataFrame.pData = NULL;
    ClearRecvFlag();
    RFID_SendBaseFrame(DataFrame);
    ret = WaitFlag(100);
    if(ret != Ok)
        return ret;
    if(UARTRecvFrame.ProCtrl[3] == 0x06)    //查询RFID模组的工作频率 MID = 0x00
    {
        printf("工作频率查询成功\r\n");
        // for(int k = 0; k < UARTRecvFrame.DataLen; k++)
        // {
        //     printf("%x\t", UARTRecvFrame.pData[k]);
        // }
        // printf("\r\n");
        WorkFreq->autoSet = UARTRecvFrame.pData[i++];
        if(WorkFreq->autoSet == 0)
            printf("RFID Module No't auto selecte frequency point\r\n");
        else
            printf("RFID Module auto selecte frequency point \r\n");
        WorkFreq->freqNum = ((int16_t)UARTRecvFrame.pData[i]<<8|(int16_t)UARTRecvFrame.pData[i+1]);
        i+=2;
        WorkFreq->freqList = (uint8_t*)malloc(sizeof(uint8_t)*WorkFreq->freqNum);
        memcpy( WorkFreq->freqList,&UARTRecvFrame.pData[i],WorkFreq->freqNum);   
        for(int j = 0; j < WorkFreq->freqNum; j++)
        {
            printf("Freq %d: %d\r\n",j,WorkFreq->freqList[j]);
        }
    }
    return Ok;
}

/*****************************************************
函数名称：void RFID_SendReadEpcCmd(uint8_t ant,uint8_t mode)
函数功能：发送读取ECP（温度命令）
入口参数：ant：要读取的天线，可以多个或在在一起 ，如ant1|ant4,天线编号为1——8
         mode：0：单次读取   1：连续读取
返回值：  无
注：
*********************************************************/

// 2025-04-29 19:25:18.325 -[Info]- send-   [MsgBaseStop]-        [5A000102FF0000885A]
// 2025-04-29 19:25:18.326 -[Info]- send-   [MsgBaseInventoryEpc]-[5A0001021000050000000F01D788]
// 2025-04-29 19:25:18.369 -[Info]- receive-[MsgBaseInventoryEpc]-[5A0001021000010029B5]

// 5A 00 01 02 10 00 05 00 00 00 0F 01 D7 88
// 5A 00 01 02 10 00 07 00 00 00 0F 01 12 02 56 C5 
// 5A 00 01 02 10 00 05 00 00 00 0F 01 D7 88
void RFID_SendReadEpcCmd(uint8_t ant,uint8_t mode)
{
    BaseDataFrame_t DataFrame;
    uint8_t Cmd[8] = {'\0'};
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x10;
    DataFrame.DevAddr = 0;

    if(type_epc==TAG_TYPE_YH)
    {
        DataFrame.DataLen = 7;  
        Cmd[5] = 0x12;          //读取 CTESIUS系列芯片温度数据,LTU31
        Cmd[6] = 0x02;   
    }else{
        DataFrame.DataLen = 5;     
    }
    
    // DataFrame.DataLen = 6;
    Cmd[0] = 0;     //MSB ，大端格式，Bit 31-24
    Cmd[1] = 0;
    Cmd[2] = 0;
    Cmd[3] = ant;   // MLB Bit 7-0
    if(mode) 
        Cmd[4] = 0x01;
    else    
        Cmd[4] = 0x00;
   
    DataFrame.pData = Cmd;
    ClearRecvFlag();
    RFID_StopRead();
    RFID_SendBaseFrame(DataFrame);
}
result_t RFID_ReadEPC(rfid_read_config_t rfid_read_config)
{
    BaseDataFrame_t DataFrame;
    uint8_t Cmd[8] = {'\0'};
    DataFrame.ProCtrl[0] = 0x00;
    DataFrame.ProCtrl[1] = 0x01;
    DataFrame.ProCtrl[2] = 0x02;
    DataFrame.ProCtrl[3] = 0x10;
    DataFrame.DevAddr = 0;

    if(type_epc==TAG_TYPE_YH)
    {
        DataFrame.DataLen = 7;  
        Cmd[5] = 0x12;          //读取 CTESIUS系列芯片温度数据,LTU31
        Cmd[6] = 0x02;   
    }else{
        DataFrame.DataLen = 5;     
    }

    Cmd[0] = 0;     //MSB ，大端格式，Bit 31-24
    Cmd[1] = 0;
    Cmd[2] = 0;
    Cmd[3] = rfid_read_config.ant_sel;   // MLB Bit 7-0
    if(rfid_read_config.rfid_read_mode == RFID_READ_MODE_CONTINUOUS)
        Cmd[4] = 0x01;
    else
        Cmd[4] = 0x00;
    // Cmd[5] = 0x12;          //读取 CTESIUS系列芯片温度数据,LTU31
    // Cmd[6] = 0x02;
    DataFrame.pData = Cmd;

    if(rfid_read_config.rfid_read_on_off == RFID_READ_OFF)
    {
        RFID_StopRead();
        return RFID_StopRead();
    }
    else if(rfid_read_config.rfid_read_on_off == RFID_READ_ON)
    {    
        ClearRecvFlag();
        if(RFID_StopRead() != Ok)
            return Error;
        RFID_SendBaseFrame(DataFrame);
        return Ok;
    }
    else
        return ErrorInvalidParameter;
    return Error;
}

void RFID_ShowEpc(EPC_Info_t  **EPC_ptr)
{
    char buf[200];
    printf("\r\n\r\n");
   // uart2_SendStr("\r\n\r\n\r\n");
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr[i] == NULL)
            break;
        memset(buf,0,sizeof(buf));
        sprintf(buf,"No:%d       EPCID:%x%x        Temp:%.2f\r\n",i+1,EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0);
       // uart2_SendStr(buf);
        printf("No:%d       EPCID:%x%x    antID:%d    Temp:%.2f\r\n",i+1,EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->antID,EPC_ptr[i]->tempe/100.0);
    }
}


//读EPC任务
// void RFID_ReadEpcTask(void *arg)
// {
//     BaseDataFrame_t  EPCFrame;
//     bool flag = false;
//     // uint8_t abs_err_temp;

//     while(1)
//     {
//         // int index = 0;
//         if( xQueueReceive( RFID_EpcQueue, &( EPCFrame ), ( TickType_t ) 10 ) )
//         {
//             for(index = 0; index < 120; index++)
//             {
              
//                 if(0== EPCFrame.pData[2] && 0 == EPCFrame.pData[3])//无EPCID
//                 {
//                   break;
//                 }

//                 //判断标签厂商
//                 switch(get_tag_type(EPCFrame.pData, EPCFrame.DataLen))
//                 {
//                     case TAG_TYPE_YH:


//                         break;
//                     case TAG_TYPE_XY:


//                         break;
//                     default:
//                         ESP_LOGI("Unknowed_company","未知标签厂商!");
//                         break;
//                 }

//                 if(LTU3_Lable[index] == NULL)
//                 {
//                     epcCnt++;
//                     LTU3_Lable[index] = (EPC_Info_t*)malloc(sizeof(EPC_Info_t));
//                     if (LTU3_Lable[index] == NULL) {
//                         ESP_LOGE(TAG, "Memory allocation failed for EPC_Info_t");
//                         break; // 内存分配失败跳出
//                     }
//                     LTU3_Lable[index]->epcId[0] = EPCFrame.pData[2];
//                     LTU3_Lable[index]->epcId[1] = EPCFrame.pData[3];
//                     LTU3_Lable[index]->antID = EPCFrame.pData[6];
//                     LTU3_Lable[index]->rssi = EPCFrame.pData[8];
//                     LTU3_Lable[index]->tempe = ((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13]);
//                     LTU3_Lable[index]->last_temp = ((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13])/100.0;
//                     LTU3_Lable[index]->filtered_tempe = ((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13])/100.0;//滤波初始值
//                     sprintf((char*)modbusRtuDataTAB[index],"%x%x:%.2f  ",LTU3_Lable[index]->epcId[0],LTU3_Lable[index]->epcId[1],((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13])/100.0);
//                     flag=true;
//                     break;
//                 }
//                 if(LTU3_Lable[index]->epcId[0] == EPCFrame.pData[2] && LTU3_Lable[index]->epcId[1] == EPCFrame.pData[3])
//                 {
//                     sprintf((char*)modbusRtuDataTAB[index],"%x%x:%.2f  ",LTU3_Lable[index]->epcId[0],LTU3_Lable[index]->epcId[1],((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13])/100.0);
//                     // 先保存旧温度值
//                     // float old_temp = LTU3_Lable[index]->tempe / 100.0; 
//                     // LTU3_Lable[index]->last_temp = old_temp;           // 更新 last_temp 为旧值

//                     // // 再更新新温度值
//                     // int16_t raw_tempe = (int16_t)(EPCFrame.pData[12] << 8 | EPCFrame.pData[13]);
//                     // LTU3_Lable[index]->tempe = raw_tempe;              // 更新 tempe 为新值
//                     // float new_temp = raw_tempe / 100.0;

//                     // 更新已有标签
//                     int16_t raw_tempe = (EPCFrame.pData[12] << 8) | EPCFrame.pData[13];
//                     float new_temp = raw_tempe / 100.0f;

//                     // 保存旧温度值
//                     float old_temp = LTU3_Lable[index]->filtered_tempe;
//                     LTU3_Lable[index]->last_temp = old_temp;

//                     // 应用低通滤波
//                     float filtered_temp = ALPHA * new_temp + (1 - ALPHA) * old_temp;
//                     LTU3_Lable[index]->filtered_tempe = filtered_temp;
    

//                     LTU3_Lable[index]->tempe =  LTU3_Lable[index]->filtered_tempe*100.0f;
//                     LTU3_Lable[index]->antID = EPCFrame.pData[6];
//                     LTU3_Lable[index]->rssi = EPCFrame.pData[8];
//                     // abs_err_temp= abs((int)((LTU3_Lable[index]->last_temp)-(LTU3_Lable[index]->tempe)/100.0));

//                     // // 计算温差
//                     // abs_err_temp = (uint8_t)(fabsf(new_temp - old_temp)); // 转换为整数

//                     // 判断温差是否超阈值
//                     // if (abs_err_temp >= err_value) {
//                     //     ESP_LOGI(TAG, "epcID:%02x%02x, last: %.1f, now: %.1f, err: %d",
//                     //             LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1],
//                     //             old_temp, new_temp, abs_err_temp);
//                     //     flag = true;
//                     // }
//                     // break;

//                      // 计算温差
//                      abs_err_temp = (uint8_t)(fabsf(filtered_temp - old_temp));

//                      // 判断是否触发报警
//                      if (abs_err_temp >= err_value) {
//                          ESP_LOGI(TAG, "epcID:%02x%02x, last: %.1f, now: %.1f, err: %d",
//                                  LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1],
//                                  old_temp, filtered_temp, abs_err_temp);
//                          flag = true;
//                      }
//                      break;
//                 }
//             }

//             ESP_LOGI(TAG,"%02x%02x:%.2f\r\n",EPCFrame.pData[2],EPCFrame.pData[3],(((int16_t)EPCFrame.pData[12]<<8|(int16_t)EPCFrame.pData[13])/100.0));

//             // printf("epcCnt:%d\r\n",epcCnt);
//             // free(EPCFrame.pData); //释放EPC数据帧的数据存储地址

//                 // xSemaphoreGive(uart1_rx_xBinarySemaphore);//给予一次信息量

//             epc_read_speed++;
//             if(epc_read_speed > 800) 
//             {
//                 epc_read_speed = 0;
//             }

//             // xSemaphoreGive(xBinarySemaphore);     //给予一次信息量

//             if (flag){
//                 xSemaphoreGive(mqtt_xBinarySemaphore);//给予一次信息量
//                 flag=false;
//             }
//             free(EPCFrame.pData);//20250219
                
//         }
//         vTaskDelay(100/portTICK_PERIOD_MS);
//     }
// }
void RFID_ReadEpcTask(void *arg)
{
    BaseDataFrame_t EPCFrame;
    bool flag = false;

    while (1) {
        if (xQueueReceive(RFID_EpcQueue, &EPCFrame, (TickType_t)5)) {
            TagType tagType = get_tag_type(EPCFrame.pData, EPCFrame.DataLen);

            switch (tagType) {
                case TAG_TYPE_YH:
                    // ESP_LOGI("YH", "111111111!");
                    
                    handle_tag_yh(&EPCFrame, &flag);
                    ESP_LOGI(TAG, "%02x%02x:%.2f\r\n",
                        EPCFrame.pData[2], EPCFrame.pData[3],
                        (((int16_t)EPCFrame.pData[12] << 8 | EPCFrame.pData[13]) / 100.0f));
                    break;
                case TAG_TYPE_XY:
                    // ESP_LOGI("XY", "222222222!");
                    
                    handle_tag_xy(&EPCFrame, &flag);
                    
                    break;
                default:
                    ESP_LOGI("Unknowed_company", "未知标签厂商!");
                    break;
            }

            epc_read_speed++;
            if (epc_read_speed > 800) epc_read_speed = 0;


            if (flag) {
                xSemaphoreGive(mqtt_xBinarySemaphore);
                flag = false;
            }

            free(EPCFrame.pData);
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void handle_tag_yh(BaseDataFrame_t *frame, bool *flag_ptr)
{

    // for(int j = 0; j < UARTRecvFrame.DataLen; j++) {
    //     printf("[%d]%02X ",j, UARTRecvFrame.pData[j]);  // Print as hexadecimal
    //     // Alternatively for ASCII: printf("%c", UARTRecvFrame.pData[j]);
    // }
    for (int index = 0; index < 120; index++) {
        if (frame->pData[2] == 0 && frame->pData[3] == 0) break;

        if (LTU3_Lable[index] == NULL) {
            epcCnt++;
            LTU3_Lable[index] = (EPC_Info_t *)malloc(sizeof(EPC_Info_t));
            if (LTU3_Lable[index] == NULL) return;

            LTU3_Lable[index]->epcId[0] = frame->pData[2];
            LTU3_Lable[index]->epcId[1] = frame->pData[3];
            LTU3_Lable[index]->antID = frame->pData[6];
            LTU3_Lable[index]->rssi = frame->pData[8];
            int16_t raw_temp = (frame->pData[12] << 8) | frame->pData[13];
            float temp = raw_temp / 100.0f;

            LTU3_Lable[index]->tempe = raw_temp;
            LTU3_Lable[index]->last_temp = temp;
            LTU3_Lable[index]->filtered_tempe = temp;
            sprintf((char *)modbusRtuDataTAB[index], "%x%x:%.2f  ",
                    LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1], temp);

            *flag_ptr = true;
            break;
        }

        if (LTU3_Lable[index]->epcId[0] == frame->pData[2] &&
            LTU3_Lable[index]->epcId[1] == frame->pData[3]) {

            int16_t raw_temp = (frame->pData[12] << 8) | frame->pData[13];
            float new_temp = raw_temp / 100.0f;
            float old_temp = LTU3_Lable[index]->filtered_tempe;
            LTU3_Lable[index]->last_temp = old_temp;

            float filtered_temp = ALPHA * new_temp + (1 - ALPHA) * old_temp;
            LTU3_Lable[index]->filtered_tempe = filtered_temp;
            LTU3_Lable[index]->tempe = filtered_temp * 100.0f;

            LTU3_Lable[index]->antID = frame->pData[6];
            LTU3_Lable[index]->rssi = frame->pData[8];

            uint8_t abs_err_temp = fabsf(filtered_temp - old_temp);
            if (abs_err_temp >= err_value) {
                ESP_LOGI(TAG, "epcID:%02x%02x, last: %.1f, now: %.1f, err: %d",
                         LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1],
                         old_temp, filtered_temp, abs_err_temp);
                *flag_ptr = true;
            }

            sprintf((char *)modbusRtuDataTAB[index], "%x%x:%.2f  ",
                    LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1], filtered_temp);
            break;
        }
    }
}

void handle_tag_xy(BaseDataFrame_t *frame, bool *flag_ptr)
{
    // 与YH相似，这里可以自定义不同的温度处理逻辑
    // 暂时复用相同逻辑，后期可以加不同滤波系数或结构
    // handle_tag_yh(frame, flag_ptr);

    // for(int j = 0; j < UARTRecvFrame.DataLen; j++) {
    //     printf("[%d]%02X ",j, UARTRecvFrame.pData[j]);  // Print as hexadecimal
    //     // Alternatively for ASCII: printf("%c", UARTRecvFrame.pData[j]);
    // }
    for (int index = 0; index < 120; index++) {
        if (frame->pData[2] == 0 && frame->pData[3] == 0) break;

        if (LTU3_Lable[index] == NULL) {
            epcCnt++;
            LTU3_Lable[index] = (EPC_Info_t *)malloc(sizeof(EPC_Info_t));
            if (LTU3_Lable[index] == NULL) return;

            LTU3_Lable[index]->epcId[0] = frame->pData[2];
            LTU3_Lable[index]->epcId[1] = frame->pData[3];
            LTU3_Lable[index]->antID = frame->pData[10];
            LTU3_Lable[index]->rssi = frame->pData[12];


            uint16_t adc = ((frame->pData[4] <<8)| frame->pData[5]);    // ADC 原始值
            uint16_t cali = ((frame->pData[6]<<8) | frame->pData[7]);   // 校准值（高4位为标志位，低12位为C）

            double temperature = calculate_temperature(adc, cali);   
            
            if (temperature<-10.00) break;
            
            // printf("temp:%0.2f\n",temperature);
            // int16_t raw_temp = (frame->pData[12] << 8) | frame->pData[13];
            float temp = temperature;

            LTU3_Lable[index]->tempe = (uint16_t)(temperature*100);
            LTU3_Lable[index]->last_temp = temp;
            LTU3_Lable[index]->filtered_tempe = temp;
            sprintf((char *)modbusRtuDataTAB[index], "%x%x:%.2f  ",
                    LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1], temp);

            *flag_ptr = true;
            break;
        }

        if (LTU3_Lable[index]->epcId[0] == frame->pData[2] &&
            LTU3_Lable[index]->epcId[1] == frame->pData[3]) {

            // int16_t raw_temp = (frame->pData[12] << 8) | frame->pData[13];
            uint16_t adc = ((frame->pData[4]<<8) | frame->pData[5]);    // ADC 原始值
            uint16_t cali = ((frame->pData[6]<<8) | frame->pData[7]);   // 校准值（高4位为标志位，低12位为C）
            
            // printf("ADC (Raw): 0x%04X\n", adc);          // Print as 4-digit hex
            // printf("Calibration: 0x%04X\n", cali);       // Print as 4-digit hex

            double temperature = calculate_temperature(adc, cali);  
            // printf("temp:%0.2f\n",temperature);
            
            if (temperature<-10.00) break;

            float new_temp = temperature;
            float old_temp = LTU3_Lable[index]->filtered_tempe;
            LTU3_Lable[index]->last_temp = old_temp;

            float filtered_temp = ALPHA * new_temp + (1 - ALPHA) * old_temp;//低通滤波
            LTU3_Lable[index]->filtered_tempe = filtered_temp;
            LTU3_Lable[index]->tempe = (uint16_t)(filtered_temp*100);

            LTU3_Lable[index]->antID = frame->pData[10];
            LTU3_Lable[index]->rssi = frame->pData[12];

            uint8_t abs_err_temp = fabsf(filtered_temp - old_temp);

            ESP_LOGI(TAG, "%02x%02x:%.2f\r\n",
                LTU3_Lable[index]->epcId[0] ,  LTU3_Lable[index]->epcId[1],filtered_temp
                );     

            if (abs_err_temp >= err_value) {
                ESP_LOGI(TAG, "epcID:%02x%02x, last: %.1f, now: %.1f, err: %d",
                         LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1],
                         old_temp, filtered_temp, abs_err_temp);
                *flag_ptr = true;
            }

            sprintf((char *)modbusRtuDataTAB[index], "%x%x:%.2f  ",
                    LTU3_Lable[index]->epcId[0], LTU3_Lable[index]->epcId[1], filtered_temp);
            break;
        }
    }

}


// 计算温度函数
double calculate_temperature(uint16_t adc_raw, uint16_t cali_raw) {
    int16_t C = (cali_raw & 0x0FFF);
    int16_t Cali = C - 256;

    double x = ((int16_t)adc_raw - Cali) / 100.0 + 10.0;

    double x2 = x * x;
    double x3 = x2 * x;
    double T = P1 * x3 + P2 * x2 + P3 * x + P4;
    return T;
}


TagType get_tag_type(uint8_t *data, uint16_t dataLen) {
    if (dataLen == 0x13 && data[0] == 0x00 && data[1] == 0x02) {
        return TAG_TYPE_YH;
    } else if (dataLen == 0x12 && data[0] == 0x00 && data[1] == 0x06) {
        return TAG_TYPE_XY;
    }
    return TAG_TYPE_UNKNOWN;
}

