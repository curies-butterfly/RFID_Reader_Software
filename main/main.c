
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
#include "hal/wdt_hal.h"

#include "tp1107.h"

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

char str[26] = "";
size_t str_size = sizeof(str);

/*      相关配置         */
// char *strcop="mqtt://123.60.157.221";//测试
char *strcop="mqtt://101.37.253.97:4635";
/*      end             */

// // 定义事件组句柄
// static EventGroupHandle_t xEventGroup;

// // 定义事件标志位
// #define TASK_1_EVENT_BIT (1 << 0)  // 任务1事件标志位
// #define TASK_2_EVENT_BIT (1 << 1)  // 任务2事件标志位

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
        epc_data = (char*)malloc(sizeof(char) * 1 * 120);
        memset(epc_data,'\0',sizeof(char) * 1 * 120);
    }
    else
    {
        epc_data = (char*)malloc(sizeof(char) * epcCnt * 120);
        memset(epc_data,'\0',sizeof(char) * epcCnt * 120);
    }
    char  one_epc_data[120] = {'\0'};
    uint16_t epc_read_rate = 20;
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr[i] == NULL)
            break;
        memset(one_epc_data,0,sizeof(one_epc_data));

        EPC_ptr[i]->last_temp =(EPC_ptr[i]->tempe)/100.0;//EPC_ptr

        sprintf(one_epc_data,"{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",              \
        EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],EPC_ptr[i]->tempe/100.0,EPC_ptr[i]->antID,EPC_ptr[i]->rssi); 
        
        ESP_LOGI(TAG,"%02x%02x:%.2f\r\n",EPC_ptr[i]->epcId[0],EPC_ptr[i]->epcId[1],(LTU3_Lable[i]->tempe)/100.0);
        
        strcat(epc_data,one_epc_data);
        strcat(epc_data,",");
    }
    epc_data[strlen(epc_data) - 1] = '\0';
    size_t size = 0;
    char *json_str = NULL;
    size = asprintf(&json_str,"{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",\
            epcCnt,epc_read_rate,epc_data);
    free(epc_data);

    if (epcCnt != 0) //有数据才上报
    {
        mqtt_client_publish(send_topic, json_str, size, 0, 1);
    }
   
    free(json_str);
    
}

void mqtt_publish_epc_data2()
{
    EPC_Info_t  **EPC_ptr2 = &LTU3_Lable;
    char  *epc_data2 = NULL;
    if(epcCnt == 0)         //防止出现空指针
    {
        epc_data2 = (char*)malloc(sizeof(char) * 1 * 120);
        memset(epc_data2,'\0',sizeof(char) * 1 * 120);
    }
    else
    {
        epc_data2 = (char*)malloc(sizeof(char) * epcCnt * 120);
        memset(epc_data2,'\0',sizeof(char) * epcCnt * 120);
    }
    char  one_epc_data2[120] = {'\0'};
    uint16_t epc_read_rate2 = 20;
    for(int i = 0; i < 120; i++)
    {
        if(EPC_ptr2[i] == NULL)
            break;
        memset(one_epc_data2,0,sizeof(one_epc_data2));

        EPC_ptr2[i]->last_temp =(EPC_ptr2[i]->tempe)/100.0;//EPC_ptr

        sprintf(one_epc_data2,"{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",              \
        EPC_ptr2[i]->epcId[0],EPC_ptr2[i]->epcId[1],EPC_ptr2[i]->tempe/100.0,EPC_ptr2[i]->antID,EPC_ptr2[i]->rssi); 
        strcat(epc_data2,one_epc_data2);
        strcat(epc_data2,",");
    }
    epc_data2[strlen(epc_data2) - 1] = '\0';
    size_t size2 = 0;
    char *json_str2 = NULL;
    size2 = asprintf(&json_str2,"{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",\
            epcCnt,epc_read_rate2,epc_data2);
    free(epc_data2);

    if (epcCnt != 0) //有数据才上报
    {
        mqtt_client_publish(send_topic, json_str2, size2, 0, 1);
    }
   
    free(json_str2);
    
}

