// REDLINE — racing dash for the CYD (ESP32-2432S028R family)
// Copyright (c) 2026 moomdate. PolyForm Noncommercial 1.0.0 — see LICENSE.md
//
//   core 0 : data task  — active DataSource (simulator / serial / OBD-II BT / custom) + serial commands
//   core 1 : loop()     — touch, GaugeModel, partial-redraw renderer, settings page, LED + buzzer
//
// Touch (gauge):  SETUP button or status bar -> settings page
//                 hold main area   -> throttle, in SIM TOUCH (further right = more throttle)
//                 long-press main  -> reset peak RPM (other sources)
// Serial: mode=sim|touch|serial|obd|custom  theme=ice|lime|amber  shift=7000  bright=80
//         beep=on|off  peak=reset  help    — plus data lines in SERIAL mode.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <driver/gpio.h>

#include "config.h"
#include "settings.h"
#include "data/gauge_bus.h"
#include "data/sim_source.h"
#include "data/serial_source.h"
#include "data/obd_source.h"
#include "data/custom_source.h"
#include "ui/gauge_model.h"
#include "ui/gauge_ui.h"
#include "ui/settings_ui.h"
#include "ui/splash_ui.h"
#include "ui/theme.h"

static TFT_eSPI  tft;
//                    DCS DCLK DIN DOUT  (bit-banged XPT2046, proven for this board)
static TFT_Touch touch(33, 25, 32, 39);
static Preferences prefs;

// ---- data sources (order = SourceId in settings.h) -----------------------------------
static SimSource    simAuto(false);
static SimSource    simTouch(true);
static SerialSource serialSrc;
static ObdSource    obdSrc;
static CustomSource customSrc;
static DataSource *const sources[SRC_COUNT] = { &simAuto, &simTouch, &serialSrc, &obdSrc, &customSrc };

static volatile int requestedSrc = SRC_SIM_AUTO;
static volatile int activeSrc = -1;

// ---- settings --------------------------------------------------------------------------
static Settings settings;

static void loadSettings() {
    prefs.begin("gauge", false);
    settings.theme      = prefs.getUChar("theme", settings.theme);
    settings.source     = prefs.getUChar("src", settings.source);
    settings.shiftRpm   = prefs.getUShort("shift", settings.shiftRpm);
    settings.brightness = prefs.getUChar("bright", settings.brightness);
    settings.beep       = prefs.getBool("beep", settings.beep);
    if (settings.theme >= THEME_COUNT) settings.theme = 0;
    if (settings.source >= SRC_COUNT) settings.source = SRC_SIM_AUTO;
    settings.shiftRpm = constrain(settings.shiftRpm, Settings::kShiftMin, Settings::kShiftMax);
    settings.brightness = constrain(settings.brightness, Settings::kBrightMin, 100);
}

static void saveSettings() {   // Preferences only writes keys whose value changed
    prefs.putUChar("theme", settings.theme);
    prefs.putUChar("src", settings.source);
    prefs.putUShort("shift", settings.shiftRpm);
    prefs.putUChar("bright", settings.brightness);
    prefs.putBool("beep", settings.beep);
}

static void backlightBegin() {
#if BACKLIGHT_DIMMING
    ledcAttach(TFT_BL, 5000, 8);                     // after tft.init() has driven TFT_BL HIGH
#else
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);                      // full on: this board can't dim with PWM
#endif
}

static void setBacklight(uint8_t pct) {
#if BACKLIGHT_DIMMING
    ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
    (void)pct;
#endif
}

// ---- serial commands --------------------------------------------------------------------------
// Read in loop() (core 1), NOT in the data task: the OBD source can sit in a blocking
// Bluetooth connect for 10+ s, and commands must still work meanwhile.
static void applySettings(const Settings &next, bool repaint = true);
static GaugeModel model;
static void printHelp() {
    Serial.println(F(
        "\n=== REDLINE " REDLINE_VERSION " — crafted by birdlab.th (birdlab.moomdate.tech) ===\n"
        "  mode=sim|touch|serial|obd|custom   theme=ice|lime|amber   shift=7000\n"
        "  bright=20..100   beep=on|off   peak=reset   help\n"
        "  obd=scan | obd=AA:BB:CC:DD:EE:FF   obdpin=1234|0000 (empty = auto)\n"
        "data (SERIAL mode):  rpm=3200 spd=86 clt=87 volt=13.9 iat=42 gear=3\n"
        "           or JSON:  {\"rpm\":3200,\"speed\":86,\"coolant\":87,\"voltage\":13.9}\n"));
}

