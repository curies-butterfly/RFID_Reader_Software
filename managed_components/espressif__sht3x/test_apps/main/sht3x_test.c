/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "i2c_bus.h"
#include "esp_system.h"
#include "sht3x.h"

#define I2C_MASTER_SCL_IO          GPIO_NUM_2              /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO          GPIO_NUM_1              /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0              /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          100000                 /*!< I2C master clock frequency */

static i2c_bus_handle_t i2c_bus = NULL;
static sht3x_handle_t sht3x = NULL;

/**
 * @brief i2c master initialization
 */
static void sht3x_init_test()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &conf);
    sht3x = sht3x_create(i2c_bus, SHT3x_ADDR_PIN_SELECT_VSS);
    sht3x_set_measure_mode(sht3x, SHT3x_PER_2_MEDIUM);     /*!< here read data in periodic mode*/
}

static void sht3x_deinit_test()
{
    sht3x_delete(&sht3x);
    i2c_bus_delete(&i2c_bus);
}

void sht3x_get_data_test()
{
    float Tem_val, Hum_val;
    int cnt = 10;

    while (cnt--) {
        if (sht3x_get_humiture(sht3x, &Tem_val, &Hum_val) == 0) {
            printf("temperature %.2f°C    ", Tem_val);
            printf("humidity:%.2f %%\n", Hum_val);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

TEST_CASE("Sensor sht3x test", "[sht3x][iot][sensor]")
{
    sht3x_init_test();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    sht3x_get_data_test();
    sht3x_deinit_test();
}

void app_main(void)
{
    printf("SHT3X TEST \n");
    unity_run_menu();
}
