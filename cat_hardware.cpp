/**
 * CAT 硬件抽象层实现 - 支持串口、BLE
 * 
 * 修改记录:
 * - 2026-03-28: 移除重复的BLE缓冲区定义,使用ble_civ.cpp提供的接口
 * - 2026-03-30: 删除HC-05支持,简化代码
 */

#include "cat_hardware.h"
#include "ble_civ.h"

static bool hwInitialized = false;
static CatHWType hwType = CAT_HW_SERIAL;

bool catHardwareInit(uint32_t baudrate) {
    if (hwInitialized) return true;
    
    uart_config_t uart_config = {
        .baud_rate = (int)baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_driver_install(CAT_UART_NUM, CAT_RX_BUF_SIZE, CAT_TX_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) return false;
    
    ret = uart_param_config(CAT_UART_NUM, &uart_config);
    if (ret != ESP_OK) return false;
    
    ret = uart_set_pin(CAT_UART_NUM, CAT_TX_PIN, CAT_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return false;
    
    hwInitialized = true;
    hwType = CAT_HW_SERIAL;
    return true;
}

void catHardwareDeinit() {
    if (!hwInitialized) return;
    uart_driver_delete(CAT_UART_NUM);
    hwInitialized = false;
}

void catHardwareSetType(CatHWType type) {
    hwType = type;
}

CatHWType catHardwareGetType() {
    return hwType;
}

int catHardwareRead(uint8_t* buf, int len) {
    if (!hwInitialized) return 0;
    
    switch (hwType) {
        case CAT_HW_BLE:
            return bleCivRead(buf, len);
        case CAT_HW_SERIAL:
        default:
            return uart_read_bytes(CAT_UART_NUM, buf, len, 0);
    }
}

int catHardwareWrite(const uint8_t* buf, int len) {
    if (!hwInitialized) return 0;
    
    switch (hwType) {
        case CAT_HW_BLE:
            return bleCivWrite(buf, len) ? len : 0;
        case CAT_HW_SERIAL:
        default:
            return uart_write_bytes(CAT_UART_NUM, (const char*)buf, len);
    }
}

int catHardwareAvailable() {
    if (!hwInitialized) return 0;
    
    switch (hwType) {
        case CAT_HW_BLE:
            return bleCivAvailable();
        case CAT_HW_SERIAL:
        default:
            {
                size_t uartAvailable;
                uart_get_buffered_data_len(CAT_UART_NUM, &uartAvailable);
                return (int)uartAvailable;
            }
    }
}

void catHardwareFlush() {
    if (!hwInitialized) return;
    
    switch (hwType) {
        case CAT_HW_BLE:
            // BLE缓冲区由ble_civ.cpp管理，通过读取并丢弃数据来清空
            while (bleCivAvailable() > 0) {
                uint8_t dummy;
                bleCivRead(&dummy, 1);
            }
            break;
        case CAT_HW_SERIAL:
        default:
            uart_flush(CAT_UART_NUM);
            break;
    }
}
