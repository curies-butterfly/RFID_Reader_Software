#include <stdio.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "dev_info.h"
#include "driver/uart.h"
#include "string.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "parameter.h"


uint64_t chip_id;
char chip_id_str[20];  // 用于存储格式化后的十六进制数
char send_topic[100];  //发送主题字符串
char lwt_content[100];  //遗嘱主题内容设备id号

char send_topic_wifi[100];  //发送主题字符串
char send_topic_4G[100];  //发送主题字符串
char send_topic_eth[100];  //发送主题字符串


#define BUF_SIZE 256
#define CONFIG_WAIT_TIMEOUT_MS 10000  // 系统配置等待时间，单位：毫秒（10秒）

static const char *TAG = "dev_config";

config_info_t g_config = {
    .enable_4g = false,
    .enable_eth = false,
    .enable_lora = false,
    .enable_wifi = false,
    .wifi_ssid = "",
    .wifi_psd = "",
    .mqtt_addr = "",
    .mqtt_username = "",
    .mqtt_password = "",
    .epcbb_type = "",
};

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
    snprintf(send_topic_wifi, sizeof(send_topic_wifi), "/rfidReader/Wifi/to/server/%s", chip_id_str);
    snprintf(send_topic_4G, sizeof(send_topic_4G), "/rfidReader/4G/to/server/%s", chip_id_str);
    snprintf(send_topic_eth, sizeof(send_topic_eth), "/rfidReader/Eth/to/server/%s", chip_id_str);
    snprintf(lwt_content, sizeof(lwt_content), "%s", chip_id_str);//遗嘱消息设备id号

}


// 解析配置字符串
static void parse_config_line(const char *line)
{
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf + 1, ";");
    while (token) {
        if (strncmp(token, "4G=", 3) == 0) {
            g_config.enable_4g = (token[3] == '1');
        } 
        else if (strncmp(token, "ETH=", 4) == 0) {
            g_config.enable_eth = (token[4] == '1');
        } 
        else if (strncmp(token, "LORA=", 5) == 0) {
            g_config.enable_lora = (token[5] == '1');
        }
        else if (strncmp(token, "WIFI=", 5) == 0) {   // 注意这里是大写WIFI
            g_config.enable_wifi = (token[5] == '1');
        }
        else if (strncmp(token, "wifi=", 5) == 0) {  // 小写wifi 配置SSID和密码
            char *ssid_start = token + 5;
            char *comma = strchr(ssid_start, ',');
            if (comma) {
                *comma = '\0';
                strncpy(g_config.wifi_ssid, ssid_start, sizeof(g_config.wifi_ssid) - 1);
                g_config.wifi_ssid[sizeof(g_config.wifi_ssid) - 1] = '\0';
                strncpy(g_config.wifi_psd, comma + 1, sizeof(g_config.wifi_psd) - 1);
                g_config.wifi_psd[sizeof(g_config.wifi_psd) - 1] = '\0';
            }
        }
        else if (strncmp(token, "TYPE=", 5) == 0) {
            strncpy(g_config.label_mode, token + 5, sizeof(g_config.label_mode) - 1);
            g_config.label_mode[sizeof(g_config.label_mode) - 1] = '\0';
        } 
        else if (strncmp(token, "MQTT=", 5) == 0) {
            strncpy(g_config.mqtt_addr, token + 5, sizeof(g_config.mqtt_addr) - 1);
            g_config.mqtt_addr[sizeof(g_config.mqtt_addr) - 1] = '\0';
        }
        else if (strncmp(token, "MQTT_USER=", 10) == 0) {
            if (token[10] != '\0') {  // 非空才覆盖
                strncpy(g_config.mqtt_username, token + 10, sizeof(g_config.mqtt_username) - 1);
                g_config.mqtt_username[sizeof(g_config.mqtt_username) - 1] = '\0';
            }
            // 空字符串就不改，保留原有值
        }

        else if (strncmp(token, "MQTT_PWD=", 9) == 0) {
            if (token[9] != '\0') {  // 非空才覆盖
                strncpy(g_config.mqtt_password, token + 9, sizeof(g_config.mqtt_password) - 1);
                g_config.mqtt_password[sizeof(g_config.mqtt_password) - 1] = '\0';
            }
        }

        else if (strncmp(token, "EPCBB_TYPE=", 11) == 0) {
            memset(g_config.epcbb_type, 0, sizeof(g_config.epcbb_type));
            strncpy(g_config.epcbb_type, token + 11, sizeof(g_config.epcbb_type) - 1);
            g_config.epcbb_type[sizeof(g_config.epcbb_type) - 1] = '\0';
        }
        token = strtok(NULL, ";");
    }

    ESP_LOGI(TAG,
         "Parsed Config: 4G=%d, ETH=%d, LORA=%d, WIFI=%d, "
         "wifi_ssid=%s, wifi_psd=%s, TYPE=%s, MQTT=%s, "
         "MQTT_USER=%s, EPCBB_TYPE=%s",
         g_config.enable_4g,
         g_config.enable_eth,
         g_config.enable_lora,
         g_config.enable_wifi,
         g_config.wifi_ssid,
         g_config.wifi_psd,
         g_config.label_mode,
         g_config.mqtt_addr,
         g_config.mqtt_username,
         g_config.epcbb_type);
}


