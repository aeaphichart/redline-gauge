# Changelog

Ready-to-flash images for every version are on the [Releases page](https://github.com/moomdate/redline-gauge/releases)
(one per panel type, see [how to flash](docs/release-flashing.md)). Versions follow [semver](https://semver.org):
new features bump the middle number, fixes the last one.

## [1.4.0] - 2026-10-05

### Added
- **Drag timer** (SETUP → TIMER, or `timer` over serial): 0-100, 100-120, 120-160 and 0-200 km/h from one run.
  - Arms itself after 1 s at a standstill (three amber lights) and starts when the car moves. The launch is
    back-estimated from the first samples, and every speed crossing is interpolated between two samples.
  - A beep at each split; green BEST / NEW BEST when you beat your log.
  - A run ends at 200 km/h, or when you lift or brake (speed 10 km/h under its max), and is saved with the
    splits it reached. Stopping for 3 s re-arms the timer.
  - **Run log** of the last 20 runs, stored on the board (also keeps 160-200 and top speed). Hold
    HOLD TO CLEAR to wipe it. Over serial: `timerlog` prints CSV, `timerlog=clear` wipes it.
  - Works with any source that has speed (OBD, SERIAL/GPS, CUSTOM, SIM). On OBD the timer screen polls speed
    every other request.
- Release images: factory and update `.bin` for each panel type, built by CI for every tag.
- **REDLINE Flasher** (`tools/flasher`): an app for Windows, macOS and Linux.
  - Picks the CYD's port automatically (you can choose another).
  - Downloads the chosen version from GitHub Releases, or flashes a local `.bin`.
  - Flashes as a factory install or as an update that keeps settings.
  - Also has a terminal mode (`--cli`).

### Changed
- The settings header shows the version and a TIMER button.
- Side-panel labels can use digits.

## [1.3.0] - 2026-10-04

### Added
- **Hybrid panels**: SETUP → PANEL AUTO / STD / HYB. HYB shows COOLANT · HV BATT % · HV POWER kW
  (negative = regen). AUTO switches to HYB as soon as the car reports hybrid data, e.g. a Honda Civic e:HEV
  over OBD.
- OBD reads hybrid battery SOC (PID 015B) and power from voltage × current (019A), only on cars that list them.
  It reads the supported-PID pages 0100 … 0180 and handles multi-frame replies.
- **GEAR AUTO / OFF** in SETUP (`gearmode=`): hide the gear on hybrids and CVTs.
- Serial data keys `soc=` and `kw=`. Commands `panels=` and `gearmode=`.
- **Dimmable-backlight build** (`esp32dev-dim` / the `invert-dim` image): BRIGHTNESS 20-100 % comes back on CYD
  batches whose backlight dims with PWM.

## [1.2.1] - 2026-10-02

### Fixed
- **OBD Bluetooth adapters that were never found**:
  - The scan now finds adapters by name pattern (OBDII, OBD2, V-LINK, Vgate, VEEPEAK, KONNWEI …) instead of
    requiring the exact name `OBDII`.
  - Unnamed Bluetooth 2.0 clones get a name lookup.
  - The MAC is remembered for the next boot.
  - It tries the PINs 1234, 0000, 6789 and 1111.
  - Pairing that uses SSP is accepted.
  - A stale pairing key is dropped.
- New serial commands `obd=scan`, `obd=AA:BB:…` and `obdpin=…`.
- ELM327 mini clones that answer `?` to some AT commands no longer loop on ELM ERROR, and a slow ATZ is tolerated.
- Name hints no longer match helmet intercoms ("HELMET").
- A PID that answered NO DATA while the ECU was waking up is no longer disabled for good. The car's
  supported-PID bitmask is used instead.
- The Arduino examples use `Serial1` on native-USB boards (Leonardo, Pro Micro, ESP32-S3/C3).
- The NTC open-circuit check now works with the ADC's ~3.1 V ceiling.
- CUSTOM mode shows live when values arrive via `gauge::set()`.

## [1.2.0] - 2026-10-01

### Changed
- **60 fps**: the screen pushes only what moved: the edge of the shift bar and the digits that changed.
  This is 2.4× faster per frame.
- SPI runs at 80 MHz. The old 65 MHz setting really ran at 40 MHz.

### Fixed
- The backlight is driven full-on and BRIGHTNESS is hidden, because PWM dimming blanks the screen on the
  common CYD.

### Docs
- Wiring diagrams: system overview, CYD connectors, Arduino serial, battery divider, NTC, RPM opto.

## [1.1.0] - 2026-09-28

### Added
- External serial input on GPIO 27 (CN1), so an Arduino or ESP32 can feed the gauge while USB stays free.
- Arduino examples: `SerialSenderDemo` and `SerialSenderSensors`.
- Boot splash with the birdlab.th credit.

### Fixed
- Issues found while following the BirdLab build article step by step.

## [1.0.0] - 2026-09-27

- First release: a racing-style dash for the ESP32 CYD.
  - 13-slot shift bar with a shift light, LED and beep.
  - RPM, speed and estimated gear.
  - Coolant, voltage and intake panels.
  - Three themes and a touch settings page.
- Data sources: SIM AUTO, SIM TOUCH, SERIAL, OBD-II over Bluetooth (ELM327) and CUSTOM sensors.
