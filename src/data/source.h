#pragma once
// A data source feeds the gauge through bus::publish(). Exactly one is active;
// it runs in the dedicated data task (see main.cpp), so blocking calls such as
// Bluetooth connects or waiting for an ELM327 reply never stall the display.
//
// To use your own hardware (analog sensors, CAN transceiver, another MCU …):
// subclass DataSource, publish values in poll(), add it to the list in main.cpp.
class DataSource {
public:
    virtual ~DataSource() {}
    virtual const char *name() const = 0;   // shown in the status bar, keep ≤ 10 chars
    virtual void begin() = 0;                // called when the source becomes active
    virtual void end() {}                    // called when switching away
    virtual void poll() = 0;                 // called repeatedly (≈ every 5 ms)
};
