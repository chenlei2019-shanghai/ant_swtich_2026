/**
 * ICOM CI-V 协议驱动 - 增强版
 * 基于 K7MDL2/IC-705-BLE-Serial-Example 项目优化
 */

#ifndef ICOM_CIV_H
#define ICOM_CIV_H

#include <Arduino.h>
#include <stdint.h>

// CI-V 协议常量
#define ICOM_PREAMBLE_1     0xFE
#define ICOM_PREAMBLE_2     0xFE
#define ICOM_END            0xFD
#define ICOM_BROADCAST      0x00

// CI-V 命令定义
#define ICOM_CMD_READ_FREQ      0x03
#define ICOM_CMD_READ_MODE      0x04
#define ICOM_CMD_SET_FREQ       0x05
#define ICOM_CMD_SEND_MODE      0x01
#define ICOM_CMD_SET_MODE       0x06
#define ICOM_CMD_READ_TX        0x1C
#define ICOM_CMD_READ_S_METER   0x15
#define ICOM_CMD_READ_RF_POWER  0x14
#define ICOM_CMD_READ_PREAMp    0x16
#define ICOM_CMD_READ_ATTN      0x11
#define ICOM_CMD_READ_AGC       0x16
#define ICOM_CMD_READ_SPLIT     0x0F
#define ICOM_CMD_READ_RIT       0x21
#define ICOM_CMD_READ_DUPLEX    0x0C
#define ICOM_CMD_READ_RADIo_ID  0x19
#define ICOM_CMD_OK             0xFB
#define ICOM_CMD_NG             0xFA

// 电台地址
#define ICOM_ADDR_CONTROLLER    0xE0
#define ICOM_ADDR_CONTROLLER_ALT 0xE5  // 避免与 wfView/WSJT-X 冲突
#define ICOM_ADDR_IC7300        0x94
#define ICOM_ADDR_IC7610        0x98
#define ICOM_ADDR_IC9700        0xA2
#define ICOM_ADDR_IC705         0xA4
#define ICOM_ADDR_IC905         0xAC
#define ICOM_ADDR_ICR30         0xA6  // IC-R30 接收机 (手册地址)

// 连接类型
enum IcomConnType {
    ICOM_CONN_NONE = 0,
    ICOM_CONN_BLE,      // IC-705 BLE 连接
    ICOM_CONN_BT_CLASSIC, // 蓝牙经典
    ICOM_CONN_USB_HOST, // USB Host 模式
    ICOM_CONN_SERIAL    // 有线串口
};

// 电台型号
enum IcomRadioModel {
    ICOM_MODEL_UNKNOWN = 0,
    ICOM_MODEL_IC705,
    ICOM_MODEL_IC7300,
    ICOM_MODEL_IC7610,
    ICOM_MODEL_IC9700,
    ICOM_MODEL_IC905,
    ICOM_MODEL_ICR30
};

// 运行状态
struct IcomStatus {
    uint64_t frequency;     // 当前频率 (Hz)
    uint32_t freq_hz;       // 频率低32位 (Hz)
    uint8_t mode;           // 模式
    uint8_t filter;         // 滤波器
    uint8_t datamode;       // 数据模式
    uint8_t agc;            // AGC 设置
    uint8_t preamp;         // 前置放大器
    uint8_t atten;          // 衰减器
    uint8_t rfpwr;          // RF 功率 (0-255)
    bool ptt;               // PTT 状态
    bool split;             // 分割模式
    int16_t rit_offset;     // RIT 偏移 (Hz)
    int32_t duplex_offset;  // 双工偏移 (Hz)
    bool connected;         // 连接状态
    uint32_t last_update;   // 最后更新时间
};

// CI-V 配置
struct IcomConfig {
    uint8_t radioAddress;
    uint8_t controllerAddress;
    uint32_t pollInterval;
    uint32_t timeout;
    IcomConnType connType;
};

// 初始化与基本控制
void icomInit(const IcomConfig* config);
void icomDeinit();
void icomProcess();
void icomSetConnType(IcomConnType type);

// 命令发送
void icomSendCommand(uint8_t cmd, const uint8_t* data, uint8_t dataLen);
void icomRequestFrequency();
void icomRequestMode();
void icomRequestPTT();
void icomRequestSMeter();
void icomRequestRFPower();

// 数据解析 (来自 K7MDL2)
uint64_t icomParseFrequencyBCD(const uint8_t* bcdData, uint8_t len);
void icomProcessMessage(const uint8_t* buffer, uint8_t len);
void icomCIV_Action(uint8_t cmd, const uint8_t* data, uint8_t dataLen);

// 状态获取
const IcomStatus* icomGetStatus();
uint32_t icomGetLastFrequency();
uint32_t icomGetLastUpdateTime();
bool icomIsConnected();
IcomConnType icomGetConnType();
IcomRadioModel icomDetectModel(uint8_t address);

// 连接类型字符串
const char* icomGetConnTypeStr(IcomConnType type);
const char* icomGetModelStr(IcomRadioModel model);

#endif