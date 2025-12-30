#ifndef DEV_INFO__H_
#define DEV_INFO__H_


// 配置信息结构体
typedef struct {
    bool enable_4g;
    bool enable_eth;
    bool enable_lora;
    bool enable_wifi;
    char wifi_ssid[32];
    char wifi_psd[64];
    char label_mode[8];
    char mqtt_addr[64];
    char mqtt_username[64];
    char mqtt_password[64];
    char epcbb_type[8];
} config_info_t;
extern config_info_t g_config;

void get_chip_IDinfo(void);

extern uint64_t chip_id;//esp32s3芯片id
extern char chip_id_str[20];  // 用于存储格式化后的十六进制数
extern char send_topic[100];  //发送主题字符串
extern char lwt_content[100];  //遗嘱主题内容设备id号
extern void wait_for_config_or_timeout(void);
extern char send_topic_wifi[100];  //发送主题字符串
extern char send_topic_4G[100];  //发送主题字符串
extern char send_topic_eth[100];  //发送主题字符串

#endif // !DEV_INFO__H_