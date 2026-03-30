/**
 * 配置验证实现
 */

#include "config_validator.h"
#include "logger.h"
#include "yaesu_cat.h"
#include "kenwood_cat.h"
#include "elecraft_cat.h"
#include <cstring>

// ESP32-S3 可用GPIO列表 (排除内部使用和冲突引脚)
// 参考: config.h 中的引脚定义
static const uint8_t validGPIOs[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48
};

// 已占用的GPIO (根据 config.h)
static const uint8_t usedGPIOs[] = {
    1, 2,      // UART1 TX/RX
    9, 10, 11, 12, 13, 14,  // Ethernet W5500
    15, 16, 17, 18, 21,     // 继电器
    38, 39, 40, 41, 42      // VFD显示
};

// 标准波特率
static const uint32_t standardBaudrates[] = {
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400
};

// 有效的CI-V地址 (ICOM标准)
static const uint8_t validCIVAddrs[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0E, 0x0F,
    0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
    0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B,
    0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
    0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
    0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
    0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C,
    0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
    0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0,
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
    0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2,
    0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4,
    0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD,
    0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
    0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
    0xFB, 0xFC, 0xFD, 0xFE
};

static bool isValidCIVAddr(uint8_t addr) {
    for (size_t i = 0; i < sizeof(validCIVAddrs); i++) {
        if (validCIVAddrs[i] == addr) return true;
    }
    return false;
}

static bool isStandardBaudrate(uint32_t baud) {
    for (size_t i = 0; i < sizeof(standardBaudrates)/sizeof(standardBaudrates[0]); i++) {
        if (standardBaudrates[i] == baud) return true;
    }
    return false;
}

static uint32_t findNearestBaudrate(uint32_t baud) {
    uint32_t nearest = standardBaudrates[0];
    uint32_t minDiff = abs((int32_t)(baud - nearest));
    
    for (size_t i = 1; i < sizeof(standardBaudrates)/sizeof(standardBaudrates[0]); i++) {
        uint32_t diff = abs((int32_t)(baud - standardBaudrates[i]));
        if (diff < minDiff) {
            minDiff = diff;
            nearest = standardBaudrates[i];
        }
    }
    return nearest;
}

ValidationResult validateIP(const char* ip) {
    ValidationResult result;
    
    if (!ip || strlen(ip) == 0) {
        result.setError("IP地址为空", "请输入有效的IP地址，例如：192.168.1.100");
        return result;
    }
    
    int a, b, c, d;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        result.setError("IP地址格式错误", "请使用 xxx.xxx.xxx.xxx 格式");
        return result;
    }
    
    if (a < 0 || a > 255 || b < 0 || b > 255 || 
        c < 0 || c > 255 || d < 0 || d > 255) {
        result.setError("IP地址数值超出范围", "每个字段必须在 0-255 之间");
        return result;
    }
    
    // 检查特殊地址
    if (a == 0) {
        result.setError("IP地址无效", "0.x.x.x 不是有效的IP地址");
        return result;
    }
    
    if (a == 127) {
        result.setWarn("使用回环地址", "127.x.x.x 仅用于本地测试");
    }
    
    if (a == 255 && b == 255 && c == 255 && d == 255) {
        result.setError("广播地址无效", "不能设置为广播地址");
        return result;
    }
    
    return result;
}

ValidationResult validateBaudrate(uint32_t baud, bool allowAuto) {
    ValidationResult result;
    
    if (baud == 0) {
        if (allowAuto) {
            result.setFixed("波特率设为0，将使用自动检测");
            return result;
        }
        result.setError("波特率不能为0", "请选择有效的波特率");
        return result;
    }
    
    if (!isStandardBaudrate(baud)) {
        uint32_t nearest = findNearestBaudrate(baud);
        char msg[128];
        snprintf(msg, sizeof(msg), "非标准波特率 %lu，建议改为 %lu", baud, nearest);
        result.setError(msg);
        return result;
    }
    
    // 检查过高波特率
    if (baud > 230400) {
        result.setWarn("波特率较高", "高波特率可能导致通信不稳定，建议使用115200或更低");
    }
    
    return result;
}

ValidationResult validateCIVAddress(uint8_t addr) {
    ValidationResult result;
    
    if (addr == 0x00) {
        result.setError("CI-V地址不能为0", "请使用有效的ICOM设备地址");
        return result;
    }
    
    if (!isValidCIVAddr(addr)) {
        result.setError("无效的CI-V地址", "请查阅ICOM手册获取正确的设备地址");
        return result;
    }
    
    // 特殊地址警告
    if (addr == 0xE0) {
        result.setWarn("使用广播地址", "0xE0是广播地址，可能导致通信冲突");
    }
    
    return result;
}

