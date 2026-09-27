#pragma once
// CUSTOM source: calls mySensorsRead() (src/my_sensors.cpp) and publishes
// whatever it filled in. See src/gauge_input.h.
#include "data/source.h"
#include <stdint.h>

class CustomSource : public DataSource {
public:
    const char *name() const override { return "CUSTOM"; }
    void begin() override;
    void poll() override;

private:
    uint32_t next_ = 0;
    bool     began_ = false;
    int8_t   live_ = -1;
};
