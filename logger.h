/**
 * 日志系统模块
 * 
 * 功能:
 * - 分级日志: ERROR/WARN/INFO/DEBUG
 * - 运行时动态调整日志级别
 * - 支持标签分类
 * - 持久化存储日志级别设置
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <Preferences.h>

// 日志级别 (数值越小越严重)
enum LogLevel {
    LOG_NONE  = 0,  // 关闭所有日志
    LOG_ERROR = 1,  // 错误 - 必须处理的问题
    LOG_WARN  = 2,  // 警告 - 需要注意但可继续
    LOG_INFO  = 3,  // 信息 - 正常运行状态
    LOG_DEBUG = 4   // 调试 - 详细运行信息
};

// 日志标签分类 (用于过滤特定模块)
enum LogTag {
    TAG_MAIN = 0x01,      // 主程序
    TAG_CAT  = 0x02,      // CAT协议
    TAG_BLE  = 0x04,      // 蓝牙BLE
    TAG_NET  = 0x08,      // 网络
    TAG_HW   = 0x10,      // 硬件
    TAG_WEB  = 0x20,      // Web服务器
    TAG_ALL  = 0xFF       // 所有标签
};

// 初始化与配置
void loggerInit();
void loggerDeinit();

// 日志级别设置
void loggerSetLevel(LogLevel level);
LogLevel loggerGetLevel();
const char* loggerLevelToStr(LogLevel level);

// 标签过滤设置
void loggerEnableTag(LogTag tag);
void loggerDisableTag(LogTag tag);
bool loggerIsTagEnabled(LogTag tag);

// 底层输出函数
void loggerPrint(LogLevel level, LogTag tag, const char* file, int line, 
                 const char* func, const char* fmt, ...);

// 便捷宏 (自动包含文件、行号、函数名)
#define LOG_E(tag, fmt, ...) loggerPrint(LOG_ERROR, tag, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) loggerPrint(LOG_WARN,  tag, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) loggerPrint(LOG_INFO,  tag, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) loggerPrint(LOG_DEBUG, tag, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// 条件日志 (仅在调试模式编译)
#ifdef DEBUG_BUILD
    #define LOG_D_DBG(tag, fmt, ...) LOG_D(tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_D_DBG(tag, fmt, ...) 
#endif

// 系统状态日志 (定期输出)
void loggerPrintSystemStats();

// 日志缓冲区操作 (用于Web查看)
int loggerGetBuffer(char* buf, int len);
void loggerClearBuffer();

#endif // LOGGER_H