ValidationResult validateGPIO(uint8_t pin) {
    ValidationResult result;
    
    // 检查是否为有效GPIO
    bool valid = false;
    for (size_t i = 0; i < sizeof(validGPIOs); i++) {
        if (validGPIOs[i] == pin) {
            valid = true;
            break;
        }
    }
    
    if (!valid) {
        result.setError("无效的GPIO引脚", "ESP32-S3不支持此引脚号");
        return result;
    }
    
    // 检查是否已被占用
    for (size_t i = 0; i < sizeof(usedGPIOs); i++) {
        if (usedGPIOs[i] == pin) {
            char msg[128];
            snprintf(msg, sizeof(msg), "GPIO%d已被系统使用", pin);
            result.setError(msg, "请查阅config.h中的引脚定义");
            return result;
        }
    }
    
    // 保留引脚警告
    if (pin >= 33 && pin <= 37) {
        result.setWarn("保留引脚", "GPIO33-37为ESP32-S3内部使用");
    }
    
    if (pin == 43 || pin == 44) {
        result.setWarn("USB引脚", "GPIO43/44用于USB，使用可能导致调试困难");
    }
    
    return result;
}

ValidationResult validateCatConfig(const CatControllerConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("配置指针为空");
        return result;
    }
    
    switch (cfg->type) {
        case CAT_PROTO_ICOM:
            return validateIcomConfig(&cfg->icom);
            
        case CAT_PROTO_YAESU:
            return validateYaesuConfig(&cfg->yaesu);
            
        case CAT_PROTO_KENWOOD:
            return validateKenwoodConfig(&cfg->kenwood);
            
        case CAT_PROTO_ELECRAFT:
            return validateElecraftConfig(&cfg->elecraft);
            
        case CAT_PROTO_FLEXRADIO:
            return validateFlexConfig(&cfg->flex);
            
        case CAT_PROTO_NONE:
            result.setWarn("CAT协议未启用");
            return result;
            
        default:
            result.setError("未知的CAT协议类型");
            return result;
    }
}

ValidationResult validateIcomConfig(const IcomCatConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("ICOM配置为空");
        return result;
    }
    
    // 验证CI-V地址
    auto addrResult = validateCIVAddress(cfg->radioAddress);
    if (!addrResult.valid) return addrResult;
    
    // 有线/HC-05模式需要验证波特率
    if (cfg->connType == ICOM_CONN_SERIAL || cfg->connType == ICOM_CONN_BT_CLASSIC) {
        auto baudResult = validateBaudrate(cfg->baudrate);
        if (!baudResult.valid) return baudResult;
    }
    
    // BLE模式建议波特率
    if (cfg->connType == ICOM_CONN_BLE && cfg->baudrate != 115200) {
        result.setWarn("BLE模式建议使用115200波特率");
    }
    
    return result;
}

ValidationResult validateYaesuConfig(const YaesuCatConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("YAESU配置为空");
        return result;
    }
    
    auto baudResult = validateBaudrate(cfg->baudrate);
    if (!baudResult.valid) return baudResult;
    
    // 验证型号范围 (YaesuModel 枚举目前只有5个值: FT991, FT991A, FTDX101, FTDX10, FT891)
    if (cfg->model > YAESU_FT891) {
        result.setError("无效的YAESU型号", "请选择支持的电台型号");
        return result;
    }
    
    return result;
}

ValidationResult validateKenwoodConfig(const KenwoodCatConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("Kenwood配置为空");
        return result;
    }
    
    auto baudResult = validateBaudrate(cfg->baudrate);
    if (!baudResult.valid) return baudResult;
    
    // Kenwood通常使用9600
    if (cfg->baudrate != 9600) {
        result.setWarn("Kenwood电台通常使用9600波特率");
    }
    
    return result;
}

ValidationResult validateElecraftConfig(const ElecraftCatConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("Elecraft配置为空");
        return result;
    }
    
    auto baudResult = validateBaudrate(cfg->baudrate);
    if (!baudResult.valid) return baudResult;
    
    // Elecraft通常使用38400
    if (cfg->baudrate < 38400) {
        result.setWarn("Elecraft建议使用38400或更高波特率");
    }
    
    return result;
}

