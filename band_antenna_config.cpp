/**
 * 波段-天线绑定配置实现
 */

#include "band_antenna_config.h"
#include <Preferences.h>

// 默认波段配置
const BandConfig DEFAULT_BAND_CONFIGS[] = {
    {1800000,   2000000,   1, "160m",  "160m",  true},
    {3500000,   3900000,   2, "80m",   "80m",   true},
    {7000000,   7300000,   3, "40m",   "40m",   true},
    {10100000,  10150000,  4, "30m",   "30m",   true},
    {14000000,  14350000,  4, "20m",   "20m",   true},
    {18068000,  18168000,  5, "17m",   "17m",   true},
    {21000000,  21450000,  5, "15m",   "15m",   true},
    {24890000,  24990000,  6, "12m",   "12m",   true},
    {28000000,  29700000,  6, "10m",   "10m",   true},
    {50000000,  54000000,  0, "6m",    "6m",    true},
    {144000000, 148000000, 0, "2m",    "2m",    false},
    {430000000, 450000000, 0, "70cm",  "70cm",  false},
};

const uint8_t BAND_CFG_DEFAULT_COUNT = sizeof(DEFAULT_BAND_CONFIGS) / sizeof(DEFAULT_BAND_CONFIGS[0]);

static BandConfig bandConfigs[MAX_BANDS];
static uint8_t bandCount = 0;
static Preferences prefs;

void bandAntennaInit() {
    bandAntennaLoad();
}

void bandAntennaLoad() {
    prefs.begin("band_cfg", true);
    bandCount = prefs.getUChar("count", 0);
    
    if (bandCount == 0 || bandCount > MAX_BANDS) {
        // 使用默认配置
        bandCount = BAND_CFG_DEFAULT_COUNT;
        memcpy(bandConfigs, DEFAULT_BAND_CONFIGS, sizeof(DEFAULT_BAND_CONFIGS));
        bandAntennaSave();
    } else {
        // 从Flash加载
        for (uint8_t i = 0; i < bandCount; i++) {
            char key[16];
            sprintf(key, "band_%d", i);
            prefs.getBytes(key, &bandConfigs[i], sizeof(BandConfig));
        }
    }
    
    prefs.end();
}

void bandAntennaSave() {
    prefs.begin("band_cfg", false);
    prefs.putUChar("count", bandCount);
    
    for (uint8_t i = 0; i < bandCount; i++) {
        char key[16];
        sprintf(key, "band_%d", i);
        prefs.putBytes(key, &bandConfigs[i], sizeof(BandConfig));
    }
    
    prefs.end();
}

BandConfig* bandAntennaGetConfig(uint8_t index) {
    if (index >= bandCount) return nullptr;
    return &bandConfigs[index];
}

uint8_t bandAntennaGetCount() {
    return bandCount;
}

void bandAntennaSetConfig(uint8_t index, const BandConfig* config) {
    if (index >= bandCount) return;
    memcpy(&bandConfigs[index], config, sizeof(BandConfig));
    bandAntennaSave();
}

bool bandAntennaAddBand(const BandConfig* config) {
    if (bandCount >= MAX_BANDS) return false;
    memcpy(&bandConfigs[bandCount], config, sizeof(BandConfig));
    bandCount++;
    bandAntennaSave();
    return true;
}

bool bandAntennaRemoveBand(uint8_t index) {
    if (index >= bandCount) return false;
    
    // 移动后面的元素
    for (uint8_t i = index; i < bandCount - 1; i++) {
        memcpy(&bandConfigs[i], &bandConfigs[i + 1], sizeof(BandConfig));
    }
    
    bandCount--;
    bandAntennaSave();
    return true;
}

uint8_t bandAntennaFindChannel(uint32_t freqHz) {
    for (uint8_t i = 0; i < bandCount; i++) {
        if (bandConfigs[i].enabled && 
            freqHz >= bandConfigs[i].minFreq && 
            freqHz <= bandConfigs[i].maxFreq) {
            return bandConfigs[i].antennaCh;
        }
    }
    return 0;  // 默认OPEN
}

const char* bandAntennaGetBandName(uint32_t freqHz) {
    for (uint8_t i = 0; i < bandCount; i++) {
        if (bandConfigs[i].enabled && 
            freqHz >= bandConfigs[i].minFreq && 
            freqHz <= bandConfigs[i].maxFreq) {
            return bandConfigs[i].name;
        }
    }
    return "Unknown";
}
