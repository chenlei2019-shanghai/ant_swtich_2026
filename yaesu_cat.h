/**
 * YAESU CAT 协议驱动
 */

#ifndef YAESU_CAT_H
#define YAESU_CAT_H

#include <Arduino.h>

enum YaesuModel {
    YAESU_FT991 = 0,
    YAESU_FT991A,
    YAESU_FTDX101,
    YAESU_FTDX10,
    YAESU_FT891
};

enum YaesuState {
    YAESU_IDLE,
    YAESU_WAIT_RESPONSE
};

struct YaesuConfig {
    YaesuModel model;
    uint32_t pollInterval;
    uint32_t timeout;
};

void yaesuInit(const YaesuConfig* config);
void yaesuDeinit();
void yaesuProcess();
uint32_t yaesuParseFrequencyFT991(const uint8_t* data);
uint32_t yaesuParseFrequencyFTDX101(const uint8_t* data);
uint32_t yaesuGetLastFrequency();
bool yaesuIsConnected();

#endif
