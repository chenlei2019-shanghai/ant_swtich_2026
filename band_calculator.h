/**
 * 波段计算器
 */

#ifndef BAND_CALCULATOR_H
#define BAND_CALCULATOR_H

#include <Arduino.h>

struct AmateurBand {
    uint32_t minFreq;
    uint32_t maxFreq;
    uint8_t antennaChannel;
    const char* name;
    const char* meter;
};

void bandCalcInit(const AmateurBand* bands, uint8_t count);

const AmateurBand* freqToBand(uint32_t freqHz);
uint8_t getAntennaChannel(uint32_t freqHz);

extern const AmateurBand DEFAULT_BANDS[];
extern const uint8_t BAND_CALC_DEFAULT_COUNT;

#endif
