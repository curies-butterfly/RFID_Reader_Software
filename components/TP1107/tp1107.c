#include "tp1107.h"
#include "at.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/timers.h"


static const char *TAG = "TP1107";
#define U2_TXD_PIN 21//(GPIO_NUM_19)
#define U2_RXD_PIN 47//(GPIO_NUM_5)
static const int U2_RX_BUF_SIZE = 2048;


static const struct at_urc tp1107_urc_table[] = { { "joined", "\r\n", urc_func1 }, { "+NNMI", "\r\n", urc_func2 }, };
static const char GO_AT_MODE[] = "+++\r\n";
static const char DEF_DEFAUT[] = "AT+DEF\r\n";
/**
 * AT+VER?   查询模块版本信息
 * AT+EUI?   查询模块的ESN:模组唯一ID号
 * AT+JOIN?  查询入网状态
 * AT+FREQ?  查询当前使用频点信息FREQ
 * AT+REBOOT 重启模块
 * AT+JOIN   手动入网
 */
static const char *COMMAND_LISTS[] = { "AT+VER?", "AT+EUI?", "AT+JOIN?", "AT+FREQ?", "AT+REBOOT" , "AT+JOIN"};
static const char *COMMAND_RESPS[] = { "TP1107", "FF", "joined", "UL", "NONE" };

// 定义 UNB_URC_TABLE_SIZE 常量
#define UNB_URC_TABLE_SIZE sizeof(tp1107_urc_table) / sizeof(tp1107_urc_table[0])


#define  TP1107_DEVICE_NAME "tp1107"
#define  TP1107_CLIENT_NAME "uart2"

//28
#define  TP1107_RST_PIN 35

//29
#define  TP1107_WAKE_PIN 36

#define  TP1107_DEFAUT_ESN "FF01FFFF0000"

#define UNB_DOUBLE 0
#define UNB_DEBUG  0

// 定义消息队列参数
#define MQ_ITEM_SIZE    1       // 每个消息1字节
#define MQ_POOL_SIZE    256     // 消息池总字节数（与原msg_pool_1匹配）
#define MQ_LENGTH       (MQ_POOL_SIZE / MQ_ITEM_SIZE) // 计算队列容量

// 静态分配存储区
static uint8_t msg_pool_1[MQ_POOL_SIZE];
// 静态队列控制结构体（FreeRTOS要求）
static StaticQueue_t mq1_static_struct;
// 全局队列句柄
QueueHandle_t mq_1 = NULL;

const static uint8_t UNB1_HEART[6]="402331";
// const static uint8_t UNB2_HEART[6]="402332";

static uint8_t unbbuffer1[128];

/**
 * 定时器周期参数：period_ms，参数单位为毫秒
 */
#define HEART_TICK_MINUTES     5   // 心跳间隔 分钟数
#define TIMER_NAME            "UNB_Heartbeat"
static TimerHandle_t timer1 = NULL; // 定时器句柄
const uint32_t period_ms = (uint32_t)HEART_TICK_MINUTES * 60 * 1000;// 计算周期 分钟转毫秒
const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);

at_device_tp1107 tp1107 = {
    TP1107_DEVICE_NAME,
    TP1107_CLIENT_NAME, 
    0,
    TP1107_RST_PIN,
    TP1107_WAKE_PIN,
    TP1107_DEFAUT_ESN,
    NULL,
    NULL };
    
/**
 * @brief URC回调函数
 * 
*/
void urc_func1(struct at_client *client, const char *data, size_t  size)
{
    assert(data);
    ESP_LOGI(TAG,"URC data: %.*s", size, data);
    ESP_LOGI(TAG,"tp1107 joined gateway");
    tp1107.net_joined = 1;
}

/**
 * @brief URC回调函数
 */
void urc_func2(struct at_client *client, const char *data, size_t  size)
{
    assert(data);

    uint8_t hex_len = 0;
    uint8_t str[256] = { 0 };

    ESP_LOGI(TAG,"URC data: %.*s", size, data);
    sscanf(data, "+NNMI:%d,%s", (int *)(&hex_len), str);
#if UNB_DEBUG

    ESP_LOGI(TAG,"tp1107 recived form gateway->%s",msg);
#else
#if UNB_DOUBLE
    // TODO 这里暂时屏蔽单TP1107来自URC的数据，后续的配置处理还需进一步考虑
    int result;
    uint8_t len = hex_len * 2;
    result = rt_mq_send(&mq_1, &len, 1);
    if (result != RT_EOK)
    {
        rt_kprintf("unb1 rt_mq_send ERR\n");
    }
    else {
        LOG_I("unb1 len=%d",len);
    }
//    rt_kprintf("unb1: send message_len - %d\n", len);
//    rt_kprintf("str:%s\r\n", str);
//    str[len]='\0';
    result = rt_mq_send(&mq_1, &str, len);
    if (result != RT_EOK)
    {
        rt_kprintf("unb1 rt_mq_send ERR\n");
    }
    else{
        LOG_I("unb1 data mq send");
    }
//    rt_kprintf("unb1: send message - %s\n", str);
#endif
#endif
}


