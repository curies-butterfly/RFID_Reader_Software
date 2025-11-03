#include "data_deal.h"
#include "rfidmodule.h"
#include "parameter.h"
#include "rfidnetwork.h"
#include "dev_info.h"


#define NB_FLAGS   0  // AT+UNBSEND 的 flags 参数

/**
 * send_data:json转十六进制字符串
 */
static char *ascii_to_hex(const char *ascii)
{
    if (!ascii) return NULL;

    size_t ascii_len = strlen(ascii);
    char *hex = (char *)malloc(ascii_len * 2 + 1);
    if (!hex) return NULL;

    for (size_t i = 0; i < ascii_len; i++) {
        sprintf(&hex[i * 2], "%02X", (unsigned char)ascii[i]);
    }
    hex[ascii_len * 2] = '\0';
    return hex;
}


static void send_json_over_nb(const char *json_str)
{
    if (!json_str) return;

    char *hex_str = ascii_to_hex(json_str);
    if (!hex_str) {
        printf("[send_json_over_nb] malloc hex_str failed\n");
        return;
    }

    size_t json_len = strlen(json_str);
    size_t at_cmd_len = json_len * 2 + 40;

    char *at_cmd = (char *)malloc(at_cmd_len);
    if (!at_cmd) {
        printf("[send_json_over_nb] malloc at_cmd failed\n");
        free(hex_str);
        return;
    }

    snprintf(at_cmd, at_cmd_len, "AT+UNBSEND=%zu,%s,%d\r\n", json_len, hex_str, NB_FLAGS);

    uart_write_bytes(UART_NUM_2, at_cmd, strlen(at_cmd));//

    printf("[send_json_over_nb] send: %s", at_cmd);

    free(at_cmd);
    free(hex_str);
}

static void Json_info_to_middleware()
{
    // TODO: Implement the function to send JSON info to middleware
    EPC_Info_t  **EPC_ptr2 = &LTU3_Lable;
    char  *epc_data2 = NULL;
    if(epcCnt == 0)         //防止出现空指针
    {
        epc_data2 = (char*)malloc(sizeof(char) * 1 * 120);
        memset(epc_data2,'\0',sizeof(char) * 1 * 120);
    }
    else
    {
        epc_data2 = (char*)malloc(sizeof(char) * epcCnt * 120);
        memset(epc_data2,'\0',sizeof(char) * epcCnt * 120);
    }
    char  one_epc_data2[120] = {'\0'};
    uint16_t epc_read_rate2 = 20;
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr2[i] == NULL)
            break;
        memset(one_epc_data2,0,sizeof(one_epc_data2));

        EPC_ptr2[i]->last_temp =(EPC_ptr2[i]->tempe)/100.0;//EPC_ptr

        sprintf(one_epc_data2,"{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",             
        EPC_ptr2[i]->epcId[0],EPC_ptr2[i]->epcId[1],EPC_ptr2[i]->tempe/100.0,EPC_ptr2[i]->antID,EPC_ptr2[i]->rssi); 
        strcat(epc_data2,one_epc_data2);
        strcat(epc_data2,",");
    }
    epc_data2[strlen(epc_data2) - 1] = '\0';
    size_t size2 = 0;
    char *json_str2 = NULL;
    size2 = asprintf(&json_str2,"{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",
            epcCnt,epc_read_rate2,epc_data2);
    free(epc_data2);
    
    mqtt_client_publish(send_topic, json_str2, size2, 0, 1);
    
    free(json_str2);
}


void publish_epc_data2()
{
    

    if (epcCnt != 0) //有数据才上报
    {
        
        if(sys_info_config.sys_networking_mode==SYS_NETWORKING_UNB){
        //    send_json_over_nb(json_str2); 
           loraInfoToGateway();   
        }else{
            Json_info_to_middleware();//json data to middleware
        }
        const char *ip = get_effective_ip_str();//获取有效IP
        if (ip != NULL) {
            Json_info_to_middleware();//json data to middleware
        }
     
    }
   
   
    
    // EPC_Info_t  **EPC_ptr2 = &LTU3_Lable;
    // char  *epc_data2 = NULL;
    
    // if(epcCnt == 0) // 防止空指针
    // {
    //     epc_data2 = (char*)malloc(sizeof(char) * 1 * 120);
    //     memset(epc_data2, '\0', sizeof(char) * 1 * 120);
    // }
    // else
    // {
    //     epc_data2 = (char*)malloc(sizeof(char) * epcCnt * 120);
    //     memset(epc_data2, '\0', sizeof(char) * epcCnt * 120);
    // }
    
    // char  one_epc_data2[120] = {'\0'};
    // uint16_t epc_read_rate2 = 20;
    
    // for(int i = 0; i < 120; i++)
    // {
    //     if(EPC_ptr2[i] == NULL)
    //         break;
    //     memset(one_epc_data2, 0, sizeof(one_epc_data2));
    
    //     EPC_ptr2[i]->last_temp = EPC_ptr2[i]->tempe / 100.0;
    
    //     if (sys_info_config.sys_networking_mode == SYS_NETWORKING_UNB)
    //     {
    //         // 只包含 epc 和 tem 字段
    //         sprintf(one_epc_data2,
    //             "{\"epc\":\"%02x%02x\",\"tem\":%.2f}",
    //             EPC_ptr2[i]->epcId[0], EPC_ptr2[i]->epcId[1],
    //             EPC_ptr2[i]->tempe / 100.0);
    //     }
    //     else
    //     {
    //         // 全字段输出
    //         sprintf(one_epc_data2,
    //             "{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",
    //             EPC_ptr2[i]->epcId[0], EPC_ptr2[i]->epcId[1],
    //             EPC_ptr2[i]->tempe / 100.0,
    //             EPC_ptr2[i]->antID, EPC_ptr2[i]->rssi);
    //     }
    
    //     strcat(epc_data2, one_epc_data2);
    //     strcat(epc_data2, ",");
    // }
    
    // // 删除最后多余的逗号
    // if (strlen(epc_data2) > 0)
    //     epc_data2[strlen(epc_data2) - 1] = '\0';
    
    // size_t size2 = 0;
    // char *json_str2 = NULL;
    // size2 = asprintf(&json_str2,
    //     "{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",
    //     epcCnt, epc_read_rate2, epc_data2);
    
    // free(epc_data2);
    
    // if (epcCnt != 0) // 有数据才上报
    // {
    //     if (sys_info_config.sys_networking_mode == SYS_NETWORKING_UNB)
    //     {
    //         send_json_over_nb(json_str2);
    //     }
    //     else
    //     {
    //         mqtt_client_publish(send_topic, json_str2, size2, 0, 1);
    //     }
    
    //     const char *ip = get_effective_ip_str();
    //     if (ip != NULL)
    //     {
    //         mqtt_client_publish(send_topic, json_str2, size2, 0, 1);
    //     }
    // }
    
    // free(json_str2);
}



