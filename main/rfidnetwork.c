#include "rfidnetwork.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "led_indicator.h"
#include "led.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "usbh_modem_board.h"
#include "usbh_modem_wifi.h"

#include "mqtt_client.h"
#include "parameter.h"

#include "json_parser.h"
#include "rfidmodule.h"
#include <cJSON.h> // 包含 cJSON 库以生成 JSON 格式的消息

#include "hal/wdt_hal.h" //watchdog
#include "tp1107.h"
#include "nvs.h"
#include "dev_info.h"

static const char *TAG = "rfid_network";
static const char *TAG_4G = "network_4G_module";
static const char *TAG_ETHERNET = "network_ethernet";
static const char *TAG_MQTT = "mqtt";
static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";

char chip_id_ssid_str[32]; // 设备ID作为AP的ssid

/* STA Configuration */
#define EXAMPLE_ESP_WIFI_STA_SSID CONFIG_ESP_WIFI_REMOTE_AP_SSID
#define EXAMPLE_ESP_WIFI_STA_PASSWD CONFIG_ESP_WIFI_REMOTE_AP_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_STA_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID CONFIG_ESP_WIFI_AP_SSID
#define EXAMPLE_ESP_WIFI_AP_PASSWD CONFIG_ESP_WIFI_AP_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_AP_CHANNEL
#define EXAMPLE_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN_AP

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected/disconnected */
static EventGroupHandle_t s_wifi_event_group;

typedef struct
{
    char ssid[32];     // 最多32字节
    char password[64]; // 最多64字节
} user_wifi_sta_info_t;
user_wifi_sta_info_t wifi_sta_info;
// 定义全局变量以确保生命周期
static char *lwt_msg = NULL;

int pushtime_count = 10000;
int err_value = 1; // 默认1

#define INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num)                                  \
    do                                                                                      \
    {                                                                                       \
        eth_module_config[num].spi_cs_gpio = CONFIG_EXAMPLE_ETH_SPI_CS##num##_GPIO;         \
        eth_module_config[num].int_gpio = CONFIG_EXAMPLE_ETH_SPI_INT##num##_GPIO;           \
        eth_module_config[num].phy_reset_gpio = CONFIG_EXAMPLE_ETH_SPI_PHY_RST##num##_GPIO; \
        eth_module_config[num].phy_addr = CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR##num;             \
    } while (0)

typedef struct
{
    uint8_t spi_cs_gpio;
    uint8_t int_gpio;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
} spi_eth_module_config_t;

static esp_mqtt_client_handle_t mqtt_client;
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG_ETHERNET, "Ethernet Link Up");
        ESP_LOGI(TAG_ETHERNET, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        if (s_led_net_status_handle)
        {
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTING);
        }
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Link Down");
        if (s_led_net_status_handle)
        {
            led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTED);
            led_indicator_stop(s_led_net_status_handle, BLINK_RECONNECTING); // 停止可能存在的快闪
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTING); // 启动慢闪
        }
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Started");
        if (s_led_net_status_handle)
        {
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTING);
        }
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Stopped");
        if (s_led_net_status_handle)
        {
            led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTED);
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTING); // 启动慢闪
        }
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG_ETHERNET, "Ethernet Got IP Address");
    ESP_LOGI(TAG_ETHERNET, "~~~~~~~~~~~");
    ESP_LOGI(TAG_ETHERNET, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG_ETHERNET, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG_ETHERNET, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG_ETHERNET, "~~~~~~~~~~~");

    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP)
    {
  
        if (s_led_net_status_handle)
        {
            // printf("Ethernet Connected\n！！！！！！！！！！！！！！！！！！！！！！！！！！！！！");
            led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTING);    // 停止慢闪
            led_indicator_stop(s_led_net_status_handle, BLINK_RECONNECTING); // 停止快闪
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTED);   // 启动常亮
        }
      
    }
}

