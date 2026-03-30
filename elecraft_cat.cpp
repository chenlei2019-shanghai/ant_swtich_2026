/**
 * Elecraft CAT 协议实现
 * 
 * 参考: Elecraft K3/KX3 Programmer's Reference (Rev G5)
 *       Hamlib elecraft.c / kenwood/k3.c
 * 
 * 核心命令 (基于 Kenwood 协议):
 * - FA; / FAxxxxxxxx; - VFO A 频率 (8位, Hz)
 * - FB; / FBxxxxxxxx; - VFO B 频率
 * - MD; / MDx; - 模式 (0=LSB, 1=USB, 2=CW, 3=FM, 4=AM, 5=DATA, 6=CW-R, 9=DATA-R)
 * - TX; / TXx; - 发射控制 (0=RX, 1=TX, 2=TX Tune)
 * - SM; - S表读取 (000-015)
 * - PC; / PCnnn; - 功率设置 (000-110W 或 0-15 表示 QRP)
 * - PO; - 实际输出功率 (KX3 特有)
 * - IF; - 信息查询
 * - OM; - 型号查询
 * - AG; - AF增益
 * - RG; - RF增益
 * - KS; - CW速度
 * - BW; / BWxxxx; - 滤波器带宽 (Hz/10)
 * 
 * K3/KX3 特有命令:
 * - K31; - 启用 K3 扩展模式
 * - AIx; - 自动信息上报 (0=关, 1=部分, 2=全部)
 */

#include "elecraft_cat.h"
#include "cat_hardware.h"
#include <string.h>

// 命令定义
#define E_CMD_FREQ_A    "FA"
#define E_CMD_FREQ_B    "FB"
#define E_CMD_MODE      "MD"
#define E_CMD_PTT       "TX"
#define E_CMD_SMETER    "SM"
#define E_CMD_RFPWR     "PC"
#define E_CMD_ACT_PWR   "PO"    // KX3 实际输出功率
#define E_CMD_INFO      "IF"    // 完整信息
#define E_CMD_MODEL     "OM"    // 型号查询
#define E_CMD_AGC       "GT"    // AGC (0=OFF, 1=FAST, 2=SLOW)
#define E_CMD_PREAMP    "PA"    // 前置放大器 (0=OFF, 1=ON)
#define E_CMD_ATTEN     "RA"    // 衰减器 (0=OFF, 1=ON)
#define E_CMD_AI        "AI"    // 自动信息上报
#define E_CMD_EXT_MODE  "K31"   // K3 扩展模式
#define E_CMD_TERM      ';'

// 状态变量
static ElecraftConfig eleCfg;
static ElecraftStatus eleStatus;
static ElecraftState eleState = ELECRAFT_IDLE;

static char rxBuffer[128];
static uint8_t rxIndex = 0;
static uint32_t lastPollTime = 0;
static uint32_t lastRxTime = 0;

// 轮询命令
static const char* pollCommands[] = {
    E_CMD_INFO,      // IF 获取完整信息
    E_CMD_SMETER,    // S表
};
static const uint8_t pollCmdCount = 2;
static uint8_t currentPollCmd = 0;

void elecraftInit(const ElecraftConfig* config) {
    memcpy(&eleCfg, config, sizeof(ElecraftConfig));
    memset(&eleStatus, 0, sizeof(ElecraftStatus));
    eleState = ELECRAFT_IDLE;
    rxIndex = 0;
    memset(rxBuffer, 0, sizeof(rxBuffer));
    
    // 如果启用扩展模式,发送 K31;
    if (eleCfg.extendedMode) {
        delay(100);  // 等待电台就绪
        char cmd[] = "K31;";
        catHardwareWrite((const uint8_t*)cmd, strlen(cmd));
        
        // 启用自动信息上报
        char ai[] = "AI2;";
        catHardwareWrite((const uint8_t*)ai, strlen(ai));
    }
}

