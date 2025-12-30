#ifndef PARAMETER__H_
#include "usbh_modem_wifi.h"
#include "sdkconfig.h"

typedef struct {
    char username[20];
    char password[20];
} web_auth_info_t;

#define WEB_AUTH_INFO_DEFAULT_CONFIG()            \
{                                                 \
    .username = CONFIG_EXAMPLE_WEB_USERNAME,      \
    .password = CONFIG_EXAMPLE_WEB_PASSWORD,      \
}

typedef enum {
    SYS_WORK_MODE_NULL = 0,         /**< null mode */
    SYS_WORK_MODE_READER,           /**< RFID Reader mode */
    SYS_WORK_MODE_GATEWAY,          /**< GATEWAY mode */
} sys_work_mode_t;

typedef enum {
    SYS_NETWORKING_MODE_NULL = 0,       /**< null mode */
    SYS_NETWORKING_4G,                  /**< 4G */
    SYS_NETWORKING_ETHERNET,            /**< ethernet */
    SYS_NETWORKING_ALL,                 /**< 4G + ethernet */
    SYS_NETWORKING_UNB,                 /**< unb lora mode */
    SYS_NETWORKING_WIFI,                /**< wifi mode */
} sys_networking_mode_t;

typedef enum {
    SYS_NET_COMMUNICATION_PROTOCOL_NULL = 0,    /**< null ptotocol */
    SYS_NET_COMMUNICATION_PROTOCOL_MQTT,        /**< MQTT protocol */
    SYS_NET_COMMUNICATION_PROTOCOL_TCP,         /**< TCP  protocol */
} sys_net_communication_protocol_t;

typedef struct {
    char sys_hard_version[10];                                  /*!< system hardware version */
    char sys_soft_version[10];                                  /*!< system software version */
    sys_work_mode_t sys_work_mode;                              /*!< system Work mode */
    sys_networking_mode_t sys_networking_mode;                  /*!< system networking mode */
    sys_net_communication_protocol_t sys_net_communication_protocol;    /*!< system communication protocol */
    char mqtt_address[128];                                      /*!< mqtt address */
    char mqtt_username[64];
    char mqtt_password[64];
    char tcp_address[32]; 
    uint16_t tcp_port;
} sys_info_config_t;

extern modem_wifi_config_t *s_modem_wifi_config;   //WIF AP 配置参数
extern web_auth_info_t  web_auth_info;  //login info
extern sys_info_config_t sys_info_config;

void sys_info_config_init(sys_info_config_t *config);

esp_err_t from_nvs_set_value(char *key, char *value);
esp_err_t from_nvs_get_value(char *key, char *value, size_t *size);
esp_err_t get_nvs_wifi_config(modem_wifi_config_t *wifi_config);
esp_err_t get_nvs_auth_info_config(void);
esp_err_t get_nvs_sys_info_config(void);

#endif // !PARAMETER__H_
