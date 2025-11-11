#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @file app_tasks.h
 * @brief FreeRTOS任务统一管理头文件
 *
 * 本文件用于声明系统中所有任务的创建接口，便于在 main.c 中统一初始化。
 * 各任务的实现文件（如 sensor_task.c、network_task.c 等）需自行实现对应的任务函数。
 */


/**
 * @brief 创建系统中的所有任务
 *
 * 建议在 app_main() 中调用此函数，以便统一注册和启动所有任务。
 */


/* ==================== 各模块任务函数声明 ==================== */

// /**
//  * @brief 传感器采集任务
//  */
// void sensor_task(void *pvParameters);

// /**
//  * @brief 网络通信任务
//  */
// void network_task(void *pvParameters);

// /**
//  * @brief 显示或数据处理任务
//  */
// void display_task(void *pvParameters);

void RFID_MqttTimeTask(void *arg);
void RFID_MqttErrTask(void *arg);
void Screen_DataTask(void *arg);


#ifdef __cplusplus
}
#endif