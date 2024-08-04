#include "rfidcommon.h"

/*****************************************************
函数名称：void Uint16toByte(uint16_t data_16bit,uint8_t *pByte)
函数功能：将给定的 CRC 值和要计算的数据做 CRC 计算
入口参数：pchMsg 要计算的数据直针      wDataLen 数据长度 
返 回 值：wCRC 函数新计算的 CRC 值
注：
*********************************************************/
uint16_t CRC16_CCITT(uint8_t* pchMsg, uint16_t wDataLen) 
{
    uint8_t i, chChar;
    uint16_t wCRC = 0;
    while (wDataLen--)
    {
        chChar = *pchMsg++;
        wCRC ^= (((uint16_t)chChar) << 8);
        for (i = 0; i < 8; i++)
        {
            if (wCRC & 0x8000)
                wCRC = (wCRC << 1) ^ CRC_16_CCITT;
            else
                wCRC <<= 1;
        }
    }
    return wCRC;
}


/*****************************************************
函数名称：void Uint16toByte(uint16_t data_16bit,uint8_t *pByte)
函数功能：将16位整形数据转换成2个8位整形数据
入口参数：data_16bit：要被转换的16位整形数据，pByte:转换后的数据存放地址
返回值：无
注：
*********************************************************/
void Uint16toByte(uint16_t data_16bit,uint8_t *pByte)
{
    Uint16toByteType D;
    D.data_16bit = data_16bit;
    if(pByte == NULL)   return; //如果是空指针，则转换失败
    *pByte++ = D.Byte[1];
    if(pByte == NULL)   return;
    *pByte = D.Byte[0];
}


void moduleSetGpioInit()
{
    gpio_config_t gpio_cfg =
    {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL<<MODULE_SET_PIN ,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&gpio_cfg);
    gpio_set_level(MODULE_SET_PIN,0); //default:使用485
}

void selRs485Mosule()
{
    gpio_set_level(MODULE_SET_PIN,1);
}

void sel4GModule()
{
    gpio_set_level(MODULE_SET_PIN,0);
}