static bool keyIs(const char *p, const char *key, const char **val) {
    size_t n = strlen(key);
    if (strncasecmp(p, key, n)) return false;
    p += n;
    if (*p != '=' && *p != ':' && *p != ' ') return false;
    while (*p == '=' || *p == ':' || *p == ' ') p++;
    *val = p;
    return true;
}

static void runBench();

static void handleLine(char *line) {
    char *p = line;
    while (*p == ' ') p++;
    if (!*p) return;
    const char *v;

    Settings s = settings;
    bool changed = true;

    if (keyIs(p, "mode", &v)) {
        static const char *const keys[SRC_COUNT] = { "sim", "touch", "serial", "obd", "custom" };
        int found = -1;
        for (int i = 0; i < SRC_COUNT; i++) if (!strncasecmp(v, keys[i], strlen(keys[i]))) found = i;
        if (!strncasecmp(v, "auto", 4)) found = SRC_SIM_AUTO;
        if (found < 0) { Serial.println("[gauge] unknown mode"); return; }
        s.source = found;
    } else if (keyIs(p, "theme", &v)) {
        int found = -1;
        for (int i = 0; i < THEME_COUNT; i++) if (!strncasecmp(v, kThemes[i].key, strlen(kThemes[i].key))) found = i;
        if (found < 0) { Serial.println("[gauge] themes: ice lime amber"); return; }
        s.theme = found;
    } else if (keyIs(p, "shift", &v)) {
        s.shiftRpm = constrain(atoi(v), Settings::kShiftMin, Settings::kShiftMax);
    } else if (keyIs(p, "bright", &v)) {
#if BACKLIGHT_DIMMING
        s.brightness = constrain(atoi(v), Settings::kBrightMin, 100);
#else
        Serial.println("[gauge] brightness is fixed: this board's backlight can't dim (BACKLIGHT_DIMMING 0)");
        return;
#endif
    } else if (keyIs(p, "beep", &v)) {
        s.beep = !strncasecmp(v, "on", 2) || *v == '1';
    } else if (keyIs(p, "obdpin", &v)) {
        char arg[16];
        snprintf(arg, sizeof arg, "pin=%s", v);
        ObdSource::command(arg);
        return;
    } else if (keyIs(p, "obd", &v)) {
        if (!ObdSource::command(v)) Serial.println("[gauge] obd=scan | obd=AA:BB:CC:DD:EE:FF | obdpin=1234");
        return;
    } else if (!strncasecmp(p, "bench", 5)) {
        runBench();
        return;
    } else if (!strncasecmp(p, "peak", 4)) {
        model.resetPeaks();
        Serial.println("[gauge] ok  peak reset");
        return;
    } else if (!strncasecmp(p, "help", 4) || *p == '?') {
        printHelp();
        return;
    } else {
        changed = false;
    }

    if (changed) {
        applySettings(s);
        // confirm, so someone typing into `pio device monitor` sees it worked
        Serial.printf("[gauge] ok  theme=%s shift=%u bright=%u%% beep=%s source=%s\n",
                      kThemes[settings.theme].key, settings.shiftRpm, settings.brightness,
                      settings.beep ? "on" : "off", sources[settings.source]->name());
        return;
    }
    int n = SerialSource::feed(p, activeSrc == SRC_SERIAL);
    // An external board may stream 20 lines/s: each hint prints at most every 5 s.
    static uint32_t lastIgnored = 0, lastUnknown = 0;
    uint32_t now = millis();
    if (n && activeSrc != SRC_SERIAL && (!lastIgnored || now - lastIgnored > 5000)) {
        Serial.println("[gauge] data ignored — switch to SERIAL first (mode=serial)");
        lastIgnored = now;
    } else if (!n && (!lastUnknown || now - lastUnknown > 5000)) {
        Serial.printf("[gauge] ? unknown: '%.40s' — type help\n", p);
        lastUnknown = now;
    }
}

