
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
#include "sht30.h"
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
#include "oleddisplayfonts.h"
#include "ntc_temp_adc.h"

static const char *TAG = "rfid_reader";

void RFID_MqttTask(void *arg);

Dictionary DicPower1;
PoweInfo_t  PoweInfo;
uint8_t freaRang = 0;
CapacityInfo_t CapacityInfo;
WorkFreq_t WorkFreq; 

result_t rfidready_flag;//rfid初始化标志位

char oled_buffer[20];//oled显示  字符数组
char oled_number[20];//oled显示  
void oled_data_display(OLEDDisplay_t *oled);

//RFID 模组初始化，显示和配置相关参数
void rfidModuleInit()
{
    result_t ret = Error;
    int timeout = 3;
    int timecnt = 0;
    while(ret != Ok) 
    {  
        timecnt++;
        ret= RFID_StopRead();
        if(timecnt > timeout)
        {
          return;
        }
    }
    DicPower1.AntennaNo = 0x01;
    DicPower1.Power = 33;
    RFID_SetPower(DicPower1,0,1);
    DicPower1.AntennaNo = 0x02;
    RFID_SetPower(DicPower1,0,1);
    DicPower1.AntennaNo = 0x03;
    RFID_SetPower(DicPower1,0,1);
    DicPower1.AntennaNo = 0x04;
    RFID_SetPower(DicPower1,0,1);

    if(Ok == RFID_GetCapacity(&CapacityInfo))
    {
        RFID_ShowCapacityInfo(CapacityInfo);
    }
    else
        printf("读写器能力查询失败\r\n");

    if(Ok == RFID_GetFreqRanger(&freaRang))
        printf("读写器RF频段:%d,%s\r\n",freaRang,FreqTab[freaRang]);
    else
        printf("读写器RF频段查询失败\r\n");

    if(Ok == RFID_GetPower(&PoweInfo))
        RFID_ShowPower(PoweInfo);
    else
        printf("读写器功率查询失败\r\n");

    if(Ok != RFID_GetWorkFreq(&WorkFreq))
        printf("读写器工作频率查询失败\r\n");

    if(Ok != RFID_StopRead())
        printf("RFID stop read error\r\n");
}


void mqtt_publish_epc_data()
{
    EPC_Info_t  **EPC_ptr = &LTU3_Lable;
    char  *epc_data = NULL;
    if(epcCnt == 0)         //防止出现空指针
    {
        epc_data = (char*)malloc(sizeof(char) * 1 * 50);
        memset(epc_data,'\0',sizeof(char) * 1 * 50);
    }
    else
    {
        epc_data = (char*)malloc(sizeof(char) * epcCnt * 50);
        memset(epc_data,'\0',sizeof(char) * epcCnt * 50);
    }
    char  one_epc_data[50] = {'\0'};
    uint16_t epc_read_rate = 20;
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr[i] == NULL)
            break;
        memset(one_epc_data,0,sizeof(one_epc_data));
        sprintf(one_epc_data,"{\"epc\":\"%x%x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",              \
        EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0,EPC_ptr[i]->antID,EPC_ptr[i]->rssi); 
        strcat(epc_data,one_epc_data);
        strcat(epc_data,",");
    }
    epc_data[strlen(epc_data) - 1] = '\0';
    size_t size = 0;
    char *json_str = NULL;
    size = asprintf(&json_str,"{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",\
            epcCnt,epc_read_rate,epc_data);
    free(epc_data);
    mqtt_client_publish("rfid", json_str, size, 0, 1);
    free(json_str);
}


/*****************************************************
函数名称：void RFID_MqttTask(void *arg)
函数功能：
返回值：  无
注：
*********************************************************/
void RFID_MqttTask(void *arg)
{
    while(1)
    {
        // printf("hello1");
        // static int flag=0;
        // //读EPC标签温度指令

        // //将RFID模块读取到的天线号 温度值 信号强度 转为json
        // if(Ok==rfidready_flag)
        // {

        if(xSemaphoreTake(mqtt_xBinarySemaphore, portMAX_DELAY)!= pdTRUE)
            continue;
        mqtt_publish_epc_data();
        vTaskDelay(1000/portTICK_PERIOD_MS);
        // }
       
        // MQTT把json数据发送服务器
    }
 
}

void oled_data_display(OLEDDisplay_t *oled){
  //ID:########
  //温度：38℃ 
  //运行状态：正常/异常

  
  sprintf(oled_number, "%2d", temp);
  OLEDDisplay_clear(oled);
  //________________第一行ID_______________
  OLEDDisplay_setTextAlignment(oled,TEXT_ALIGN_CENTER);
  OLEDDisplay_setFont(oled,ArialMT_Plain_16);
  sprintf(oled_buffer, "ID: %08lx", chip_id);
  OLEDDisplay_drawString(oled,64, 0, oled_buffer);

  //________________第二行温度_______________
  OLEDDisplay_drawXbm(oled,1, 16,128,18,myBitmap1);
  OLEDDisplay_setTextAlignment(oled,TEXT_ALIGN_LEFT);
  OLEDDisplay_setFont(oled,ArialMT_Plain_16);
  OLEDDisplay_drawString(oled,85, 16, oled_number);
    
  //________________第三行工作状态_______________
  OLEDDisplay_drawXbm(oled,1, 32,128,18,myBitmap2);
  OLEDDisplay_display(oled);
  OLEDDisplay_flipScreenVertically(oled);//反转镜像
}

