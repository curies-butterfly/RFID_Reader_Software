#include "parameter.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"


static const char *TAG = "parameter_manage";

static modem_wifi_config_t wifi_config_default = MODEM_WIFI_DEFAULT_CONFIG();
modem_wifi_config_t *s_modem_wifi_config = &wifi_config_default;  
web_auth_info_t  web_auth_info = WEB_AUTH_INFO_DEFAULT_CONFIG();
sys_info_config_t sys_info_config;

void nvs_get_str_log(esp_err_t err, char *key, char *value)
{
    switch (err) {
    case ESP_OK:
        ESP_LOGI(TAG, "%s = %s", key, value);
        break;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(TAG, "%s : Can't find in NVS!", key);
        break;
    default:
        ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
    }
}

esp_err_t from_nvs_set_value(char *key, char *value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return ESP_FAIL;
    } else {
        err = nvs_set_str(my_handle, key, value);
        ESP_LOGI(TAG, "set %s is %s!,the err is %d\n", key, (err == ESP_OK) ? "succeed" : "failed", err);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "NVS close Done\n");
    }
    return ESP_OK;
}

esp_err_t from_nvs_get_value(char *key, char *value, size_t *size)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return ESP_FAIL;
    } else {
        err = nvs_get_str(my_handle, key, value, size);
        nvs_get_str_log(err, key, value);
        nvs_close(my_handle);
    }
    return err;
}


