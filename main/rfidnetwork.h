#ifndef     RFIDNETWORK__H_
#define     RFIDNETWORK__H_
#include "usbh_modem_wifi.h"

void network_init(void);
void mqtt_init(void);
int mqtt_client_publish(const char *topic, const char *data, int len, int qos, int retain);

extern char lwt_content[100];  //遗嘱主题内容设备id号
extern int pushtime_count;
extern int err_value;
extern bool sim_card_connected;


#endif