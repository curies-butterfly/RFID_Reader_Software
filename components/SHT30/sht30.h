#ifndef     SHT30__H_
#define     SHT30__H_

#include "esp_err.h"
#include "driver/i2c.h"
#include "hal\i2c_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#define SHT30_I2C_ADDRESS 0x44   // SHT30 传感器的 I2C 地址

typedef struct
{
    float Temperature;
    float Humidity;

}sht30_data_t;

extern sht30_data_t sht30_data;


esp_err_t iic_init();
void SHT30_read_result(uint8_t addr, sht30_data_t *sht30_data);




#endif