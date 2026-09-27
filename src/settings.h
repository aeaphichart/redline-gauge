#pragma once
// User settings, editable on the device's settings screen (and over serial),
// stored in NVS by main.cpp. Defaults come from config.h.
#include <stdint.h>
#include "config.h"

enum SourceId : uint8_t { SRC_SIM_AUTO, SRC_SIM_TOUCH, SRC_SERIAL, SRC_OBD, SRC_CUSTOM, SRC_COUNT };

struct Settings {
    uint8_t  theme      = 0;             // index into kThemes
    uint8_t  source     = SRC_SIM_AUTO;
    uint16_t shiftRpm   = RPM_SHIFT;     // shift light point
    uint8_t  brightness = 100;           // backlight %, 20..100
    bool     beep       = SHIFT_BEEP;    // shift / warning beeps

    static const uint16_t kShiftMin = 3000, kShiftMax = RPM_MAX, kShiftStep = 250;
    static const uint8_t  kBrightMin = 20, kBrightStep = 20;
};
