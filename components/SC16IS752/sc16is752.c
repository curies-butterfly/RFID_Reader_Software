#include "sc16is752.h"
#include "esp_log.h"

static const char *TAG = "SC16IS752";

#define I2C_TIMEOUT_MS 100


esp_err_t sc16is752_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SC16IS752_SDA_IO,
        .scl_io_num = SC16IS752_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SC16IS752_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(SC16IS752_I2C_PORT, &conf));
    return i2c_driver_install(SC16IS752_I2C_PORT, conf.mode, 0, 0, 0);
}

// ------------------ 基础读写函数 ------------------

esp_err_t sc16is752_write_register(i2c_port_t i2c_num, uint8_t addr, uint8_t ch, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];
    buf[0] = (reg << 3) | (ch << 1); // Subaddress 格式
    buf[1] = val;

    return i2c_master_write_to_device(i2c_num, addr, buf, 2, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

esp_err_t sc16is752_read_register(i2c_port_t i2c_num, uint8_t addr, uint8_t ch, uint8_t reg, uint8_t *data)
{
    uint8_t sub = (reg << 3) | (ch << 1);
    esp_err_t ret = i2c_master_write_read_device(i2c_num, addr, &sub, 1, data, 1, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    return ret;
}

// ------------------ 初始化 ------------------

esp_err_t sc16is752_init(i2c_port_t i2c_num, uint8_t addr)
{
    ESP_LOGI(TAG, "SC16IS752 init start");

    // 复位 FIFO
    sc16is752_write_register(i2c_num, addr, SC16IS752_CHANNEL_A, SC16IS752_FCR, 0x06);
    sc16is752_write_register(i2c_num, addr, SC16IS752_CHANNEL_B, SC16IS752_FCR, 0x06);

    vTaskDelay(pdMS_TO_TICKS(10));

    // 再次写入 0x07 重新启用 FIFO
    sc16is752_write_register(i2c_num, addr, SC16IS752_CHANNEL_A, SC16IS752_FCR, 0x07);
    sc16is752_write_register(i2c_num, addr, SC16IS752_CHANNEL_B, SC16IS752_FCR, 0x07);

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "SC16IS752 init done");
    return ESP_OK;
}

// ------------------ UART配置 ------------------

esp_err_t sc16is752_uart_init(uint8_t ch, uint32_t baudrate)
{
    uint16_t divisor = 1843200 / baudrate / 16;

    // 打开 DLAB 位
    sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_LCR, 0x80);

    // 写 DLL、DLH
    sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_DLL, divisor & 0xFF);
    sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_DLH, (divisor >> 8) & 0xFF);

    // 关闭 DLAB，设置 8N1
    sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_LCR, 0x03);

    // 启用 FIFO
    sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_FCR, 0x07);

    ESP_LOGI(TAG, "UART channel %c initialized, baud=%" PRIu32,
             (ch == SC16IS752_CHANNEL_A) ? 'A' : 'B', baudrate);
    return ESP_OK;
}

// ------------------ 发送函数 ------------------

esp_err_t sc16is752_send_byte(uint8_t ch, uint8_t data)
{
    uint8_t lsr;
    int retry = 0;

    do {
        sc16is752_read_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_LSR, &lsr);
        if (lsr & SC16IS752_LSR_THRE) break;
        vTaskDelay(pdMS_TO_TICKS(1));
        retry++;
    } while (retry < 100);

    if (!(lsr & SC16IS752_LSR_THRE)) {
        ESP_LOGW(TAG, "TX not ready on channel %c", (ch == 0) ? 'A' : 'B');
        return ESP_FAIL;
    }

    return sc16is752_write_register(SC16IS752_I2C_PORT, SC16IS752_ADDR, ch, SC16IS752_THR, data);
}


esp_err_t sc16is752_send_buffer(uint8_t ch, const uint8_t *buf, size_t len)
{
    esp_err_t ret;
    for (size_t i = 0; i < len; i++) {
        ret = sc16is752_send_byte(ch, buf[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Send byte failed at index %d", i);
            return ret;  // 发送失败就返回
        }
        // 可根据需要添加适当延时，防止发送过快
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}


esp_err_t sc16is752_init_all(void)
{
    esp_err_t ret;

    ret = sc16is752_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE("SC16IS752", "I2C init failed");
        return ret;
    }

    ret = sc16is752_init(SC16IS752_I2C_PORT, SC16IS752_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE("SC16IS752", "SC16IS752 chip init failed");
        return ret;
    }

    ret = sc16is752_uart_init(SC16IS752_CHANNEL_A, 115200);
    if (ret != ESP_OK) {
        ESP_LOGE("SC16IS752", "UART init failed");
        return ret;
    }

    // ret = sc16is752_send_byte(SC16IS752_CHANNEL_A, 'A');
    // if (ret != ESP_OK) {
    //     ESP_LOGE("SC16IS752", "Send byte failed");
    //     return ret;
    // } else {
    //     ESP_LOGI("SC16IS752", "Send byte 'A' success");
    // }

    return ESP_OK;
}
