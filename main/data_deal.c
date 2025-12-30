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
           loraInfoToGateway();  //通过LoRa 将信息发送到网关 
        }else{//4G/WiFi/Ethernet 方式
            EPC_Json_info_to_middleware(sys_info_config.sys_networking_mode);//json data to middleware
        }
        printf("[publish_epc_data2] %d\n",sys_info_config.sys_networking_mode);
     
    }
   
   
   
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




/**
 * @brief 构建 EPC 协议 HEX 字符串（LoRa / MQTT 共用）
 * @return malloc 得到的 HEX 字符串，使用完必须 free
 */
char *build_epc_hex_frame(EPC_Info_t **epc_list, uint16_t epcCnt)
{
    if (epcCnt == 0 || epc_list == NULL) {
        return NULL;
    }

    uint8_t data_len  = epcCnt * 6;
    size_t  frame_len = 1 + 1 + data_len + 2 + 1;

    uint8_t *frame = malloc(frame_len);
    if (!frame) return NULL;

    size_t i = 0;

    /* 帧头 */
    frame[i++] = 0xAA;

    /* 长度 */
    frame[i++] = data_len;

    /* Payload */
    for (uint16_t t = 0; t < epcCnt; t++) {
        if (epc_list[t] == NULL) break;

        uint16_t epc  = (epc_list[t]->epcId[0] << 8) |
                         epc_list[t]->epcId[1];
        int16_t  temp = (int16_t)epc_list[t]->tempe;
        uint8_t  rssi = epc_list[t]->rssi;

        frame[i++] = (epc >> 8) & 0xFF;//EPCID 高位
        frame[i++] = epc & 0xFF;//EPCID 低位

        frame[i++] = (temp >> 8) & 0xFF;//温度 高位
        frame[i++] = temp & 0xFF;//温度 低位

        frame[i++] = rssi;   // 信号质量
        frame[i++] = 0x00;   // 保留字段
    }

    /* CRC16：Length + Payload */
    uint16_t crc = modbus_crc16(&frame[1], 1 + data_len);//计算CRC16 
    frame[i++] = (crc >> 8) & 0xFF;//CRC16 高位
    frame[i++] = crc & 0xFF;//CRC16 低位

    /* 帧尾 */
    frame[i++] = 0xFF;

    /* 二进制 → HEX 字符串 */
    char *hex = malloc(frame_len * 2 + 1);
    if (!hex) {
        free(frame);
        return NULL;
    }

    for (size_t j = 0; j < frame_len; j++) {
        sprintf(&hex[j * 2], "%02X", frame[j]);
    }

    hex[frame_len * 2] = '\0';

    free(frame);
    return hex;
}


// static void lora_send_epc(EPC_Info_t **epc_list, uint16_t epcCnt)
// {
//     char *hex = build_epc_hex_frame(epc_list, epcCnt);
//     if (!hex) return;

//     lora_send_string(hex);

//     free(hex);
// }


static void send_epc_over_nb(EPC_Info_t **epc_list, uint16_t epcCnt)
{
    char *hex = build_epc_hex_frame(epc_list, epcCnt);
    if (!hex) return;

    // hex 已经是十六进制字符串，无需ascii_to_hex转换
    size_t hex_len = strlen(hex);
    size_t at_cmd_len = hex_len + 40;

    char *at_cmd = malloc(at_cmd_len);
    if (!at_cmd) {
        printf("[send_epc_over_nb] malloc at_cmd failed\n");
        free(hex);
        return;
    }

    // 发送长度是 hex_len/2，因为每两个字符代表1字节数据
    snprintf(at_cmd, at_cmd_len, "AT+UNBSEND=%zu,%s,%d\r\n", hex_len / 2, hex, NB_FLAGS);

    uart_write_bytes(UART_NUM_2, at_cmd, strlen(at_cmd));
    printf("[send_epc_over_nb] send: %s", at_cmd);

    free(at_cmd);
    free(hex);
}

static void lora_send_epc(EPC_Info_t **epc_list, uint16_t epcCnt)
{
    if (epcCnt == 0 || epc_list == NULL) return;

    char *hex = build_epc_hex_frame(epc_list, epcCnt);
    if (!hex) {
        printf("[lora_send_epc] build_epc_hex_frame failed\n");
        return;
    }

    size_t hex_len = strlen(hex);
    size_t at_cmd_len = hex_len + 40;

    char *at_cmd = malloc(at_cmd_len);
    if (!at_cmd) {
        printf("[lora_send_epc] malloc at_cmd failed\n");
        free(hex);
        return;
    }

    // AT 命令中长度是字节数，所以要除以2
    snprintf(at_cmd, at_cmd_len, "AT+UNBSEND=%zu,%s,%d\r\n", hex_len / 2, hex, NB_FLAGS);

    uart_write_bytes(UART_NUM_2, at_cmd, strlen(at_cmd));
    printf("[lora_send_epc] send: %s", at_cmd);

    free(at_cmd);
    free(hex);
}






void loraInfoToGateway(){
    /*
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

 */
    lora_send_epc(&LTU3_Lable, epcCnt);
}


void EPC_Json_info_to_middleware(int mode)
{
    EPC_Info_t **EPC_ptr2 = &LTU3_Lable;

    if (epcCnt == 0) {
        printf("No EPC tags to send\n");
        return;
    }

    char *hex_frame = build_epc_hex_frame(EPC_ptr2, epcCnt);//构建EPC标签帧 十六进制字符串
    if (!hex_frame) {
        printf("Failed to build hex frame\n");
        return;
    }

    char *json_str = NULL;
    int json_len = asprintf(&json_str,
        "{\"type\":227,\"data\":\"%s\"}",
        hex_frame);

    free(hex_frame);

    if (json_len < 0 || !json_str) {
        printf("Failed to allocate JSON string\n");
        return;
    }
    if(mode==SYS_NETWORKING_4G){
        mqtt_client_publish(send_topic_4G, json_str, json_len, 0, 1);
    }else if(mode==SYS_NETWORKING_ETHERNET){
        mqtt_client_publish(send_topic_eth, json_str, json_len, 0, 1);
    }else if(mode==SYS_NETWORKING_WIFI){
        const char *ip = get_effective_ip_str();//获取有效IP
        if (ip != NULL) {
            printf("[EPC_Json_info_to_middleware] ip: %s\n", ip);
            mqtt_client_publish(send_topic_wifi, json_str, json_len, 0, 1);
        } else {
            printf("[EPC_Json_info_to_middleware] 未获取有效IP，跳过发送\n");
        }
       
    }
    

    free(json_str);
}