/**
 * FlexRadio SmartSDR 协议驱动
 */

#ifndef FLEXRADIO_SMSDR_H
#define FLEXRADIO_SMSDR_H

#include <Arduino.h>
#include <WiFi.h>

#define FLEX_DISCOVERY_PORT 4992
#define FLEX_COMMAND_PORT   4992
#define FLEX_VITA_PORT      4991

enum FlexState {
    FLEX_DISCONNECTED,
    FLEX_DISCOVERING,
    FLEX_CONNECTING,
    FLEX_CONNECTED,
    FLEX_SUBSCRIBED
};

struct FlexSlice {
    uint8_t number;
    uint32_t frequency;
    bool isTX;
    bool active;
};

struct FlexConfig {
    IPAddress radioIP;
    uint16_t commandPort;
    uint32_t timeout;
    bool autoDiscover;
};

void flexInit(const FlexConfig* config);
void flexDeinit();
void flexProcess();
bool flexDiscoverRadio(IPAddress* outIP, uint32_t timeoutMs);
bool flexConnect(IPAddress ip);
void flexDisconnect();
bool flexIsConnected();
void flexSubscribeSlices();
uint32_t flexGetTXFrequency();

#endif