ValidationResult validateFlexConfig(const FlexCatConfig* cfg) {
    ValidationResult result;
    
    if (!cfg) {
        result.setError("FlexRadio配置为空");
        return result;
    }
    
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", 
             cfg->radioIP[0], cfg->radioIP[1], 
             cfg->radioIP[2], cfg->radioIP[3]);
    
    return validateIP(ipStr);
}

ValidationResult validateAntennaChannel(uint8_t ch) {
    ValidationResult result;
    
    if (ch > 6) {
        result.setError("天线通道超出范围", "通道号必须在 0-6 之间 (0=断开)");
        return result;
    }
    
    return result;
}

ValidationResult validateFrequencyRange(uint64_t min, uint64_t max) {
    ValidationResult result;
    
    if (min == 0 || max == 0) {
        result.setError("频率不能为0");
        return result;
    }
    
    if (min >= max) {
        result.setError("频率范围无效", "最小频率必须小于最大频率");
        return result;
    }
    
    // 检查合理范围 (射频范围)
    if (min < 1000) { // < 1kHz
        result.setWarn("频率过低", "确认这是正确的射频频率吗？");
    }
    
    if (max > 100000000000ULL) { // > 100GHz
        result.setWarn("频率极高", "超出常规业余无线电范围");
    }
    
    return result;
}

bool autoFixCatConfig(CatControllerConfig* cfg, ValidationResult* result) {
    if (!cfg || !result) return false;
    
    bool fixed = false;
    
    // 修复波特率
    if (cfg->type == CAT_PROTO_YAESU || cfg->type == CAT_PROTO_KENWOOD ||
        cfg->type == CAT_PROTO_ELECRAFT) {
        
        uint32_t* baudPtr = nullptr;
        switch (cfg->type) {
            case CAT_PROTO_YAESU:   baudPtr = &cfg->yaesu.baudrate; break;
            case CAT_PROTO_KENWOOD: baudPtr = &cfg->kenwood.baudrate; break;
            case CAT_PROTO_ELECRAFT: baudPtr = &cfg->elecraft.baudrate; break;
            default: break;
        }
        
        if (baudPtr && *baudPtr == 0) {
            // 设置默认值
            switch (cfg->type) {
                case CAT_PROTO_YAESU:   *baudPtr = 9600; break;
                case CAT_PROTO_KENWOOD: *baudPtr = 9600; break;
                case CAT_PROTO_ELECRAFT: *baudPtr = 38400; break;
                default: break;
            }
            result->setFixed("波特率设为默认值");
            fixed = true;
        }
    }
    
    // 修复CI-V地址
    if (cfg->type == CAT_PROTO_ICOM && cfg->icom.radioAddress == 0) {
        cfg->icom.radioAddress = 0xA4; // IC-705默认
        result->setFixed("CI-V地址设为默认(0xA4)");
        fixed = true;
    }
    
    return fixed;
}

const uint32_t* getStandardBaudrates(int* count) {
    if (count) {
        *count = sizeof(standardBaudrates) / sizeof(standardBaudrates[0]);
    }
    return standardBaudrates;
}

void getDefaultCatConfig(CatControllerConfig* cfg, CatProtocolType type) {
    if (!cfg) return;
    
    memset(cfg, 0, sizeof(CatControllerConfig));
    cfg->type = type;
    cfg->autoSwitch = true;
    cfg->switchDelay = 1000;
    
    switch (type) {
        case CAT_PROTO_ICOM:
            cfg->icom.radioAddress = 0xA4;  // IC-705
            cfg->icom.baudrate = 115200;
            cfg->icom.connType = ICOM_CONN_BLE;
            break;
            
        case CAT_PROTO_YAESU:
            cfg->yaesu.model = YAESU_FT991;
            cfg->yaesu.baudrate = 9600;
            break;
            
        case CAT_PROTO_KENWOOD:
            cfg->kenwood.model = KENWOOD_TS480;
            cfg->kenwood.baudrate = 9600;
            break;
            
        case CAT_PROTO_ELECRAFT:
            cfg->elecraft.model = ELECRAFT_K3;
            cfg->elecraft.baudrate = 38400;
            cfg->elecraft.extendedMode = true;
            break;
            
        case CAT_PROTO_FLEXRADIO:
            cfg->flex.radioIP = IPAddress(192, 168, 1, 100);
            cfg->flex.autoDiscover = true;
            break;
            
        default:
            break;
    }
}
