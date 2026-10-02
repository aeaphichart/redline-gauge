#include "data/obd_source.h"
#include "data/gauge_bus.h"
#include "data/obd_parse.h"
#include "config.h"
#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>

static BluetoothSerial SerialBT;

// ---- which adapter ------------------------------------------------------------------
// ELM327 clones go by many Bluetooth names ("OBDII", "OBD2", "V-LINK", "VEEPEAK" …), so
// an exact-name connect missed most of them. Instead: scan, list everything found on
// the serial log, take the first device whose name looks like an OBD adapter, and
// remember its MAC (NVS) so the next boot connects straight away.
// Serial:  obd=scan  forget the saved adapter and scan again
//          obd=AA:BB:CC:DD:EE:FF  use this adapter      obdpin=0000  fixed PIN
static const char *const kNameHints[] = {
    "OBD", "ELM", "V-LINK", "VLINK", "VEEPEAK", "KONNWEI", "VGATE", "ICAR", "CARISTA", "SCAN",
};
// Clones use one of these legacy PINs; without a saved/configured match we cycle them.
static const char *const kPins[] = { OBD_BT_PIN, "1234", "0000", "6789", "1111" };
static volatile bool s_retarget = false;    // set by serial commands (core 1)

static bool validMac(const char *m) {
    if (strlen(m) != 17) return false;
    for (int i = 0; i < 17; i++)
        if (i % 3 == 2 ? m[i] != ':' : !isxdigit((unsigned char)m[i])) return false;
    return true;
}

static bool looksLikeObd(const std::string &name) {
    if (name.empty()) return false;
    String up = String(name.c_str());
    up.toUpperCase();
    String want = OBD_BT_NAME;
    want.toUpperCase();
    if (want.length() && up == want) return true;
    for (const char *h : kNameHints) if (up.indexOf(h) >= 0) return true;
    return false;
}

bool ObdSource::command(const char *line) {
    Preferences p;
    if (!p.begin("obd", false)) return false;
    bool ok = true;
    if (!strncasecmp(line, "scan", 4) || !strncasecmp(line, "forget", 6)) {
        p.remove("mac");
        Serial.println("[obd] saved adapter forgotten, scanning on next attempt");
    } else if (validMac(line) || (strlen(line) > 17 && validMac(String(line).substring(0, 17).c_str()))) {
        p.putString("mac", String(line).substring(0, 17));
        Serial.printf("[obd] adapter set to %.17s\n", line);
    } else if (!strncasecmp(line, "pin=", 4) || !strncasecmp(line, "pin:", 4)) {
        String pin = String(line + 4);
        pin.trim();
        if (pin.length()) p.putString("pin", pin); else p.remove("pin");
        Serial.printf("[obd] PIN %s\n", pin.length() ? pin.c_str() : "auto (1234/0000/6789/1111)");
    } else {
        ok = false;
    }
    p.end();
    if (ok) s_retarget = true;
    return ok;
}

void ObdSource::loadTarget() {
    Preferences p;
    mac_[0] = pin_[0] = 0;
    if (p.begin("obd", true)) {
        p.getString("mac", mac_, sizeof mac_);
        p.getString("pin", pin_, sizeof pin_);
        p.end();
    }
    if (!validMac(mac_) && validMac(OBD_BT_MAC)) strlcpy(mac_, OBD_BT_MAC, sizeof mac_);
    if (!validMac(mac_)) mac_[0] = 0;
}

static void saveMac(const char *mac) {
    Preferences p;
    if (!p.begin("obd", false)) return;
    if (p.getString("mac", "") != mac) p.putString("mac", mac);
    p.end();
}

