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

static const char *TAG = "rfid_network";
static const char *TAG_4G = "network_4G_module";
static const char *TAG_ETHERNET = "network_ethernet";
static const char *TAG_MQTT = "mqtt";

#define INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num)                                      \
    do {                                                                                        \
        eth_module_config[num].spi_cs_gpio = CONFIG_EXAMPLE_ETH_SPI_CS ##num## _GPIO;           \
        eth_module_config[num].int_gpio = CONFIG_EXAMPLE_ETH_SPI_INT ##num## _GPIO;             \
        eth_module_config[num].phy_reset_gpio = CONFIG_EXAMPLE_ETH_SPI_PHY_RST ##num## _GPIO;   \
        eth_module_config[num].phy_addr = CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR ##num;                \
    } while(0)

typedef struct {
    uint8_t spi_cs_gpio;
    uint8_t int_gpio;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
}spi_eth_module_config_t;

static esp_mqtt_client_handle_t mqtt_client;
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
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

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG_ETHERNET, "Ethernet Link Up");
        ESP_LOGI(TAG_ETHERNET, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG_ETHERNET, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG_ETHERNET, "Ethernet Got IP Address");
    ESP_LOGI(TAG_ETHERNET, "~~~~~~~~~~~");
    ESP_LOGI(TAG_ETHERNET, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG_ETHERNET, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG_ETHERNET, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG_ETHERNET, "~~~~~~~~~~~");
}


static void on_modem_event(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == MODEM_BOARD_EVENT) {
        if ( event_id == MODEM_EVENT_SIMCARD_DISCONN) {
            ESP_LOGW(TAG_4G, "Modem Board Event: SIM Card disconnected");
            //led_indicator_start(s_led_system_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_SIMCARD_CONN) {
            ESP_LOGI(TAG_4G, "Modem Board Event: SIM Card Connected");
            //led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_DTE_DISCONN) {
            ESP_LOGW(TAG_4G, "Modem Board Event: USB disconnected");
            //led_indicator_start(s_led_system_handle, BLINK_CONNECTING);
        } else if ( event_id == MODEM_EVENT_DTE_CONN) {
            ESP_LOGI(TAG_4G, "Modem Board Event: USB connected");
            //led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
            //led_indicator_stop(s_led_system_handle, BLINK_CONNECTING);
        } else if ( event_id == MODEM_EVENT_DTE_RESTART) {
            ESP_LOGW(TAG_4G, "Modem Board Event: Hardware restart");
            //led_indicator_start(s_led_system_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_DTE_RESTART_DONE) {
            ESP_LOGI(TAG_4G, "Modem Board Event: Hardware restart done");
            //led_indicator_stop(s_led_system_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_NET_CONN) {
            ESP_LOGI(TAG_4G, "Modem Board Event: Network connected");
            led_indicator_start(s_led_net_status_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_NET_DISCONN) {
            ESP_LOGW(TAG_4G, "Modem Board Event: Network disconnected");
            led_indicator_stop(s_led_net_status_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_WIFI_STA_CONN) {
            ESP_LOGI(TAG_4G, "Modem Board Event: Station connected");
            //led_indicator_start(s_led_wifi_handle, BLINK_CONNECTED);
        } else if ( event_id == MODEM_EVENT_WIFI_STA_DISCONN) {
            ESP_LOGW(TAG_4G, "Modem Board Event: All stations disconnected");
            //led_indicator_stop(s_led_wifi_handle, BLINK_CONNECTED);
        }
    }
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
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "ctlrfid", 0);//订阅一个控制读写器的主题
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



        // 解析JSON数据
        char on_off[5] = "";
        char read_mode[20] = "";
        char ant_sel[4] = "";
        char interval_time[5] = "";
        char buf[512] = { 0 };
        rfid_read_config_t rfid_read_config;

        if (strncmp(event->topic, "ctlrfid", event->topic_len) != 0) {
            // 处理 ctlrfid 的消息
            break;
        } 

        // // 判断数据是否是 JSON 格式
        if (is_json_data(event->data, event->data_len)) {
            
            ESP_LOGI(TAG, "Valid JSON data received");
            // 使用 json_parse_message 函数解析 JSON 消息
            // json_parse_message(event->data, event->data_len);
        } else {
            ESP_LOGI(TAG, "Received data is not in JSON format");
            break;
        }
        

        jparse_ctx_t jctx;
        int ps_ret = json_parse_start(&jctx, event->data, event->data_len);
        char str_val[64];
        if (json_obj_get_string(&jctx, "on_off", str_val, sizeof(str_val)) == OS_SUCCESS) {
            snprintf(on_off, sizeof(on_off), "%.*s", sizeof(on_off) - 1, str_val);
            ESP_LOGI(TAG, "rfid read control: %s\n", on_off);
        } else {
            // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
            // return ESP_FAIL;
        }
        if (json_obj_get_string(&jctx, "read_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
            snprintf(read_mode, sizeof(read_mode), "%.*s", sizeof(read_mode) - 1, str_val);
            ESP_LOGI(TAG, "rfid read mode: %s\n", read_mode);
        } else {
            // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
            // return ESP_FAIL;
        }
        if (json_obj_get_string(&jctx, "ant_sel", str_val, sizeof(str_val)) == OS_SUCCESS) {
            snprintf(ant_sel, sizeof(ant_sel), "%.*s", sizeof(ant_sel) - 1, str_val);
            ESP_LOGI(TAG, "rfid read ant: %s\n", ant_sel);
        } else {
            // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
            // return ESP_FAIL;
        }
        if (json_obj_get_string(&jctx, "interval_time", str_val, sizeof(str_val)) == OS_SUCCESS) {
            snprintf(interval_time, sizeof(interval_time), "%.*s", sizeof(interval_time) - 1, str_val);
            ESP_LOGI(TAG, "rfid read interval time: %s\n", interval_time);
        } else {
            // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
            // return ESP_FAIL;
        }

        if(!strcmp(on_off, "on"))
            rfid_read_config.rfid_read_on_off = RFID_READ_ON;
        else if(!strcmp(on_off, "off"))
            rfid_read_config.rfid_read_on_off = RFID_READ_OFF;
        else {
            // httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter on_off error");
            // return ESP_FAIL;
        }
        if(!strcmp(read_mode, "once"))
            rfid_read_config.rfid_read_mode = RFID_READ_MODE_ONCE;
        else if(!strcmp(read_mode, "continuous"))
            rfid_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;
        else {
            // httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parameter read_mode error");
            // return ESP_FAIL;
        }
        rfid_read_config.ant_sel = atoi(ant_sel);
        rfid_read_config.read_interval_time = atoi(interval_time);
        printf("ant_sel:%x\r\n", rfid_read_config.ant_sel);
        printf("read_interval_time:%ld\r\n", rfid_read_config.read_interval_time);
        

        RFID_ReadEPC(rfid_read_config);


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
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
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
    esp_mqtt_client_config_t mqtt_cfg = {
        // .broker.address.uri = sys_info_config.mqtt_address,
        .broker.address.uri =sys_info_config.mqtt_address,
        .credentials.username = "starry",
        .credentials.authentication.password = "emqttx"
    };
    // mqtt_cfg.authentication.username="starry";
    // mqtt_cfg.authentication.password="";
    // mqtt_cfg.broker.address.uri = sys_info_config.mqtt_address;
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, -1, mqtt_event_handler, NULL);//注册事件处理函数
    esp_mqtt_client_start(mqtt_client);//启动MQTT客户端
}


