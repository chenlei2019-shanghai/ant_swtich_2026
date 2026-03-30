/**
 * HC-05 蓝牙 CAT 协议实现
 * 使用 UART2 (GPIO1/2) 与 HC-05 模块通信
 * 支持经典蓝牙 SPP 协议
 */

#ifndef BT_CAT_H
#define BT_CAT_H

#include <Arduino.h>

// HC-05 UART 配置
#define BT_UART_NUM     2
#define BT_TX_PIN       1   // ESP32 TX → HC-05 RX
#define BT_RX_PIN       2   // ESP32 RX ← HC-05 TX

// 波特率
#define AT_BAUD_RATE    38400   // AT 指令模式
#define BT_BAUD_RATE    9600    // 蓝牙透传模式

// 蓝牙 CAT 状态
typedef enum {
    BT_CAT_DISCONNECTED,
    BT_CAT_CONNECTING,
    BT_CAT_CONNECTED
} BTCatState;

// 蓝牙 CAT 配置
typedef struct {
    char deviceName[32];    // 目标设备名称
    char deviceAddr[20];    // 蓝牙地址
    char pinCode[8];        // 配对密码
} BTCatConfig;

// 初始化/反初始化
void btCatInit(const BTCatConfig* config);
void btCatDeinit();

// 连接管理
bool btCatConnect(const char* deviceName);
void btCatDisconnect();
bool btCatIsConnected();
BTCatState btCatGetState();

// 数据读写 (供 cat_hardware 调用)
int btCatRead(uint8_t* buf, int len);
int btCatWrite(const uint8_t* buf, int len);
int btCatAvailable();
void btCatFlush();

// 数据处理
void btCatProcess();
uint32_t btCatGetLastFrequency();
const char* btCatGetConnectedDevice();

// AT 指令模式
bool btCatEnterATMode();
void btCatExitATMode();
bool btCatIsATMode();
String btCatSendATCommand(const char* cmd);

// AT 指令快捷操作
bool btCatSetMasterMode();
bool btCatSetSlaveMode();
String btCatSearchDevices();
bool btCatConnectToAddress(const char* addr);
bool btCatDisconnectLink();
bool btCatSetPinCode(const char* pin);
bool btCatSetDeviceName(const char* name);

#endif