// Scan ~10 s, log every device, return the best OBD-looking one (strongest signal).
bool ObdSource::scan(char *macOut) {
    bus::setLink(Link::Connecting, "BT SCAN");
    Serial.println("[obd] scanning for Bluetooth Classic devices (10 s)...");
    BTScanResults *r = SerialBT.discover(10240);
    if (!r) { Serial.println("[obd] scan failed"); return false; }
    int best = -1, bestRssi = -1000, n = r->getCount();
    for (int i = 0; i < n; i++) {
        BTAdvertisedDevice *d = r->getDevice(i);
        std::string name = d->haveName() ? d->getName() : std::string();
        bool obd = looksLikeObd(name);
        int rssi = d->haveRSSI() ? d->getRSSI() : -999;
        Serial.printf("[obd]   %s  %-20s rssi=%d%s\n", d->getAddress().toString().c_str(),
                      name.empty() ? "(no name)" : name.c_str(), rssi, obd ? "  <- OBD" : "");
        if (obd && rssi > bestRssi) { best = i; bestRssi = rssi; }
    }
    if (best < 0) {
        Serial.printf("[obd] %d device(s), none named like an OBD adapter.\n"
                      "[obd] Pick yours from the list and type  obd=AA:BB:CC:DD:EE:FF\n"
                      "[obd] Nothing at all? The adapter may be BLE-only, or a phone is still connected to it.\n", n);
        SerialBT.discoverClear();
        return false;
    }
    strlcpy(macOut, r->getDevice(best)->getAddress().toString().c_str(), 18);
    SerialBT.discoverClear();
    return true;
}

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
    pinIdx_ = 0;
    macFails_ = 0;
    s_retarget = false;
    loadTarget();
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
bool ObdSource::elm(const char *cmd, uint32_t timeoutMs) {
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
    if (!elm(d.cmd, OBD_CMD_TIMEOUT_MS)) return false;
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
    if (s_retarget) {                       // obd=… typed: drop the link, reconnect to the new target
        s_retarget = false;
        loadTarget();
        pinIdx_ = 0;
        macFails_ = 0;
        if (btStarted_) {
            if (SerialBT.connected()) SerialBT.disconnect();
            state_ = S_CONNECT;
        }
    }
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
        // A saved/configured MAC is tried first; after 3 misses (adapter swapped?) scan again.
        char target[18];
        bool fromScan = false;
        if (mac_[0] && macFails_ < 3) {
            strlcpy(target, mac_, sizeof target);
        } else if (scan(target)) {
            fromScan = true;
        } else {
            retryIn(3000, S_CONNECT, "NO ADAPTER");
            break;
        }
        const char *pin = pin_[0] ? pin_ : kPins[pinIdx_];
        SerialBT.setPin(pin, strlen(pin));
        bus::setLink(Link::Connecting, "BT PAIRING");
        Serial.printf("[obd] connecting to %s (PIN %s)\n", target, pin);
        bool ok = SerialBT.connect(BTAddress(target)) && SerialBT.connected(5000);
        if (!ok) {
            Serial.println("[obd] connect failed");
            if (!pin_[0]) pinIdx_ = (pinIdx_ + 1) % (sizeof kPins / sizeof kPins[0]);
            if (!fromScan && macFails_ < 255) macFails_++;
            retryIn(2000, S_CONNECT, "NO ADAPTER");
            break;
        }
        Serial.printf("[obd] bluetooth connected to %s\n", target);
        if (strcmp(mac_, target)) { strlcpy(mac_, target, sizeof mac_); saveMac(target); }
        macFails_ = 0;
        state_ = S_INIT;
        break;
    }

    case S_INIT: {
        bus::setLink(Link::Connecting, "ELM INIT");
        elm("ATZ", 2500);                               // reset; reply is the version banner
        Serial.printf("[obd] ATZ -> %s\n", resp_);
        static const char *const init[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
        bool ok = true;
        for (const char *c : init) {
            if (!elm(c, OBD_CMD_TIMEOUT_MS) || !strstr(resp_, "OK")) { ok = false; break; }
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
        if (elm("0100", 10000) && strstr(resp_, "4100")) {
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
