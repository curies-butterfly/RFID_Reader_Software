#include <stdio.h>
#include "led.h"
#include "esp_system.h"
#include "ntc_temp_adc.h"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

#define ADC_CHANNEL ADC_CHANNEL_9       // GPIO10
#define ADC_ATTEN ADC_ATTEN_DB_0       // 设置衰减为 12dB
#define ADC_UNIT ADC_UNIT_1             // 使用 ADC1
#define ADC_WIDTH ADC_WIDTH_BIT_12      // 使用 12 位的采样精度
#define ADC_BIT_WIDTH ADC_BITWIDTH_12   // 设置位宽为 12 位
#define NUM_SAMPLES 64                  // 多次采样
#define TAG "ADC"

#define DEFAULT_VREF    3300        // 默认参考电压 (单位: mV)
#define ADC_MAX_VALUE   4095        // 12位ADC的最大值
#define R_REF           10000       // 参考电阻值 (单位: Ω)
#define R_0             10000       // NTC在25°C时的电阻值 (单位: Ω)
#define B_COEFF         3950        // NTC的B值 (单位: K)

#define fan_open_threshold 40 //控制风扇打开温度高阈值
#define fan_close_threshold 35 //控制风扇关闭温度低阈值
void ntc_init(void)
{
    adc_digi_pattern_config_t adc1_digi_pattern_config; // 定义 ADC1 的模式配置结构体
    adc_digi_configuration_t adc1_init_config; // 定义 ADC1 的初始化配置结构体

    adc1_digi_pattern_config.atten = ADC_ATTEN; // 设置衰减为 12dB
    adc1_digi_pattern_config.channel = ADC_CHANNEL; // 设置通道为 ADC_CHANNEL
    adc1_digi_pattern_config.unit = ADC_UNIT; // 设置单元为 ADC_UNIT_1
    adc1_digi_pattern_config.bit_width = ADC_BIT_WIDTH; // 设置位宽为 12 位
    adc1_init_config.adc_pattern = &adc1_digi_pattern_config; // 将 ADC1 的模式配置结构体赋值给初始化配置结构体的 adc_pattern 成员

    adc_digi_controller_configure(&adc1_init_config); // 调用 adc_digi_controller_configure 函数配置 ADC 数字控制器
}

uint32_t adc_get_result_average(uint32_t ch, uint32_t times)
{
    uint32_t temp_val = 0; // 定义一个临时变量用于存储 ADC 读取的值
    uint8_t t;

    for (t = 0; t < times; t++) // 循环 times 次
    {
        temp_val += adc1_get_raw(ch); // 读取 ADC 的原始值并累加到 temp_val
        vTaskDelay(5); // 延时 5ms
    }

    return temp_val / times; // 返回平均值
}

int temp;
float T_C; 
void ntc_temp_adc_run(void)
{
  
    // uint32_t voltage = adc_get_result_average(ADC_CHANNEL, NUM_SAMPLES); // 读取 ADC 的平均值
    // ESP_LOGI(TAG, "平均ADC读数: %d", voltage); // 将 ADC 的平均值打印到串口

    int adc_reading = adc1_get_raw(ADC1_CHANNEL_9);
    // 将ADC原始值转换为电压 (单位: mV)
    uint32_t voltage = (adc_reading * DEFAULT_VREF) / ADC_MAX_VALUE;
    // 计算NTC热敏电阻的电阻值 (单位: Ω)
    // float V_in = voltage / 1000.0; // 将电压从mV转换为V
    float R_NTC = R_REF / ((DEFAULT_VREF / (float)voltage) - 1.0);
    // 使用Steinhart-Hart方程计算温度 (单位: K)
    float T = 1.0 / ((1.0 / 298.15) + (1.0 / B_COEFF) * log(R_NTC / R_0));
    T_C = T - 273.15; // 转换为摄氏度

    // 打印电阻值和温度
    // ESP_LOGI(TAG, "ADC Raw: %d\tVoltage: %dmV\tR_NTC: %.2fΩ\tTemperature: %.2f°C", adc_reading, voltage, R_NTC, T_C);

    // printf("ADC Raw: %d\t Voltage: %ldmV\t R_NTC: %.2fΩ \tTemperature: %.2f°C", adc_reading, voltage, R_NTC, T_C);
   
    temp=(int)T_C;
    if(temp>=fan_open_threshold)//风扇大于等于阈值，打开
    {
        fan_gpio_set(1);  // 1：风扇开 0：关闭
    }else{
        if(temp<=fan_close_threshold)//风扇小于阈值，关闭
        {
            fan_gpio_set(0);  // 1：风扇开 0：关闭
        }
    }

    // printf("ADC: %ld", voltage);
    
}