static void set_nvs_LabelType(const char *label_mode_str){
    if (strcmp(label_mode_str, "YH") != 0 && strcmp(label_mode_str, "XY") != 0) {
        ESP_LOGW(TAG, "Invalid label_mode: %s, skipping NVS write.", label_mode_str);
        return;
    }
    nvs_handle_t my_handle;//nvs句柄
    esp_err_t ret_nvs=nvs_open("memory",NVS_READWRITE,&my_handle);
    if(ret_nvs!=ESP_OK){
        ESP_LOGE(TAG,"nvs_open error");
    }else{
        //写入数据
        ret_nvs = nvs_set_str(my_handle,"label_mode",label_mode_str);
        if(ret_nvs!=ESP_OK){
            ESP_LOGE(TAG,"nvs_set_str error");
        }else{
            nvs_commit(my_handle);//提交数据
            ESP_LOGI(TAG, "nvs_set_str success, label_mode = %s", label_mode_str);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "NVS close Done\n");
        }
    }
}

static void set_nvs_netSel(bool num1,bool num2,bool num3,bool num4){
    if ((num1 + num2 + num3 + num4) != 1) {
        ESP_LOGE(TAG, "Invalid network selection.");
        return;
    }
    if(num1)
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "4G"));
    else if(num2)
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "ethernet"));
    else if(num3)
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "lora"));
    else if(num4)
        ESP_ERROR_CHECK(from_nvs_set_value("net_sel", "wifi"));
    else {
        return;
    }
}

static void set_nvs_wifista(const char *ssid, const char *pswd)
{
    if (ssid == NULL || pswd == NULL || strlen(ssid) == 0 || strlen(pswd) == 0) {
        ESP_LOGE(TAG, "Invalid SSID or password.");
        return;
    }

    ESP_ERROR_CHECK(from_nvs_set_value("wifi_ssid", ssid));
    ESP_ERROR_CHECK(from_nvs_set_value("wifi_pswd", pswd));

    ESP_LOGI(TAG, "WiFi STA configuration saved: SSID = %s,PSWD = %s", ssid, pswd);
}

// 保存MQTT地址到NVS(用于串口写入)
// 保存ssid和pswd到NVS(用于串口写入)
static void set_nvs_mqtt_addr(const char *mqtt_addr)
{
    if (mqtt_addr == NULL || strlen(mqtt_addr) == 0) {
        ESP_LOGE(TAG, "Invalid MQTT address.");
        return;
    }

    esp_err_t err = from_nvs_set_value("mqtt_addr", mqtt_addr);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT address saved: %s", mqtt_addr);
    } else {
        ESP_LOGE(TAG, "Failed to save MQTT address: %s", esp_err_to_name(err));
    }
}
static void set_nvs_mqtt_user(const char *username)
{
    if (username == NULL || strlen(username) == 0) {
        ESP_LOGW(TAG, "MQTT username empty, skip save");
        return;
    }

    esp_err_t err = from_nvs_set_value("mqtt_user", username);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT username saved");
    } else {
        ESP_LOGE(TAG, "Save mqtt_user failed: %s", esp_err_to_name(err));
    }
}
static void set_nvs_mqtt_pwd(const char *password)
{
    if (password == NULL || strlen(password) == 0) {
        ESP_LOGW(TAG, "MQTT password empty, skip save");
        return;
    }

    esp_err_t err = from_nvs_set_value("mqtt_pwd", password);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT password saved");
    } else {
        ESP_LOGE(TAG, "Save mqtt_pwd failed: %s", esp_err_to_name(err));
    }
}




// 串口0配置信息
void wait_for_config_or_timeout(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, GPIO_NUM_43, GPIO_NUM_44, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "Waiting For Configuration Information(Within 10s)");

    uint8_t data[BUF_SIZE];
    int64_t start_time = esp_timer_get_time();  // 微秒

    while ((esp_timer_get_time() - start_time) < CONFIG_WAIT_TIMEOUT_MS * 1000) {
        int len = uart_read_bytes(UART_NUM_0, data, sizeof(data) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            data[len] = '\0';
            ESP_LOGI(TAG, "Received: %s", data);
            
            if (data[0] == '#') {// 判断是否为有效配置开头
                parse_config_line((char *)data);

                // 写入 NVS
                // ESP_ERROR_CHECK(from_nvs_set_value("ssid", user_ssid));
                // ESP_ERROR_CHECK(from_nvs_set_value("password", user_password));
                set_nvs_LabelType(g_config.label_mode);//设置读取的标签类型
                set_nvs_netSel(g_config.enable_4g, g_config.enable_eth, g_config.enable_lora, g_config.enable_wifi);//设置四种上网模式
                set_nvs_wifista(g_config.wifi_ssid,g_config.wifi_psd);//设置wifi sta ssid psd
                // set_nvs_mqtt(g_config.mqtt_addr);
                set_nvs_mqtt_addr(g_config.mqtt_addr);
                set_nvs_mqtt_user(g_config.mqtt_username);
                set_nvs_mqtt_pwd(g_config.mqtt_password);
                // save_config_to_nvs();

                return; // 正常接收后直接返回
            } else {
                ESP_LOGW(TAG, "Invalid config format (missing '#'), ignored: %s", data);
            }
        }
    }

    ESP_LOGW(TAG, "Enter the default program...");
    // TODO: 进入主程序
}