/*****************************************************
函数名称：void RFID_MqttTimingTask(void *arg)
函数功能：
返回值：  无
注：
*********************************************************/
void RFID_MqttTimeTask(void *arg)
{
    while(1)
    {
        // printf("hello1");
        // static int flag=0;
        // //读EPC标签温度指令

        // //将RFID模块读取到的天线号 温度值 信号强度 转为json
        // if(Ok==rfidready_flag)
        // {
  
        // if (xSemaphoreTake(xBinarySemaphore, pdMS_TO_TICKS(5000)) != pdTRUE)//5s 超时后会返回pdFalse
        // {
        //     ESP_LOGI(TAG, "Semaphore timeout. No EPC data.");
        //     continue; // 超时则继续下一次循环
        // }

        // xEventGroupWaitBits(xEventGroup, TASK_1_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        
        mqtt_publish_epc_data2();
        ESP_LOGI(TAG, "Timing task sending..,and time is %d",pushtime_count);

        // // 完成发送后，清除事件组标志位，允许任务二执行
        // xEventGroupClearBits(xEventGroup, TASK_1_EVENT_BIT);
        // // 任务一完成后设置任务二的标志位，允许任务二执行
        // xEventGroupSetBits(xEventGroup, TASK_2_EVENT_BIT);

        vTaskDelay(pdMS_TO_TICKS(pushtime_count));
        // }
       
        // MQTT把json数据发送服务器
    }
 
}



void RFID_MqttErrTask(void *arg)
{
    while(1)
    {
        // printf("hello1");
        // static int flag=0;
        // //读EPC标签温度指令

        // //将RFID模块读取到的天线号 温度值 信号强度 转为json
        // if(Ok==rfidready_flag)
        // {
        // 等待事件组标志，任务二可以发送数据
        // EventBits_t uxBits = xEventGroupWaitBits(xEventGroup, TASK_2_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(mqtt_xBinarySemaphore, pdMS_TO_TICKS(5000)) != pdTRUE)//5s 超时后会返回pdFalse
        {
            ESP_LOGI(TAG, "Semaphore timeout. No EPC data.");
            continue; // 超时则继续下一次循环
        }

        mqtt_publish_epc_data();

        // // 完成发送后，清除事件组标志位，允许任务一执行
        // xEventGroupClearBits(xEventGroup, TASK_2_EVENT_BIT);
        // // 任务二完成后设置任务一的标志位，允许任务一执行
        // xEventGroupSetBits(xEventGroup, TASK_1_EVENT_BIT);
    

        vTaskDelay(pdMS_TO_TICKS(500));
        // }
       
        // MQTT把json数据发送服务器
    }
 
}


