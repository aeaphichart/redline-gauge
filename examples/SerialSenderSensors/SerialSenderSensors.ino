/*
  REDLINE — SerialSenderSensors
  ---------------------------------------------------------------------------
  Reads real car / motorcycle sensors on an Arduino (Uno / Nano / Pro Mini)
  and streams them to a REDLINE gauge over one serial wire.

      Signal                 Arduino pin   Notes
      ---------------------  -----------   ------------------------------------------
      RPM (ignition/tach)    D2  (INT0)    via opto-isolator (PC817), NEVER direct
      Speed (VSS / hall)     D3  (INT1)    open-collector sensor, 5 V pull-up
      Battery voltage        A0            divider 47k (to +12V) / 10k (to GND)
      Coolant / oil temp     A1            NTC sender + 2.2k pull-up to 5 V
      Intake air temp        A2            NTC + 2.2k pull-up to 5 V

  Turn each one on/off below; disabled values are simply not sent (the gauge
  shows "--"). Wiring to the gauge: see SerialSenderDemo / examples/README.md.

  !! Car wiring is 12-14 V with big spikes. Protect every input: opto for
  !! pulses, dividers + a 5.1 V zener for analog. You connect it at your own risk.

  Copyright (c) 2026 moomdate — PolyForm Noncommercial 1.0.0
*/

#define GAUGE Serial            // serial port wired to the gauge (Uno/Nano: D1 / TX)

// ---- what is connected (1 = on) ------------------------------------------------
#define USE_RPM       1
#define USE_SPEED     0
#define USE_BATTERY   1
#define USE_COOLANT   0
#define USE_INTAKE    0

// ---- calibration ----------------------------------------------------------------------
// RPM: ignition pulses per crank revolution.
//   4-cyl wasted spark coil = 2, 4-cyl tach wire = 2, 6-cyl = 3,
//   most single-cylinder motorcycles (e.g. Honda Wave) = 1.
const float PULSES_PER_REV = 2.0;

// Speed: pulses per wheel revolution and wheel circumference.
const float SPEED_PULSES_PER_REV = 4.0;
const float WHEEL_CIRCUMFERENCE_M = 1.94;       // car 205/55R16 ≈ 1.94, Wave 17" ≈ 1.75

// Battery divider: V = adc_volts * (R1 + R2) / R2.  Tweak VREF if the reading is off
// (measure the Arduino's 5 V pin with a multimeter and put that value here).
const float VREF = 5.0;
const float R1 = 47000.0, R2 = 10000.0;

// NTC: pull-up value, sensor resistance at 25 °C, beta coefficient (datasheet).
const float NTC_PULLUP = 2200.0;
const float NTC_R25 = 2500.0;                   // many coolant senders ≈ 2.5 kΩ @ 25 °C
const float NTC_BETA = 3950.0;

const unsigned long SEND_EVERY_MS = 50;         // 20 Hz
const unsigned long PULSE_TIMEOUT_US = 500000;  // no pulse for 0.5 s = engine/wheel stopped

// ---- pulse measurement (interrupts) ---------------------------------------------------------
struct PulseMeter {
  volatile unsigned long last = 0, period = 0;
  void onPulse() {
    unsigned long now = micros(), d = now - last;
    if (d < 300) return;                                  // ignore ringing / noise
    period = period ? (period * 3 + d) / 4 : d;           // light smoothing
    last = now;
  }
  float hz() {
    noInterrupts();
    unsigned long p = period, l = last;
    interrupts();
    if (!p || micros() - l > PULSE_TIMEOUT_US) return 0;
    return 1e6 / p;
  }
};

PulseMeter rpmMeter, speedMeter;
void rpmIsr()   { rpmMeter.onPulse(); }
void speedIsr() { speedMeter.onPulse(); }

float readNtcC(int pin) {
  int adc = analogRead(pin);
  if (adc <= 5 || adc >= 1018) return NAN;              // open or shorted sensor
  float r = NTC_PULLUP * adc / (1023.0 - adc);
  return 1.0 / (log(r / NTC_R25) / NTC_BETA + 1.0 / 298.15) - 273.15;
}

void sendValue(const char *key, float v, int decimals) {
  if (isnan(v)) return;
  GAUGE.print(key);
  GAUGE.print('=');
  GAUGE.print(v, decimals);
  GAUGE.print(' ');
}

void setup() {
  GAUGE.begin(115200);
#if USE_RPM
  pinMode(2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(2), rpmIsr, FALLING);
#endif
#if USE_SPEED
  pinMode(3, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(3), speedIsr, FALLING);
#endif
  delay(1500);
  GAUGE.print("mode=serial\n");
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < SEND_EVERY_MS) return;
  last = millis();

#if USE_RPM
  sendValue("rpm", rpmMeter.hz() * 60.0 / PULSES_PER_REV, 0);
#endif
#if USE_SPEED
  sendValue("spd", speedMeter.hz() / SPEED_PULSES_PER_REV * WHEEL_CIRCUMFERENCE_M * 3.6, 0);
#endif
#if USE_BATTERY
  sendValue("volt", analogRead(A0) * VREF / 1023.0 * (R1 + R2) / R2, 1);
#endif
#if USE_COOLANT
  sendValue("clt", readNtcC(A1), 0);
#endif
#if USE_INTAKE
  sendValue("iat", readNtcC(A2), 0);
#endif
  GAUGE.print('\n');
}
