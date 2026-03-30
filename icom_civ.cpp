/**
 * ICOM CI-V 协议实现 - 增强版
 * 整合 K7MDL2/IC-705-BLE-Serial-Example 的 processCatMessages 和 CIV_Action
 */

#include "icom_civ.h"
#include "cat_hardware.h"

static IcomConfig icomCfg;
static IcomStatus icomStatus;
static uint8_t rxBuffer[64];
static uint8_t rxIndex = 0;
static uint32_t lastPollTime = 0;

// 模式名称表 (参考 K7MDL2)
static const char* modeNames[] = {
    "LSB", "USB", "AM", "CW", "RTTY", "FM", "WFM", "CW-R", "RTTY-R",
    "???", "???", "???", "???", "???", "???", "???", "???", "???", "???",
    "???", "???", "???", "???", "DV"
};

void icomInit(const IcomConfig* config) {
    memcpy(&icomCfg, config, sizeof(IcomConfig));
    memset(&icomStatus, 0, sizeof(IcomStatus));
    rxIndex = 0;
    
    // 默认使用备用控制器地址避免冲突
    if (icomCfg.controllerAddress == 0) {
        icomCfg.controllerAddress = ICOM_ADDR_CONTROLLER_ALT;
    }
}

void icomDeinit() {
    icomStatus.connected = false;
}

void icomSetConnType(IcomConnType type) {
    icomCfg.connType = type;
}

IcomConnType icomGetConnType() {
    return icomCfg.connType;
}

const char* icomGetConnTypeStr(IcomConnType type) {
    switch (type) {
        case ICOM_CONN_BLE: return "蓝牙BLE";
        case ICOM_CONN_BT_CLASSIC: return "蓝牙SPP";
        case ICOM_CONN_USB_HOST: return "USB";
        case ICOM_CONN_SERIAL: return "有线";
        default: return "--";
    }
}

IcomRadioModel icomDetectModel(uint8_t address) {
    switch (address) {
        case ICOM_ADDR_IC705: return ICOM_MODEL_IC705;
        case ICOM_ADDR_IC7300: return ICOM_MODEL_IC7300;
        case ICOM_ADDR_IC7610: return ICOM_MODEL_IC7610;
        case ICOM_ADDR_IC9700: return ICOM_MODEL_IC9700;
        case ICOM_ADDR_IC905: return ICOM_MODEL_IC905;
        case ICOM_ADDR_ICR30: return ICOM_MODEL_ICR30;
        default: return ICOM_MODEL_UNKNOWN;
    }
}

const char* icomGetModelStr(IcomRadioModel model) {
    switch (model) {
        case ICOM_MODEL_IC705: return "IC-705";
        case ICOM_MODEL_IC7300: return "IC-7300";
        case ICOM_MODEL_IC7610: return "IC-7610";
        case ICOM_MODEL_IC9700: return "IC-9700";
        case ICOM_MODEL_IC905: return "IC-905";
        case ICOM_MODEL_ICR30: return "IC-R30";
        default: return "--";
    }
}

void icomSendCommand(uint8_t cmd, const uint8_t* data, uint8_t dataLen) {
    uint8_t frame[32];
    uint8_t idx = 0;
    
    frame[idx++] = ICOM_PREAMBLE_1;
    frame[idx++] = ICOM_PREAMBLE_2;
    frame[idx++] = icomCfg.radioAddress;
    frame[idx++] = icomCfg.controllerAddress;
    frame[idx++] = cmd;
    
    for (uint8_t i = 0; i < dataLen; i++) {
        frame[idx++] = data[i];
    }
    
    frame[idx++] = ICOM_END;
    
    // 调试输出实际发送的地址
    Serial.printf("[ICOM] TX: 目标地址=0x%02X, 控制器=0x%02X, 命令=0x%02X\n", 
                  icomCfg.radioAddress, icomCfg.controllerAddress, cmd);
    
    catHardwareWrite(frame, idx);
}

