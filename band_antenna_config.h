/**
 * 波段-天线绑定配置
 * 允许用户自定义每个波段对应的天线通道
 */

#ifndef BAND_ANTENNA_CONFIG_H
#define BAND_ANTENNA_CONFIG_H

#include <Arduino.h>

#define MAX_BANDS 16

// 波段配置结构
struct BandConfig {
    uint32_t minFreq;       // 最小频率 (Hz)
    uint32_t maxFreq;       // 最大频率 (Hz)
    uint8_t antennaCh;      // 天线通道 (0-6, 0=OPEN)
    char name[16];          // 波段名称
    char meter[8];          // 米波段标识
    bool enabled;           // 是否启用
};

// 默认波段表
extern const BandConfig DEFAULT_BAND_CONFIGS[];
extern const uint8_t DEFAULT_BAND_COUNT;

// 初始化
void bandAntennaInit();

// 加载/保存配置
void bandAntennaLoad();
void bandAntennaSave();

// 获取波段配置
BandConfig* bandAntennaGetConfig(uint8_t index);
uint8_t bandAntennaGetCount();

// 更新波段配置
void bandAntennaSetConfig(uint8_t index, const BandConfig* config);

// 添加/删除波段
bool bandAntennaAddBand(const BandConfig* config);
bool bandAntennaRemoveBand(uint8_t index);

// 根据频率查找天线通道
uint8_t bandAntennaFindChannel(uint32_t freqHz);

// 获取波段名称
const char* bandAntennaGetBandName(uint32_t freqHz);

#endif
