/**
 * 配置验证模块
 * 
 * 功能:
 * - 验证各类配置参数的有效性
 * - 提供友好的错误信息
 * - 自动修正可修复的配置问题
 */

#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <Arduino.h>
#include "cat_controller.h"

// 验证结果
struct ValidationResult {
    bool valid;           // 是否通过验证
    bool autoFixed;       // 是否自动修复
    char errorMsg[128];   // 错误信息
    char suggestion[128]; // 修复建议
    
    ValidationResult() : valid(true), autoFixed(false) {
        errorMsg[0] = '\0';
        suggestion[0] = '\0';
    }
    
    void setError(const char* msg, const char* sugg = nullptr) {
        valid = false;
        strncpy(errorMsg, msg, sizeof(errorMsg) - 1);
        errorMsg[sizeof(errorMsg) - 1] = '\0';
        if (sugg) {
            strncpy(suggestion, sugg, sizeof(suggestion) - 1);
            suggestion[sizeof(suggestion) - 1] = '\0';
        }
    }
    
    void setFixed(const char* msg) {
        autoFixed = true;
        strncpy(errorMsg, msg, sizeof(errorMsg) - 1);
        errorMsg[sizeof(errorMsg) - 1] = '\0';
    }
    
    void setWarn(const char* msg, const char* sugg = nullptr) {
        // 警告不影响valid状态，只记录信息
        strncpy(errorMsg, msg, sizeof(errorMsg) - 1);
        errorMsg[sizeof(errorMsg) - 1] = '\0';
        if (sugg) {
            strncpy(suggestion, sugg, sizeof(suggestion) - 1);
            suggestion[sizeof(suggestion) - 1] = '\0';
        }
    }
};

// IP地址验证
ValidationResult validateIP(const char* ip);

// 波特率验证
ValidationResult validateBaudrate(uint32_t baud, bool allowAuto = false);

// CI-V地址验证
ValidationResult validateCIVAddress(uint8_t addr);

// GPIO引脚验证 (检查是否为ESP32-S3可用引脚)
ValidationResult validateGPIO(uint8_t pin);

// CAT配置验证
ValidationResult validateCatConfig(const CatControllerConfig* cfg);

// ICOM配置验证
ValidationResult validateIcomConfig(const IcomCatConfig* cfg);

// YAESU配置验证
ValidationResult validateYaesuConfig(const YaesuCatConfig* cfg);

// Kenwood配置验证
ValidationResult validateKenwoodConfig(const KenwoodCatConfig* cfg);

// Elecraft配置验证
ValidationResult validateElecraftConfig(const ElecraftCatConfig* cfg);

// FlexRadio配置验证
ValidationResult validateFlexConfig(const FlexCatConfig* cfg);

// 天线通道验证
ValidationResult validateAntennaChannel(uint8_t ch);

// 频率范围验证
ValidationResult validateFrequencyRange(uint64_t min, uint64_t max);

// 自动修复配置
bool autoFixCatConfig(CatControllerConfig* cfg, ValidationResult* result);

// 获取可用波特率列表
const uint32_t* getStandardBaudrates(int* count);

// 获取建议的默认配置
void getDefaultCatConfig(CatControllerConfig* cfg, CatProtocolType type);

#endif // CONFIG_VALIDATOR_H
