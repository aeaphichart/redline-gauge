#include "data/honda_source.h"
#include "data/honda_kline.h"
#include "data/gauge_bus.h"
#include "config.h"
#include <Arduino.h>
#include <driver/gpio.h>

volatile bool HondaKSource::ownsUart = false;
volatile bool HondaKSource::dumpOn = false;

static HardwareSerial &K = Serial2;

void HondaKSource::retryIn(uint32_t ms, State then, const char *msg) {
    bus::setLink(Link::Error, msg);
    waitUntil_ = millis() + ms;
    after_ = then;
    state_ = S_WAIT;
}

void HondaKSource::begin() {
    ownsUart = true;                   // main.cpp stops reading Serial2 as the text input
    delay(20);                         // let a poll in progress on the other core finish
    state_ = S_WAKE;
    errors_ = fails_ = cycle_ = 0;
    sawEcho_ = false;
    bus::setLink(Link::Connecting, "K-LINE INIT");
}

void HondaKSource::end() {
    K.end();
#if EXT_SERIAL_RX_PIN >= 0
    // hand GPIO 27 back to the text serial input (receive-only), idle-high
    pinMode(KLINE_TX_PIN, INPUT);
    K.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX_PIN, -1);
    gpio_pullup_en((gpio_num_t)EXT_SERIAL_RX_PIN);
#endif
    ownsUart = false;
}

// Wake-up: K-line low 70 ms, high 120 ms (a long "break" the UART can't produce on its own)
void HondaKSource::wake() {
    K.end();
    pinMode(KLINE_TX_PIN, OUTPUT);
    digitalWrite(KLINE_TX_PIN, LOW);
    delay(70);
    digitalWrite(KLINE_TX_PIN, HIGH);
    delay(120);
    K.begin(10400, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN);
    while (K.available()) K.read();
}

static bool readByte(uint8_t &b, uint32_t until) {
    while ((int32_t)(millis() - until) < 0) {
        if (K.available()) { b = (uint8_t)K.read(); return true; }
        delay(1);
    }
    return false;
}

// Send a frame and read the reply into resp_. The single wire echoes what we send; the
// echo is skipped when it is there (some isolated boards don't echo).
bool HondaKSource::transact(const uint8_t *req, uint8_t n, uint32_t timeoutMs) {
    while (K.available()) K.read();
    K.write(req, n);
    K.flush();                                         // wait until it is on the wire
    uint32_t until = millis() + timeoutMs;
    uint8_t b, got = 0;
    respLen_ = 0;
    // echo: up to n bytes equal to the request; the first mismatch starts the reply
    while (got < n) {
        if (!readByte(b, millis() + 40)) break;
        if (b == req[got]) { got++; continue; }
        resp_[respLen_++] = b;
        break;
    }
    if (got == n) sawEcho_ = true;
    if (!respLen_ && !readByte(resp_[respLen_++], until)) return false;     // address byte
    if (respLen_ < 2 && !readByte(resp_[respLen_++], until)) return false;  // length byte
    uint8_t len = resp_[1];
    if (len < 4 || len > sizeof(resp_)) return false;
    while (respLen_ < len)
        if (!readByte(resp_[respLen_++], until + 50)) return false;
    return hkFrameOk(resp_, respLen_);
}

void HondaKSource::poll() {
    switch (state_) {
    case S_WAIT:
        if ((int32_t)(millis() - waitUntil_) >= 0) state_ = after_;
        else delay(20);
        break;

    case S_WAKE:
        bus::setLink(Link::Connecting, "K-LINE INIT");
        wake();
        transact(HK_PING, sizeof HK_PING, 100);        // answer optional, some ECUs stay quiet
        state_ = S_INIT;
        break;

    case S_INIT:
        if (transact(HK_INIT, sizeof HK_INIT, 300) && resp_[0] == 0x02) {
            Serial.println("[honda] ECU answered init");
            fails_ = 0;
            state_ = S_PROBE;
        } else {
            fails_++;
            Serial.printf("[honda] no init reply (%s)\n",
                          sawEcho_ ? "echo OK: wiring fine, ignition ON? 2019+ bikes are CAN"
                                   : "no echo: check TX/RX swap, 3.3 V, 12 V on the board");
            retryIn(fails_ < 3 ? 1000 : 3000, S_WAKE, sawEcho_ ? "NO ECU" : "NO K-LINE");
        }
        break;

    case S_PROBE: {
        static const uint8_t candidates[] = { 0x11, 0x10, 0x17, 0x13 };
        for (uint8_t t : candidates) {
            uint8_t req[5];
            hkTableRequest(t, req);
            HondaData d;
            if (transact(req, 5) && hkDecodeMain(resp_, respLen_, t, d)) {
                table_ = t;
                Serial.printf("[honda] engine table 0x%02X (%u bytes), streaming\n", t, respLen_);
                bus::setLink(Link::Live, "");
                errors_ = 0;
                state_ = S_RUN;
                return;
            }
            delay(30);
        }
        retryIn(2000, S_WAKE, "NO TABLE");
        break;
    }

    case S_RUN: {
        if (dumpOn && millis() - lastDump_ >= 1000) { dump(); lastDump_ = millis(); }
        uint8_t req[5];
        bool neutralTurn = (++cycle_ & 3) == 0;            // 0xD1 every 4th request
        hkTableRequest(neutralTurn ? 0xD1 : table_, req);
        bool ok = transact(req, 5);
        if (ok && !neutralTurn) {
            HondaData d;
            ok = hkDecodeMain(resp_, respLen_, table_, d);
            if (ok) {
                bus::publish(CH_RPM, d.rpm);
                bus::publish(CH_SPEED, d.speed);
                bus::publish(CH_COOLANT, d.ect);
                bus::publish(CH_IAT, d.iat);
                bus::publish(CH_VOLTAGE, d.batt);
                bus::publish(CH_THROTTLE, d.tps);
            }
        } else if (ok) {
            bool neutral;
            // no gear number on the bus: publish N in neutral, else let the UI estimate it
            if (hkDecodeNeutral(resp_, respLen_, neutral) && neutral) bus::publish(CH_GEAR, 0);
        }
        if (ok) {
            errors_ = 0;
            bus::setLink(Link::Live, "");
        } else if (++errors_ >= 3) {
            Serial.println("[honda] ECU stopped answering, waking it again");
            retryIn(500, S_WAKE, "ECU LOST");
        }
        delay(15);
        break;
    }
    }
}

// Raw dump of every known table, bytes that changed since the last dump in [brackets].
void HondaKSource::dump() {
    static const uint8_t tables[] = { 0x00, 0x10, 0x11, 0x13, 0x17, 0x20, 0x21, 0x60, 0x61,
                                      0x67, 0x70, 0x71, 0xD0, 0xD1 };
    static uint8_t prev[sizeof tables][64];
    static uint8_t prevLen[sizeof tables];
    Serial.println("[kdump] ---");
    for (size_t i = 0; i < sizeof tables; i++) {
        uint8_t req[5];
        hkTableRequest(tables[i], req);
        if (!transact(req, 5)) { Serial.printf("[kdump] T%02X  (no reply)\n", tables[i]); continue; }
        Serial.printf("[kdump] T%02X len=%2u:", tables[i], respLen_);
        for (uint8_t k = 0; k < respLen_; k++) {
            bool changed = prevLen[i] == respLen_ && prev[i][k] != resp_[k];
            Serial.printf(changed ? " [%02X]" : " %02X", resp_[k]);
        }
        Serial.println();
        memcpy(prev[i], resp_, respLen_);
        prevLen[i] = respLen_;
        delay(20);
    }
}