bool sim_card_connected = false; // 全局变量来记录 SIM 卡的状态（是否插入）
static void on_modem_event(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    // wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
    // wdt_hal_write_protect_disable(&rwdt_ctx);
    // wdt_hal_feed(&rwdt_ctx);
    if (event_base == MODEM_BOARD_EVENT)
    {
        if (event_id == MODEM_EVENT_SIMCARD_DISCONN)
        {
            ESP_LOGW(TAG_4G, "Modem Board Event: SIM Card disconnected");
            sim_card_connected = false; // 没有插入
            // led_indicator_start(s_led_system_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_SIMCARD_CONN)
        {
            ESP_LOGI(TAG_4G, "Modem Board Event: SIM Card Connected");
            sim_card_connected = true; // sim卡已插入
            // led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_DTE_DISCONN)
        {
            ESP_LOGW(TAG_4G, "Modem Board Event: USB disconnected");
            // led_indicator_start(s_led_system_handle, BLINK_CONNECTING);
        }
        else if (event_id == MODEM_EVENT_DTE_CONN)
        {
            ESP_LOGI(TAG_4G, "Modem Board Event: USB connected");
            // led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
            // led_indicator_stop(s_led_system_handle, BLINK_CONNECTING);
        }
        else if (event_id == MODEM_EVENT_DTE_RESTART)
        {
            ESP_LOGW(TAG_4G, "Modem Board Event: Hardware restart");
            // led_indicator_start(s_led_system_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_DTE_RESTART_DONE)
        {
            ESP_LOGI(TAG_4G, "Modem Board Event: Hardware restart done");
            // led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_NET_CONN)
        {
            ESP_LOGI(TAG_4G, "Modem Board Event: Network connected");
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_NET_DISCONN)
        {
            ESP_LOGW(TAG_4G, "Modem Board Event: Network disconnected");
            led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_WIFI_STA_CONN)
        {
            ESP_LOGI(TAG_4G, "Modem Board Event: Station connected");
            // led_indicator_start(s_led_wifi_handle, BLINK_CONNECTED);
        }
        else if (event_id == MODEM_EVENT_WIFI_STA_DISCONN)
        {
            ESP_LOGW(TAG_4G, "Modem Board Event: All stations disconnected");
            // led_indicator_stop(s_led_wifi_handle, BLINK_CONNECTED);
        }
    }

    // modem_board_init(&modem_config);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");

        // msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        // 创建 JSON 对象
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "id", lwt_content);
        // // cJSON_AddStringToObject(json, "status", "online");

        // // 将 JSON 对象转换为字符串
        char *json_string = cJSON_PrintUnformatted(json);

        // // 发布 JSON 消息
        esp_mqtt_client_publish(event->client, "devices/status/connect", json_string, 0, 1, true);

        // // 释放 JSON 对象
        cJSON_Delete(json);
        free(json_string);

        // ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "ctlrfid", 0); // 订阅一个控制读写器的主题
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "pushtime", 0); // 订阅一个控制读写器发布时间的主题
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        if (strncmp(event->topic, "ctlrfid", event->topic_len) == 0)
        {

            // 解析JSON数据
            char on_off[5] = "";
            char read_mode[20] = "";
            char ant_sel[4] = "";
            char interval_time[5] = "";
            char buf[512] = {0};
            rfid_read_config_t rfid_read_config;

            // // // 判断数据是否是 JSON 格式
            // if (is_json_data(event->data, event->data_len)) {

            //     ESP_LOGI(TAG, "Valid JSON data received");
            //     // 使用 json_parse_message 函数解析 JSON 消息
            //     // json_parse_message(event->data, event->data_len);
            // } else {
            //     ESP_LOGI(TAG, "Received data is not in JSON format");
            //     break;
            // }

            // 声明 JSON 解析上下文
            jparse_ctx_t jctx;
            memset(&jctx, 0, sizeof(jparse_ctx_t));

            // 使用 json_parse_start 严格验证 JSON 格式
            int parse_ret = json_parse_start(&jctx, event->data, event->data_len);
            if (parse_ret == OS_SUCCESS)
            {
                ESP_LOGI(TAG, "Valid JSON data received");

                /* 这里添加具体解析逻辑，例如：
                if (json_obj_get_string(&jctx, "on_off", on_off, sizeof(on_off)) == OS_SUCCESS) {
                    ESP_LOGI(TAG, "on_off: %s", on_off);
                }
                */

                // 必须清理解析上下文
                json_parse_end(&jctx);
            }
            else
            {
                ESP_LOGE(TAG, "Invalid JSON format (error=%d)", parse_ret);
                json_parse_end(&jctx); // 仍然需要清理
                break;
            }

            // jparse_ctx_t jctx;
            int ps_ret = json_parse_start(&jctx, event->data, event->data_len);
            char str_val[64];
            if (json_obj_get_string(&jctx, "on_off", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(on_off, sizeof(on_off), "%.*s", sizeof(on_off) - 1, str_val);
                ESP_LOGI(TAG, "rfid read control: %s\n", on_off);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }
            if (json_obj_get_string(&jctx, "read_mode", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(read_mode, sizeof(read_mode), "%.*s", sizeof(read_mode) - 1, str_val);
                ESP_LOGI(TAG, "rfid read mode: %s\n", read_mode);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }
            if (json_obj_get_string(&jctx, "ant_sel", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(ant_sel, sizeof(ant_sel), "%.*s", sizeof(ant_sel) - 1, str_val);
                ESP_LOGI(TAG, "rfid read ant: %s\n", ant_sel);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }
            if (json_obj_get_string(&jctx, "interval_time", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(interval_time, sizeof(interval_time), "%.*s", sizeof(interval_time) - 1, str_val);
                ESP_LOGI(TAG, "rfid read interval time: %s\n", interval_time);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }

            if (!strcmp(on_off, "on"))
                rfid_read_config.rfid_read_on_off = RFID_READ_ON;
            else if (!strcmp(on_off, "off"))
                rfid_read_config.rfid_read_on_off = RFID_READ_OFF;
            else
            {
                // httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter on_off error");
                // return ESP_FAIL;
            }
            if (!strcmp(read_mode, "once"))
                rfid_read_config.rfid_read_mode = RFID_READ_MODE_ONCE;
            else if (!strcmp(read_mode, "continuous"))
                rfid_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;
            else
            {
                // httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter read_mode error");
                // return ESP_FAIL;
            }
            rfid_read_config.ant_sel = atoi(ant_sel);
            rfid_read_config.read_interval_time = atoi(interval_time);
            printf("ant_sel:%x\r\n", rfid_read_config.ant_sel);
            printf("read_interval_time:%ld\r\n", rfid_read_config.read_interval_time);

            RFID_ReadEPC(rfid_read_config);
        }

        if (strncmp(event->topic, "pushtime", event->topic_len) == 0)
        {
            // 解析JSON数据
            char time[20] = "";
            char errvalue[20] = "";
            jparse_ctx_t jctx;
            // // 判断数据是否是 JSON 格式
            // if (is_json_data(event->data, event->data_len)) {

            //     ESP_LOGI(TAG, "Valid JSON data received");
            //     // 使用 json_parse_message 函数解析 JSON 消息
            //     // json_parse_message(event->data, event->data_len);
            // } else {
            //     ESP_LOGI(TAG, "Received data is not in JSON format");
            //     break;
            // }

            int parse_ret = json_parse_start(&jctx, event->data, event->data_len);
            if (parse_ret == OS_SUCCESS)
            {
                ESP_LOGI(TAG, "Valid JSON data received");
                // 必须清理解析上下文
                json_parse_end(&jctx);
            }
            else
            {
                ESP_LOGE(TAG, "Invalid JSON format (error=%d)", parse_ret);
                json_parse_end(&jctx); // 仍然需要清理
                break;
            }

            int ps_ret = json_parse_start(&jctx, event->data, event->data_len);
            char str_val[64];

            if (json_obj_get_string(&jctx, "time", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(time, sizeof(time), "%.*s", sizeof(time) - 1, str_val);
                ESP_LOGI(TAG, "control time is: %s\n", time);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }
            if (json_obj_get_string(&jctx, "errValue", str_val, sizeof(str_val)) == OS_SUCCESS)
            {
                snprintf(errvalue, sizeof(errvalue), "%.*s", sizeof(errvalue) - 1, str_val);
                ESP_LOGI(TAG, "errValue  is: %s\n", errvalue);
            }
            else
            {
                // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
                // return ESP_FAIL;
            }
            pushtime_count = atoi(time);
            printf("push time is:%d\r\n", pushtime_count);
            err_value = atoi(errvalue);
            printf("errvalue is:%d\r\n", err_value);
        }

        //------------------

        // char *json_str = NULL;
        // size_t size = 0;
        // if(RFID_ReadEPC(rfid_read_config) != Ok)
        //     size = asprintf(&json_str, "{\"status\":\"200\",\"result\":\"failed\"}");
        // else
        //     size = asprintf(&json_str, "{\"status\":\"200\",\"result\":\"success\"}");
        // esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
        // ESP_ERROR_CHECK(ret);
        // ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
        // ESP_ERROR_CHECK(ret);
        // ret = httpd_resp_send(req, json_str, size);
        // free(json_str);
        // ESP_ERROR_CHECK(ret);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "ABC: %s", sys_info_config.mqtt_address);
    // 创建遗嘱消息的 JSON 对象
    cJSON *lwt_json = cJSON_CreateObject();
    cJSON_AddStringToObject(lwt_json, "id", lwt_content);
    lwt_msg = cJSON_PrintUnformatted(lwt_json); // 将 JSON 对象转换为字符串

    esp_mqtt_client_config_t mqtt_cfg = {
        // .broker.address.uri = sys_info_config.mqtt_address,
        .broker = {// 嵌套的结构体需要用花括号
                   .address = {
                       .uri = sys_info_config.mqtt_address, // MQTT Broker URI
                   }},
        .credentials = {// .username = "starry",
                        // .authentication = {
                        //     .password = "emqttx",  // MQTT Password
                        // }

                        // .username = "doubleCoinChongQing",
                        // .authentication = {
                        //     .password = "Cadcad431.",  // MQTT Password
                        // }

                        .username = "hjie",
                        .authentication = {
                            .password = "129223.", // MQTT Password
                        }

        },

        .session = {.keepalive = 60, // 设置 Keep-Alive 时间

                    .last_will = {
                        .topic = "devices/status/disconnect", // 遗嘱消息主题
                        .msg = lwt_msg,                       // 遗嘱消息内容
                        .qos = 1,
                        .retain = true,
                    }}};

    // mqtt_cfg.authentication.username="starry";
    // mqtt_cfg.authentication.password="";
    // mqtt_cfg.broker.address.uri = sys_info_config.mqtt_address;
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, -1, mqtt_event_handler, NULL); // 注册事件处理函数
    esp_mqtt_client_start(mqtt_client);                                        // 启动MQTT客户端

    // 释放 JSON 对象，但不释放 lwt_msg，因为 MQTT 客户端需要使用它
    cJSON_Delete(lwt_json);
}

static void ethernet_w5500_init(void)
{
    // Create instance(s) of esp-netif for SPI Ethernet(s)
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    esp_netif_t *eth_netif_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = {NULL};
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++)
    {
        itoa(i, num_str, 10);
        strcat(strcpy(if_key_str, "ETH_SPI_"), num_str);
        strcat(strcpy(if_desc_str, "eth"), num_str);
        esp_netif_config.if_key = if_key_str;
        esp_netif_config.if_desc = if_desc_str;
        esp_netif_config.route_prio = 30 - i;
        eth_netif_spi[i] = esp_netif_new(&cfg_spi);
    }

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

    // Install GPIO ISR handler to be able to service SPI Eth modlues interrupts
    gpio_install_isr_service(0);

    // Init SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 0);

    // Configure SPI interface and Ethernet driver for specific SPI module
    esp_eth_mac_t *mac_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    esp_eth_phy_t *phy_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    esp_eth_handle_t eth_handle_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = {NULL};
    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20};
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++)
    {
        // Set SPI module Chip Select GPIO
        spi_devcfg.spics_io_num = spi_eth_module_config[i].spi_cs_gpio;
        // Set remaining GPIO numbers and configuration used by the SPI module
        phy_config_spi.phy_addr = spi_eth_module_config[i].phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config[i].phy_reset_gpio;
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(CONFIG_EXAMPLE_ETH_SPI_HOST, &spi_devcfg);
        w5500_config.int_gpio_num = spi_eth_module_config[i].int_gpio;
        mac_spi[i] = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
        phy_spi[i] = esp_eth_phy_new_w5500(&phy_config_spi);
        esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi[i], phy_spi[i]);
        ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_handle_spi[i]));

        /* The SPI Ethernet module might not have a burned factory MAC address, we cat to set it manually.
          02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
        */
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle_spi[i], ETH_CMD_S_MAC_ADDR, (uint8_t[]){0x02, 0x00, 0x00, 0x12, 0x34, 0x56 + i}));
        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi[i], esp_eth_new_netif_glue(eth_handle_spi[i])));
    }

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++)
    {
        ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi[i]));
    }
}