void icomRequestFrequency() {
    icomSendCommand(ICOM_CMD_READ_FREQ, NULL, 0);
}

void icomRequestMode() {
    icomSendCommand(ICOM_CMD_READ_MODE, NULL, 0);
}

void icomRequestPTT() {
    uint8_t subCmd = 0x00;
    icomSendCommand(ICOM_CMD_READ_TX, &subCmd, 1);
}

void icomRequestSMeter() {
    uint8_t subCmd = 0x02;
    icomSendCommand(ICOM_CMD_READ_S_METER, &subCmd, 1);
}

void icomRequestRFPower() {
    uint8_t subCmd = 0x0A;
    icomSendCommand(ICOM_CMD_READ_RF_POWER, &subCmd, 1);
}

/**
 * BCD 频率解析 - 来自 K7MDL2 的优化版本
 * 支持 5字节 (常规) 和 6字节 (10GHz+) 频率
 */
uint64_t icomParseFrequencyBCD(const uint8_t* bcdData, uint8_t len) {
    uint64_t freq = 0;
    uint64_t mul = 1;
    
    for (uint8_t i = 0; i < len; i++) {
        if (bcdData[i] == ICOM_END) continue; // 跳过终止符
        
        // 低4位 + 高4位，每半字节代表一个十进制数字
        freq += (bcdData[i] & 0x0F) * mul;
        mul *= 10;
        freq += ((bcdData[i] >> 4) & 0x0F) * mul;
        mul *= 10;
    }
    
    return freq;
}

/**
 * CI-V 命令处理 - 基于 K7MDL2 的 CIV_Action
 */