// CRC-16 Modbus（多项式 0x8005，初值 0xFFFF，Little Endian 输出）
static uint16_t modbus_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}


void loraInfoToGateway(){
    EPC_Info_t  **EPC_ptr2 = &LTU3_Lable;
    
    if(epcCnt == 0) return;    //防止出现空指针
  
    uint8_t data_len = epcCnt * 6; // 每个标签6字节
    size_t frame_len = 1 + 1 + data_len + 2 + 1; // 帧头1字节，数据长度1字节，数据区，CRC16 2字节，帧尾1字节

    // TagInfo tags[] = {
    //     { .EPCID = 0xCB08, .tempe = 30.88, .ant = 2 },
    //     { .EPCID = 0xD308, .tempe = -15.25, .ant = 2 }
    // };

    // size_t tag_count = sizeof(tags) / sizeof(TagInfo);
    // uint8_t data_len = tag_count * 6;
    // size_t frame_len = 1 + 1 + data_len + 2 + 1;

    uint8_t *frame = (uint8_t *)malloc(frame_len);
    if (!frame) return;

    size_t i = 0;
    frame[i++] = 0xAA;         // 帧头
    frame[i++] = data_len;     // 数据区长度

    // 每个标签数据（6字节）
    for (size_t t = 0; t < epcCnt; t++) {
        if (EPC_ptr2[t] == NULL) continue;
        // frame[i++] = (tags[t].EPCID >> 8) & 0xFF;
        // frame[i++] = tags[t].EPCID & 0xFF;

        // int16_t temp100 = (int16_t)(tags[t].tempe * 100);  // 支持负温
        // frame[i++] = (temp100 >> 8) & 0xFF;
        // frame[i++] = temp100 & 0xFF;

        // frame[i++] = tags[t].ant;
        uint16_t epc = (EPC_ptr2[t]->epcId[0] << 8) | EPC_ptr2[t]->epcId[1];
        int16_t temp = (int16_t)(EPC_ptr2[t]->tempe);
        // uint8_t ant = EPC_ptr2[t]->antID;// 天线号
        uint8_t rssi = EPC_ptr2[t]->rssi;// 天线号
        frame[i++] = (epc >> 8) & 0xFF;//大端序’ 高位
        frame[i++] = epc & 0xFF;//大端序’ 低位

        frame[i++] = (temp >> 8) & 0xFF;//温度 大端序 高位
        frame[i++] = temp & 0xFF;//温度 大端序 低位

        frame[i++] = rssi;    

        frame[i++] = 0x00;  // 保留字段 预留字段 先赋值0x00
    }

    // CRC16 (计算 [长度+数据] 部分)
    uint16_t crc = modbus_crc16(&frame[1], 1 + data_len);
    frame[i++] = (crc >> 8) & 0xFF;  // CRC高位
    frame[i++] = crc & 0xFF;         // CRC低位
    frame[i++] = 0xFF;               // 帧尾

    // 转为十六进制字符串
    char hex_str[frame_len * 2 + 1];
    for (size_t j = 0; j < frame_len; ++j) {
        sprintf(&hex_str[j * 2], "%02X", frame[j]);
    }

    size_t at_cmd_len = strlen(hex_str) + 40;
    char *at_cmd = (char *)malloc(at_cmd_len);
    if (!at_cmd) {
        printf("[send_json_over_nb] at_cmd malloc failed\n");
        free(hex_str);
        return;
    }

    snprintf(at_cmd, at_cmd_len, "AT+UNBSEND=%zu,%s,%d\r\n", strlen(hex_str)/2, hex_str, NB_FLAGS);//构建lora发送AT命令

    // 串口发送
    uart_write_bytes(UART_NUM_2, at_cmd, strlen(at_cmd));
    printf("[send_json_over_nb] send: %s", at_cmd);

    // 释放内存
    free(at_cmd);
    free(frame); 
}