void app_main(void)
{
    get_chip_IDinfo();//打印芯片信息


    /* Initialize led indicator */
    _led_indicator_init();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sys_info_config_init(&sys_info_config);
    get_nvs_wifi_config(s_modem_wifi_config);
    get_nvs_auth_info_config();
    get_nvs_sys_info_config();
    
    //from_nvs_set_value 目前没有加nvs写操作，只有读取nvs中的数据
    char strcop[]="mqtt://123.60.157.221";
    strcpy(sys_info_config.mqtt_address, strcop);
    // sys_info_config.mqtt_address="mqtt://123.60.157.221";
    // sys_info_config.tcp_address="123.60.157.221";
    // sys_info_config.tcp_port=1883;

    network_init();
    mqtt_init();

    rfid_http_init(s_modem_wifi_config);

    uart1_Init();

    uart2_Init();
    moduleSetGpioInit();
    // iic_init();
    modBusRtu_Init();
    xTaskCreate(rx_task, "u1_rx_task", 4096*3, NULL,configMAX_PRIORITIES-2, NULL); 
    xTaskCreate(modbusRtuRx_task, "modbusRtuRx_task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL); 
    xTaskCreate(modbusRtuDeal_Task, "modbusRtuDeal_Task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL);
    xTaskCreate(RFID_ReadEpcTask, "RFID_ReadEpcTask", 4096*3, NULL,configMAX_PRIORITIES-4, NULL);
    xTaskCreate(RFID_MqttTask, "RFID_MqttTask", 4096*3, NULL,configMAX_PRIORITIES-5, NULL);
    
    rfidModuleInit();

    //RFID_SendReadEpcCmd(0x08,1);
    led_indicator_start(s_led_run_status_handle, BLINK_CONNECTING);
    
    ntc_init();

    OLEDDisplay_t *oled = OLEDDisplay_init(I2C_NUM_1,0x78,I2C_MASTER_SDA_IO,I2C_MASTER_SCL_IO);

    // char on_off[5] = "";
    // char read_mode[20] = "";
    // char ant_sel[4] = "";
    // char interval_time[5] = "";
    // char buf[512] = { 0 };
    // rfid_read_config_t rfid_read_config;
    // rfid_read_config.rfid_read_on_off = RFID_READ_ON;//读写器开
    // rfid_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;//连续模式
    // rfid_read_config.ant_sel = 0x0F;//00001111 ANT4 ANT3 ANT2 ANT1
    // rfid_read_config.read_interval_time =1000;//读取频率间隔
    // rfidready_flag=RFID_ReadEPC(rfid_read_config);
    
    // ant_sel[0]="1";
    // ant_sel[1]="2";
    // ant_sel[2]="3";
    // ant_sel[3]="4";
   

    // interval_time[0]="200";
    // interval_time[1]="250";
    // interval_time[2]="300";
    // interval_time[3]="350";
    // interval_time[4]="400";

    // OLEDDisplay_setTextAlignment(oled,TEXT_ALIGN_CENTER);
    // OLEDDisplay_setFont(oled,ArialMT_Plain_24);
    // OLEDDisplay_drawString(oled,64, 00, "HB RFID");
    // OLEDDisplay_drawXbm(oled,1, 1,128,48,myBitmap);
    // // OLEDDisplay_drawString(oled,64,00,(char *)charray);
    // // OLEDDisplay_flipScreenVertically(oled);//反转镜像
  
    // OLEDDisplay_display(oled);
    // OLEDDisplay_setTextAlignment(oled,TEXT_ALIGN_CENTER);
    // OLEDDisplay_setFont(oled,ArialMT_Plain_16);
    // sprintf(buffer, "ID: %08lx", chip_id);
    // OLEDDisplay_drawString(oled,64, 48, buffer);
    // OLEDDisplay_display(oled);
    // OLEDDisplay_flipScreenVertically(oled);//反转镜像

    fan_gpio_init();
   
    // vTaskDelay(1000 / portTICK_PERIOD_MS); // 延迟 1 秒

    while(1)
    {
        printf("free heap size: %d\r\n",xPortGetFreeHeapSize());
        // RFID_ShowEpc(LTU3_Lable);
        // SHT30_read_result(0x44, &sht30_data);
        // char *json_str = NULL;
        // size_t size = 0;
        // size = asprintf(&json_str,
        // "{\"T_rfid\":%.2f,\"T_sht30\":%.2f,\"H_rfid\":%.2f,\"H_sht30\":%.2f}",
        // 0.0, sht30_data.Temperature, 0.0, sht30_data.Humidity);
        // mqtt_client_publish("rfid", json_str, size, 0, 1);
        // printf("tem:%.2f\thumt:%.2f\r\n", sht30_data.Temperature, sht30_data.Humidity);
        // mqtt_publish_epc_data();
        ntc_temp_adc_run();
        oled_data_display(oled);
        vTaskDelay(2000/portTICK_PERIOD_MS);
    }
}