static void module_4G_init(void)
{
    /* Initialize modem board. Dial-up internet */
    modem_config_t modem_config = MODEM_DEFAULT_CONFIG();
    /* Modem init flag, used to control init process */
#ifndef CONFIG_EXAMPLE_ENTER_PPP_DURING_INIT
    /* if Not enter ppp, modem will enter command mode after init */
    modem_config.flags |= MODEM_FLAGS_INIT_NOT_ENTER_PPP;
    /* if Not waiting for modem ready, just return after modem init */
    modem_config.flags |= MODEM_FLAGS_INIT_NOT_BLOCK;
#endif
    modem_config.handler = on_modem_event;

    modem_board_init(&modem_config);
}

static void wifi_nvs_par(void)
{
    char ssid[32] = {0};
    char password[64] = {0};
    size_t len;
    esp_err_t err;
    nvs_handle_t nvs_handle;

    // // 打开 NVS
    // err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG_STA, "Failed to open NVS: %s", esp_err_to_name(err));
    //     return;
    // }

    // 读取 SSID
    len = sizeof(ssid);
    err = from_nvs_get_value("wifi_ssid", ssid, &len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to read SSID from NVS: %s", esp_err_to_name(err));
        return;
    }

    // 读取密码
    len = sizeof(password);
    err = from_nvs_get_value("wifi_pswd", password, &len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_STA, "Failed to read PSWD from NVS: %s", esp_err_to_name(err));
        return;
    }
    strncpy(wifi_sta_info.ssid, ssid, sizeof(wifi_sta_info.ssid));
    strncpy(wifi_sta_info.password, password, sizeof(wifi_sta_info.password));
}

