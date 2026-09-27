#include "data/custom_source.h"
#include "gauge_input.h"

void CustomSource::begin() {
    if (!began_) { mySensorsBegin(); began_ = true; }   // pins/interrupts set up once
    next_ = millis();
    live_ = -1;
    bus::setLink(Link::Connecting, "NO INPUT");
}

void CustomSource::poll() {
    uint32_t now = millis();
    if ((int32_t)(now - next_) < 0) return;
    next_ = now + CUSTOM_READ_MS;

    GaugeInput in;
    mySensorsRead(in);
    const float vals[] = { in.rpm, in.speed, in.coolant, in.voltage, in.intake, in.gear, in.throttle };
    const Channel chs[] = { CH_RPM, CH_SPEED, CH_COOLANT, CH_VOLTAGE, CH_IAT, CH_GEAR, CH_THROTTLE };
    int n = 0;
    for (int i = 0; i < 7; i++)
        if (!isnan(vals[i])) { bus::publish(chs[i], vals[i]); n++; }

    int8_t live = n > 0;
    if (live != live_) {
        live_ = live;
        if (live) bus::setLink(Link::Live, "");
        else bus::setLink(Link::Connecting, "NO INPUT");   // nothing filled in my_sensors.cpp yet
    }
}
