#pragma once
#define REDLINE_VERSION "1.0.0"
// ============================================================================
//  Smart gauge configuration — everything you'd tune for a specific car lives here.
// ============================================================================

// ---- RPM bar / shift light -------------------------------------------------
#define RPM_MAX            8000    // full scale of the 13-slot shift bar
#define RPM_SHIFT          7000    // shift light: bar flashes, LED red, beep
#define RPM_IDLE            850    // simulator idle
#define RPM_DISPLAY_STEP     50    // big RPM number is rounded to this (reduces flicker)

// ---- Warning thresholds (value turns yellow = warn, red + blink = critical) --
#define COOLANT_COLD         60    // below: shown blue (engine not warm yet)
#define COOLANT_WARN        100
#define COOLANT_CRIT        105
#define VOLT_LOW_CRIT      11.8f
#define VOLT_LOW_WARN      12.4f
#define VOLT_HIGH_WARN     14.8f
#define VOLT_HIGH_CRIT     15.2f
#define IAT_WARN             55
#define IAT_CRIT             70

// Ranges of the thin bar graphs under each side-panel value.
#define COOLANT_BAR_MIN      40
#define COOLANT_BAR_MAX     120
#define VOLT_BAR_MIN       10.0f
#define VOLT_BAR_MAX       16.0f
#define IAT_BAR_MIN         -10
#define IAT_BAR_MAX          80

// ---- Data freshness ----------------------------------------------------------
#define DATA_STALE_MS      2500    // a value not refreshed for this long shows "--"
#define PEAK_HOLD_MS       1500    // peak marker on the shift bar holds, then falls

// ---- Gear estimation (used when the source doesn't report a gear, e.g. OBD) --
// gear = the ratio closest to  engine_rpm / wheel_rpm / FINAL_DRIVE.
// Defaults are a typical 6-speed hatchback; set GEAR_COUNT 0 to hide the gear.
#define GEAR_COUNT           6
#define GEAR_RATIOS        { 3.636f, 2.235f, 1.521f, 1.137f, 0.891f, 0.707f }
#define FINAL_DRIVE        4.06f
#define TIRE_CIRCUMFERENCE_M 1.94f  // e.g. 205/55R16 ≈ 1.94 m
#define GEAR_TOLERANCE     0.12f    // ±12 % of a ratio still counts as that gear

// ---- OBD-II over Bluetooth Classic (ELM327 "OBDII" dongles) -----------------
// BLE-only adapters (Vgate iCar Pro BLE, some Veepeak) are NOT supported by this path.
#define OBD_BT_NAME        "OBDII"  // adapter's Bluetooth name
#define OBD_BT_MAC         ""       // optional "AA:BB:CC:DD:EE:FF" — faster, skips discovery
#define OBD_BT_PIN         "1234"   // most clones use 1234 or 0000
#define OBD_CMD_TIMEOUT_MS 1500

// ---- Serial input (USB) ------------------------------------------------------
#define SERIAL_BAUD      115200

// ---- Hardware (CYD) -----------------------------------------------------------
#define PIN_SPEAKER          26
#define PIN_LED_R             4     // RGB LED, active LOW
#define PIN_LED_G            16
#define PIN_LED_B            17
#define SHIFT_BEEP            1     // 1 = short beep when crossing RPM_SHIFT
#define TOUCH_LONGPRESS_MS  800

// ---- Side-panel captions (uppercase A-Z and spaces only) ----------------------
// Air-cooled motorcycle (e.g. Honda Wave)? publish oil temp on CH_COOLANT and
// rename the panel "OIL TEMP".
#define PANEL1_LABEL  "COOLANT"
#define PANEL2_LABEL  "VOLTAGE"
#define PANEL3_LABEL  "INTAKE"

// ---- UI ------------------------------------------------------------------------
#define UI_FPS               30
#define STATUS_TITLE       "REDLINE"
