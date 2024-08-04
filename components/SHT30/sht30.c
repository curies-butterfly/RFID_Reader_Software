#include "sht30.h"

#define I2C_MPU6050_SDA_IO   6
#define I2C_MPU6050_SCL_IO   7 
#define I2C_MASTER_FREQ_HZ   400000

#define WRITE_BIT 0x0              /*!< I2C master write */
#define READ_BIT  0x1              /*!< I2C master read */
#define ACK_CHECK_EN 0x1           /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0          /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                /*!< I2C ack value */
#define NACK_VAL 0x1               /*!< I2C nack value */

sht30_data_t sht30_data;


esp_err_t iic_init()
{
    i2c_config_t conf = 
    {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MPU6050_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MPU6050_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_1, &conf);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(I2C_NUM_1, conf.mode, 0, 0, 0);
}


/*******************************************************************
 温湿度获取函数
函数原型: SHT30_read_result(uint8_t addr);
功能: 用来接收从器件采集并合成温湿度
********************************************************************/
void SHT30_read_result(uint8_t addr, sht30_data_t *sht30_data)
{
	uint16_t tem,hum;
	uint8_t buff[6];
	float Temperature=0;
	float Humidity=0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | WRITE_BIT, ACK_CHECK_EN);//发送写命令，设备地址+读写位，并等待ACK信号
    i2c_master_write_byte(cmd, 0x2c, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x06, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_NUM_1, cmd, 1);
    i2c_cmd_link_delete(cmd);
    vTaskDelay(50/portTICK_PERIOD_MS);
    
    i2c_cmd_handle_t cmd1 = i2c_cmd_link_create();
    i2c_master_start(cmd1);
    i2c_master_write_byte(cmd1, (addr << 1) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read(cmd1, buff, 6 - 1, ACK_VAL);
    i2c_master_read_byte(cmd1, buff+5, NACK_VAL);
    i2c_master_stop(cmd1);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_1, cmd1, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd1);

    tem = ((buff[0]<<8) | buff[1]);//温度拼接
	hum = ((buff[3]<<8) | buff[4]);//湿度拼接

	/*转换实际温度*/
	Temperature= (175.0*(float)tem/65535.0-45.0) ;// T = -45 + 175 * tem / (2^16-1)
	Humidity= (100.0*(float)hum/65535.0);// RH = hum*100 / (2^16-1)


    sht30_data->Temperature = Temperature;
    sht30_data->Humidity = Humidity;
    hum=0;
	tem=0;
}

