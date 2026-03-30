/**
 * FlexRadio SmartSDR 协议实现
 */

#include "flexradio_smsdr.h"
#include <WiFiUdp.h>
#include <WiFiClient.h>

static const uint8_t FLEX_DISCOVERY_PACKET[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static FlexConfig flexCfg;
static FlexState flexState = FLEX_DISCONNECTED;
static WiFiUDP udpClient;
static WiFiClient tcpClient;
static FlexSlice slices[8];
static uint32_t lastActivity = 0;

void flexInit(const FlexConfig* config) {
    memcpy(&flexCfg, config, sizeof(FlexConfig));
    flexState = FLEX_DISCONNECTED;
    memset(slices, 0, sizeof(slices));
}

void flexDeinit() {
    flexDisconnect();
}

bool flexDiscoverRadio(IPAddress* outIP, uint32_t timeoutMs) {
    udpClient.begin(FLEX_DISCOVERY_PORT);
    
    udpClient.beginPacket(IPAddress(255, 255, 255, 255), FLEX_DISCOVERY_PORT);
    udpClient.write(FLEX_DISCOVERY_PACKET, sizeof(FLEX_DISCOVERY_PACKET));
    udpClient.endPacket();
    
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        int packetSize = udpClient.parsePacket();
        if (packetSize >= 0) {
            uint8_t buffer[64];
            int len = udpClient.read(buffer, sizeof(buffer));
            if (len >= 16) {
                *outIP = udpClient.remoteIP();
                udpClient.stop();
                return true;
            }
        }
        delay(10);
    }
    
    udpClient.stop();
    return false;
}

bool flexConnect(IPAddress ip) {
    flexDisconnect();
    
    if (!tcpClient.connect(ip, flexCfg.commandPort)) {
        return false;
    }
    
    flexState = FLEX_CONNECTED;
    lastActivity = millis();
    tcpClient.println("C1|");
    
    return true;
}

void flexDisconnect() {
    if (tcpClient.connected()) {
        tcpClient.stop();
    }
    flexState = FLEX_DISCONNECTED;
}

bool flexIsConnected() {
    return tcpClient.connected() && (millis() - lastActivity) < flexCfg.timeout;
}

void flexSubscribeSlices() {
    if (!tcpClient.connected()) return;
    tcpClient.println("sub slice all");
    flexState = FLEX_SUBSCRIBED;
}

uint32_t flexGetTXFrequency() {
    for (int i = 0; i < 8; i++) {
        if (slices[i].active && slices[i].isTX) {
            return slices[i].frequency;
        }
    }
    return 0;
}

void flexParseResponse(const char* data) {
    if (strncmp(data, "S", 1) == 0) {
        int sliceNum = data[1] - '0';
        if (sliceNum >= 0 && sliceNum < 8) {
            const char* freqPtr = strstr(data, "freq=");
            if (freqPtr) {
                float freqMHz = atof(freqPtr + 5);
                slices[sliceNum].frequency = (uint32_t)(freqMHz * 1000000);
            }
            slices[sliceNum].isTX = (strstr(data, "tx=1") != NULL);
            slices[sliceNum].active = true;
        }
    }
    lastActivity = millis();
}

void flexProcess() {
    switch (flexState) {
        case FLEX_DISCONNECTED:
            if (flexCfg.autoDiscover) {
                IPAddress discoveredIP;
                if (flexDiscoverRadio(&discoveredIP, 3000)) {
                    if (flexConnect(discoveredIP)) {
                        flexSubscribeSlices();
                    }
                }
            } else if (flexCfg.radioIP != IPAddress(0, 0, 0, 0)) {
                if (flexConnect(flexCfg.radioIP)) {
                    flexSubscribeSlices();
                }
            }
            break;
            
        case FLEX_CONNECTED:
        case FLEX_SUBSCRIBED:
            while (tcpClient.available()) {
                String line = tcpClient.readStringUntil('\n');
                flexParseResponse(line.c_str());
            }
            
            if (!tcpClient.connected()) {
                flexState = FLEX_DISCONNECTED;
            }
            break;
            
        default:
            break;
    }
}