void ctrl_rfid_mode(uint8_t mode){
    rfid_read_config_t rfid_read_config;
    
    if (mode==2)//连续模式
    {
        rfid_read_config.rfid_read_on_off = RFID_READ_ON;//读写器开
        rfid_read_config.rfid_read_mode = RFID_READ_MODE_CONTINUOUS;//连续模式
        rfid_read_config.ant_sel = 0x0F;//00001111 ANT4 ANT3 ANT2 ANT1
        rfid_read_config.read_interval_time =1000;//读取频率间隔
        rfidready_flag=RFID_ReadEPC(rfid_read_config);
    }else if (mode==1)//单次模式
    {
        rfid_read_config.rfid_read_on_off = RFID_READ_ON;//读写器开
        rfid_read_config.rfid_read_mode = RFID_READ_MODE_ONCE;//单次模式
        rfid_read_config.ant_sel = 0x0F;//00001111 ANT4 ANT3 ANT2 ANT1
        rfid_read_config.read_interval_time =1000;//读取频率间隔
        rfidready_flag=RFID_ReadEPC(rfid_read_config);
    }else{
        rfid_read_config.rfid_read_on_off = RFID_READ_OFF;//读写器关
        rfid_read_config.rfid_read_mode = RFID_READ_MODE_NULL;
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
  sprintf(oled_buffer, "ID: %012llx", chip_id);
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
    get_chip_IDinfo();//获取芯片ID信息

    wdt_hal_context_t rwdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();//看门狗计时器WDT 初始化
    wdt_hal_write_protect_disable(&rwdt_ctx);//禁用写保护
    wdt_hal_feed(&rwdt_ctx);//喂狗 4G初始化有点长
   
   /* Initialize led indicator */
    _led_indicator_init();//指示灯 io初始化

    esp_err_t ret = nvs_flash_init();//nvs初始化

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        //若由于分区版本更新或无可用页(存储空间不足)，尝试格式化并重新初始化
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
    // //nvs写入操作
    // nvs_handle_t my_handle;//nvs句柄
    // ret=nvs_open("storage",NVS_READWRITE,&my_handle);
    // if(ret!=ESP_OK){
    //     ESP_LOGE(TAG,"nvs_open error");
    // }else{
    //     //写入数据
    //     ret = nvs_set_str(my_handle,"mqtt_address",strcop);
    //     if(ret!=ESP_OK){
    //         ESP_LOGE(TAG,"nvs_set_str error");
    //     }else{
    //         ret=nvs_commit(my_handle);//提交数据
    //         ESP_LOGI(TAG,"nvs_set_str success");
    //     }
    //     nvs_close(my_handle);//关闭nvs句柄
   
    /*
    esp_err_t err = from_nvs_get_value("mqtt_addr", str, &str_size);
    if ( err == ESP_OK ) {
        // strncpy(, str, str_size);
        // ESP_LOGI(TAG,"ABCD_ADDRESS: %s",str);
        
        // 检查新地址是否与现有地址不同
        if (strcmp(strcop, str) != 0) {
            // 地址不同，更新 NVS
            ESP_LOGI(TAG, "Updating MQTT_ADDRESS from '%s' to '%s'", str, strcop);

            err =  from_nvs_set_value("mqtt_addr",strcop);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Successfully updated MQTT_ADDRESS to: %s", strcop);
            } else {
                ESP_LOGE(TAG, "Failed to update MQTT_ADDRESS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGI(TAG, "MQTT_ADDRESS is already: %s", str);
        }
    }

    */

    /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());//初始化网络接口

    /*所有网络相关的事件（如连接、断开连接等）都会通过事件循环进行分发。默认事件循环可以让你的应用程序监听并处理这些事件。*/
    ESP_ERROR_CHECK(esp_event_loop_create_default());//创建了一个默认的事件循环

    sys_info_config_init(&sys_info_config);
    get_nvs_wifi_config(s_modem_wifi_config);
    get_nvs_auth_info_config();
    get_nvs_sys_info_config();
    
    //from_nvs_set_value 目前没有加nvs写操作，只有读取nvs中的数据
    // char strcop2[]="mqtt://101.37.253.97:4635";
    // strcpy(sys_info_config.mqtt_address, strcop2);
    ESP_LOGI(TAG,"MQTT_ADDRESS: %s",sys_info_config.mqtt_address);

    // sys_info_config.mqtt_address="mqtt://123.60.157.221";
    // sys_info_config.tcp_address="123.60.157.221";
    // sys_info_config.tcp_port=1883;

    network_init();
    wdt_hal_feed(&rwdt_ctx); //4G初始化有点长，先喂一次狗
    mqtt_init();
    rfid_http_init(s_modem_wifi_config);

    uart1_Init();
    
    uart2_Init();

    moduleSetGpioInit();
    // iic_init();
    modBusRtu_Init();

    // xEventGroup = xEventGroupCreate();
    // if (xEventGroup == NULL) {
    //     ESP_LOGE("app_main", "Event group creation failed!");
    //     return;  // 事件组创建失败，直接返回
    // }
    // xEventGroupSetBits(xEventGroup, TASK_2_EVENT_BIT);

 
    xTaskCreate(rx_task, "u1_rx_task", 4096*2, NULL,configMAX_PRIORITIES-2, NULL); 
    // xTaskCreate(modbusRtuRx_task, "modbusRtuRx_task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL); 
    // xTaskCreate(modbusRtuDeal_Task, "modbusRtuDeal_Task", 4096*3, NULL,configMAX_PRIORITIES-3, NULL);

    xTaskCreate(RFID_ReadEpcTask, "RFID_ReadEpcTask", 4096*2, NULL,configMAX_PRIORITIES-4, NULL);
    
    // xTaskCreate(RFID_MqttTimeTask, "RFID_MqttTimeTask", 4096*2, NULL,configMAX_PRIORITIES-5, NULL);
    // xTaskCreate(RFID_MqttErrTask, "RFID_MqttErrTask", 4096*2, NULL,configMAX_PRIORITIES-5, NULL);
 


    // xTaskCreate(modbusRtuDeal_Task, "modbusRtuDeal_Task", 4096*2, NULL,configMAX_PRIORITIES-6, NULL);

    wdt_hal_feed(&rwdt_ctx);//喂一次狗
    rfidModuleInit();
    wdt_hal_feed(&rwdt_ctx);//喂一次狗
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
    // xSemaphoreGive(uart1_rx_xBinarySemaphore);//给予一次信息量
    // vTaskDelay(1000 / portTICK_PERIOD_MS); // 延迟 1 秒

    // ctrl_rfid_mode(2);


    while(1)
    {
        wdt_hal_feed(&rwdt_ctx);
       
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

