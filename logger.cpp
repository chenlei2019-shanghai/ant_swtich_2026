/**
 * 日志系统实现
 */

#include "logger.h"
#include "config.h"

// 配置存储键
#define LOG_NAMESPACE     "logger"
#define KEY_LOG_LEVEL     "level"
#define KEY_LOG_TAGS      "tags"

// 全局状态
static LogLevel currentLevel = LOG_INFO;
static uint8_t enabledTags = TAG_ALL;
static bool initialized = false;
static Preferences prefs;

// 环形日志缓冲区 (用于Web查看)
#define LOG_RING_SIZE 2048
static char logRing[LOG_RING_SIZE];
static volatile int ringHead = 0;
static volatile int ringTail = 0;

// 级别名称
static const char* levelNames[] = {"NONE", "E", "W", "I", "D"};
static const char* levelColors[] = {"", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m"};

// 标签名称
static const char* tagNames[] = {
    "MAIN", "CAT", "BLE", "NET", "HW", "WEB", "???"
};

// 写入环形缓冲区
static void ringWrite(const char* str) {
    while (*str) {
        int next = (ringHead + 1) % LOG_RING_SIZE;
        if (next == ringTail) {
            // 缓冲区满，丢弃最旧的数据
            ringTail = (ringTail + 1) % LOG_RING_SIZE;
        }
        logRing[ringHead] = *str++;
        ringHead = next;
    }
    // 添加换行
    int next = (ringHead + 1) % LOG_RING_SIZE;
    if (next != ringTail) {
        logRing[ringHead] = '\n';
        ringHead = next;
    }
}

// 获取标签名称
static const char* getTagName(LogTag tag) {
    switch (tag) {
        case TAG_MAIN: return "MAIN";
        case TAG_CAT:  return "CAT";
        case TAG_BLE:  return "BLE";
        case TAG_NET:  return "NET";
        case TAG_HW:   return "HW";
        case TAG_WEB:  return "WEB";
        default:       return "???";
    }
}

void loggerInit() {
    if (initialized) return;
    
    // 从 NVS 恢复设置
    if (prefs.begin(LOG_NAMESPACE, true)) {
        currentLevel = (LogLevel)prefs.getUChar(KEY_LOG_LEVEL, LOG_INFO);
        enabledTags = prefs.getUChar(KEY_LOG_TAGS, TAG_ALL);
        prefs.end();
    }
    
    initialized = true;
    
    // 输出启动信息
    Serial.println("\n================================");
    Serial.println("  ESP32-S3 天线切换器 v1.0");
    Serial.println("================================");
    LOG_I(TAG_MAIN, "日志系统初始化完成, 级别=%s", loggerLevelToStr(currentLevel));
}

void loggerDeinit() {
    if (!initialized) return;
    prefs.end();
    initialized = false;
}

void loggerSetLevel(LogLevel level) {
    currentLevel = level;
    
    // 保存到 NVS
    if (prefs.begin(LOG_NAMESPACE, false)) {
        prefs.putUChar(KEY_LOG_LEVEL, level);
        prefs.end();
    }
    
    LOG_I(TAG_MAIN, "日志级别设置为: %s", loggerLevelToStr(level));
}

LogLevel loggerGetLevel() {
    return currentLevel;
}

const char* loggerLevelToStr(LogLevel level) {
    if (level <= LOG_DEBUG) {
        return levelNames[level];
    }
    return "???";
}

void loggerEnableTag(LogTag tag) {
    enabledTags |= (uint8_t)tag;
    
    if (prefs.begin(LOG_NAMESPACE, false)) {
        prefs.putUChar(KEY_LOG_TAGS, enabledTags);
        prefs.end();
    }
}

void loggerDisableTag(LogTag tag) {
    enabledTags &= ~(uint8_t)tag;
    
    if (prefs.begin(LOG_NAMESPACE, false)) {
        prefs.putUChar(KEY_LOG_TAGS, enabledTags);
        prefs.end();
    }
}

bool loggerIsTagEnabled(LogTag tag) {
    return (enabledTags & (uint8_t)tag) != 0;
}

void loggerPrint(LogLevel level, LogTag tag, const char* file, int line, 
                 const char* func, const char* fmt, ...) {
    // 检查级别和标签
    if (!initialized) return;
    if (level > currentLevel) return;
    if (!(enabledTags & (uint8_t)tag)) return;
    
    // 提取文件名 (去掉路径)
    const char* filename = strrchr(file, '/');
    if (!filename) filename = strrchr(file, '\\');
    filename = filename ? filename + 1 : file;
    
    // 格式化时间戳
    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hour = min / 60;
    
    // 构建日志头
    char header[64];
    snprintf(header, sizeof(header), "[%02lu:%02lu:%02lu.%03lu][%s][%s][%s:%d] ",
             hour % 24, min % 60, sec % 60, ms % 1000,
             levelNames[level], getTagName(tag), filename, line);
    
    // 格式化消息
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    // 输出到串口
    Serial.print(header);
    Serial.println(msg);
    
    // 写入环形缓冲区
    ringWrite(header);
    ringWrite(msg);
}

void loggerPrintSystemStats() {
    LOG_I(TAG_MAIN, "=== 系统状态 ===");
    LOG_I(TAG_MAIN, "Free Heap: %d bytes", ESP.getFreeHeap());
    LOG_I(TAG_MAIN, "Free PSRAM: %d bytes", ESP.getFreePsram());
    LOG_I(TAG_MAIN, "CPU Freq: %d MHz", ESP.getCpuFreqMHz());
    LOG_I(TAG_MAIN, "Uptime: %lu seconds", millis() / 1000);
    
    // CAT 状态
    extern bool catControllerIsConnected();
    extern uint32_t catControllerGetFrequency();
    LOG_I(TAG_CAT, "CAT连接: %s, 频率: %lu Hz", 
          catControllerIsConnected() ? "是" : "否",
          catControllerGetFrequency());
}

int loggerGetBuffer(char* buf, int len) {
    if (!buf || len <= 0) return 0;
    
    int count = 0;
    int tail = ringTail;
    
    while (tail != ringHead && count < len - 1) {
        buf[count++] = logRing[tail];
        tail = (tail + 1) % LOG_RING_SIZE;
    }
    buf[count] = '\0';
    
    return count;
}

void loggerClearBuffer() {
    ringHead = ringTail = 0;
}
