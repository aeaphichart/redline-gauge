/*
  REDLINE — SerialSenderDemo
  ---------------------------------------------------------------------------
  Sends animated test values to a REDLINE gauge. No sensors needed: use it to
  check the wiring first, then move on to SerialSenderSensors.

  Works on any Arduino-compatible board (Uno, Nano, Mega, Leonardo, ESP32, Pico…).

  Wiring (see examples/README.md):
    this board TX  ->  CYD GPIO 27 (CN1 connector)
    this board GND ->  CYD GND
    5 V boards (Uno/Nano/Mega): put a divider on TX -> 1k from TX, 2k to GND,
    and take GPIO 27 from the middle (5 V -> 3.3 V). 3.3 V boards connect directly.

  Protocol: one line per update, "key=value" pairs, ending with '\n':
    rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3
  Send only the values you have; anything not refreshed for 2.5 s shows "--".

  Copyright (c) 2026 moomdate — PolyForm Noncommercial 1.0.0
*/

// Which serial port is wired to the gauge. On an Uno/Nano this is "Serial"
// (pin D1 / TX), which is also what the USB Serial Monitor shows, so you
// can watch exactly what's being sent. Mega / ESP32: Serial1 works too.
// Boards whose "Serial" is native USB (Leonardo, Pro Micro, Micro, ESP32-S3/C3 with
// USB CDC on boot) have the TX pin on Serial1 instead.
#if defined(USBCON) || (defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
#define GAUGE Serial1
#else
#define GAUGE Serial            // Uno/Nano/Mega/ESP32: pin D1 / TX0
#endif

const unsigned long SEND_EVERY_MS = 50;   // 20 updates per second

void setup() {
  GAUGE.begin(115200);                    // must match EXT_SERIAL_BAUD on the gauge
  delay(1500);                            // give the gauge time to boot
  GAUGE.print("mode=serial\n");           // switch the gauge to SERIAL input
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < SEND_EVERY_MS) return;
  last = millis();

  float t = millis() / 1000.0;

  // a rev sweep that crosses the default 7000 rpm shift light
  float rpm   = 900 + 6600 * pow(0.5 - 0.5 * cos(t * 0.9), 1.5);
  float speed = 60 + 50 * sin(t * 0.15);
  float clt   = 80 + 28 * (0.5 - 0.5 * cos(t * 0.07));   // slowly into the warning zone
  float volt  = 13.9 + 0.3 * sin(t * 0.5);
  float iat   = 35 + 20 * (0.5 - 0.5 * cos(t * 0.05));

  GAUGE.print("rpm=");   GAUGE.print((long)rpm);
  GAUGE.print(" spd=");  GAUGE.print((int)speed);
  GAUGE.print(" clt=");  GAUGE.print((int)clt);
  GAUGE.print(" volt="); GAUGE.print(volt, 1);
  GAUGE.print(" iat=");  GAUGE.print((int)iat);
  GAUGE.print('\n');
}
