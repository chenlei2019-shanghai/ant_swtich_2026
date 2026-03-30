/**
 * Elecraft CAT 协议驱动
 * 
 * 参考: Elecraft K3/KX3 Programmer's Reference
 *       Hamlib elecraft.c / kenwood/k3.c
 * 
 * Elecraft 协议特点:
 * - 基于 Kenwood CAT 协议 (兼容 TS-2000 子集)
 * - 文本命令格式, 分号 (;) 结尾
 * - 支持扩展命令 (K2, K22, K3, K31 模式)
 * - K3 和 KX3 使用相同的命令集
 */

#ifndef ELECRAFT_CAT_H
#define ELECRAFT_CAT_H

#include <Arduino.h>

// Elecraft 电台型号
enum ElecraftModel {
    ELECRAFT_K2 = 0,     // K2 (KIO2)
    ELECRAFT_K3,         // K3 / K3S
    ELECRAFT_KX2,        // KX2
    ELECRAFT_KX3         // KX3
};

// 运行状态
enum ElecraftState {
    ELECRAFT_IDLE,
    ELECRAFT_WAIT_RESPONSE
};

// Elecraft 配置
struct ElecraftConfig {
    ElecraftModel model;
    uint32_t pollInterval;
    uint32_t timeout;
    bool extendedMode;  // 使用扩展命令格式
};

// Elecraft 状态
struct ElecraftStatus {
    uint64_t frequencyA;  // VFO A 频率
    uint64_t frequencyB;  // VFO B 频率
    uint8_t mode;
    bool ptt;
    int smeter;
    uint8_t rfpwr;        // 设定功率
    uint8_t actualPwr;    // 实际输出功率 (KX3)
    uint8_t agc;          // AGC 状态
    uint8_t preamp;       // 前置放大器
    uint8_t atten;        // 衰减器
    bool connected;
    uint32_t last_update;
};

// 初始化与控制
void elecraftInit(const ElecraftConfig* config);
void elecraftDeinit();
void elecraftProcess();

// 命令发送
void elecraftRequestFrequency();
void elecraftRequestFrequencyB();
void elecraftRequestMode();
void elecraftRequestPTT();
void elecraftRequestSMeter();
void elecraftRequestRFPower();
void elecraftRequestInfo();      // IF 命令获取完整信息

// 状态获取
uint64_t elecraftGetLastFrequency();
bool elecraftIsConnected();
const ElecraftStatus* elecraftGetStatus();

// 工具函数
const char* elecraftGetModelName(ElecraftModel model);
const char* elecraftModeToStr(uint8_t mode);

#endif // ELECRAFT_CAT_H