static void wifi_ap_init(void)
{
    esp_netif_t *ap_netif = modem_wifi_ap_init();
    assert(ap_netif != NULL);
    ESP_ERROR_CHECK(modem_wifi_set(s_modem_wifi_config));
}

static void wifi_ap_sta_init(void)
{
    // 初始化 Wi-Fi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 设置 Wi-Fi 模式为 AP+STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // 创建 AP 模式的网络接口
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap(); // 创建一个默认的WIFI接入点AP的网络端口
    assert(ap_netif != NULL);                                   // 确保创建成功

    snprintf(chip_id_ssid_str, sizeof(chip_id_ssid_str), "RFID_AP_%012llX", chip_id);

    // 配置 AP 模式的 Wi-Fi 参数
    wifi_config_t ap_config = {
        .ap = {
            // .ssid = chip_id_ssid_str,//ssid名称
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD, // 密钥
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,    // 通道
            .max_connection = EXAMPLE_MAX_STA_CONN, // 最大连接数
            .authmode = WIFI_AUTH_WPA2_PSK,         // 认证模式
        },
    };
    // 把芯片ID号作为AP SSID
    //  snprintf(chip_id_ssid_str, sizeof(chip_id_ssid_str), "RFID_AP%012llX", chip_id);
    strncpy((char *)ap_config.ap.ssid, chip_id_ssid_str, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0'; // 确保字符串结束符

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config)); // 设置AP模式下的Wi-Fi参数

    // 创建 STA 模式的网络接口
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta(); // 创建一个默认的WIFI客户端STA的网络端口，并返回指向该接口的指针
    assert(sta_netif != NULL);                                    // 确保创建成功

    // 从NVS中读取ssid和pswd
    wifi_nvs_par();

    // 配置 STA 模式的 Wi-Fi 参数
    // wifi_config_t sta_config = {
    //     .sta = {
    //         .ssid = EXAMPLE_ESP_WIFI_STA_SSID,//ssid名称
    //         .password = EXAMPLE_ESP_WIFI_STA_PASSWD,//密钥
    //         .scan_method = WIFI_ALL_CHANNEL_SCAN,//扫描方式
    //         .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,//失败重试次数
    //         .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,//认证模式
    //     },
    // };
    // ESP_LOGE("123","%s,len=%d",wifi_sta_info.ssid,strlen(wifi_sta_info.ssid));
    // if (strlen(wifi_sta_info.ssid) > 0) {//如果nvs中有值,就不使用默认值（idf.py menuconfig中设置的）
    //     strncpy((char *)sta_config.sta.ssid, wifi_sta_info.ssid, sizeof(sta_config.sta.ssid) - 1);
    //     strncpy((char *)sta_config.sta.password, wifi_sta_info.password, sizeof(sta_config.sta.password) - 1);
    // }

    wifi_config_t sta_config = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        }};

    if (strlen(wifi_sta_info.ssid) > 0)
    { // nvs中有ssid和pswd
        strncpy((char *)sta_config.sta.ssid, wifi_sta_info.ssid, sizeof(sta_config.sta.ssid) - 1);
        sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';

        strncpy((char *)sta_config.sta.password, wifi_sta_info.password, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';
    }
    else
    { // 没有则默认ssid pswd (idf.py menuconfig -> )
        strncpy((char *)sta_config.sta.ssid, EXAMPLE_ESP_WIFI_STA_SSID, sizeof(sta_config.sta.ssid) - 1);
        sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';

        strncpy((char *)sta_config.sta.password, EXAMPLE_ESP_WIFI_STA_PASSWD, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config)); // 设置STA模式下的Wi-Fi参数

    // 启动 Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 连接 STA 模式
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG_STA, "WiFi STA mode initialized, connecting to SSID: %s", sta_config.sta.ssid); // 打印信息，连接Wi-Fi的ssid名称
}