esp_err_t get_nvs_wifi_config(modem_wifi_config_t *wifi_config)
{
    char str[64] = "";
    size_t str_size = sizeof(str);

    esp_err_t err = from_nvs_get_value("ssid", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(wifi_config->ssid, str, str_size);
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("password", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(wifi_config->password, str, sizeof(wifi_config->password));
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("auth_mode", str, &str_size);
    if ( err == ESP_OK ) {
         if ( !strcmp(str, "OPEN") ) {
            wifi_config->authmode = WIFI_AUTH_OPEN;
        } else if ( !strcmp(str, "WEP") ) {
            wifi_config->authmode = WIFI_AUTH_WEP;
        } else if ( !strcmp(str, "WPA2_PSK") ) {
            wifi_config->authmode = WIFI_AUTH_WPA2_PSK;
        } else if ( !strcmp(str, "WPA_WPA2_PSK") ) {
            wifi_config->authmode = WIFI_AUTH_WPA_WPA2_PSK;
        } else {
            ESP_LOGE(TAG, "auth_mode %s is not define", str);
        }
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("channel", str, &str_size);
    if ( err == ESP_OK ) {
        wifi_config->channel = atoi(str);
    }
    str_size = sizeof(str);

    from_nvs_get_value("hide_ssid", str, &str_size);
    if ( err == ESP_OK ) {
        if ( !strcmp(str, "true") ) {
            wifi_config->ssid_hidden = 1;
        } else {
            wifi_config->ssid_hidden = 0;
        }
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("bandwidth", str, &str_size);
    if ( err == ESP_OK ) {
        if (!strcmp(str, "40")) {
            wifi_config->bandwidth = WIFI_BW_HT40;
        } else {
            wifi_config->bandwidth = WIFI_BW_HT20;
        }
    }

    err = from_nvs_get_value("max_connection", str, &str_size);
    if ( err == ESP_OK ) {
        wifi_config->max_connection = atoi(str);
    }

    return ESP_OK;
}


esp_err_t get_nvs_auth_info_config(void)
{
    char str[64] = "";
    size_t str_size = sizeof(str);

    esp_err_t err = from_nvs_get_value("auth_username", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(web_auth_info.username, str, str_size);
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("auth_password", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(web_auth_info.password, str, str_size);
    }
    str_size = sizeof(str);
    return ESP_OK;
}


esp_err_t get_nvs_sys_info_config(void)
{
    char str[64] = "";
    size_t str_size = sizeof(str);
    esp_err_t err;
    // err = from_nvs_get_value("h_ver", str, &str_size);
    // if ( err == ESP_OK ) {
    //     memset(sys_info_config.sys_hard_version, '\0', 
    //     sizeof(sys_info_config.sys_hard_version));
    //     strncpy(sys_info_config.sys_hard_version, str, str_size);
    // }
    // str_size = sizeof(str);
    // err = from_nvs_get_value("s_ver", str, &str_size);
    // if ( err == ESP_OK ) {
    //     memset(sys_info_config.sys_soft_version, '\0', 
    //     sizeof(sys_info_config.sys_soft_version));
    //     strncpy(sys_info_config.sys_soft_version, str, str_size);
    // }
    // str_size = sizeof(str);
    err = from_nvs_get_value("w_mod", str, &str_size);
    if ( err == ESP_OK ) {
        if ( !strcmp(str, "rfid reader") ) {
            sys_info_config.sys_work_mode = SYS_WORK_MODE_READER;
        } else if ( !strcmp(str, "gateway") ) {
            sys_info_config.sys_work_mode = SYS_WORK_MODE_GATEWAY;
        } else {
            ESP_LOGE(TAG, "system work mode %s is not define", str);
        }
    }
    str_size = sizeof(str);
    err = from_nvs_get_value("net_sel", str, &str_size);
    if ( err == ESP_OK ) {
        if ( !strcmp(str, "4G") ) {
            sys_info_config.sys_networking_mode = SYS_NETWORKING_4G;
        } else if ( !strcmp(str, "ethernet") ) {
            sys_info_config.sys_networking_mode = SYS_NETWORKING_ETHERNET;
        } else if ( !strcmp(str, "lora") ) {
            sys_info_config.sys_networking_mode = SYS_NETWORKING_UNB;
        }else {
            ESP_LOGE(TAG, "system networking mode %s is not define", str);
        }
    }
    str_size = sizeof(str);
    err = from_nvs_get_value("ncp_sel", str, &str_size);
    if ( err == ESP_OK ) {
        if ( !strcmp(str, "mqtt") ) {
            sys_info_config.sys_net_communication_protocol = SYS_NET_COMMUNICATION_PROTOCOL_MQTT;
        } else if ( !strcmp(str, "tcp") ) {
            sys_info_config.sys_net_communication_protocol = SYS_NET_COMMUNICATION_PROTOCOL_TCP;
        } else {
            ESP_LOGE(TAG, "system communication protocol  %s is not define", str);
        }
    }
    str_size = sizeof(str);
    err = from_nvs_get_value("mqtt_addr", str, &str_size);
    if ( err == ESP_OK ) {
         memset(sys_info_config.mqtt_address, '\0', \
        sizeof(sys_info_config.mqtt_address));
        strncpy(sys_info_config.mqtt_address, str, str_size);
    }
    str_size = sizeof(str);
    err = from_nvs_get_value("tcp_addr", str, &str_size);
    if ( err == ESP_OK ) {
         memset(sys_info_config.tcp_address, '\0', \
        sizeof(sys_info_config.tcp_address));
        strncpy(sys_info_config.tcp_address, str, str_size);
    }
    str_size = sizeof(str);
    err = from_nvs_get_value("tcp_port", str, &str_size);
    if ( err == ESP_OK ) {
        sys_info_config.tcp_port = atoi(str);
    }
    return ESP_OK;
}

void sys_info_config_init(sys_info_config_t *config)
{
    strcpy(config->sys_hard_version, CONFIG_SYS_INFO_HARD_VERSION);
    strcpy(config->sys_soft_version, CONFIG_SYS_INFO_SOFT_VERSION);
#if CONFIG_SYS_WORK_MODE_READER
    config->sys_work_mode = SYS_WORK_MODE_READER;
#elif CONFIG_SYS_WORK_MODE_GATEWAY
    config->sys_work_mode = SYS_WORK_MODE_GATEWAY;
#endif

#if CONFIG_EXAMPLE_ENABLE_4GMODULE && CONFIG_EXAMPLE_ENABLE_ETHERNET
    config->sys_networking_mode = SYS_NETWORKING_ALL;
#elif CONFIG_EXAMPLE_ENABLE_4GMODULE
    config->sys_networking_mode = SYS_NETWORKING_4G;
#elif CONFIG_EXAMPLE_ENABLE_ETHERNET
    config->sys_networking_mode = SYS_NETWORKING_ETHERNET;
#endif
    
#if CONFIG_SYS_NET_COMMUNICATION_PROTOCOL_MQTT
    config->sys_net_communication_protocol = SYS_NET_COMMUNICATION_PROTOCOL_MQTT;
#elif CONFIG_SYS_NET_COMMUNICATION_PROTOCOL_TCP
    config->sys_net_communication_protocol = SYS_NET_COMMUNICATION_PROTOCOL_TCP;
#endif
    strcpy(config->mqtt_address, CONFIG_BROKER_URL);
    strcpy(config->tcp_address, CONFIG_TCP_ADDRESS);
    config->tcp_port = CONFIG_TCP_PORT;
}