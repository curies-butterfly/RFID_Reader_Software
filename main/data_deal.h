#ifndef DATA_DEAL__H_
#define DATA_DEAL__H_

#include "stdio.h"

typedef struct{
    uint16_t EPCID; //epcID 两个字节
    float tempe;//温度 两个字节
    uint8_t ant; //天线号 一个字节

}TagInfo;

void publish_epc_data2();
void loraInfoToGateway();


#endif /* _DATA_DEAL_H_ */