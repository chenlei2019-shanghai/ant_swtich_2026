// BLE CI-V Driver for ICOM IC-705
// Based on K7MDL2/IC-705-BLE-Serial-Example
//
// 改进记录 2026-03-28:
// - 添加数据就绪标志防止数据竞争
// - 添加分层连接状态管理

#ifndef BLE_CIV_H
#define BLE_CIV_H

#include <Arduino.h>

// 改进2: 分层连接状态枚举
typedef enum {
    BLE_STATUS_DISCONNECTED = 0,         // 未连接
    BLE_STATUS_CONNECTED_NO_CIV = 1,     // BLE 已连接但 CI-V 未授权
    BLE_STATUS_FULLY_OPERATIONAL = 2     // 完全连接并授权
} BleConnStatus;

// IC-705 Custom Service UUID (from K7MDL2)
// This is the UUID advertised by IC-705 for BLE serial connection
#define IC705_SERVICE_UUID      "14cf8001-1ec2-d408-1b04-2eb270f14203"

// IC-705 Characteristic UUID (RX/TX use the same UUID - half duplex like CI-V bus)
#define IC705_CHAR_UUID         "14cf8002-1ec2-d408-1b04-2eb270f14203"

// Initialize BLE
bool bleCivInit(const char* deviceName);
void bleCivDeinit();

// Scan and connect
String bleCivScanDevices(uint32_t timeoutMs);
bool bleCivConnect(const char* deviceName);
void bleCivDisconnect();
bool bleCivIsConnected();
bool bleCivIsPaired();  // Check if pairing completed

// 改进2: 新增 - 检查 CI-V 授权状态
bool bleCivIsAuthorized();
// 改进2: 新增 - 获取详细连接状态
BleConnStatus bleCivGetStatus();

// Data transfer
bool bleCivWrite(const uint8_t* data, size_t length);
int bleCivRead(uint8_t* buf, int len);
int bleCivAvailable();
void bleCivSetCallback(void (*callback)(const uint8_t* data, size_t length));
void bleCivProcess();

// 改进1: 新增 - 数据就绪标志接口
bool bleCivDataReady();                    // 检查是否有新消息
int bleCivGetMessage(uint8_t* buf, int maxLen);  // 获取完整消息 (非阻塞)

// Status
String bleCivGetConnectedDevice();
int bleCivGetRSSI();

// 自动重连功能
void bleCivSaveLastDevice(const char* deviceAddr);   // 保存上次连接的设备
String bleCivGetLastDevice();                        // 获取上次连接的设备地址
bool bleCivAutoConnect(uint32_t scanTimeoutMs);      // 自动扫描并连接上次设备
bool bleCivIsAutoReconnectEnabled();                 // 检查是否启用自动重连
void bleCivSetAutoReconnect(bool enable);            // 设置自动重连开关
void bleCivClearLastDevice();                        // 清除保存的设备

#endif