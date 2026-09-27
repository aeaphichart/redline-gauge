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

static void setBacklight(uint8_t pct) {
    ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
}

// ---- serial commands --------------------------------------------------------------------------
// Read in loop() (core 1), NOT in the data task: the OBD source can sit in a blocking
// Bluetooth connect for 10+ s, and commands must still work meanwhile.
static void applySettings(const Settings &next, bool repaint = true);
static GaugeModel model;
static void printHelp() {
    Serial.println(F(
        "\n=== REDLINE " REDLINE_VERSION " ===\n"
        "  mode=sim|touch|serial|obd|custom   theme=ice|lime|amber   shift=7000\n"
        "  bright=20..100   beep=on|off   peak=reset   help\n"
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
        s.brightness = constrain(atoi(v), Settings::kBrightMin, 100);
    } else if (keyIs(p, "beep", &v)) {
        s.beep = !strncasecmp(v, "on", 2) || *v == '1';
    } else if (!strncasecmp(p, "peak", 4)) {
        model.resetPeaks();
        return;
    } else if (!strncasecmp(p, "help", 4) || *p == '?') {
        printHelp();
        return;
    } else {
        changed = false;
    }

    if (changed) {
        applySettings(s);
        return;
    }
    int n = SerialSource::feed(p, activeSrc == SRC_SERIAL);
    if (n && activeSrc != SRC_SERIAL)
        Serial.println("[gauge] data ignored — switch to SERIAL first (mode=serial)");
}

static void pollSerial() {
    static char line[160];
    static size_t len = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            if (len) handleLine(line);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
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
static void pushToTft(int x, int y, int w, int h, const uint16_t *px) {
    tft.pushImage(x, y, w, h, px);
}

void setup() {
    Serial.begin(SERIAL_BAUD);
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
    ledcAttach(TFT_BL, 5000, 8);                     // backlight PWM (after init set it HIGH)
    setBacklight(settings.brightness);
    touch.setCal(526, 3443, 750, 3377, 320, 240, 1); // proven values for this board

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
