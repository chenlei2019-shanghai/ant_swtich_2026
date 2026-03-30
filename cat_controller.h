/**
 * CAT 控制器主模块 - 增强版
 * 整合 K7MDL2 的 CI-V 处理逻辑
 * 
 * 改进记录 2026-03-28:
 * - 添加分层连接状态管理
 * - 添加详细连接状态查询
 */

#ifndef CAT_CONTROLLER_H
#define CAT_CONTROLLER_H

#include <Arduino.h>
#include "icom_civ.h"

enum CatProtocolType {
    CAT_PROTO_NONE,
    CAT_PROTO_ICOM,
    CAT_PROTO_YAESU,
    CAT_PROTO_KENWOOD,    // 新增: Kenwood (参考 Hamlib)
    CAT_PROTO_ELECRAFT,   // 新增: Elecraft K3/KX3 (参考 Elecraft Programmer's Reference)
    CAT_PROTO_FLEXRADIO
};

// ICOM 配置
struct IcomCatConfig {
    uint8_t radioAddress;
    uint32_t baudrate;
    IcomConnType connType;  // 连接类型：串口/BLE/蓝牙经典
};

// YAESU 配置
struct YaesuCatConfig {
    uint8_t model;
    uint32_t baudrate;
};

// Kenwood 配置
struct KenwoodCatConfig {
    uint8_t model;       // KenwoodModel 枚举
    uint32_t baudrate;
};

// Elecraft 配置
struct ElecraftCatConfig {
    uint8_t model;       // ElecraftModel 枚举
    uint32_t baudrate;
    bool extendedMode;   // 启用 K31 扩展模式
};

// FlexRadio 配置
struct FlexCatConfig {
    IPAddress radioIP;
    bool autoDiscover;
};

// CAT 控制器配置
struct CatControllerConfig {
    CatProtocolType type;
    bool autoSwitch;
    uint32_t switchDelay;
    IcomCatConfig icom;
    YaesuCatConfig yaesu;
    KenwoodCatConfig kenwood;    // 新增
    ElecraftCatConfig elecraft;  // 新增
    FlexCatConfig flex;
};

// CAT 控制器状态
struct CatControllerStatus {
    bool connected;
    uint32_t frequency;
    uint32_t lastFrequency;
    bool ptt;
    uint8_t mode;
    uint8_t rfpwr;
    uint8_t smeter;
    IcomConnType connType;
};

// 改进2: 新增 - 整体连接状态枚举
typedef enum {
    CAT_CONN_DISCONNECTED = 0,   // 完全断开
    CAT_CONN_BLE_ONLY = 1,       // BLE 已连接但 CI-V 未授权
    CAT_CONN_FULL = 2            // 完全连接并授权
} CatConnState;

// 改进2: 新增 - 详细连接状态结构
typedef struct {
    bool initialized;            // 控制器是否初始化
    CatProtocolType protocol;    // 当前协议类型
    IcomConnType icomConnType;   // ICOM 连接类型 (BLE/串口等)
    CatConnState overallState;   // 整体连接状态
    // BLE 特有状态
    bool bleConnected;           // BLE 层连接状态
    bool civAuthorized;          // CI-V 授权状态
} CatConnDetail;

// 初始化与控制
bool catControllerInit(const CatControllerConfig* config);
void catControllerDeinit();
void catControllerProcess();

// 频率与波段
uint32_t catControllerGetFrequency();
uint32_t catControllerGetLastFrequency();
const char* catControllerGetBandName();
uint8_t catControllerGetRecommendedChannel();

// 状态获取
bool catControllerIsConnected();
const CatControllerStatus* catControllerGetStatus();
// 改进2: 新增 - 获取详细连接状态
CatConnDetail catControllerGetConnDetail();
bool catControllerGetPTT();
uint8_t catControllerGetMode();
uint8_t catControllerGetRFPower();
const char* catControllerGetConnTypeStr();

// 设置
void catControllerSetAutoSwitch(bool enable);
bool catControllerGetAutoSwitch();
CatProtocolType catControllerGetProtocol();
void catControllerSetConnType(IcomConnType type);

#endif