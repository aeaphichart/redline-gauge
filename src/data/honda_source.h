#pragma once
// Honda motorcycles with the red 4-pin diagnostic connector (K-line, roughly 2008-2018
// PGM-FI bikes: CB/CBR500, CBR250/300, CRF250L, MSX, PCX, Wave/Click i ...) through a
// K-line transceiver board (L9637D or an opto-isolated "K-line FTDI" board) on CN1:
//   transceiver RX-out -> GPIO 27      CYD GPIO 22 -> transceiver TX-in
//   logic VCC <- 3.3 V (never 5 V on GPIO 27)    car side: K-line, GND, +12 V switched
// Same blocking state machine style as ObdSource:
//   wake pulse -> ping -> init -> find the engine table (0x11 / 0x10 / 0x17) -> poll it
// Serial `kdump` toggles a raw dump of every known table once a second (with the bytes
// that changed marked) so table offsets can be checked on a new model.
#include "data/source.h"
#include <stdint.h>

class HondaKSource : public DataSource {
public:
    const char *name() const override { return "HONDA K"; }
    void begin() override;
    void end() override;
    void poll() override;

    static volatile bool ownsUart;     // main.cpp leaves Serial2 alone while true
    static volatile bool dumpOn;       // `kdump` toggles
    // Opto-isolated DIY interfaces invert the line (see docs/wiring/kline-opto-schematic):
    // invert both UART directions and the wake pulse. Saved in NVS; `klineinvert=on|off`.
    static void setInvert(bool on);
    static bool invert();

private:
    enum State : uint8_t { S_WAKE, S_INIT, S_PROBE, S_RUN, S_WAIT };
    State    state_ = S_WAKE, after_ = S_WAKE;
    uint32_t waitUntil_ = 0, lastDump_ = 0;
    uint8_t  table_ = 0x11;            // engine table this ECU answers
    uint8_t  errors_ = 0, cycle_ = 0, fails_ = 0;
    bool     sawEcho_ = false;
    uint8_t  resp_[64];
    uint8_t  respLen_ = 0;

    void retryIn(uint32_t ms, State then, const char *msg);
    void wake();
    bool transact(const uint8_t *req, uint8_t n, uint32_t timeoutMs = 150);
    void dump();
};
