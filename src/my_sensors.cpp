// ============================================================================
//  YOUR SENSORS — edit this file to show real values on the gauge.
//  Select it on the device: SETUP -> DATA SOURCE -> CUSTOM
//
//  CYD pins free for you:  GPIO 35 (P3, analog/input only) · GPIO 22, 27 (CN1)
//  Car signals are 12-14 V and noisy: always use a divider / opto-isolator.
//
//  Flip an example to 1 to try it, or write your own code in mySensorsRead().
// ============================================================================
#include "gauge_input.h"

#define EXAMPLE_BATTERY_GPIO35   0   // 47k/10k divider from +12V to GPIO 35
#define EXAMPLE_RPM_PULSE_GPIO22 0   // tach / ignition pulse (via opto) on GPIO 22
#define EXAMPLE_FAKE_VALUES      0   // no wiring: animated test values

#if EXAMPLE_RPM_PULSE_GPIO22
static PulseInput tach;
#endif

void mySensorsBegin() {
#if EXAMPLE_BATTERY_GPIO35
    analogSetPinAttenuation(35, ADC_11db);                     // full 0..3.1 V range
#endif
#if EXAMPLE_RPM_PULSE_GPIO22
    tach.begin(22, 60.0f / 2);                                  // 2 pulses per rev (4-cyl)
#endif
}

void mySensorsRead(GaugeInput &in) {
#if EXAMPLE_BATTERY_GPIO35
    in.voltage = readDividerVolts(35, 47000, 10000);
#endif
#if EXAMPLE_RPM_PULSE_GPIO22
    in.rpm = tach.value();
#endif
#if EXAMPLE_FAKE_VALUES
    float t = millis() / 1000.0f;
    in.rpm = 3500 + 3000 * sinf(t * 0.8f);
    in.speed = 80 + 40 * sinf(t * 0.2f);
    in.coolant = 90 + 8 * sinf(t * 0.1f);
    in.voltage = 14.0f;
    in.intake = 40;
#endif

    // ---- your code here -----------------------------------------------------
    // in.coolant = readNtcCelsius(35, 2200, 2500, 3950);
    // in.speed   = myCanSpeed;
    (void)in;
}