void elecraftDeinit() {
    eleState = ELECRAFT_IDLE;
    eleStatus.connected = false;
}

// 发送命令
static void elecraftSendCommand(const char* cmd) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s%c", cmd, E_CMD_TERM);
    catHardwareWrite((const uint8_t*)buffer, strlen(buffer));
    
    Serial.print("[Elecraft] TX: ");
    Serial.println(buffer);
}

// 解析频率 (格式: FA14250000; - 8位数字, Hz)
static uint32_t parseFrequency(const char* resp) {
    uint32_t freq = 0;
    const char* p = resp + 2;  // 跳过 'FA' 或 'FB'
    
    while (*p && *p >= '0' && *p <= '9') {
        freq = freq * 10 + (*p - '0');
        p++;
    }
    return freq;
}

// 解析模式
static uint8_t parseMode(const char* resp) {
    if (resp[0] != 'M' || resp[1] != 'D') return 0xFF;
    
    char modeChar = resp[2];
    // Elecraft 模式: 0=LSB, 1=USB, 2=CW, 3=FM, 4=AM, 5=DATA, 6=CW-R, 7=DATA-R
    switch (modeChar) {
        case '0': return 0x00;  // LSB
        case '1': return 0x01;  // USB
        case '2': return 0x03;  // CW
        case '3': return 0x05;  // FM
        case '4': return 0x02;  // AM
        case '5': return 0x04;  // DATA/RTTY
        case '6': return 0x07;  // CW-R
        case '7': return 0x08;  // DATA-R
        default:  return 0xFF;
    }
}

const char* elecraftModeToStr(uint8_t mode) {
    switch (mode) {
        case 0x00: return "LSB";
        case 0x01: return "USB";
        case 0x02: return "AM";
        case 0x03: return "CW";
        case 0x04: return "DATA";
        case 0x05: return "FM";
        case 0x07: return "CW-R";
        case 0x08: return "DATA-R";
        default:   return "???";
    }
}

// 解析 IF (信息) 响应
// 格式: IF[f]*****+yyyyrx*00tmvspbd1*;
// Elecraft IF 响应与 Kenwood 相同
static void parseIF(const char* resp) {
    if (resp[0] != 'I' || resp[1] != 'F') return;
    
    // 解析频率 (位置 2-9, 8位)
    uint32_t freq = 0;
    for (int i = 2; i < 10 && resp[i] >= '0' && resp[i] <= '9'; i++) {
        freq = freq * 10 + (resp[i] - '0');
    }
    eleStatus.frequencyA = freq;
    
    // 解析 TX 状态 (位置 26, 基于 K3 文档)
    // IF响应长度约为 38 字节
    const char* semi = strchr(resp, ';');
    if (semi && (semi - resp) >= 27) {
        eleStatus.ptt = (resp[26] == '1');
    }
    
    eleStatus.last_update = millis();
    eleStatus.connected = true;
}

