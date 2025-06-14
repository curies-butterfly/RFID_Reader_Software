/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/queue.h>
#include <inttypes.h>
#include "esp_log.h"
#include "iot_sensor_hub.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

static const char *TAG = "SENSOR_HUB";
const char *SENSOR_TYPE_STRING[] = {"NULL", "HUMITURE", "IMU", "LIGHTSENSOR"};
const char *SENSOR_MODE_STRING[] = {"MODE_DEFAULT", "MODE_POLLING", "MODE_INTERRUPT"};

#define SENSOR_CHECK(a, str, ret) if(!(a)) { \
        ESP_LOGE(TAG,"%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        return (ret); \
    }

#define SENSOR_CHECK_GOTO(a, str, label) if(!(a)) { \
        ESP_LOGE(TAG,"%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        goto label; \
    }

#define ESP_INTR_FLAG_DEFAULT 0
#define ISR_STATE_INVALID NULL
#define ISR_STATE_INITIALIZED 0x01
#define ISR_STATE_INACTIVE ISR_STATE_INITIALIZED
#define ISR_STATE_ACTIVE (ISR_STATE_INITIALIZED | (0x01 << 1))

/*event group related*/
#define SENSORS_NUM_MAX 20 /*max num is 23*/
#define BIT23_KILL_WAITING_TASK (0x01 << 23)
#define BIT22_COMMON_DATA_READY (0x01 << 22)

/*default sensor task related*/
static EventGroupHandle_t s_event_group = NULL;
static uint32_t s_sensor_wait_bits = BIT23_KILL_WAITING_TASK;
static TaskHandle_t s_sensor_task_handle = NULL;
static SemaphoreHandle_t s_sensor_node_mutex = NULL;    /* mutex to achieve thread-safe*/
#define SENSOR_NODE_MUTEX_TICKS_TO_WAIT 200

#ifdef CONFIG_SENSOR_TASK_PRIORITY
#define SENSOR_DEFAULT_TASK_PRIORITY CONFIG_SENSOR_TASK_PRIORITY /*will be overwrite if PRIORITY_INHERIT enable*/
#else
#define SENSOR_DEFAULT_TASK_PRIORITY (uxTaskPriorityGet(NULL))
#endif
#define SENSOR_DEFAULT_TASK_NAME "SENSOR_HUB"
#define SENSOR_DEFAULT_TASK_STACK_SIZE CONFIG_SENSOR_TASK_STACK_SIZE
#define SENSOR_DEFAULT_TASK_CORE_ID 0
#define SENSOR_DEFAULT_TASK_DELETE_TIMEOUT_MS 1000

/*event loop related*/
ESP_EVENT_DEFINE_BASE(SENSOR_IMU_EVENTS);
ESP_EVENT_DEFINE_BASE(SENSOR_HUMITURE_EVENTS);
ESP_EVENT_DEFINE_BASE(SENSOR_LIGHTSENSOR_EVENTS);
#define SENSOR_EVENT_NAME_LENGTH 50
#ifdef CONFIG_SENSOR_DEFAULT_HANDLER
static sensor_event_handler_instance_t s_sensor_default_handler_instance = NULL;
static void sensor_default_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);
#endif

/*private sensor struct type*/
typedef struct {
    bus_handle_t bus;
    sensor_type_t type;
    sensor_mode_t mode;
    uint32_t min_delay;
    const char *name;
    sensor_driver_handle_t driver_handle;
    iot_sensor_impl_t *impl;
    char event_base[SENSOR_EVENT_NAME_LENGTH];
    uint32_t event_bit;
    uint8_t addr;
    TaskHandle_t task_handle;
    int intr_pin;                   /*!< set interrupt pin */
    union {
        TimerHandle_t timer_handle; /*polling mode*/
        int32_t isr_state;          /*interrupt mode*/
    };
} _iot_sensor_t;

typedef struct _iot_sensor_slist_t {
    _iot_sensor_t *p_sensor;
    SLIST_ENTRY(_iot_sensor_slist_t) next;
} _iot_sensor_slist_t;

static SLIST_HEAD(sensor_slist_head_t, _iot_sensor_slist_t) s_sensor_slist_head = SLIST_HEAD_INITIALIZER(s_sensor_slist_head);

static iot_sensor_impl_t s_sensor_impls[] = {
#ifdef CONFIG_SENSOR_INCLUDED_HUMITURE
    {
        .type = HUMITURE_ID,
        .create = humiture_create,
        .delete = humiture_delete,
        .acquire = humiture_acquire,
        .control = humiture_control,
    },
#endif
#ifdef CONFIG_SENSOR_INCLUDED_IMU
    {
        .type = IMU_ID,
        .create = imu_create,
        .delete = imu_delete,
        .acquire = imu_acquire,
        .control = imu_control,
    },
#endif
#ifdef CONFIG_SENSOR_INCLUDED_LIGHT
    {
        .type = LIGHT_SENSOR_ID,
        .create = light_sensor_create,
        .delete = light_sensor_delete,
        .acquire = light_sensor_acquire,
        .control = light_sensor_control,
    },
#endif
};

/******************************************Private Functions*********************************************/
static iot_sensor_impl_t *sensor_find_impl(int sensor_type)
{
    int count = sizeof(s_sensor_impls) / sizeof(iot_sensor_impl_t);

    for (int i = 0; i < count; i++) {
        if (s_sensor_impls[i].type == sensor_type) {
            return &s_sensor_impls[i];
        }
    }

    return NULL;
}

static inline esp_err_t sensor_take_event_bit(uint32_t *p_bit)
{
    SENSOR_CHECK(p_bit != NULL, "pointer can not be NULL", ESP_ERR_INVALID_ARG);
    SENSOR_CHECK(s_event_group != NULL, "s_event_group can not be NULL", ESP_ERR_INVALID_STATE);
    /*take a free event bit*/
    uint32_t i = 0;

    for (; i < SENSORS_NUM_MAX; i++) {
        if (((s_sensor_wait_bits >> i) & 0x01) == false) {
            s_sensor_wait_bits |= (0x01 << i);
            break;
        }
    }

    if (i >= SENSORS_NUM_MAX) {
        return ESP_FAIL;
    }

    *p_bit = i;
    return ESP_OK;
}

static inline esp_err_t sensor_give_event_bit(uint32_t bit)
{
    SENSOR_CHECK(s_event_group != NULL, "s_event_group can not be NULL", ESP_ERR_INVALID_STATE);

    /*give back a used event bit*/
    if (bit >= SENSORS_NUM_MAX) {
        return ESP_FAIL;
    }

    s_sensor_wait_bits &= ~(0x00000001 << bit);
    return ESP_OK;
}

static esp_err_t sensor_add_node(_iot_sensor_t *p_sensor)
{
    SENSOR_CHECK(p_sensor != NULL, "sensor pointer can not be NULL", ESP_ERR_INVALID_ARG);
    SENSOR_CHECK(s_event_group != NULL, "s_event_group can not be NULL", ESP_ERR_INVALID_STATE);
    _iot_sensor_slist_t *node = calloc(1, sizeof(_iot_sensor_slist_t));
    SENSOR_CHECK(node != NULL, "calloc node failed", ESP_ERR_NO_MEM);

    uint32_t event_bit = 0;
    SENSOR_CHECK_GOTO(pdTRUE == xSemaphoreTake(s_sensor_node_mutex, SENSOR_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", cleanupnode);
    SENSOR_CHECK_GOTO(ESP_OK == sensor_take_event_bit(&event_bit), "take event bit failed", cleanupnode);
    p_sensor->event_bit = event_bit;
    node->p_sensor = p_sensor;
    SLIST_INSERT_HEAD(&s_sensor_slist_head, node, next);
    SENSOR_CHECK_GOTO(pdTRUE == xSemaphoreGive(s_sensor_node_mutex), "give semaphore failed", cleanupnode);

    return ESP_OK;

cleanupnode:
    free(node);
    return ESP_FAIL;
}

static esp_err_t sensor_remove_node(_iot_sensor_t *p_sensor)
{
    SENSOR_CHECK(p_sensor != NULL, "sensor pointer can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_slist_t *node;
    SENSOR_CHECK(pdTRUE == xSemaphoreTake(s_sensor_node_mutex, SENSOR_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", ESP_ERR_TIMEOUT);
    SLIST_FOREACH(node, &s_sensor_slist_head, next) {
        if (node->p_sensor == p_sensor) {
            SENSOR_CHECK(ESP_OK == sensor_give_event_bit(p_sensor->event_bit), "give event bit failed", ESP_FAIL);
            SLIST_REMOVE(&s_sensor_slist_head, node, _iot_sensor_slist_t, next);
            free(node);
            break;
        }
    }
    SENSOR_CHECK(pdTRUE == xSemaphoreGive(s_sensor_node_mutex), "give semaphore failed", ESP_FAIL);
    return ESP_OK;
}

static int64_t sensor_get_timestamp_us()
{
    int64_t time = esp_timer_get_time();
    return time;
}

static void sensor_default_task(void *arg)
{
    struct sensor_slist_head_t *p_sensor_list_head = (struct sensor_slist_head_t *)(arg);
    EventBits_t uxBits = 0;
    sensor_data_group_t sensor_data_group = {0};
    _iot_sensor_slist_t *node = NULL;
    int64_t acquire_time = 0;
    ESP_LOGI(TAG, "task: sensor_default_task created!");

    while (arg) { /*arg == NULL is invalid, task will be deleted*/
        uxBits = xEventGroupWaitBits(s_event_group, s_sensor_wait_bits, true, false, portMAX_DELAY);

        if ((uxBits & BIT23_KILL_WAITING_TASK) != 0) { /*task delete event*/
            break;
        }

        xSemaphoreTake(s_sensor_node_mutex, portMAX_DELAY);
        SLIST_FOREACH(node, p_sensor_list_head, next) {
            if ((uxBits >> (node->p_sensor->event_bit)) & 0x01) {
                node->p_sensor->impl->acquire(node->p_sensor->driver_handle, &sensor_data_group);
                acquire_time = sensor_get_timestamp_us();
                for (uint8_t i = 0; i < sensor_data_group.number; i++) {
                    /*.event_id and .data assignment during acquire stage*/
                    sensor_data_group.sensor_data[i].timestamp = acquire_time;
                    sensor_data_group.sensor_data[i].sensor_type = node->p_sensor->type;
                    sensor_data_group.sensor_data[i].sensor_name = node->p_sensor->name;
                    sensor_data_group.sensor_data[i].min_delay = node->p_sensor->min_delay;
                    sensor_data_group.sensor_data[i].sensor_addr = node->p_sensor->addr;
                    sensors_event_post(node->p_sensor->event_base, sensor_data_group.sensor_data[i].event_id, &(sensor_data_group.sensor_data[i]), sizeof(sensor_data_t), 0);
                }
            }
        }
        xSemaphoreGive(s_sensor_node_mutex);
    }

    /*set task handle to NULL*/
    SLIST_FOREACH(node, p_sensor_list_head, next) {
        node->p_sensor->task_handle = NULL;
    }
    s_sensor_task_handle = NULL;
    ESP_LOGI(TAG, "task: delete sensor_default_task !");
    vTaskDelete(NULL);
}

static void sensors_timer_cb(TimerHandle_t xTimer)
{
    uint32_t event_bit = (uint32_t) pvTimerGetTimerID(xTimer);
    xEventGroupSetBits(s_event_group, (0x01 << event_bit));
}

static void IRAM_ATTR sensors_intr_isr_handler(void *arg)
{
    BaseType_t task_woken = pdFALSE;
    uint32_t event_bit = (uint32_t)(arg);
    xEventGroupSetBitsFromISR(s_event_group, (0x01 << event_bit), &task_woken);

    //Switch context if necessary
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static TimerHandle_t sensor_polling_mode_init(const char *const pcTimerName, uint32_t min_delay, void *arg)
{
    SENSOR_CHECK((pcTimerName != NULL) && (min_delay != 0), "sensor polling mode init failed", NULL);
    TimerHandle_t timer_handle = xTimerCreate(pcTimerName, (min_delay / portTICK_PERIOD_MS), pdTRUE, arg, sensors_timer_cb);

    if (timer_handle == NULL) {
        ESP_LOGE(TAG, "timer create failed!");
        return NULL;
    }

    return timer_handle;
}

static esp_err_t sensor_intr_mode_init(int pin, gpio_int_type_t intr_type)
{
    SENSOR_CHECK((pin != 0) && (intr_type != 0), "sensor intr pin/type invalid", ESP_ERR_INVALID_ARG);
    gpio_config_t io_conf = {
        //interrupt of rising edge
        .intr_type = intr_type,
        //bit mask of the pins
        .pin_bit_mask = (1ULL << pin),
        //set as input mode
        .mode = GPIO_MODE_INPUT,
        //disable pull-down mode
        .pull_down_en = 0,
        //enable pull-up mode
        .pull_up_en = 0
    };
    gpio_config(&io_conf);
    //install gpio isr service
    gpio_set_intr_type(pin, intr_type);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    return ESP_OK;
}

static inline esp_err_t sensor_intr_isr_add(int pin, void *arg)
{
    SENSOR_CHECK((pin != 0) && (arg != NULL), "sensor intr args invalid", ESP_ERR_INVALID_ARG);
    return gpio_isr_handler_add(pin, sensors_intr_isr_handler, arg);
}

static inline esp_err_t sensor_intr_isr_remove(int pin)
{
    SENSOR_CHECK(pin != 0, "sensor intr pin invalid", ESP_ERR_INVALID_ARG);
    return gpio_isr_handler_remove(pin);
}

/******************************************Public Functions*********************************************/
esp_err_t iot_sensor_create(const char *sensor_name, const sensor_config_t *config, sensor_handle_t *p_sensor_handle)
{
    if (sensor_name == NULL || config == NULL) {
        ESP_LOGE(TAG, "Please validate sensor name or config");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;
    // copy first for safety operation
    sensor_config_t config_copy = *config;

    if (config_copy.type <= NULL_ID || config_copy.type >= SENSOR_TYPE_MAX) {
        ESP_LOGE(TAG, "Invalid sensor type");
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Set Sensor Abstraction Layer Information
    _iot_sensor_t *sensor = (_iot_sensor_t *)calloc(1, sizeof(_iot_sensor_t));
    SENSOR_CHECK(sensor != NULL, "calloc node failed", ESP_ERR_NO_MEM);
    sensor->type = config_copy.type;
    sensor->bus = config_copy.bus;
    sensor->name = sensor_name;
    sensor->addr = config_copy.addr;
    sensor->min_delay = config_copy.min_delay;
    sensor->intr_pin = config_copy.intr_pin;
    sensor->mode = config_copy.mode;
    sensor->impl = sensor_find_impl(sensor->type);
    sprintf(sensor->event_base, "%s_%x", sensor_name, config->addr);
    SENSOR_CHECK_GOTO(sensor->impl != NULL && sensor->event_base != NULL, "no driver found !!", cleanup_sensor);

    // create a new sensor
    sensor->driver_handle = sensor->impl->create(sensor->bus, sensor->name, sensor->addr);
    SENSOR_CHECK_GOTO(sensor->driver_handle != NULL, "sensor create failed", cleanup_sensor);
    // config sensor work mode, not supported case will be skipped
    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_MODE, (void *)(config_copy.mode));
    SENSOR_CHECK_GOTO(ESP_OK == ret || ESP_ERR_NOT_SUPPORTED == ret, "set sensor mode failed !!", cleanup_sensor);
    /*config sensor measuring range, not supported case will be skipped*/
    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_RANGE, (void *)(config_copy.range));
    SENSOR_CHECK_GOTO(ESP_OK == ret || ESP_ERR_NOT_SUPPORTED == ret, "set sensor range failed !!", cleanup_sensor);
    /*config sensor work mode, not supported case will be skipped*/
    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_ODR, (void *)(config_copy.min_delay));
    SENSOR_CHECK_GOTO(ESP_OK == ret || ESP_ERR_NOT_SUPPORTED == ret, "set sensor odr failed !!", cleanup_sensor);
    /*test if sensor is valid, can not be skipped*/
    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SELF_TEST, NULL);
    SENSOR_CHECK_GOTO(ret == ESP_OK, "sensor test failed !!", cleanup_sensor);

    /*create a default event group if have not created*/
    if (s_event_group == NULL) {
        s_event_group = xEventGroupCreate();
        s_sensor_node_mutex = xSemaphoreCreateMutex();
        SENSOR_CHECK(s_sensor_node_mutex != NULL, "sensor_node xSemaphoreCreateMutex failed", ESP_FAIL);
    }

    // add sensor to list, event_bit will be set internal
    ret = sensor_add_node(sensor);
    SENSOR_CHECK_GOTO(ret == ESP_OK, "add sensor node to list failed !!", cleanup_sensor);

    switch (sensor->mode) {
    case MODE_POLLING:
        sensor->timer_handle = sensor_polling_mode_init(sensor->name, sensor->min_delay, (void *)(sensor->event_bit));
        SENSOR_CHECK_GOTO(sensor->timer_handle != NULL, "sensor timer create failed", cleanup_sensor_node);
        break;

    case MODE_INTERRUPT:
        ret = sensor_intr_mode_init(config_copy.intr_pin, config_copy.intr_type);
        SENSOR_CHECK_GOTO(ret == ESP_OK, "sensor intr init failed", cleanup_sensor_node);
        sensor->isr_state = ISR_STATE_INITIALIZED;
        break;

    default:
        break;
    }

    /*create a default sensor task if not created*/
    if (s_sensor_task_handle == NULL) {
        BaseType_t task_created = xTaskCreatePinnedToCore(sensor_default_task, SENSOR_DEFAULT_TASK_NAME, SENSOR_DEFAULT_TASK_STACK_SIZE,
                                                          ((void *)(&s_sensor_slist_head)), SENSOR_DEFAULT_TASK_PRIORITY, &s_sensor_task_handle, SENSOR_DEFAULT_TASK_CORE_ID);
        SENSOR_CHECK_GOTO(task_created == pdPASS, "create default sensor task failed", cleanup_sensor_node);
    }
    sensor->task_handle = s_sensor_task_handle;

    // regist default event handler for message print
#ifdef CONFIG_SENSOR_DEFAULT_HANDLER_DATA
    if (s_sensor_default_handler_instance == NULL) {
        // print all sensor event includes data
        sensors_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, sensor_default_event_handler, (void *)true, &s_sensor_default_handler_instance);
    }
#elif CONFIG_SENSOR_DEFAULT_HANDLER
    if (s_sensor_default_handler_instance == NULL) {
        // print sensor state message only
        sensors_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, sensor_default_event_handler, (void *)false, &s_sensor_default_handler_instance);
    }
#endif

    ESP_LOGI(TAG, "Sensor created, Task name = %s, Type = %s, Sensor Name = %s, Mode = %s, Min Delay = %" PRIu32 " ms",
             SENSOR_DEFAULT_TASK_NAME,
             SENSOR_TYPE_STRING[sensor->type],
             sensor->name,
             SENSOR_MODE_STRING[sensor->mode],
             sensor->min_delay);

    *p_sensor_handle = (sensor_handle_t)sensor;
    return ESP_OK;

cleanup_sensor:
    free(sensor);
    *p_sensor_handle = NULL;
    return ret;
cleanup_sensor_node:
    sensor_remove_node(sensor);
    free(sensor);
    *p_sensor_handle = NULL;
    return ret;
}

esp_err_t iot_sensor_stop(sensor_handle_t sensor_handle)
{
    SENSOR_CHECK(sensor_handle != NULL, "sensor handle can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_t *sensor = (_iot_sensor_t *)sensor_handle;
    SENSOR_CHECK(sensor->timer_handle != NULL, "sensor timmer handle can not be NULL", ESP_ERR_INVALID_ARG);

    switch (sensor->mode) {
    case MODE_POLLING:
        SENSOR_CHECK(pdTRUE == xTimerStop(sensor->timer_handle, portMAX_DELAY), "sensor stop failed", ESP_FAIL);
        break;

    case MODE_INTERRUPT:
        if (sensor->isr_state == ISR_STATE_ACTIVE) {
            SENSOR_CHECK(ESP_OK == sensor_intr_isr_remove(sensor->intr_pin), "sensor stop failed", ESP_FAIL);
            sensor->isr_state = ISR_STATE_INACTIVE;
        }
        break;
    default:
        SENSOR_CHECK(false, "sensor stop failed", ESP_ERR_NOT_SUPPORTED);
        break;
    }

    sensor_data_t sensor_data;
    memset(&sensor_data, 0, sizeof(sensor_data_t));
    sensor_data.sensor_type = sensor->type;
    sensor_data.sensor_name = sensor->name;
    sensor_data.sensor_addr = sensor->addr;
    sensor_data.timestamp = sensor_get_timestamp_us();
    sensor_data.event_id = SENSOR_STOPED;
    esp_err_t ret = sensors_event_post(sensor->event_base, sensor_data.event_id, &sensor_data, sizeof(sensor_data_t), portMAX_DELAY);

    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "sensor event post failed = %s, or eventloop not initialized", esp_err_to_name(ret));
    }

    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_POWER, (void *)POWER_MODE_SLEEP);

    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) { /*not supported case will be skip*/
        ESP_LOGW(TAG, "sensor set power failed ret = %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t iot_sensor_start(sensor_handle_t sensor_handle)
{
    SENSOR_CHECK(sensor_handle != NULL, "sensor handle can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_t *sensor = (_iot_sensor_t *)sensor_handle;
    SENSOR_CHECK(sensor->timer_handle != NULL, "sensor timmer_handle/isr_state can not be NULL", ESP_ERR_INVALID_ARG);

    switch (sensor->mode) {
    case MODE_POLLING:
        SENSOR_CHECK(pdTRUE == xTimerStart(sensor->timer_handle, portMAX_DELAY), "sensor start failed", ESP_FAIL);
        break;

    case MODE_INTERRUPT:
        if (sensor->isr_state == ISR_STATE_INITIALIZED) {
            SENSOR_CHECK(ESP_OK == sensor_intr_isr_add(sensor->intr_pin, ((void *)sensor->event_bit)), "sensor start failed", ESP_FAIL);
            sensor->isr_state = ISR_STATE_ACTIVE;
        }
        break;
    default:
        SENSOR_CHECK(false, "sensor start failed", ESP_ERR_NOT_SUPPORTED);
        break;
    }

    sensor_data_t sensor_data;
    memset(&sensor_data, 0, sizeof(sensor_data_t));
    sensor_data.sensor_type = sensor->type;
    sensor_data.sensor_name = sensor->name;
    sensor_data.sensor_addr = sensor->addr;
    sensor_data.timestamp = sensor_get_timestamp_us();
    sensor_data.event_id = SENSOR_STARTED;
    esp_err_t ret = sensors_event_post(sensor->event_base, sensor_data.event_id, &sensor_data, sizeof(sensor_data_t), portMAX_DELAY);

    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "sensor event post failed = %s, or eventloop not initialized", esp_err_to_name(ret));
    }

    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_POWER, (void *)POWER_MODE_WAKEUP);

    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) { /*not supported case will be skip*/
        ESP_LOGW(TAG, "sensor set power failed ret = %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t iot_sensor_delete(sensor_handle_t p_sensor_handle)
{
    SENSOR_CHECK(p_sensor_handle != NULL, "sensor handle can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_t *sensor = (_iot_sensor_t *)(p_sensor_handle);
    SENSOR_CHECK(sensor->timer_handle != NULL, "sensor timmer handle can not be NULL", ESP_ERR_INVALID_ARG);

    switch (sensor->mode) {
    case MODE_POLLING:
        SENSOR_CHECK(pdTRUE == xTimerDelete(sensor->timer_handle, portMAX_DELAY), "sensor delete failed", ESP_FAIL);
        sensor->timer_handle = NULL;
        break;

    case MODE_INTERRUPT:
        if (sensor->isr_state == ISR_STATE_ACTIVE) {
            SENSOR_CHECK(ESP_OK == sensor_intr_isr_remove(sensor->intr_pin), "sensor delete failed", ESP_FAIL);
            sensor->isr_state = ISR_STATE_INACTIVE;
        }
        break;

    default:
        SENSOR_CHECK(false, "sensor delete failed", ESP_ERR_NOT_SUPPORTED);
        break;
    }

    /*remove from sensor list*/
    esp_err_t ret = sensor_remove_node(sensor);
    SENSOR_CHECK(ret == ESP_OK, "sensor node remove failed", ret);

    /*set sensor to sleep mode then delete the driver*/
    ret = sensor->impl->control(sensor->driver_handle, COMMAND_SET_POWER, (void *)POWER_MODE_SLEEP);

    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) { /*not supported case will be skip*/
        ESP_LOGW(TAG, "sensor set power failed ret = %s", esp_err_to_name(ret));
    }

    ret = sensor->impl->delete (&sensor->driver_handle);
    SENSOR_CHECK(ret == ESP_OK && sensor->driver_handle == NULL, "sensor driver delete failed", ret);

    /*free the resource then set handle to NULL*/
    free(sensor);
    p_sensor_handle = NULL;

    /*if no event bits to wait, delete the default sensor task*/
    if (s_sensor_wait_bits == BIT23_KILL_WAITING_TASK) {
        xEventGroupSetBits(s_event_group, BIT23_KILL_WAITING_TASK); /*set bit to delete the task*/
        int timerout_counter = 0;/*wait for task deleted*/
        int  timerout_counter_step = 50;
        while (s_sensor_task_handle) {
            ESP_LOGW(TAG, "......waiting for sensor default task deleted.....");
            vTaskDelay(timerout_counter_step / portTICK_PERIOD_MS);
            timerout_counter += timerout_counter_step;
            if (timerout_counter >= SENSOR_DEFAULT_TASK_DELETE_TIMEOUT_MS) {
                return ESP_ERR_TIMEOUT;
            }
        }
        /*delete default event group*/
        if (s_event_group != NULL) {
            vEventGroupDelete(s_event_group);
        }
        s_event_group = NULL;
        /*delete default mutex group*/
        if (s_sensor_node_mutex != NULL) {
            vSemaphoreDelete(s_sensor_node_mutex);
        }
        s_sensor_node_mutex = NULL;

#ifdef CONFIG_SENSOR_DEFAULT_HANDLER
        if (s_sensor_default_handler_instance != NULL) {
            /*unregist default event handler*/
            sensors_event_handler_instance_unregister(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, s_sensor_default_handler_instance);
            s_sensor_default_handler_instance = NULL;
        }
#endif
    }

    return ESP_OK;
}

int iot_sensor_scan()
{
    int sensor_count = 0;
    // search the sensor driver from a specific segment
    for (sensor_hub_detect_fn_t *p = &__sensor_hub_detect_fn_array_start; p < &__sensor_hub_detect_fn_array_end; ++p) {
        sensor_info_t info;
        sensor_device_impl_t sensor_device_impl = (*(p->fn))(&info);
        if (sensor_device_impl != NULL) {
            ESP_LOGI(TAG, "Find %s driver, type: %s", info.name, SENSOR_TYPE_STRING[info.sensor_type]);
            sensor_count++;
        }
    }
    return sensor_count;
}

esp_err_t iot_sensor_handler_register(sensor_handle_t sensor_handle, sensor_event_handler_t handler, sensor_event_handler_instance_t *context)
{
    SENSOR_CHECK(handler != NULL, "handler can not be NULL", ESP_ERR_INVALID_ARG);
    SENSOR_CHECK(sensor_handle != NULL, "sensor handle can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_t *sensor = (_iot_sensor_t *)sensor_handle;
    SENSOR_CHECK(sensor->event_base != NULL, "sensor event_base can not be NULL", ESP_FAIL);
    ESP_ERROR_CHECK(sensors_event_handler_instance_register(sensor->event_base, ESP_EVENT_ANY_ID, handler, NULL, context));
    return ESP_OK;
}

esp_err_t iot_sensor_handler_unregister(sensor_handle_t sensor_handle, sensor_event_handler_instance_t context)
{
    SENSOR_CHECK(context != NULL, "context can not be NULL", ESP_ERR_INVALID_ARG);
    SENSOR_CHECK(sensor_handle != NULL, "sensor handle can not be NULL", ESP_ERR_INVALID_ARG);
    _iot_sensor_t *sensor = (_iot_sensor_t *)sensor_handle;
    SENSOR_CHECK(sensor->event_base != NULL, "sensor event_base can not be NULL", ESP_FAIL);
    ESP_ERROR_CHECK(sensors_event_handler_instance_unregister(sensor->event_base, ESP_EVENT_ANY_ID, context));
    return ESP_OK;
}

esp_err_t iot_sensor_handler_register_with_type(sensor_type_t sensor_type, int32_t event_id, sensor_event_handler_t handler, sensor_event_handler_instance_t *context)
{
    SENSOR_CHECK(handler != NULL, "handler can not be NULL", ESP_ERR_INVALID_ARG);

    switch (sensor_type) {
    case NULL_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, handler, NULL, context));
        break;

    case HUMITURE_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_register(SENSOR_HUMITURE_EVENTS, event_id, handler, NULL, context));
        break;

    case IMU_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_register(SENSOR_IMU_EVENTS, event_id, handler, NULL, context));
        break;

    case LIGHT_SENSOR_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_register(SENSOR_LIGHTSENSOR_EVENTS, event_id, handler, NULL, context));
        break;

    default :
        ESP_LOGE(TAG, "no driver founded, sensor base = %d", sensor_type);
        return ESP_FAIL;
        break;
    }

    return ESP_OK;
}

