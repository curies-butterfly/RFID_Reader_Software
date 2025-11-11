
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"
#include "driver_uart.h"
#include "rfidmodule.h"
#include "rfidcommon.h"
#include "modbus_rtu.h"
#include "sc16is752.h"
#include "led.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "usbh_modem_wifi.h"
#include "driver/spi_master.h"
#include "rfidnetwork.h"
#include "httpconfig.h"
#include "parameter.h"
#include "sdkconfig.h"
#include "dev_info.h"
#include "OLEDDisplay.h"
#include "OLEDDisplayFonts.h"
#include "ntc_temp_adc.h"
#include "hal/wdt_hal.h"
#include "at.h"
#include "tp1107.h"

#include "data_deal.h"
#include "app_tasks.h"

static const char *TAG = "rfid_reader";

void RFID_MqttTask(void *arg);


char str[26] = "";
size_t str_size = sizeof(str);

// char *strcop="mqtt://123.60.157.221";//测试
char *strcop = "mqtt://101.37.253.97:4635";
/*      end             */



void app_main(void)
{

    get_chip_IDinfo(); // 获取芯片ID信息

    wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT(); // 看门狗计时器WDT 初始化
    wdt_hal_write_protect_disable(&rwdt_ctx);                // 禁用写保护
    wdt_hal_feed(&rwdt_ctx);                                 // 喂狗 4G初始化有点长

    /* Initialize led indicator */
    _led_indicator_init(); // 指示灯 io初始化

    esp_err_t ret = nvs_flash_init(); // nvs初始化

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 若由于分区版本更新或无可用页(存储空间不足)，尝试格式化并重新初始化
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wait_for_config_or_timeout(); // 串口0配置

    /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init()); // 初始化网络接口

    /*所有网络相关的事件（如连接、断开连接等）都会通过事件循环进行分发。默认事件循环可以让你的应用程序监听并处理这些事件。*/
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建了一个默认的事件循环

    sys_info_config_init(&sys_info_config);
    get_nvs_wifi_config(s_modem_wifi_config);
    get_nvs_auth_info_config();
    get_nvs_sys_info_config();

    ESP_LOGI(TAG, "MQTT_ADDRESS: %s", sys_info_config.mqtt_address);
    ESP_LOGI("Type_EPC", "type_epc = %d\n", type_epc);

    wdt_hal_feed(&rwdt_ctx); // 先喂一次狗
    network_init();
    wdt_hal_feed(&rwdt_ctx); // 先喂一次狗
    mqtt_init();
    rfid_http_init(s_modem_wifi_config);

    uart1_Init();

    // uart2_Init();
    // moduleSetGpioInit();
    // // iic_init();
    // modBusRtu_Init();

    // sc16is752_uart_init(SC16IS752_CHANNEL_A, 115200);
    // sc16is752_uart_init(SC16IS752_CHANNEL_B, 19200);
    if (sc16is752_init_all() != ESP_OK)
    {
        ESP_LOGE("MAIN", "SC16IS752 initialization failed");
        return;
    }

    // const char *msg = "Hello SC16IS752!";

    // ret1 = sc16is752_send_buffer(SC16IS752_CHANNEL_A, (const uint8_t *)sendbuf1, strlen(sendbuf1));
    // sc16is752_send_buffer(SC16IS752_CHANNEL_A, sendbuf1, sizeof(sendbuf1));

    // xEventGroup = xEventGroupCreate();
    // if (xEventGroup == NULL) {
    //     ESP_LOGE("app_main", "Event group creation failed!");
    //     return;  // 事件组创建失败，直接返回
    // }
    // xEventGroupSetBits(xEventGroup, TASK_2_EVENT_BIT);

    xTaskCreate(rx_task, "u1_rx_task", 4096 * 2, NULL, configMAX_PRIORITIES - 2, NULL);
    // xTaskCreate(modbusRtuRx_task, "modbusRtuRx_task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL);
    // xTaskCreate(modbusRtuDeal_Task, "modbusRtuDeal_Task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL);
    xTaskCreate(RFID_ReadEpcTask, "RFID_ReadEpcTask", 4096 * 2, NULL, configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(RFID_MqttTimeTask, "RFID_MqttTimeTask", 4096 * 2, NULL, configMAX_PRIORITIES - 5, NULL);
    // xTaskCreate(RFID_MqttErrTask, "RFID_MqttErrTask", 4096*2, NULL,configMAX_PRIORITIES-5, NULL);
    xTaskCreate(Screen_DataTask, "Screen_DataTask", 2048, NULL, configMAX_PRIORITIES - 8, NULL);//tjc屏幕显示温度标签数据
    
    // xTaskCreate(modbusRtuDeal_Task, "modbusRtuDeal_Task", 4096*2, NULL,configMAX_PRIORITIES-6, NULL);

    wdt_hal_feed(&rwdt_ctx); // 喂一次狗
    rfidModuleInit();
    wdt_hal_feed(&rwdt_ctx); // 喂一次狗
    ntc_init();
    fan_gpio_init();

    ctrl_rfid_mode(0);
    vTaskDelay(2000/portTICK_PERIOD_MS);
    ctrl_rfid_mode(2);

    while (1)
    {
        wdt_hal_feed(&rwdt_ctx);
        printf("free heap size: %d\r\n", xPortGetFreeHeapSize());
        // sc16is752_send_buffer(SC16IS752_CHANNEL_A, sendbuf1, sizeof(sendbuf1));
        ntc_temp_adc_run();
        // oled_data_display(oled);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}


