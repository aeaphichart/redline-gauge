#!/usr/bin/env python3
"""
Feed the gauge real-format values over USB serial (SERIAL mode) — the same path any
external device (another MCU, a Raspberry Pi, a CAN logger) would use.

  ~/.platformio/penv/bin/python tools/serial_feed.py /dev/cu.usbserial-XXXX           # sweep demo
  ~/.platformio/penv/bin/python tools/serial_feed.py /dev/cu.usbserial-XXXX --fixed "rpm=3000 spd=60"

Protocol: one line per update, key=value pairs, e.g.
  rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3
Needs pyserial (already inside PlatformIO's python).
"""
import argparse, math, sys, time
import serial

ap = argparse.ArgumentParser()
ap.add_argument("port")
ap.add_argument("--baud", type=int, default=115200)
ap.add_argument("--hz", type=float, default=20)
ap.add_argument("--fixed", help="send this line repeatedly instead of the sweep")
a = ap.parse_args()

s = serial.Serial(a.port, a.baud, timeout=0)
time.sleep(0.3)
s.write(b"mode=serial\n")
print("sending, Ctrl-C to stop")
t0 = time.time()
try:
    while True:
        t = time.time() - t0
        if a.fixed:
            line = a.fixed
        else:
            rpm = 900 + 6400 * (0.5 - 0.5 * math.cos(t * 0.9)) ** 1.5
            spd = 60 + 50 * math.sin(t * 0.15)
            clt = 80 + 28 * (0.5 - 0.5 * math.cos(t * 0.07))
            volt = 13.8 + 0.4 * math.sin(t * 0.5) - (2.4 if int(t) % 40 > 36 else 0)
            iat = 35 + 40 * (0.5 - 0.5 * math.cos(t * 0.05))
            line = f"rpm={rpm:.0f} spd={spd:.0f} clt={clt:.0f} volt={volt:.2f} iat={iat:.0f}"
        s.write((line + "\n").encode())
        while s.in_waiting:
            sys.stdout.write(s.read(s.in_waiting).decode(errors="replace"))
        time.sleep(1 / a.hz)
except KeyboardInterrupt:
    pass