static void ethernet_w5500_init(void)
{
    // Create instance(s) of esp-netif for SPI Ethernet(s)
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t *eth_netif_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];    
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
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
    esp_eth_handle_t eth_handle_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20
    };
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
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle_spi[i], ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
            0x02, 0x00, 0x00, 0x12, 0x34, 0x56 + i
        }));
        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi[i], esp_eth_new_netif_glue(eth_handle_spi[i])));
    }

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
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


static void wifi_ap_init(void)
{
    esp_netif_t *ap_netif = modem_wifi_ap_init();
    assert(ap_netif != NULL);
    ESP_ERROR_CHECK(modem_wifi_set(s_modem_wifi_config));
}

void network_init(void)
{
    // module_4G_init();
    if(sys_info_config.sys_networking_mode == SYS_NETWORKING_4G) {
        module_4G_init();
    } else if(sys_info_config.sys_networking_mode == SYS_NETWORKING_ETHERNET) {
        ethernet_w5500_init();
    } else if(sys_info_config.sys_networking_mode == SYS_NETWORKING_ALL) {
        module_4G_init();
        ethernet_w5500_init();
    } else {
         ESP_LOGI(TAG, "=========fatal err=========");
         return;
    } 
    ESP_LOGI(TAG, "=========Network connected=========");
    wifi_ap_init();
}

void mqtt_init(void)
{
    mqtt_app_start();
}

int mqtt_client_publish(const char *topic, const char *data, int len, int qos, int retain)
{
    return esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
}

