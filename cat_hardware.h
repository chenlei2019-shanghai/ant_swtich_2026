/**
 * CAT 硬件抽象层 - 支持串口和BLE
 */

#ifndef CAT_HARDWARE_H
#define CAT_HARDWARE_H

#include <Arduino.h>
#include <driver/uart.h>

// UART 配置
#define CAT_UART_NUM    UART_NUM_1
// 注意: 引脚定义在 config.h 中 UART1_TX/UART1_RX
// 这里使用 GPIO1/2，避开 SD卡(GPIO4/5)和继电器(GPIO16/17)
#define CAT_TX_PIN      1   // 同 UART1_TX
#define CAT_RX_PIN      2   // 同 UART1_RX
#define CAT_BAUDRATE    9600

// 缓冲区大小
#define CAT_RX_BUF_SIZE 256
#define CAT_TX_BUF_SIZE 256

// 硬件类型
typedef enum {
    CAT_HW_SERIAL,      // 有线串口
    CAT_HW_BLE          // BLE 蓝牙 (IC-705)
} CatHWType;

// 硬件初始化
bool catHardwareInit(uint32_t baudrate);
void catHardwareDeinit();

// 设置/获取硬件类型
void catHardwareSetType(CatHWType type);
CatHWType catHardwareGetType();

// 数据读写
int catHardwareRead(uint8_t* buf, int len);
int catHardwareWrite(const uint8_t* buf, int len);
int catHardwareAvailable();
void catHardwareFlush();

#endif
