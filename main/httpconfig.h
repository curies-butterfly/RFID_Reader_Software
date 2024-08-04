#ifndef HTTPCONFIG__H_
#define HTTPCONFIG__H_

#include <sys/queue.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "usbh_modem_wifi.h"


/**
 * @brief Structure for recording the connected sta
 * 
 */
struct modem_netif_sta_info {
    SLIST_ENTRY(modem_netif_sta_info) field;
    uint8_t mac[6];
    char name[32];
    esp_ip4_addr_t ip;
    int64_t start_time;
};

typedef SLIST_HEAD(modem_http_list_head, modem_netif_sta_info) modem_http_list_head_t;



esp_err_t modem_http_get_nvs_wifi_config(modem_wifi_config_t *wifi_config);
esp_err_t rfid_http_init(modem_wifi_config_t *wifi_config);


#endif // !HTTPCONFIG__H