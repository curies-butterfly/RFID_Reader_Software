#include "app_tasks.h"
#include "rfidnetwork.h"
#include "rfidmodule.h"
#include "esp_log.h"
#include "data_deal.h"
#include "dev_info.h"

/*****************************************************
函数名称：void RFID_MqttTimingTask(void *arg)
函数功能：
返回值：  无
注：
*********************************************************/

void mqtt_publish_epc_data()
{
    EPC_Info_t **EPC_ptr = &LTU3_Lable;
    char *epc_data = NULL;
    if (epcCnt == 0) // 防止出现空指针
    {
        epc_data = (char *)malloc(sizeof(char) * 1 * 120);
        memset(epc_data, '\0', sizeof(char) * 1 * 120);
    }
    else
    {
        epc_data = (char *)malloc(sizeof(char) * epcCnt * 120);
        memset(epc_data, '\0', sizeof(char) * epcCnt * 120);
    }
    char one_epc_data[120] = {'\0'};
    uint16_t epc_read_rate = 20;
    for (int i = 0; i < 120; i++)
    {
        if (EPC_ptr[i] == NULL)
            break;
        memset(one_epc_data, 0, sizeof(one_epc_data));

        EPC_ptr[i]->last_temp = (EPC_ptr[i]->tempe) / 100.0; // EPC_ptr

        sprintf(one_epc_data, "{\"epc\":\"%02x%02x\",\"tem\":%.2f,\"ant\":%d,\"rssi\":%d}",
                EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], EPC_ptr[i]->tempe / 100.0, EPC_ptr[i]->antID, EPC_ptr[i]->rssi);

        ESP_LOGI("mqtt_publish_epc", "%02x%02x:%.2f\r\n", EPC_ptr[i]->epcId[0], EPC_ptr[i]->epcId[1], (LTU3_Lable[i]->tempe) / 100.0);

        strcat(epc_data, one_epc_data);
        strcat(epc_data, ",");
    }
    epc_data[strlen(epc_data) - 1] = '\0';
    size_t size = 0;
    char *json_str = NULL;
    size = asprintf(&json_str, "{\"status\":\"200\",\"epc_cnt\":\"%d\",\"read_rate\":\"%d\",\"data\":[%s]}",
                    epcCnt, epc_read_rate, epc_data);
    free(epc_data);

    if (epcCnt != 0) // 有数据才上报
    {
        mqtt_client_publish(send_topic, json_str, size, 0, 1);
    }
    free(json_str);
}

void RFID_MqttTimeTask(void *arg)
{
    while (1)
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

        publish_epc_data2();
        ESP_LOGI("Timing_Task", "Timing task sending..,and time is %d", pushtime_count);

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
    while (1)
    {
        // printf("hello1");
        // static int flag=0;
        // //读EPC标签温度指令

        // //将RFID模块读取到的天线号 温度值 信号强度 转为json
        // if(Ok==rfidready_flag)
        // {
        // 等待事件组标志，任务二可以发送数据
        // EventBits_t uxBits = xEventGroupWaitBits(xEventGroup, TASK_2_EVENT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(mqtt_xBinarySemaphore, pdMS_TO_TICKS(5000)) != pdTRUE) // 5s 超时后会返回pdFalse
        {
            ESP_LOGI("mqtt_task", "Semaphore timeout. No EPC data.");
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

