# Changelog

Ready-to-flash images for every version are on the [Releases page](https://github.com/moomdate/redline-gauge/releases)
(one per panel type, see [how to flash](docs/release-flashing.md)). Versions follow [semver](https://semver.org):
new features bump the middle number, fixes the last one.

## [1.5.3] - 2026-10-05

### Fixed
- **Dimmed screen staying dark after power-on**:
  - With BRIGHTNESS saved below 100 %, the `-dim` images booted with the backlight off. On some boards it also
    went dark when dimmed later.
  - Cause: the ESP32's LEDC accepts one duty change per PWM period. The brightness write right after attaching
    the backlight was silently lost.
  - This was never a hardware limit. Older notes saying "this CYD can't dim" were wrong.
- **Beeps were always full volume**: the 60 % volume write came right after the tone started and was lost
  the same way.
- **Display glitch on `-dim` images at 80 MHz** (shifted / torn picture coinciding with beeps):
  - Beeps are now at 60 % volume.
  - Touch clicks finish before the screen redraws.
  - The speaker and the backlight use fixed, separate PWM channels.

### Changed
- All images run 80 MHz SPI again, the `-dim` ones included (tested on the board that showed the glitch).
- `SPEAKER_VOLUME` (default 60 %) in `config.h`.

## [1.5.2] - 2026-10-05

### Changed
- **80 MHz SPI is back for the full-backlight images** (`invert`, `noinvert`). Root cause of the 1.5.0 glitch:
  80 MHz together with a PWM-dimmed backlight sometimes garbled the picture while Bluetooth started or stopped.
  With the backlight full-on, 80 MHz is clean (tested by switching SIM ↔ OBD BT repeatedly on the board
  that showed it).
  - The dimmable images (`invert-dim`, `noinvert-dim`) stay at 40 MHz.
  - Either way it runs 60 fps.

## [1.5.1] - 2026-10-05

### Fixed
- **Garbled screen** (shifted or torn text and images), seen now and then while switching the data source.
  The display link is back to 40 MHz SPI; 80 MHz was too close to the edge while Bluetooth started and stopped.
  Still 60 fps.
- **RESET PEAK**:
  - The button now lights up "CLEARED" when tapped, so the tap is visible.
  - A tap on its edge can no longer also change the PANEL button next to it.
  - The gauge's PEAK label stays empty after a reset (and while you sit at the peak), and appears once the
    engine has been higher than it is now. Before, it refilled with the current rpm at once and looked like
    the reset had done nothing.

## [1.5.0] - 2026-10-05

### Changed
- **Drag timer logs every run**, not only runs that reach 100 km/h.
  - A run that ends early (lift or brake) is saved with its **top speed and the time to reach it**, plus any
    splits it passed. The big number then shows that time ("0-144 KM/H (TOP SPEED) 10.30").
  - Only runs that never got past 30 km/h are skipped.
  - The log's last column is now TOP @TIME, and `timerlog` adds `to_max_s`.
  - Logs saved by 1.4.0 are cleared once by this update (the record layout changed).

### Added
- **`noinvert-dim` image** (`cyd-noinvert-dim` env): normal-colour panel with BRIGHTNESS. The flasher lists it.

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