void TP1107_Init(){
    at_client_t client = at_client_init(
        UART_NUM_2,      // LoRa模块连接的UART端口，原485串口2
        256,             // 每行接收缓冲区大小
        512              // 接收队列大小
    );
    
    if (!client) {
        ESP_LOGE(TAG, "AT客户端初始化失败");
        return;
    }
    //创建响应对象
    at_response_t resp = at_create_resp(
        1024,            // 总响应缓冲区大小（根据预期最大响应数据配置）
        0,               // 0表示等待"OK"或"ERROR"结尾（具体看模块协议）
        2000 / portTICK_PERIOD_MS // 超时时间（单位转换为系统时钟周期）
    );
    
    if (!resp) {
        ESP_LOGE(TAG, "响应对象创建失败");
        return;
    }
}

// 新增GPIO初始化配置
void basic_io_init(at_device_tp1107 *unb_device) {
    // 配置唤醒引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << unb_device->wake_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(unb_device->wake_pin, 1);

    // 配置复位引脚
    io_conf.pin_bit_mask = (1ULL << unb_device->rst_pin);
    gpio_config(&io_conf);
    gpio_set_level(unb_device->rst_pin, 1);
}

void basic_hard_wake_up_operate(at_device_tp1107 *unb_device, uint8_t state) {
    gpio_set_level(unb_device->wake_pin, state ? 1 : 0);
}