void network_init(void)
{
    // module_4G_init();

    // 2025.1.2
    if (sys_info_config.sys_networking_mode == SYS_NETWORKING_4G)
    {
        module_4G_init();
    }
    else if (sys_info_config.sys_networking_mode == SYS_NETWORKING_ETHERNET)
    {
        ethernet_w5500_init();
    }
    else if (sys_info_config.sys_networking_mode == SYS_NETWORKING_UNB)
    {
        // module_4G_init();
        // ethernet_w5500_init();
        unb_tp1107_init(); // tp1107初始化
    }
    else
    {
        ESP_LOGI(TAG, "=========fatal err=========");
        return;
    }
    ESP_LOGI(TAG, "=========Network connected=========");

    // wifi_ap_init();//热点
    wifi_ap_sta_init(); // 配置ap+sta模式
}

void mqtt_init(void)
{
    mqtt_app_start();
    mqtt_xBinarySemaphore = xSemaphoreCreateBinary();
    if (mqtt_xBinarySemaphore == NULL)
    {
        // printf("创建信号量失败\r\n");
        return;
    }
}

int mqtt_client_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    return esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
}

char *get_effective_ip_str(void)
{
    static char ip_str[16] = {0}; // 静态分配，避免野指针（最大长度 xxx.xxx.xxx.xxx）
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0)
    {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI("IP", "已获取 IP: %s", ip_str);
        return ip_str;
    }
    else
    {
        ESP_LOGW("IP", "未获取有效 IP");
        return NULL;
    }
}