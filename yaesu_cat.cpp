/**
 * YAESU CAT 协议实现
 */

#include "yaesu_cat.h"
#include "cat_hardware.h"

#define YAESU_991_READ_FREQ     0xFA
#define YAESU_101_READ_FREQ     0x10

static YaesuConfig yaesuCfg;
static YaesuState yaesuState = YAESU_IDLE;
static uint8_t rxBuffer[16];
static uint8_t rxIndex = 0;
static uint32_t lastPollTime = 0;
static uint32_t lastRxTime = 0;
static uint32_t lastFrequency = 0;

void yaesuInit(const YaesuConfig* config) {
    memcpy(&yaesuCfg, config, sizeof(YaesuConfig));
    yaesuState = YAESU_IDLE;
    rxIndex = 0;
    lastFrequency = 0;
}

void yaesuDeinit() {
    yaesuState = YAESU_IDLE;
}

uint32_t yaesuParseFrequencyFT991(const uint8_t* data) {
    uint64_t freq = 0;
    for (int i = 0; i < 5; i++) {
        freq = freq * 100;
        freq += ((data[i] >> 4) & 0x0F) * 10;
        freq += (data[i] & 0x0F);
    }
    return (uint32_t)(freq * 10);
}

uint32_t yaesuParseFrequencyFTDX101(const uint8_t* data) {
    uint64_t freq = 0;
    for (int i = 4; i >= 0; i--) {
        freq = freq * 100;
        freq += ((data[i] >> 4) & 0x0F) * 10;
        freq += (data[i] & 0x0F);
    }
    return (uint32_t)freq;
}

void yaesuProcess() {
    uint32_t now = millis();
    
    if (now - lastPollTime >= yaesuCfg.pollInterval) {
        uint8_t cmd[5] = {0};
        
        switch (yaesuCfg.model) {
            case YAESU_FT991:
            case YAESU_FT991A:
                cmd[4] = YAESU_991_READ_FREQ;
                break;
            case YAESU_FTDX101:
            case YAESU_FTDX10:
                cmd[0] = YAESU_101_READ_FREQ;
                break;
            default:
                cmd[4] = YAESU_991_READ_FREQ;
                break;
        }
        
        catHardwareWrite(cmd, 5);
        lastPollTime = now;
        yaesuState = YAESU_WAIT_RESPONSE;
    }
    
    if (yaesuState == YAESU_WAIT_RESPONSE) {
        int available = catHardwareAvailable();
        int expectedLen = 5;
        
        if (available >= expectedLen) {
            uint8_t data[16];
            int read = catHardwareRead(data, expectedLen);
            
            if (read == expectedLen) {
                lastRxTime = now;
                
                switch (yaesuCfg.model) {
                    case YAESU_FT991:
                    case YAESU_FT991A:
                        lastFrequency = yaesuParseFrequencyFT991(data);
                        break;
                    case YAESU_FTDX101:
                    case YAESU_FTDX10:
                        lastFrequency = yaesuParseFrequencyFTDX101(data);
                        break;
                    default:
                        lastFrequency = yaesuParseFrequencyFT991(data);
                        break;
                }
            }
            
            yaesuState = YAESU_IDLE;
        }
        
        if (now - lastPollTime > yaesuCfg.timeout) {
            yaesuState = YAESU_IDLE;
        }
    }
}

uint32_t yaesuGetLastFrequency() {
    return lastFrequency;
}

bool yaesuIsConnected() {
    return (millis() - lastRxTime) < yaesuCfg.timeout;
}
