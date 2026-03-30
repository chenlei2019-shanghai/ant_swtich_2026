/**
 * Kenwood CAT 协议实现
 * 
 * 参考: Hamlib kenwood.c / rigctl
 * 命令格式: 2字母命令 + 参数 + ';'
 * 
 * 常用命令:
 * - FA; / FAxxxxxxxxx; - VFO A 频率读/写
 * - FB; / FBxxxxxxxxx; - VFO B 频率读/写
 * - MD; / MDx; - 模式读/写
 * - TX; / TXx; - 发射状态/PTT控制
 * - SM; - S表读取
 * - PC; / PCxxx; - 功率读/写
 * - ID; - 电台识别
 * - IF; - 信息查询 (频率/模式/VFO等)
 */

#include "kenwood_cat.h"
#include "cat_hardware.h"

// 命令定义
#define KENWOOD_CMD_FREQ_A  "FA"   // VFO A 频率
#define KENWOOD_CMD_FREQ_B  "FB"   // VFO B 频率
#define KENWOOD_CMD_MODE    "MD"   // 模式
#define KENWOOD_CMD_PTT     "TX"   // 发射状态
#define KENWOOD_CMD_SMETER  "SM"   // S表
#define KENWOOD_CMD_RFPWR   "PC"   // 功率
#define KENWOOD_CMD_INFO    "IF"   // 信息查询
#define KENWOOD_CMD_ID      "ID"   // 电台识别

// 结束符
#define KENWOOD_TERMINATOR  ';'

// 状态变量
static KenwoodConfig kenwoodCfg;
static KenwoodStatus kenwoodStatus;
static KenwoodState kenwoodState = KENWOOD_IDLE;

static char rxBuffer[128];
static uint8_t rxIndex = 0;
static uint32_t lastPollTime = 0;
static uint32_t lastRxTime = 0;

// 轮询命令列表
static const char* pollCommands[] = {
    KENWOOD_CMD_INFO,   // IF; - 获取完整信息
    KENWOOD_CMD_SMETER, // SM; - S表
};
static const uint8_t pollCmdCount = sizeof(pollCommands) / sizeof(pollCommands[0]);
static uint8_t currentPollCmd = 0;

void kenwoodInit(const KenwoodConfig* config) {
    memcpy(&kenwoodCfg, config, sizeof(KenwoodConfig));
    memset(&kenwoodStatus, 0, sizeof(KenwoodStatus));
    kenwoodState = KENWOOD_IDLE;
    rxIndex = 0;
    memset(rxBuffer, 0, sizeof(rxBuffer));
}

void kenwoodDeinit() {
    kenwoodState = KENWOOD_IDLE;
    kenwoodStatus.connected = false;
}

// 发送命令
static void kenwoodSendCommand(const char* cmd) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s%c", cmd, KENWOOD_TERMINATOR);
    catHardwareWrite((const uint8_t*)buffer, strlen(buffer));
    
    Serial.print("[Kenwood] TX: ");
    Serial.println(buffer);
}

// 解析频率响应 (格式: FA014250000;)
uint32_t kenwoodParseFrequency(const char* response) {
    uint32_t freq = 0;
    
    // 查找命令前缀
    if (response[0] != 'F' || (response[1] != 'A' && response[1] != 'B')) {
        return 0;
    }
    
    // 解析数字部分
    const char* p = response + 2;  // 跳过 'FA' 或 'FB'
    while (*p && *p >= '0' && *p <= '9') {
        freq = freq * 10 + (*p - '0');
        p++;
    }
    
    return freq;
}

// 解析模式响应 (格式: MD2;)
uint8_t kenwoodParseMode(const char* modeStr) {
    if (modeStr[0] != 'M' || modeStr[1] != 'D') {
        return 0;
    }
    
    // Kenwood 模式代码
    // 1=LSB, 2=USB, 3=CW, 4=FM, 5=AM, 6=FSK, 7=CWR
    switch (modeStr[2]) {
        case '1': return 0x00;  // LSB
        case '2': return 0x01;  // USB
        case '3': return 0x03;  // CW
        case '4': return 0x05;  // FM
        case '5': return 0x02;  // AM
        case '6': return 0x04;  // RTTY/FSK
        case '7': return 0x07;  // CW-R
        default:  return 0xFF;  // Unknown
    }
}

const char* kenwoodModeToStr(uint8_t mode) {
    switch (mode) {
        case 0x00: return "LSB";
        case 0x01: return "USB";
        case 0x02: return "AM";
        case 0x03: return "CW";
        case 0x04: return "RTTY";
        case 0x05: return "FM";
        case 0x07: return "CW-R";
        default:   return "???";
    }
}

// 解析 IF (信息) 响应
// 格式: IF[f]*****+yyyyrx*00tmvspbd1*;
// f = 频率 (11位)
// +yyyy = RIT/XIT 偏移
// r = RIT on/off
// x = XIT on/off  
// t = TX on/off
// m = 模式
// v = VFO
// s = scan
// p = split
// b = 基本/扩展格式标志
// d = DATA模式
static void kenwoodParseIF(const char* response) {
    // 确保是 IF 响应
    if (response[0] != 'I' || response[1] != 'F') {
        return;
    }
    
    // 解析频率 (位置 2-12, 11位)
    uint32_t freq = 0;
    for (int i = 2; i < 13 && response[i] >= '0' && response[i] <= '9'; i++) {
        freq = freq * 10 + (response[i] - '0');
    }
    kenwoodStatus.frequency = freq;
    
    // 解析 TX 状态 (位置 28, 基于协议文档)
    // IF响应格式: IF014250000*****+000000 0 0 01 0 0 00 0 0;
    // 查找分号前的模式字符
    const char* p = strchr(response, ';');
    if (p && (p - response) > 28) {
        kenwoodStatus.ptt = (response[28] == '1');
    }
    
    kenwoodStatus.last_update = millis();
    kenwoodStatus.connected = true;
}

