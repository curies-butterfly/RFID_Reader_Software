#ifndef     SHT30__H_
#define     SHT30__H_


#include "esp_err.h"
#include "driver/i2c.h"
#include "hal/i2c_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

// ---------- I2C CONFIG ----------
#define SC16IS752_I2C_PORT     I2C_NUM_0
#define SC16IS752_SDA_IO       6
#define SC16IS752_SCL_IO       7
#define SC16IS752_FREQ_HZ      400000
#define SC16IS752_ADDR     (0x90 >> 1)   // A1=A0=VDD -> 7位地址=0x48

// ---------- UART CHANNEL ----------
typedef enum {
    SC16IS752_CHANNEL_A = 0,
    SC16IS752_CHANNEL_B = 1
} sc16is752_channel_t;

// ---------- REGISTER MAP ----------
// #define SC16IS752_RHR   0x00
// #define SC16IS752_THR   0x00
// #define SC16IS752_IER   0x01
// #define SC16IS752_FCR   0x02
// #define SC16IS752_LCR   0x03
// #define SC16IS752_MCR   0x04
// #define SC16IS752_LSR   0x05
// #define SC16IS752_MSR   0x06
// #define SC16IS752_SPR   0x07
// #define SC16IS752_TXLVL 0x08
// #define SC16IS752_RXLVL 0x09
// #define SC16IS752_DLL   0x00
// #define SC16IS752_DLH   0x01
// #define SC16IS752_EFR   0x02

// 寄存器定义
#define SC16IS752_THR        0x00  // 发送寄存器
#define SC16IS752_RHR        0x00  // 接收寄存器
#define SC16IS752_IER        0x01
#define SC16IS752_FCR        0x02
#define SC16IS752_LCR        0x03
#define SC16IS752_MCR        0x04
#define SC16IS752_LSR        0x05
#define SC16IS752_MSR        0x06
#define SC16IS752_SPR        0x07
#define SC16IS752_TCR        0x06
#define SC16IS752_TLR        0x07
#define SC16IS752_TXLVL      0x08
#define SC16IS752_RXLVL      0x09
#define SC16IS752_DLL        0x00
#define SC16IS752_DLH        0x01
#define SC16IS752_EFR        0x02
#define SC16IS752_XON1       0x04
#define SC16IS752_XON2       0x05
#define SC16IS752_XOFF1      0x06
#define SC16IS752_XOFF2      0x07


// // ---------- API ----------
// esp_err_t sc16is752_i2c_init(void);
// esp_err_t sc16is752_write_reg(sc16is752_channel_t ch, uint8_t reg, uint8_t val);
// esp_err_t sc16is752_read_reg(sc16is752_channel_t ch, uint8_t reg, uint8_t *val);
// esp_err_t sc16is752_uart_init(sc16is752_channel_t ch, uint32_t baudrate);
// esp_err_t sc16is752_send_byte(sc16is752_channel_t ch, uint8_t data);
// esp_err_t sc16is752_recv_byte(sc16is752_channel_t ch, uint8_t *data);

#define SC16IS752_LSR_THRE   0x20  // 发送缓存空

esp_err_t sc16is752_i2c_init(void);
esp_err_t sc16is752_init(i2c_port_t i2c_num, uint8_t addr);
esp_err_t sc16is752_uart_init(uint8_t ch, uint32_t baudrate);
esp_err_t sc16is752_send_byte(uint8_t ch, uint8_t data);
esp_err_t sc16is752_read_register(i2c_port_t i2c_num, uint8_t addr, uint8_t ch, uint8_t reg, uint8_t *data);
esp_err_t sc16is752_write_register(i2c_port_t i2c_num, uint8_t addr, uint8_t ch, uint8_t reg, uint8_t val);
esp_err_t sc16is752_send_buffer(uint8_t ch, const uint8_t *buf, size_t len);

esp_err_t sc16is752_init_all(void);


#endif