void basic_hard_rst_operate(at_device_tp1107 *unb_device) {
    gpio_set_level(unb_device->rst_pin, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(unb_device->rst_pin, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
}



/* 定时器回调函数实现 */
void timeout1(TimerHandle_t xTimer)
{
    ESP_LOGI("HEART", "Send to [%s] heart packet", tp1107.ESN);
    
    // 第一阶段：发送数据长度
    uint8_t len = 6;
    BaseType_t result = xQueueSend(mq_1, &len, 0); // 立即返回
    if (result != pdPASS) {
        ESP_LOGE("HEART", "unb1 send failed!");
        return;
    }

    // 第二阶段：发送实际数据
    result = xQueueSend(mq_1, UNB1_HEART, 0);
    if (result != pdPASS) {
        ESP_LOGE("HEART", "unb1 Data send failed!");
    }
}


static int unb_data_send_direct(at_device_tp1107 *ins, uint8_t *data, uint8_t len)
{
    char str[256] = { 0 };
    if (len >= 128)
        return 1;
    ESP_LOGI(TAG, "AT+UNBSEND=%d,%s,1", len, data);
    if (at_obj_exec_cmd(at_client_get(UART_NUM_2), ins->at_resp, str) != RT_EOK)
    {
        ESP_LOGI(TAG," [%s] AT client send commands failed, response error or timeout !", ins->device_name);
        return -1;
    }
    else
    {

        if (NULL != at_resp_get_line_by_kw(ins->at_resp, "SENT"))
        {
            ESP_LOGD(TAG," [%s] send successfull->%s", ins->device_name, str);
            return 0;
        }
    }
    return 1;
}


/**
 * @brief UNB1线程 从消息队列接收数据，然后通过unb_data_send_direct发送到网关，同时对发送失败做了容错处理
 */
static void unb1_entry(void *parameter){
    
    uint8_t len;        //接收数据的长度
    uint8_t res;        //发送结果
    uint8_t err_tick=0; //错误计数器
//    char msg[128] = { 0 };
//    for (uint16_t i = 0; i < hex_len; i++)
//    {
//        msg[i] = StrConv2Hex(str + i * 2);
//    }
    at_device_tp1107 *device;
#if UNB_DOUBLE
    device=&tp1107_2;
#else
    device=&tp1107;
#endif
    while (1)
    {
        // 接收消息长度
        if (xQueueReceive(mq_1, &len, portMAX_DELAY) == pdTRUE) {//从消息队列mq_1中接收数据长度len,阻塞等待portMAX_DELAY
            if (len > 0 ) {
                // 接收实际数据
                if (xQueueReceive(mq_1, unbbuffer1, portMAX_DELAY) != pdTRUE) {//再从同一个队列mq_1中读取实际数据内容，存储到unbbuffer1
                    ESP_LOGE(TAG, "Data receive failed!");
                    continue;
                }
                
                unbbuffer1[len] = '\0'; // 添加字符串终止符
                ESP_LOGD(TAG, "unb1 Received size:[%d] data: %.*s", len, len, unbbuffer1);
                vTaskDelay(pdMS_TO_TICKS(10));

                // 发送数据
                res = unb_data_send_direct(device, unbbuffer1, len / 2);  //发送数据，返回结果 ASCII 字符串形式的十六进制数据 字节=长度除以2
                while (res != ESP_OK) {
                    res = unb_data_send_direct(device, unbbuffer1, len / 2);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    err_tick++;
                    ESP_LOGE(TAG, "[%s] send err tick:%d", device->device_name, err_tick);

                    // 错误处理
                    if (err_tick >= 10) {
                        // 发送重启指令
                        if (at_obj_exec_cmd(at_client_get(UART_NUM_2),device->at_resp, COMMAND_LISTS[4]) != ESP_OK) {
                            ESP_LOGI(TAG, "Reboot command failed");
                        } else {
                            ESP_LOGI(TAG, "Reboot unb: %s", device->device_name);
                        }
                        err_tick = 0; // 重置计数器
                    } else if (err_tick >= 5) {
                        // 发送重新加入指令
                        if (at_obj_exec_cmd(at_client_get(UART_NUM_2), device->at_resp, COMMAND_LISTS[5]) != ESP_OK) {
                            ESP_LOGI(TAG, "Re-JOIN command failed");
                        } else {
                            ESP_LOGI(TAG, "Re-JOIN unb: %s", device->device_name);
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                err_tick = 0; // 成功发送后重置
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    free(unbbuffer1); // 理论上不会执行到这里
    vTaskDelete(NULL);

}


int unb_tp1107_rea_init(at_device_tp1107 *unb_device, const struct at_urc urc_temp[])
{
    char *str_ptr;
    uint8_t index = 0;
    uint8_t join_tick = 0;
    uint8_t once = 1;

RESTART:
    basic_io_init(unb_device);
    basic_hard_wake_up_operate(unb_device, 1);
    basic_hard_rst_operate(unb_device);

    ESP_LOGI(TAG, "[%s] init start", unb_device->device_name);

    if (1 == once)
    {
        once = 0;
        // 配置UART参数
        const uart_config_t uart_config = {
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        // uart_event_queue = xQueueCreate(40, sizeof(uart_event_t));  // 创建队列，最多可以存放 10 个事件
        // 初始化UART驱动
        esp_err_t ret = uart_driver_install(
            UART_NUM_2, U2_RX_BUF_SIZE, 0, 40, &uart_event_queue, 0
        );
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        }

        uart_param_config(UART_NUM_2, &uart_config);
        uart_set_pin(UART_NUM_2, U2_TXD_PIN, U2_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        at_client_init(UART_NUM_2, 255,255);
        at_obj_set_urc_table(at_client_get(UART_NUM_2), urc_temp, UNB_URC_TABLE_SIZE);
        ESP_LOGI(TAG, "[%s] one init start", unb_device->device_name);
        unb_device->at_resp = at_create_resp(128, 2, pdMS_TO_TICKS(4000));
    }
    
    ESP_LOGI(TAG, "device_name:[%s]", unb_device->device_name);
    // ESP_LOGI(TAG, "at_resp->buf:[%d]", unb_device->at_resp->buf_size);
    // ESP_LOGI(TAG, "at_resp->line_num:[%d]", unb_device->at_resp->line_num);
    // ESP_LOGI(TAG,"COMMAND_LISTS[0]:[%s]",COMMAND_LISTS[0]);

    //memset(unb_device->at_resp->buf, 0, sizeof(unb_device->at_resp->buf));


    if (at_obj_exec_cmd(at_client_get(UART_NUM_2), unb_device->at_resp, COMMAND_LISTS[0]) != RT_EOK)//查询固件版本  VERAT+VER?\r\n
    {
        ESP_LOGI(TAG,"unb_tp1107_rea_init 1 > AT client send commands failed, response error or timeout !");
        uart_write_bytes(UART_NUM_2,  GO_AT_MODE, (sizeof(GO_AT_MODE) - 1));
    }
    else
    {
        /*
            发送:AT+VER?\r\n
            返回:V1.3.5_T211112_56520942_TP1107\r\nOK\r\n
        */
        ESP_LOGD(TAG,"send successfull->%s", COMMAND_LISTS[0]);
        // ESP_LOGI(TAG,"resp=%s", at_resp_get_line_by_kw(unb_device->at_resp, COMMAND_RESPS[0]));
        char *resp_line = at_resp_get_line_by_kw(unb_device->at_resp, COMMAND_RESPS[0]);
        if (resp_line != NULL) {
            ESP_LOGI(TAG, "resp=%s", resp_line);
        } else {
            ESP_LOGE(TAG, "Keyword '%s' not found in response", COMMAND_RESPS[0]); // 明确提示关键字未找到
        }
    }

    if (at_obj_exec_cmd(at_client_get(UART_NUM_2), unb_device->at_resp, COMMAND_LISTS[1]) != RT_EOK)//查询模组ESN  AT+EUI?\r\n
    {
        ESP_LOGI(TAG,"unb_tp1107_rea_init 2 > AT client send commands failed, response error or timeout !");
        uart_write_bytes(UART_NUM_2,  GO_AT_MODE, (sizeof(GO_AT_MODE) - 1));
    }
    else
    {
        /**
         *  发送:AT+EUI?\r\n
         *  返回:FF0100001F8F\r\nOK\r\n
         */
        ESP_LOGD(TAG,"send successfull->%s", COMMAND_LISTS[1]);
        str_ptr = at_resp_get_line_by_kw(unb_device->at_resp, (const char *) COMMAND_RESPS[1]);
        ESP_LOGI(TAG,"resp=%s", str_ptr);
        if (NULL != str_ptr)
        {
            for (index = 0; index < 12; index++)
            {
                unb_device->ESN[index] = *str_ptr;
                str_ptr++;
            }
            unb_device->ESN[12] = '\0';
            ESP_LOGI(TAG,"getted %s  ESN :%s", unb_device->device_name, unb_device->ESN);
        }
    }

    join_tick=0;
    // 发送进入AT模式的指令（"+++\r\n"）
    while (!unb_device->net_joined) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 毫秒转时钟周期
        if (join_tick > 20) {
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            uart_write_bytes(UART_NUM_2, (const uint8_t*)GO_AT_MODE, sizeof(GO_AT_MODE)-1);

            vTaskDelay(1000 / portTICK_PERIOD_MS); // 毫秒转时钟周期
            uart_write_bytes(UART_NUM_2, (const uint8_t*)DEF_DEFAUT, sizeof(DEF_DEFAUT)-1);
            goto RESTART;
        }
        join_tick++;
    }

    ESP_LOGI(TAG, "[%s] init success", unb_device->device_name);
    return 0;
}

int unb_tp1107_init(void)
{
 
    // 创建静态队列
    mq_1 = xQueueCreateStatic(
        MQ_LENGTH,          // 队列容量（消息数量）
        MQ_ITEM_SIZE,       // 每个消息大小（字节）
        msg_pool_1,         // 存储缓冲区
        &mq1_static_struct  // 队列控制结构体
    );

    // 错误处理
    if (mq_1 == NULL) {
        ESP_LOGE("MQ", "Failed to create unb_mq1");
        return RT_ERROR; // 或返回错误码
    }
    ESP_LOGI("MQ", "Message queue created (length:%d, item_size:%d)", MQ_LENGTH, MQ_ITEM_SIZE);

    unb_tp1107_rea_init(&tp1107, tp1107_urc_table);

    
    // 创建定时器（周期性）
    timer1 = xTimerCreate(
        TIMER_NAME,     // 定时器名称
        period_ticks,   // 周期（以ticks为单位）
        pdTRUE,         // 自动重载（周期性）
        (void *)0,      // 定时器ID（可用来传递参数）
        timeout1        // 回调函数
    );

    // 错误处理
    if (timer1 == NULL) {
        ESP_LOGE("TIMER", "Failed to create heartbeat timer");
        return RT_ERROR;
    }

    // 启动定时器（延迟0 ticks启动）
    if (xTimerStart(timer1, 0) != pdPASS) {
        ESP_LOGE("TIMER", "Failed to start heartbeat timer");
    }

#if UNB_DOUBLE

    unb_tp1107_rea_init(&tp1107_2, tp1107_2_urc_table);

#endif

    const uint8_t UNB1_TASK_PRIORITY =15 ;   // 优先级（ESP-IDF范围0-25）
    TaskHandle_t task_handle = NULL;

    // 创建任务（自动启动）
    BaseType_t ret = xTaskCreate(
        unb1_entry,             // 任务函数
        "UNB1_handle",          // 任务名称
        4096,          
        NULL,                   // 参数
        UNB1_TASK_PRIORITY,     // 优先级
        &task_handle            // 任务句柄
    );

    // 错误处理
    if (ret != pdPASS) {
        ESP_LOGE("TASK", "Failed to create UNB1_handle task!");
        // 这里可以添加错误处理代码
    }
    return 0;
}


/**
 * 注：发送使用lora的AT+UNBSEND=<len>,<data>,<confirm>\r\n 
 * 
 * //判断是否加入网关成功
    
    //成功 发送数据

    //不成功连接网关

    //等待网关接收回应

    //无应答 //失败次数+1 //再次发送

    //若失败
    
    //无效参数:ERROR,2\r\n
    //未入网： ERROR,3\r\n
    //设备忙:  ERROR,4\r\n

    //次数>=5 重新加入网关

    //应答》完成
 */
// uint8_t send_data_to_gateway(){

// }

    