// 处理接收到的数据
static void kenwoodProcessResponse(const char* response) {
    Serial.print("[Kenwood] RX: ");
    Serial.println(response);
    
    if (strlen(response) < 2) return;
    
    // 根据命令前缀解析
    if (strncmp(response, "FA", 2) == 0 || strncmp(response, "FB", 2) == 0) {
        // 频率响应
        kenwoodStatus.frequency = kenwoodParseFrequency(response);
        kenwoodStatus.last_update = millis();
    }
    else if (strncmp(response, "MD", 2) == 0) {
        // 模式响应
        kenwoodStatus.mode = kenwoodParseMode(response);
    }
    else if (strncmp(response, "TX", 2) == 0) {
        // PTT 状态
        kenwoodStatus.ptt = (response[2] == '1' || response[2] == '2');
    }
    else if (strncmp(response, "SM", 2) == 0) {
        // S表 (格式: SM0140;)
        int sm = 0;
        for (int i = 2; response[i] >= '0' && response[i] <= '9'; i++) {
            sm = sm * 10 + (response[i] - '0');
        }
        kenwoodStatus.smeter = sm;
    }
    else if (strncmp(response, "PC", 2) == 0) {
        // 功率 (格式: PC100;)
        int pwr = 0;
        for (int i = 2; response[i] >= '0' && response[i] <= '9'; i++) {
            pwr = pwr * 10 + (response[i] - '0');
        }
        kenwoodStatus.rfpwr = pwr;
    }
    else if (strncmp(response, "IF", 2) == 0) {
        // 完整信息
        kenwoodParseIF(response);
    }
    else if (strncmp(response, "ID", 2) == 0) {
        // 电台识别响应 (ID017; 表示 TS-2000)
        kenwoodStatus.connected = true;
    }
    
    lastRxTime = millis();
}

void kenwoodProcess() {
    uint32_t now = millis();
    
    // 1. 发送轮询命令
    if (now - lastPollTime >= kenwoodCfg.pollInterval) {
        // 轮询不同命令
        kenwoodSendCommand(pollCommands[currentPollCmd]);
        
        currentPollCmd = (currentPollCmd + 1) % pollCmdCount;
        lastPollTime = now;
        kenwoodState = KENWOOD_WAIT_RESPONSE;
    }
    
    // 2. 接收数据
    while (catHardwareAvailable() > 0) {
        uint8_t byte;
        if (catHardwareRead(&byte, 1) > 0) {
            
            if (byte == KENWOOD_TERMINATOR) {
                // 命令结束
                rxBuffer[rxIndex] = '\0';
                kenwoodProcessResponse(rxBuffer);
                rxIndex = 0;
                memset(rxBuffer, 0, sizeof(rxBuffer));
                kenwoodState = KENWOOD_IDLE;
            }
            else if (rxIndex < sizeof(rxBuffer) - 1) {
                rxBuffer[rxIndex++] = (char)byte;
            }
        }
    }
    
    // 3. 超时检测
    if (kenwoodState == KENWOOD_WAIT_RESPONSE) {
        if (now - lastPollTime > kenwoodCfg.timeout) {
            kenwoodState = KENWOOD_IDLE;
        }
    }
    
    // 4. 连接状态更新
    if (now - lastRxTime > kenwoodCfg.timeout * 3) {
        kenwoodStatus.connected = false;
    }
}

// API 函数
void kenwoodRequestFrequency() {
    kenwoodSendCommand(KENWOOD_CMD_FREQ_A);
}

void kenwoodRequestMode() {
    kenwoodSendCommand(KENWOOD_CMD_MODE);
}

void kenwoodRequestPTT() {
    kenwoodSendCommand(KENWOOD_CMD_PTT);
}

void kenwoodRequestSMeter() {
    kenwoodSendCommand(KENWOOD_CMD_SMETER);
}

void kenwoodRequestRFPower() {
    kenwoodSendCommand(KENWOOD_CMD_RFPWR);
}

uint64_t kenwoodGetLastFrequency() {
    return kenwoodStatus.frequency;
}

bool kenwoodIsConnected() {
    return kenwoodStatus.connected && 
           (millis() - kenwoodStatus.last_update) < kenwoodCfg.timeout * 3;
}

const KenwoodStatus* kenwoodGetStatus() {
    return &kenwoodStatus;
}

const char* kenwoodGetModelName(KenwoodModel model) {
    switch (model) {
        case KENWOOD_TS480: return "TS-480";
        case KENWOOD_TS570: return "TS-570";
        case KENWOOD_TS590: return "TS-590";
        case KENWOOD_TS870: return "TS-870";
        case KENWOOD_TS990: return "TS-990";
        case KENWOOD_TS2000: return "TS-2000";
        case KENWOOD_TS50: return "TS-50";
        case KENWOOD_TS140: return "TS-140";
        case KENWOOD_TS440: return "TS-440";
        case KENWOOD_TS450: return "TS-450";
        case KENWOOD_TS690: return "TS-690";
        case KENWOOD_TS850: return "TS-850";
        case KENWOOD_TS950: return "TS-950";
        case KENWOOD_TM_D710: return "TM-D710";
        case KENWOOD_TH_D72: return "TH-D72";
        case KENWOOD_TH_K20: return "TH-K20";
        default: return "Unknown";
    }
}
