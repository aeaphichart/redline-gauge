#pragma once
// Turns raw bus snapshots into exactly what the screen should show:
// smoothing, staleness, warning levels, peak hold, gear estimate, status text.
// No drawing and no hardware here — runs identically on the ESP32 and on the host.
#include <stdint.h>
#include "config.h"
#include "data/gauge_bus.h"

enum Level : uint8_t { LV_NORMAL, LV_COLD, LV_WARN, LV_CRIT };

struct GaugeView {
    // shift bar + big number
    bool  rpmValid;
    float rpmBar;        // smoothed, drives the bar
    int   rpmText;       // rounded to RPM_DISPLAY_STEP
    float peakMarker;    // rpm of the falling peak marker, 0 = hidden
    int   sessionPeak;   // highest rpm since reset, 0 = none
    bool  shift;         // at/above the shift point (settings)
    bool  shiftFlash;    // flash phase while shifting (bar off when false)

    bool  speedValid;
    int   speed;
    int   gear;          // -1 = hidden, 0 = neutral

    bool  coolValid;  int   coolant; Level coolLvl;
    bool  voltValid;  float volt;    Level voltLvl;
    bool  iatValid;   int   iat;     Level iatLvl;
    bool  blink;         // shared slow blink phase for critical values

    // status bar
    Link        link;
    const char *mode;
    char     right[12];
    uint32_t dotColor;   // RGB888
    bool     dotOn;
};

class GaugeModel {
public:
    void reset(uint32_t now);    // new source: restart timer, peaks, smoothing
    void resetPeaks();
    void setShiftRpm(int rpm) { shiftRpm_ = rpm; }
    void update(const GaugeSnapshot &s, uint32_t now, const char *modeName, GaugeView &out);

private:
    uint32_t t0_ = 0, last_ = 0;
    float    rpmSmooth_ = 0, speedSmooth_ = 0;
    float    peak_ = 0;
    uint32_t peakAt_ = 0;
    int      sessionPeak_ = 0;
    int      shiftRpm_ = RPM_SHIFT;
};

// Estimate gear from rpm/speed using config.h ratios. 0 = no match (clutch/neutral).
int estimateGear(float rpm, float speedKmh);