// 处理接收数据
static void processResponse(const char* resp) {
    Serial.print("[Elecraft] RX: ");
    Serial.println(resp);
    
    if (strlen(resp) < 2) return;
    
    if (strncmp(resp, "FA", 2) == 0) {
        eleStatus.frequencyA = parseFrequency(resp);
        eleStatus.last_update = millis();
    }
    else if (strncmp(resp, "FB", 2) == 0) {
        eleStatus.frequencyB = parseFrequency(resp);
    }
    else if (strncmp(resp, "MD", 2) == 0) {
        eleStatus.mode = parseMode(resp);
    }
    else if (strncmp(resp, "TX", 2) == 0) {
        eleStatus.ptt = (resp[2] == '1' || resp[2] == '2');
    }
    else if (strncmp(resp, "SM", 2) == 0) {
        // S表 (格式: SM0140;)
        int sm = 0;
        for (int i = 2; resp[i] >= '0' && resp[i] <= '9'; i++) {
            sm = sm * 10 + (resp[i] - '0');
        }
        eleStatus.smeter = sm;
    }
    else if (strncmp(resp, "PC", 2) == 0) {
        // 功率 (格式: PC100;)
        int pwr = 0;
        for (int i = 2; resp[i] >= '0' && resp[i] <= '9'; i++) {
            pwr = pwr * 10 + (resp[i] - '0');
        }
        eleStatus.rfpwr = pwr;
    }
    else if (strncmp(resp, "PO", 2) == 0) {
        // 实际输出功率 (KX3 特有)
        int pwr = 0;
        for (int i = 2; resp[i] >= '0' && resp[i] <= '9'; i++) {
            pwr = pwr * 10 + (resp[i] - '0');
        }
        eleStatus.actualPwr = pwr;
    }
    else if (strncmp(resp, "IF", 2) == 0) {
        parseIF(resp);
    }
    else if (strncmp(resp, "OM", 2) == 0) {
        // 型号响应 (OM0; = K2, OM1; = K3, OM2; = KX3)
        eleStatus.connected = true;
    }
    
    lastRxTime = millis();
}

void elecraftProcess() {
    uint32_t now = millis();
    
    // 发送轮询
    if (now - lastPollTime >= eleCfg.pollInterval) {
        elecraftSendCommand(pollCommands[currentPollCmd]);
        
        currentPollCmd = (currentPollCmd + 1) % pollCmdCount;
        lastPollTime = now;
        eleState = ELECRAFT_WAIT_RESPONSE;
    }
    
    // 接收数据
    while (catHardwareAvailable() > 0) {
        uint8_t byte;
        if (catHardwareRead(&byte, 1) > 0) {
            
            if (byte == E_CMD_TERM) {
                rxBuffer[rxIndex] = '\0';
                processResponse(rxBuffer);
                rxIndex = 0;
                memset(rxBuffer, 0, sizeof(rxBuffer));
                eleState = ELECRAFT_IDLE;
            }
            else if (rxIndex < sizeof(rxBuffer) - 1) {
                rxBuffer[rxIndex++] = (char)byte;
            }
        }
    }
    
    // 超时检测
    if (eleState == ELECRAFT_WAIT_RESPONSE) {
        if (now - lastPollTime > eleCfg.timeout) {
            eleState = ELECRAFT_IDLE;
        }
    }
    
    // 连接状态
    if (now - lastRxTime > eleCfg.timeout * 3) {
        eleStatus.connected = false;
    }
}

// API 实现
void elecraftRequestFrequency() {
    elecraftSendCommand(E_CMD_FREQ_A);
}

void elecraftRequestFrequencyB() {
    elecraftSendCommand(E_CMD_FREQ_B);
}

void elecraftRequestMode() {
    elecraftSendCommand(E_CMD_MODE);
}

void elecraftRequestPTT() {
    elecraftSendCommand(E_CMD_PTT);
}

void elecraftRequestSMeter() {
    elecraftSendCommand(E_CMD_SMETER);
}

void elecraftRequestRFPower() {
    elecraftSendCommand(E_CMD_RFPWR);
}

void elecraftRequestInfo() {
    elecraftSendCommand(E_CMD_INFO);
}

uint64_t elecraftGetLastFrequency() {
    return eleStatus.frequencyA;
}

bool elecraftIsConnected() {
    return eleStatus.connected && 
           (millis() - eleStatus.last_update) < eleCfg.timeout * 3;
}

const ElecraftStatus* elecraftGetStatus() {
    return &eleStatus;
}

const char* elecraftGetModelName(ElecraftModel model) {
    switch (model) {
        case ELECRAFT_K2:  return "K2";
        case ELECRAFT_K3:  return "K3/K3S";
        case ELECRAFT_KX2: return "KX2";
        case ELECRAFT_KX3: return "KX3";
        default:           return "Unknown";
    }
}