// Collects one text line per input; USB and the external UART each keep their own
// buffer so interleaved bytes from the two can't corrupt each other.
struct LineReader {
    char   buf[160];
    size_t len = 0;
    void poll(Stream &in) {
        while (in.available()) {
            char c = (char)in.read();
            if (c == '\n' || c == '\r') {
                buf[len] = 0;
                if (len) handleLine(buf);
                len = 0;
            } else if (len < sizeof(buf) - 1) {
                buf[len++] = c;
            }
        }
    }
};

static void pollSerial() {
    static LineReader usb;
    usb.poll(Serial);
#if EXT_SERIAL_RX_PIN >= 0
    static LineReader ext;
    ext.poll(Serial2);
#endif
}

static void dataTask(void *) {
    for (;;) {
        int req = requestedSrc;
        if (req != activeSrc) {
            if (activeSrc >= 0) sources[activeSrc]->end();
            bus::clear();
            Serial.printf("[gauge] source -> %s\n", sources[req]->name());
            sources[req]->begin();
            activeSrc = req;
        }
        sources[activeSrc]->poll();
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

// ---- outputs: RGB LED + speaker ---------------------------------------------------------
static uint32_t toneOffAt = 0;

static void beep(uint32_t freq, uint32_t ms, bool force = false) {
    if (!settings.beep && !force) return;
    ledcWriteTone(PIN_SPEAKER, freq);
    toneOffAt = millis() + ms;
}

static void setLed(bool r, bool g, bool b) {       // active LOW
    digitalWrite(PIN_LED_R, !r);
    digitalWrite(PIN_LED_G, !g);
    digitalWrite(PIN_LED_B, !b);
}

static void updateOutputs(const GaugeView *v) {
    static bool wasShift = false, wasCrit = false;
    bool shift = v && v->shift;
    bool crit = v && ((v->coolValid && v->coolLvl == LV_CRIT) || (v->voltValid && v->voltLvl == LV_CRIT) ||
                      (v->iatValid && v->iatLvl == LV_CRIT));

    if (shift)      setLed(v->shiftFlash, false, false);      // red strobe = shift now
    else if (crit)  setLed(v->blink, v->blink, false);         // amber blink = check a gauge
    else            setLed(false, false, false);

    if (shift && !wasShift) beep(3200, 70);
    if (crit && !wasCrit && !shift) beep(1100, 180);
    wasShift = shift;
    wasCrit = crit;

    if (toneOffAt && (int32_t)(millis() - toneOffAt) >= 0) {
        ledcWriteTone(PIN_SPEAKER, 0);
        toneOffAt = 0;
    }
}

// ---- screens ------------------------------------------------------------------------------------
enum Screen { SCR_GAUGE, SCR_SETTINGS };
static Screen screen = SCR_GAUGE;

// Apply `next` over the current settings; only what actually changed is touched.
// repaint=false when the settings page already redrew itself (tap on the page).
static void applySettings(const Settings &next, bool repaint) {
    Settings prev = settings;
    settings = next;
    if (next.source != prev.source) {
        requestedSrc = next.source;
        if (activeSrc == SRC_OBD) Serial.println("[gauge] switching after the Bluetooth attempt finishes…");
    }
    if (next.brightness != prev.brightness) setBacklight(next.brightness);
    model.setShiftRpm(next.shiftRpm);
    if (screen == SCR_GAUGE) {
        if (next.theme != prev.theme) gauge_ui::setTheme(kThemes[next.theme]);
    } else if (repaint) {
        settings_ui::draw(settings);
    }
    saveSettings();
}

static void openSettings() {
    screen = SCR_SETTINGS;
    SimSource::touchThrottle = 0;
    settings_ui::draw(settings);
}

static void closeSettings() {
    screen = SCR_GAUGE;
    gauge_ui::setTheme(kThemes[settings.theme]);   // repaint background + all regions
}

// ---- touch ------------------------------------------------------------------------------------
static void handleTouch() {
    static bool down = false, longDone = false;
    static uint32_t downAt = 0;
    static int sx = 0, sy = 0;

    bool pressed = touch.Pressed();
    int x = pressed ? touch.X() : sx, y = pressed ? touch.Y() : sy;
    if (pressed && !down) { down = true; longDone = false; downAt = millis(); sx = x; sy = y; }

    if (screen == SCR_SETTINGS) {
        if (!pressed && down) {                  // act on release, at the press position
            down = false;
            Settings s = settings;
            switch (settings_ui::tap(sx, sy, s)) {
                case settings_ui::ACT_CHANGED:    beep(2400, 15, true); applySettings(s, false); break;
                case settings_ui::ACT_RESET_PEAK: beep(1800, 40, true); model.resetPeaks(); break;
                case settings_ui::ACT_CLOSE:      beep(2400, 15, true); closeSettings(); break;
                default: break;
            }
        }
        return;
    }

    if (activeSrc == SRC_SIM_TOUCH) {
        SimSource::touchThrottle = (pressed && gauge_ui::hitMain(sx, sy))
                                   ? 0.25f + 0.75f * constrain(x, 0, 215) / 215.0f : 0.0f;
    }

    if (pressed && !longDone && millis() - downAt > TOUCH_LONGPRESS_MS &&
        gauge_ui::hitMain(sx, sy) && activeSrc != SRC_SIM_TOUCH) {
        longDone = true;
        model.resetPeaks();
        beep(1800, 40, true);
    }

    if (!pressed && down) {
        down = false;
        if (!longDone && millis() - downAt < 600 &&
            (gauge_ui::hitSetup(sx, sy) || gauge_ui::hitStatus(sx, sy))) {
            beep(2400, 15, true);
            openSettings();
        }
    }
}

// ---- display ------------------------------------------------------------------------------------
static uint32_t g_pushUs = 0, g_pushPx = 0;   // bench counters

static void pushToTft(int x, int y, int w, int h, const uint16_t *px) {
    uint32_t t = micros();
    tft.pushImage(x, y, w, h, px);
    g_pushUs += micros() - t;
    g_pushPx += (uint32_t)w * h;
}

// `bench` over serial: render a fixed 0 -> 8000 -> 0 rpm sweep as fast as possible and
// report where the time goes. Same workload every run, so tuning changes are comparable.
static void runBench() {
    GaugeSnapshot snap;
    bus::snapshot(snap);
    GaugeView v;
    model.update(snap, millis(), "BENCH", v);
    v.rpmValid = v.speedValid = v.coolValid = v.voltValid = v.iatValid = true;
    v.gear = 3;
    gauge_ui::invalidate();
    gauge_ui::render(v);                               // full draw first, not measured
    const int N = 300;
    uint32_t worst = 0;
    g_pushUs = g_pushPx = 0;
    uint32_t t0 = micros();
    for (int i = 0; i < N; i++) {
        float ph = (float)i / N, tri = ph < 0.5f ? ph * 2 : (1 - ph) * 2;
        v.rpmBar = tri * RPM_MAX;
        v.rpmText = (int)(v.rpmBar / RPM_DISPLAY_STEP) * RPM_DISPLAY_STEP;
        v.peakMarker = 0;
        v.sessionPeak = RPM_MAX;
        v.shift = v.rpmBar >= RPM_SHIFT;
        v.shiftFlash = (i / 4) & 1;
        v.speed = (int)(tri * 180);
        v.coolant = 85 + (i / 30) % 5;
        v.iat = 40 + (i / 40) % 3;
        v.volt = 13.8f + ((i / 25) % 3) * 0.1f;
        uint32_t f0 = micros();
        gauge_ui::render(v);
        uint32_t d = micros() - f0;
        if (d > worst) worst = d;
    }
    uint32_t total = micros() - t0;
    Serial.printf("[bench] %d frames: avg %.2f ms (max %.2f) = %.0f fps possible | push %.2f ms + compose %.2f ms"
                  " per frame | %.1f KB/frame\n", N, total / 1000.0f / N, worst / 1000.0f, 1e6f * N / total,
                  g_pushUs / 1000.0f / N, (total - g_pushUs) / 1000.0f / N, g_pushPx * 2 / 1024.0f / N);
    gauge_ui::invalidate();                            // repaint the real values next frame
}

void setup() {
    Serial.begin(SERIAL_BAUD);
#if EXT_SERIAL_RX_PIN >= 0
    Serial2.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX_PIN, -1);   // RX only
    gpio_pullup_en((gpio_num_t)EXT_SERIAL_RX_PIN);   // idle-high when nothing is plugged in (no noise)
#endif
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    setLed(false, false, false);
    ledcAttach(PIN_SPEAKER, 2000, 8);               // Arduino-ESP32 core 3.x API
    ledcWriteTone(PIN_SPEAKER, 0);

    loadSettings();

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);                          // canvas holds plain RGB565
    backlightBegin();
    setBacklight(settings.brightness);
    touch.setCal(526, 3443, 750, 3377, 320, 240, 1); // proven values for this board

    // Boot splash with the birdlab.th credit (required by NOTICE); its mini shift
    // bar fills as a progress bar. A tap skips it, but only after kSkipAfterMs.
    splash_ui::draw(pushToTft, kThemes[settings.theme]);
    for (uint32_t t0 = millis(), t; (t = millis() - t0) < splash_ui::kDurationMs;) {
        splash_ui::progress(pushToTft, kThemes[settings.theme], (float)t / splash_ui::kDurationMs);
        if (t > splash_ui::kSkipAfterMs && touch.Pressed()) break;
        delay(16);
    }
    splash_ui::progress(pushToTft, kThemes[settings.theme], 1.0f);
    beep(2600, 25);                                  // short "ready" chirp (if beep is on)
    delay(25);
    ledcWriteTone(PIN_SPEAKER, 0);
    toneOffAt = 0;
    delay(155);
    while (touch.Pressed()) delay(10);               // don't let the skip-tap reach the gauge

    gauge_ui::begin(pushToTft, kThemes[settings.theme]);
    model.setShiftRpm(settings.shiftRpm);
    requestedSrc = settings.source;

    xTaskCreatePinnedToCore(dataTask, "data", 8192, nullptr, 2, nullptr, 0);
    printHelp();
    Serial.printf("[gauge] theme %s, source %s, shift %u, free heap %u\n", kThemes[settings.theme].name,
                  sources[settings.source]->name(), settings.shiftRpm, (unsigned)ESP.getFreeHeap());
}

