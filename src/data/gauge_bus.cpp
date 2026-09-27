#include "data/gauge_bus.h"
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define LOCK()   portENTER_CRITICAL(&mux)
#define UNLOCK() portEXIT_CRITICAL(&mux)
uint32_t bus::nowMs() { return millis(); }
#else  // host preview build
#define LOCK()
#define UNLOCK()
uint32_t g_host_ms = 1;   // the host preview drives time explicitly
uint32_t bus::nowMs() { return g_host_ms; }
#endif

static GaugeSnapshot state = {};

void bus::publish(Channel ch, float v) {
    if (ch >= CH_COUNT || v != v) return;   // drop NaN
    uint32_t t = nowMs();
    LOCK();
    state.value[ch] = v;
    state.stamp[ch] = t ? t : 1;
    UNLOCK();
}

void bus::setLink(Link l, const char *msg) {
    LOCK();
    state.link = l;
    strncpy(state.linkMsg, msg ? msg : "", sizeof(state.linkMsg) - 1);
    state.linkMsg[sizeof(state.linkMsg) - 1] = 0;
    UNLOCK();
}

void bus::clear() {
    LOCK();
    memset(state.value, 0, sizeof(state.value));
    memset(state.stamp, 0, sizeof(state.stamp));
    state.link = Link::Idle;
    state.linkMsg[0] = 0;
    UNLOCK();
}

void bus::snapshot(GaugeSnapshot &out) {
    LOCK();
    out = state;
    UNLOCK();
}
