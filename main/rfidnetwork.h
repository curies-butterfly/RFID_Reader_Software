#ifndef     RFIDNETWORK__H_
#define     RFIDNETWORK__H_
#include "usbh_modem_wifi.h"

void network_init(void);
void mqtt_init(void);
int mqtt_client_publish(const char *topic, const char *data, int len, int qos, int retain);

#endif