void loop() {
    static GaugeView view;
    static int shownSrc = -2;
    static uint32_t nextFrame = 0, statAt = 0, frames = 0, pushes = 0;

    uint32_t now = millis();
    if ((int32_t)(now - nextFrame) < 0) { delay(1); return; }
    nextFrame = now + 1000 / UI_FPS;

    pollSerial();

    handleTouch();

    int src = activeSrc;
    if (src != shownSrc) { model.reset(now); shownSrc = src; }

    GaugeSnapshot snap;
    bus::snapshot(snap);
    model.update(snap, now, src >= 0 ? sources[src]->name() : "", view);

    if (screen == SCR_GAUGE) {
        gauge_ui::render(view);
        updateOutputs(&view);
        pushes += gauge_ui::lastPushedRegions();
    } else {
        updateOutputs(nullptr);                      // quiet while in settings
    }

    frames++;
    if (now - statAt > 10000) {
        Serial.printf("[gauge] %s  rpm=%d spd=%d clt=%d v=%.1f iat=%d  ui %.1f fps, %.1f regions/frame, heap %u\n",
                      src >= 0 ? sources[src]->name() : "-", view.rpmText, view.speed, view.coolant,
                      view.volt, view.iat, frames * 1000.0f / (now - statAt), (float)pushes / frames,
                      (unsigned)ESP.getFreeHeap());
        statAt = now; frames = 0; pushes = 0;
    }
}