esp_err_t iot_sensor_handler_unregister_with_type(sensor_type_t sensor_type, int32_t event_id, sensor_event_handler_instance_t context)
{
    SENSOR_CHECK(context != NULL, "context can not be NULL", ESP_ERR_INVALID_ARG);

    switch (sensor_type) {
    case NULL_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_unregister(SENSOR_HUMITURE_EVENTS, ESP_EVENT_ANY_ID, context));
        break;

    case HUMITURE_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_unregister(SENSOR_HUMITURE_EVENTS, event_id, context));
        break;

    case IMU_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_unregister(SENSOR_IMU_EVENTS, event_id, context));
        break;

    case LIGHT_SENSOR_ID:
        ESP_ERROR_CHECK(sensors_event_handler_instance_unregister(SENSOR_LIGHTSENSOR_EVENTS, event_id, context));
        break;

    default :
        ESP_LOGE(TAG, "no driver founded, sensor base = %d", sensor_type);
        break;
    }

    return ESP_OK;
}

#ifdef CONFIG_SENSOR_DEFAULT_HANDLER
static void sensor_default_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    sensor_data_t *sensor_data = (sensor_data_t *)event_data;

    switch (id) {
    case SENSOR_STARTED:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x STARTED",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr);
        break;
    case SENSOR_STOPED:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x STOPPED",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr);
        break;
    case SENSOR_HUMI_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x HUMI_DATA_READY - "
                 "humiture=%.2f",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->humidity);
        break;
    case SENSOR_TEMP_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x TEMP_DATA_READY - "
                 "temperature=%.2f\n",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->temperature);
        break;
    case SENSOR_ACCE_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x ACCE_DATA_READY - "
                 "acce_x=%.2f, acce_y=%.2f, acce_z=%.2f\n",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->acce.x, sensor_data->acce.y, sensor_data->acce.z);
        break;
    case SENSOR_GYRO_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x GYRO_DATA_READY - "
                 "gyro_x=%.2f, gyro_y=%.2f, gyro_z=%.2f\n",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->gyro.x, sensor_data->gyro.y, sensor_data->gyro.z);
        break;
    case SENSOR_LIGHT_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x LIGHT_DATA_READY - "
                 "light=%.2f",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->light);
        break;
    case SENSOR_RGBW_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x RGBW_DATA_READY - "
                 "r=%.2f, g=%.2f, b=%.2f, w=%.2f\n",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->rgbw.r, sensor_data->rgbw.r, sensor_data->rgbw.b, sensor_data->rgbw.w);
        break;
    case SENSOR_UV_DATA_READY:
        ESP_LOGI(TAG, "Timestamp = %llu - %s_0x%x UV_DATA_READY - "
                 "uv=%.2f, uva=%.2f, uvb=%.2f\n",
                 sensor_data->timestamp,
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr,
                 sensor_data->uv.uv, sensor_data->uv.uva, sensor_data->uv.uvb);
        break;
    default:
        ESP_LOGI(TAG, "Timestamp = %" PRIi64 " - event id = %" PRIi32, sensor_data->timestamp, id);
        break;
    }
}
#endif
