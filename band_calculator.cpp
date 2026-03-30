/**
 * 波段计算器实现
 */

#include "band_calculator.h"

const AmateurBand DEFAULT_BANDS[] = {
    {1800000,   2000000,   1, "160m Band", "160m"},
    {3500000,   3900000,   2, "80m Band",  "80m"},
    {7000000,   7300000,   3, "40m Band",  "40m"},
    {10100000,  10150000,  4, "30m Band",  "30m"},
    {14000000,  14350000,  4, "20m Band",  "20m"},
    {18068000,  18168000,  5, "17m Band",  "17m"},
    {21000000,  21450000,  5, "15m Band",  "15m"},
    {24890000,  24990000,  6, "12m Band",  "12m"},
    {28000000,  29700000,  6, "10m Band",  "10m"},
    {50000000,  54000000,  0, "6m Band",   "6m"},
};

const uint8_t BAND_CALC_DEFAULT_COUNT = sizeof(DEFAULT_BANDS) / sizeof(DEFAULT_BANDS[0]);

static const AmateurBand* bandTable = DEFAULT_BANDS;
static uint8_t bandCount = BAND_CALC_DEFAULT_COUNT;

void bandCalcInit(const AmateurBand* bands, uint8_t count) {
    bandTable = bands;
    bandCount = count;
}

const AmateurBand* freqToBand(uint32_t freqHz) {
    for (uint8_t i = 0; i < bandCount; i++) {
        if (freqHz >= bandTable[i].minFreq && freqHz <= bandTable[i].maxFreq) {
            return &bandTable[i];
        }
    }
    return NULL;
}

uint8_t getAntennaChannel(uint32_t freqHz) {
    const AmateurBand* band = freqToBand(freqHz);
    if (band) {
        return band->antennaChannel;
    }
    return 0;
}
