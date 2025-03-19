#include <stdio.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_mac.h"

uint64_t chip_id;
char chip_id_str[20];  // 用于存储格式化后的十六进制数
char send_topic[100];  //发送主题字符串
char lwt_content[100];  //遗嘱主题内容设备id号


void get_chip_IDinfo(void)
{ 
    // // 获取芯片信息
    // esp_chip_info_t chip_info;
    // esp_chip_info(&chip_info);

    // // 打印芯片ID
    // printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
    //        chip_info.cores,
    //        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
    //        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    // printf("silicon revision %d, ", chip_info.revision);

    // uint8_t mac[6];
    // esp_efuse_mac_get_default(mac);
    // printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // uint32_t chip_id = 0;
    // for(int i = 0; i < 6; i++) {
    //     chip_id |= ((uint32_t)mac[i] << (8 * i));
    // }
    // printf("Chip ID: %08x\n", chip_id);


    uint8_t mac[6];

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret == ESP_OK) {
        printf("MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        chip_id = 0;
        for (int i = 0; i < 6; i++) {
            chip_id |= ((uint64_t)mac[i] << (8 * (5-i)));
        }
        printf("Chip ID: %012llx\n", chip_id);
    } else {
        printf("Failed to get MAC address: %s\n", esp_err_to_name(ret));
    }

        // 格式化并将64位整数写入字符串
    snprintf(chip_id_str, sizeof(chip_id_str), "%012llx", chip_id);
    // 拼接字符串
    snprintf(send_topic, sizeof(send_topic), "rfid/%s", chip_id_str);
    snprintf(lwt_content, sizeof(lwt_content), "%s", chip_id_str);//遗嘱消息设备id号


}

