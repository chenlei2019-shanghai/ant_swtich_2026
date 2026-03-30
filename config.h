/**
 * config.h - 全局配置
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define VERSION "2.1.0"

// 继电器引脚
#define PIN_RESET 16
#define PIN_CH1   17
#define PIN_CH2   18
#define PIN_CH3   21
#define PIN_CH4   15
#define PIN_CH5   40
#define PIN_CH6   41

// 按键
#define PIN_BUTTON 3

// VFD数码管 (74HC595级联)
#define DISP_DATA   42
#define DISP_CLK    39
#define DISP_LATCH  38

// UART1 - CAT协议 (ICOM/YAESU) - 使用GPIO1/2
// 说明: 避开GPIO4/5(SD卡)和GPIO16/17(继电器),使用安全的GPIO1/2
#define UART1_TX    1   // GPIO1 - CAT协议发送
#define UART1_RX    2   // GPIO2 - CAT协议接收

// UART2 - 保留/扩展接口 (BLE已改用内置蓝牙,此串口可用于其他用途)
// 注意: 如不使用SD卡,可将设备接至GPIO4/5使用此串口
#define UART2_TX    4   // GPIO4 (与SD卡CS冲突,如用SD卡则不可用)
#define UART2_RX    5   // GPIO5 (与SD卡MISO冲突,如用SD卡则不可用)

// W5500以太网
#define ETH_MOSI  11
#define ETH_MISO  12
#define ETH_SCK   13
#define ETH_CS    14
#define ETH_RST   9
#define ETH_INT   10

// 参数
#define PULSE_WIDTH       100
#define RESET_PULSE_WIDTH 800
#define DEBOUNCE_DELAY    200
#define AUTO_SWITCH_MS    60000

// NTP
#define NTP_SERVER1 "ntp.aliyun.com"
#define NTP_SERVER2 "ntp1.aliyun.com"
#define GMT_OFFSET_SEC 28800

// 默认网络
#define DEFAULT_IP      "192.168.1.123"
#define DEFAULT_GATEWAY "192.168.1.1"
#define DEFAULT_SUBNET  "255.255.255.0"
#define DEFAULT_DNS     "8.8.8.8"

// 数码管段码 (共阳)
const uint8_t SEG_MAP[] = {
  B11000000, B11111001, B10100100, B10110000,
  B10011001, B10010010, B10000010, B11111000,
  B10000000, B10010000
};

#define CHR_O  B11000000
#define CHR_P  B10001100
#define CHR_E  B10000110
#define CHR_N  B11101100
#define CHR_C  B11000110
#define CHR_H  B10001001
#define CHR_D  B10100001
#define CHR_B  B10000011
#define CHR_T  B01110001
#define CHR_BAR B11111110
#define CHR_DP  B01111111
#define CHR_BLK B11111111

// 显示模式
enum DispMode { MODE_ANTENNA, MODE_TIME };

#endif
