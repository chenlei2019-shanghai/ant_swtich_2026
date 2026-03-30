/**
 * Kenwood CAT 协议驱动
 * 
 * 参考: Hamlib kenwood.c / rigctl
 * 协议: Kenwood CAT (TS-480/570/590/870/990/2000 等)
 * 
 * Kenwood 协议特点:
 * - 文本命令格式 (如 "FA;" 读取频率)
 * - 分号 (;) 结尾
 * - 双向通信, 电台主动上报频率变化
 */

#ifndef KENWOOD_CAT_H
#define KENWOOD_CAT_H

#include <Arduino.h>

// Kenwood 电台型号枚举
enum KenwoodModel {
    KENWOOD_TS480 = 0,   // TS-480SAT/HX
    KENWOOD_TS570,       // TS-570S/D
    KENWOOD_TS590,       // TS-590S/G
    KENWOOD_TS870,       // TS-870S
    KENWOOD_TS990,       // TS-990S
    KENWOOD_TS2000,      // TS-2000
    KENWOOD_TS50,        // TS-50S
    KENWOOD_TS140,       // TS-140S
    KENWOOD_TS440,       // TS-440S
    KENWOOD_TS450,       // TS-450S
    KENWOOD_TS690,       // TS-690S
    KENWOOD_TS850,       // TS-850S
    KENWOOD_TS950,       // TS-950S/DX
    KENWOOD_TM_D710,     // TM-D710
    KENWOOD_TH_D72,      // TH-D72A
    KENWOOD_TH_K20       // TH-K20A
};

// 运行状态
enum KenwoodState {
    KENWOOD_IDLE,
    KENWOOD_WAIT_RESPONSE,
    KENWOOD_RECEIVING
};

// Kenwood 配置结构
struct KenwoodConfig {
    KenwoodModel model;
    uint32_t pollInterval;   // 轮询间隔 (ms)
    uint32_t timeout;        // 超时时间 (ms)
};

// Kenwood 状态结构
struct KenwoodStatus {
    uint64_t frequency;
    uint8_t mode;
    bool ptt;
    int smeter;
    uint8_t rfpwr;
    bool connected;
    uint32_t last_update;
};

// 初始化与控制
void kenwoodInit(const KenwoodConfig* config);
void kenwoodDeinit();
void kenwoodProcess();

// 命令发送
void kenwoodRequestFrequency();
void kenwoodRequestMode();
void kenwoodRequestPTT();
void kenwoodRequestSMeter();
void kenwoodRequestRFPower();

// 状态获取
uint64_t kenwoodGetLastFrequency();
bool kenwoodIsConnected();
const KenwoodStatus* kenwoodGetStatus();

// 工具函数
const char* kenwoodGetModelName(KenwoodModel model);
uint32_t kenwoodParseFrequency(const char* response);
uint8_t kenwoodParseMode(const char* modeStr);
const char* kenwoodModeToStr(uint8_t mode);

#endif // KENWOOD_CAT_H
