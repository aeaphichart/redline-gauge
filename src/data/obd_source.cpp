#include "data/obd_source.h"
#include "data/gauge_bus.h"
#include "data/obd_parse.h"
#include "config.h"
#include <Arduino.h>
#include <BluetoothSerial.h>

static BluetoothSerial SerialBT;

// PIDs we read. Index order matters for noData_[] and the schedule below.
enum { P_RPM, P_SPEED, P_COOLANT, P_IAT, P_VOLT };
struct PidDef { const char *cmd; uint8_t pid; Channel ch; };
static const PidDef kPids[] = {
    {"010C", 0x0C, CH_RPM},
    {"010D", 0x0D, CH_SPEED},
    {"0105", 0x05, CH_COOLANT},
    {"010F", 0x0F, CH_IAT},
    {"ATRV", 0x00, CH_VOLTAGE},     // adapter's own supply-pin reading = battery voltage
};
// RPM every other request, speed often, slow-moving temps/voltage rarely.
static const uint8_t kSchedule[] = {
    P_RPM, P_SPEED, P_RPM, P_COOLANT, P_RPM, P_SPEED, P_RPM, P_IAT,
    P_RPM, P_SPEED, P_RPM, P_VOLT,
};

void ObdSource::retryIn(uint32_t ms, State then, const char *msg) {
    bus::setLink(Link::Error, msg);
    waitUntil_ = millis() + ms;
    after_ = then;
    state_ = S_WAIT;
}

void ObdSource::begin() {
    state_ = S_BT_START;
    slot_ = 0;
    errors_ = 0;
    memset(noData_, 0, sizeof noData_);
    bus::setLink(Link::Connecting, "BT START");
}

void ObdSource::end() {
    if (btStarted_) {
        SerialBT.disconnect();
        SerialBT.end();
        btStarted_ = false;
    }
}

// Send a command, collect the reply up to the '>' prompt. The reply is stored
// upper-case with spaces/CR/LF removed, e.g. "410C1AF8". Returns false on timeout.
bool ObdSource::command(const char *cmd, uint32_t timeoutMs) {
    while (SerialBT.available()) SerialBT.read();
    SerialBT.print(cmd);
    SerialBT.print('\r');
    size_t n = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        while (SerialBT.available()) {
            char c = (char)SerialBT.read();
            if (c == '>') { resp_[n] = 0; return true; }
            if (c == ' ' || c == '\r' || c == '\n' || c == 0) continue;
            if (n < sizeof(resp_) - 1) resp_[n++] = (char)toupper((unsigned char)c);
        }
        delay(2);
    }
    resp_[n] = 0;
    return false;
}

// Returns true if the request got a valid answer (published) or a clean "NO DATA".
bool ObdSource::readPid(int idx) {
    const PidDef &d = kPids[idx];
    if (!command(d.cmd, OBD_CMD_TIMEOUT_MS)) return false;
    float v;
    if (d.pid == 0) {                                   // ATRV -> "13.9V"
        if (obdParseVolt(resp_, v) == OBD_OK) bus::publish(d.ch, v);
        return true;
    }
    switch (obdParsePid(resp_, d.pid, v)) {
        case OBD_OK:     noData_[idx] = 0; bus::publish(d.ch, v); return true;
        case OBD_NODATA: if (noData_[idx] < 255) noData_[idx]++; return true;
        default:         return false;
    }
}

void ObdSource::poll() {
    switch (state_) {
    case S_WAIT:
        if ((int32_t)(millis() - waitUntil_) >= 0) state_ = after_;
        else delay(20);
        break;

    case S_BT_START:
        // master mode; BLE disabled to leave RAM for the display
        if (!SerialBT.begin("REDLINE", true, true)) { retryIn(3000, S_BT_START, "BT FAIL"); break; }
        SerialBT.setPin(OBD_BT_PIN, strlen(OBD_BT_PIN));
        btStarted_ = true;
        state_ = S_CONNECT;
        break;

    case S_CONNECT: {
        bus::setLink(Link::Connecting, "BT PAIRING");
        bool ok;
        if (strlen(OBD_BT_MAC) == 17) ok = SerialBT.connect(BTAddress(OBD_BT_MAC));
        else ok = SerialBT.connect(OBD_BT_NAME);          // discovery, can take ~10 s
        if (!ok || !SerialBT.connected(3000)) { retryIn(4000, S_CONNECT, "NO ADAPTER"); break; }
        Serial.println("[obd] bluetooth connected");
        state_ = S_INIT;
        break;
    }

    case S_INIT: {
        bus::setLink(Link::Connecting, "ELM INIT");
        command("ATZ", 2500);                               // reset; reply is the version banner
        Serial.printf("[obd] ATZ -> %s\n", resp_);
        static const char *const init[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
        bool ok = true;
        for (const char *c : init) {
            if (!command(c, OBD_CMD_TIMEOUT_MS) || !strstr(resp_, "OK")) { ok = false; break; }
        }
        if (!ok) {
            Serial.printf("[obd] init failed, last reply '%s'\n", resp_);
            if (!SerialBT.connected()) { retryIn(2000, S_CONNECT, "BT LOST"); break; }
            retryIn(2000, S_INIT, "ELM ERROR");
            break;
        }
        state_ = S_SEARCH;
        break;
    }

    case S_SEARCH:
        // First real request makes the ELM auto-detect the protocol ("SEARCHING...").
        bus::setLink(Link::Connecting, "ECU SEARCH");
        if (command("0100", 10000) && strstr(resp_, "4100")) {
            Serial.println("[obd] ECU found, streaming");
            bus::setLink(Link::Live, "");
            errors_ = 0;
            state_ = S_RUN;
        } else {
            Serial.printf("[obd] 0100 -> '%s'\n", resp_);
            if (!SerialBT.connected()) retryIn(2000, S_CONNECT, "BT LOST");
            else retryIn(3000, S_SEARCH, "NO ECU");   // ignition off?
        }
        break;

    case S_RUN: {
        // next schedule entry whose PID the car actually supports
        int idx = P_RPM;
        for (int tries = 0; tries < (int)sizeof(kSchedule); tries++) {
            idx = kSchedule[slot_];
            slot_ = (slot_ + 1) % sizeof(kSchedule);
            if (noData_[idx] < 3) break;                   // skip PIDs this car doesn't support
        }
        if (readPid(idx)) {
            errors_ = 0;
            bus::setLink(Link::Live, "");
        } else if (++errors_ >= 6) {
            Serial.printf("[obd] lost ECU, last reply '%s'\n", resp_);
            if (!SerialBT.connected()) retryIn(1000, S_CONNECT, "BT LOST");
            else retryIn(1000, S_SEARCH, "ECU LOST");
        }
        break;
    }
    }
}
