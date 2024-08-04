#ifndef COMMON__H_
#define COMMON__H_

#include "driver/gpio.h"

#define MODULE_SET_PIN    40

#define CRC_16_CCITT 0x1021


//用于将 Uint16 转换成两个Byte
typedef union
{
    uint16_t data_16bit;
    uint8_t Byte[2];
}Uint16toByteType;



/** generic error codes */
typedef enum result
{
    Ok                          = 0u,  ///< No error
    Error                       = 1u,  ///< Non-specific error code
    ErrorAddressAlignment       = 2u,  ///< Address alignment does not match
    ErrorAccessRights           = 3u,  ///< Wrong mode (e.g. user/system) mode is set
    ErrorInvalidParameter       = 4u,  ///< Provided parameter is not valid
    ErrorOperationInProgress    = 5u,  ///< A conflicting or requested operation is still in progress
    ErrorInvalidMode            = 6u,  ///< Operation not allowed in current mode
    ErrorUninitialized          = 7u,  ///< Module (or part of it) was not initialized properly
    ErrorBufferFull             = 8u,  ///< Circular buffer can not be written because the buffer is full
    ErrorTimeout                = 9u,  ///< Time Out error occurred (e.g. I2C arbitration lost, Flash time-out, etc.)
    ErrorNotReady               = 10u, ///< A requested final state is not reached
    OperationInProgress         = 11u  ///< Indicator for operation in progress
} result_t;



uint16_t CRC16_CCITT(uint8_t* pchMsg, uint16_t wDataLen);
void Uint16toByte(uint16_t data_16bit,uint8_t *pByte);
void moduleSetGpioInit();
void selRs485Mosule();
void sel4GModule();


#endif