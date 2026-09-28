#include "gauge_input.h"

void IRAM_ATTR PulseInput::isr(void *arg) {
    PulseInput *p = (PulseInput *)arg;
    uint32_t now = micros(), d = now - p->last_;
    if (d < 200) return;                                     // glitch filter (5 kHz max)
    p->period_ = p->period_ ? (p->period_ * 3 + d) / 4 : d;  // light smoothing
    p->last_ = now;
}
