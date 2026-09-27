#pragma once
// OBD-II via an ELM327 Bluetooth Classic adapter (the common blue "OBDII" dongles).
//
// Runs as a simple blocking state machine inside the data task:
//   BT start -> pair/connect -> ELM init (ATZ, ATE0 …) -> find ECU (0100) -> poll PIDs
// RPM is polled every other request so the shift bar stays responsive (~5-10 Hz on
// a typical CAN car); speed, coolant, intake temp and battery voltage fill the gaps.
// Any failure drops back a step and retries; the status bar shows where it is.
#include "data/source.h"
#include <stdint.h>

class ObdSource : public DataSource {
public:
    const char *name() const override { return "OBD BT"; }
    void begin() override;
    void end() override;
    void poll() override;

private:
    enum State : uint8_t { S_BT_START, S_CONNECT, S_INIT, S_SEARCH, S_RUN, S_WAIT };
    State    state_ = S_BT_START;
    State    after_ = S_CONNECT;     // state to resume after S_WAIT
    uint32_t waitUntil_ = 0;
    bool     btStarted_ = false;
    uint8_t  slot_ = 0;
    uint8_t  errors_ = 0;
    uint8_t  noData_[5] = {};        // per-PID "NO DATA" counter, >=3 = unsupported
    char     resp_[96];

    void retryIn(uint32_t ms, State then, const char *msg);
    bool command(const char *cmd, uint32_t timeoutMs);
    bool readPid(int idx);
};