void icomCIV_Action(uint8_t cmd, const uint8_t* data, uint8_t dataLen) {
    switch (cmd) {
        case ICOM_CMD_READ_FREQ:
        case ICOM_CMD_SET_FREQ: {
            // 解析频率数据
            if (dataLen == 5 || dataLen == 6) {
                uint64_t freq = icomParseFrequencyBCD(data, dataLen);
                icomStatus.frequency = freq;
                icomStatus.freq_hz = (uint32_t)(freq % 1000000000ULL);
                icomStatus.last_update = millis();
                icomStatus.connected = true;
                Serial.printf("[ICOM] Freq updated: %lu Hz\n", (uint32_t)freq);
            }
            break;
        }
        
        case ICOM_CMD_READ_MODE:
        case ICOM_CMD_SEND_MODE:
        case ICOM_CMD_SET_MODE: {
            if (dataLen >= 1) {
                icomStatus.mode = data[0];
                if (dataLen >= 2) {
                    icomStatus.filter = data[1];
                }
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_READ_TX: {
            if (dataLen >= 1) {
                icomStatus.ptt = (data[0] == 0x01);
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_READ_S_METER: {
            // S-meter 电平 0x00-0xFF
            if (dataLen >= 2) {
                // uint8_t s_level = data[0] * 100 + data[1];
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_READ_RF_POWER: {
            if (dataLen >= 2) {
                icomStatus.rfpwr = data[0] * 100 + data[1]; // 0-255
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_READ_ATTN: {
            if (dataLen >= 1) {
                icomStatus.atten = data[0];
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_READ_SPLIT: {
            if (dataLen >= 1) {
                icomStatus.split = (data[0] == 0x01);
                icomStatus.last_update = millis();
            }
            break;
        }
        
        case ICOM_CMD_OK:
            // 命令确认
            break;
            
        case ICOM_CMD_NG:
            // 命令拒绝
            break;
            
        default:
            // 未知命令
            break;
    }
}

/**
 * 消息解析 - 基于 K7MDL2 的 processCatMessages
 */
void icomProcessMessage(const uint8_t* buffer, uint8_t len) {
    if (len < 6) return; // 最小帧长度
    
    // 检查前导码
    if (buffer[0] != ICOM_PREAMBLE_1 || buffer[1] != ICOM_PREAMBLE_2) {
        return;
    }
    
    uint8_t toAddr = buffer[2];
    uint8_t fromAddr = buffer[3];
    uint8_t cmd = buffer[4];
    
    // 检查是否发给我们或广播
    if (toAddr != icomCfg.controllerAddress && toAddr != ICOM_BROADCAST) {
        // 也可能是来自其他控制器的消息，检查源地址
        if (fromAddr != icomCfg.radioAddress) {
            return;
        }
    }
    
    // 检查结束字节
    if (buffer[len - 1] != ICOM_END) {
        return;
    }
    
    // 提取数据部分
    uint8_t dataLen = len - 6; // 减去 2(前导) + 2(地址) + 1(命令) + 1(结束)
    const uint8_t* data = (dataLen > 0) ? &buffer[5] : NULL;
    
    // 执行命令
    icomCIV_Action(cmd, data, dataLen);
    
    // 更新连接状态
    icomStatus.connected = true;
    icomStatus.last_update = millis();
}

void icomProcess() {
    uint32_t now = millis();
    
    // 高频轮询 (默认 200ms = 5Hz)
    if (now - lastPollTime >= icomCfg.pollInterval) {
        // 每次必查频率
        Serial.printf("[ICOM] Query freq (addr=0x%02X, conn=%d)\n", 
                      icomCfg.radioAddress, icomCfg.connType);
        icomRequestFrequency();
        
        // 轮流查询其他参数 (每 5 个周期一次)
        static uint8_t pollCycle = 0;
        if (++pollCycle >= 5) pollCycle = 0;
        
        switch (pollCycle) {
            case 0: icomRequestPTT(); break;
            case 1: icomRequestMode(); break;
            case 2: icomRequestSMeter(); break;
            case 3: icomRequestRFPower(); break;
            default: break;
        }
        
        lastPollTime = now;
        
        // BLE 模式不需要额外延时
        if (icomCfg.connType == ICOM_CONN_BT_CLASSIC) {
            delay(50);  // 经典蓝牙需要更多时间
        }
    }
    
    // 读取并解析数据 (批量处理)
    int available = catHardwareAvailable();
    if (available > 0) {
        Serial.printf("[ICOM] RX available: %d bytes\n", available);
    }
    while (catHardwareAvailable() > 0) {
        uint8_t byte;
        if (catHardwareRead(&byte, 1) > 0) {
            // 打印接收的字节
            if (rxIndex == 0) Serial.print("[ICOM] RX: ");
            Serial.printf("%02X ", byte);
            
            // 状态机解析
            if (rxIndex == 0 && byte == ICOM_PREAMBLE_1) {
                rxBuffer[rxIndex++] = byte;
            } else if (rxIndex == 1) {
                if (byte == ICOM_PREAMBLE_2) {
                    rxBuffer[rxIndex++] = byte;
                } else {
                    rxIndex = 0;
                }
            } else if (rxIndex >= 2) {
                rxBuffer[rxIndex++] = byte;
                
                if (byte == ICOM_END || rxIndex >= sizeof(rxBuffer)) {
                    Serial.printf("(len=%d, cmd=0x%02X)\n", rxIndex, rxBuffer[4]);
                    icomProcessMessage(rxBuffer, rxIndex);
                    rxIndex = 0;
                }
            }
        }
    }
    
    // 超时检测
    if (now - icomStatus.last_update > icomCfg.timeout) {
        icomStatus.connected = false;
    }
}

const IcomStatus* icomGetStatus() {
    return &icomStatus;
}

uint32_t icomGetLastFrequency() {
    return (uint32_t)(icomStatus.frequency / 1000000ULL); // 返回 MHz
}

uint32_t icomGetLastUpdateTime() {
    return icomStatus.last_update;
}

bool icomIsConnected() {
    return icomStatus.connected && (millis() - icomStatus.last_update) < icomCfg.timeout;
}