// Host unit tests — run with:  pio test -e native
// Everything here is the real firmware code, compiled for the host.
#include <unity.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "settings.h"
#include "data/gauge_bus.h"
#include "data/obd_parse.h"
#include "data/serial_source.h"
#include "data/sim_source.h"
#include "ui/canvas.h"
#include "ui/gauge_model.h"
#include "ui/gauge_ui.h"
#include "ui/settings_ui.h"
#include "ui/splash_ui.h"
#include "fonts/font_title.h"
#include "assets/logo_birdlab.h"
#include "ui/shift_slots.h"
#include "ui/theme.h"
#include "fonts/font_label.h"
#include "fonts/font_rpm.h"
#include "fonts/font_small.h"
#include "fonts/font_speed.h"
#include "fonts/font_ui.h"
#include "fonts/font_value.h"

extern uint32_t g_host_ms;

// ---- fake display ----------------------------------------------------------------
static uint16_t fb[320 * 240];
static long pushedPx = 0;
static int pushes = 0;
static void push(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
    pushedPx += (long)w * h;
    pushes++;
}

void setUp() {}
void tearDown() {}

// ---- canvas / colour ----------------------------------------------------------------
static void test_rgb565_and_blend() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(0xFFFFFF));
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(0xFF0000));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0x00FF00));
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0x0000FF));
    TEST_ASSERT_EQUAL_HEX16(0x1234, blend565(0, 0xFFFF, 0x1234));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, blend565(255, 0xABCD, 0x0000));
    uint16_t mid = blend565(128, 0xFFFF, 0x0000);
    TEST_ASSERT_INT_WITHIN(2, 15, mid >> 11);           // ~half red
}

static void test_canvas_clips_and_rejects_oversize() {
    static uint16_t buf[100];
    static uint16_t bg[320 * 240];
    Canvas cv(buf, 100);
    TEST_ASSERT_FALSE(cv.begin(0, 0, 20, 20, bg));      // 400 px > capacity
    TEST_ASSERT_TRUE(cv.begin(10, 10, 10, 10, bg));
    cv.fillRect(0, 0, 320, 240, 0xFFFF);                // way outside: must clip, not overflow
    for (int i = 0; i < 100; i++) TEST_ASSERT_EQUAL_HEX16(0xFFFF, buf[i]);
    cv.pixel(-5, 500, 0x0000);                          // out of range: ignored
}

static void test_tabular_digits_have_equal_width() {
    static uint16_t buf[16];
    Canvas cv(buf, 16);
    int w = cv.textWidth(font_rpm, "0000", 0, true);
    const char *samples[] = { "1111", "4444", "7777", "1234", "9876" };
    for (const char *s : samples) TEST_ASSERT_EQUAL_INT(w, cv.textWidth(font_rpm, s, 0, true));
}

// Every string the UI draws must exist in the font it's drawn with.
static bool fontHas(const GaugeFont &f, const char *s) {
    while (*s) {
        uint8_t c = (uint8_t)*s;
        uint16_t code = c;
        if (c >= 0xC0) { code = (uint16_t)(((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F)); s++; }
        s++;
        bool found = false;
        for (int i = 0; i < f.count; i++) if (f.glyphs[i].code == code) found = true;
        if (!found) { printf("missing glyph U+%04X\n", code); return false; }
    }
    return true;
}

static void test_fonts_cover_every_ui_string() {
    TEST_ASSERT_TRUE(fontHas(font_rpm, "0123456789,-"));
    TEST_ASSERT_TRUE(fontHas(font_speed, "0123456789-N"));
    TEST_ASSERT_TRUE(fontHas(font_value, "0123456789.-"));
    TEST_ASSERT_TRUE(fontHas(font_label, "SPEED " PANEL1_LABEL PANEL2_LABEL PANEL3_LABEL));
    TEST_ASSERT_TRUE(fontHas(font_small, "ENGINE SPEED PEAK RPM km/h GEAR °C V SETUP " STATUS_TITLE
                                         "BEEP ON OFF RESET THEME DATA SOURCE SHIFT LIGHT BRIGHTNESS"));
    TEST_ASSERT_TRUE(fontHas(font_ui, "SETTINGS DONE + - 7,000 100%"));
    for (int t = 0; t < THEME_COUNT; t++) TEST_ASSERT_TRUE(fontHas(font_ui, kThemes[t].name));
    for (int i = 0; i < SRC_COUNT; i++) TEST_ASSERT_TRUE(fontHas(font_ui, settings_ui::kSourceLabels[i]));
}

static void test_font_tables_sorted() {
    const GaugeFont *fonts[] = { &font_rpm, &font_speed, &font_value, &font_label, &font_small, &font_ui };
    for (const GaugeFont *f : fonts)
        for (int i = 1; i < f->count; i++) TEST_ASSERT_TRUE(f->glyphs[i - 1].code < f->glyphs[i].code);
}

// ---- theme assets ---------------------------------------------------------------------------
// The live shift-bar segments are drawn onto slots painted in the art. A new or
// re-processed background must keep those slots where SLOT_SPAN says they are.
static void test_theme_backgrounds_match_slot_geometry() {
    for (int t = 0; t < THEME_COUNT; t++) {
        const uint16_t *bg = kThemes[t].background;
        for (int r = 0; r < SLOT_ROWS; r += 4)
            for (int s = 0; s < SLOT_COUNT; s++) {
                int y = SLOT_Y0 + r, xm = (SLOT_SPAN[r][s][0] + SLOT_SPAN[r][s][1]) / 2;
                uint16_t c = bg[y * 320 + xm];
                int lum = ((c >> 11) << 3) + (((c >> 5) & 63) << 2) + ((c & 31) << 3);
                TEST_ASSERT_TRUE_MESSAGE(lum > 60, kThemes[t].name);   // slot interior is lit grey/red, not black
            }
    }
}

// ---- serial protocol --------------------------------------------------------------------------
static void test_serial_key_value() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(6, SerialSource::feed("rpm=3200 spd=86 clt=87 volt=13.9 iat=-5 gear=3", true));
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_FLOAT(3200, s.value[CH_RPM]);
    TEST_ASSERT_EQUAL_FLOAT(86, s.value[CH_SPEED]);
    TEST_ASSERT_EQUAL_FLOAT(-5, s.value[CH_IAT]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.9f, s.value[CH_VOLTAGE]);
}

static void test_serial_json_and_aliases() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(4, SerialSource::feed("{\"rpm\":4100,\"speed\":120.5,\"coolant\":99,\"voltage\":14.1}", true));
    TEST_ASSERT_EQUAL_INT(2, SerialSource::feed("RPM:900;V:12.2", true));
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_FLOAT(900, s.value[CH_RPM]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.5f, s.value[CH_SPEED]);
}

static void test_serial_ignores_garbage() {
    bus::clear();
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("hello world", true));
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("", true));
    TEST_ASSERT_EQUAL_INT(0, SerialSource::feed("foo=12 bar=3", true));
    TEST_ASSERT_EQUAL_INT(1, SerialSource::feed("rpm=100", false));   // counted, not published
    GaugeSnapshot s;
    bus::snapshot(s);
    TEST_ASSERT_EQUAL_UINT32(0, s.stamp[CH_RPM]);
}

// ---- OBD replies ---------------------------------------------------------------------------------
static void test_obd_parse() {
    float v = 0;
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410C1AF8", 0x0C, v));
    TEST_ASSERT_EQUAL_FLOAT(1726, v);                                   // (0x1A*256+0xF8)/4
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("SEARCHING...410C0FA0", 0x0C, v));
    TEST_ASSERT_EQUAL_FLOAT(1000, v);
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410D56", 0x0D, v));
    TEST_ASSERT_EQUAL_FLOAT(86, v);
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("41057B", 0x05, v));
    TEST_ASSERT_EQUAL_FLOAT(83, v);                                     // 0x7B - 40
    TEST_ASSERT_EQUAL(OBD_OK, obdParsePid("410F28", 0x0F, v));
    TEST_ASSERT_EQUAL_FLOAT(0, v);
    TEST_ASSERT_EQUAL(OBD_NODATA, obdParsePid("NODATA", 0x0C, v));
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("?", 0x0C, v));
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("410C1A", 0x0C, v));        // truncated
    TEST_ASSERT_EQUAL(OBD_BAD, obdParsePid("UNABLETOCONNECT", 0x0D, v));
    TEST_ASSERT_EQUAL(OBD_OK, obdParseVolt("13.9V", v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.9f, v);
    TEST_ASSERT_EQUAL(OBD_BAD, obdParseVolt("?", v));
}

// ---- model ----------------------------------------------------------------------------------------
static void settle(GaugeModel &m, GaugeView &v, float rpm, float spd, float clt, float volt, float iat,
                   int frames = 60) {
    for (int i = 0; i < frames; i++) {
        g_host_ms += 33;
        bus::publish(CH_RPM, rpm);
        bus::publish(CH_SPEED, spd);
        bus::publish(CH_COOLANT, clt);
        bus::publish(CH_VOLTAGE, volt);
        bus::publish(CH_IAT, iat);
        GaugeSnapshot s;
        bus::snapshot(s);
        m.update(s, g_host_ms, "TEST", v);
    }
}

static void test_gear_estimate() {
    static const float ratios[] = GEAR_RATIOS;
    for (int g = 0; g < GEAR_COUNT; g++) {
        float kmh = 60;
        float wheelRpm = (kmh / 3.6f) / TIRE_CIRCUMFERENCE_M * 60.0f;
        TEST_ASSERT_EQUAL_INT(g + 1, estimateGear(wheelRpm * ratios[g] * FINAL_DRIVE, kmh));
    }
    TEST_ASSERT_EQUAL_INT(0, estimateGear(900, 1));          // standing still
    TEST_ASSERT_EQUAL_INT(0, estimateGear(6000, 20));        // clutch slipping, no ratio fits
}

static void test_model_levels_and_shift() {
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 10000;
    m.reset(g_host_ms);
    m.setShiftRpm(6500);
    settle(m, v, 6600, 100, COOLANT_CRIT + 1, VOLT_LOW_CRIT - 0.5f, IAT_WARN + 1);
    TEST_ASSERT_TRUE(v.rpmValid);
    TEST_ASSERT_TRUE(v.shift);
    TEST_ASSERT_EQUAL(LV_CRIT, v.coolLvl);
    TEST_ASSERT_EQUAL(LV_CRIT, v.voltLvl);
    TEST_ASSERT_EQUAL(LV_WARN, v.iatLvl);
    TEST_ASSERT_EQUAL_INT(0, v.rpmText % RPM_DISPLAY_STEP);
    settle(m, v, 3000, 60, COOLANT_COLD - 5, 14.0f, 30);
    TEST_ASSERT_FALSE(v.shift);
    TEST_ASSERT_EQUAL(LV_COLD, v.coolLvl);
    TEST_ASSERT_EQUAL(LV_NORMAL, v.voltLvl);
    TEST_ASSERT_EQUAL_INT(6600, v.sessionPeak);
    m.resetPeaks();
    settle(m, v, 3000, 60, 90, 14, 30, 2);
    TEST_ASSERT_EQUAL_INT(3000, v.sessionPeak);
}

static void test_model_marks_stale_values() {
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 50000;
    m.reset(g_host_ms);
    settle(m, v, 2000, 50, 90, 14, 30, 5);
    TEST_ASSERT_TRUE(v.rpmValid && v.speedValid && v.coolValid);
    g_host_ms += DATA_STALE_MS + 10;                         // nothing published since
    GaugeSnapshot s;
    bus::snapshot(s);
    m.update(s, g_host_ms, "TEST", v);
    TEST_ASSERT_FALSE(v.rpmValid);
    TEST_ASSERT_FALSE(v.speedValid);
    TEST_ASSERT_FALSE(v.coolValid);
}

// ---- simulator ----------------------------------------------------------------------------------
static void test_simulator_stays_physical() {
    SimSource sim(false);
    bus::clear();
    g_host_ms = 1000;
    sim.begin();
    bool sawShift = false;
    float maxSpd = 0;
    for (int i = 0; i < 200 * 100; i++) {                    // 200 s at 100 Hz
        g_host_ms += 10;
        sim.step(0.01f);
        if (i % 10) continue;
        GaugeSnapshot s;
        bus::snapshot(s);
        if (!s.stamp[CH_RPM]) continue;                      // first publish is 40 ms in
        for (int c = 0; c < CH_COUNT; c++) TEST_ASSERT_FALSE(isnan(s.value[c]));
        TEST_ASSERT_TRUE(s.value[CH_RPM] >= 0 && s.value[CH_RPM] <= RPM_MAX);
        TEST_ASSERT_TRUE(s.value[CH_SPEED] >= 0 && s.value[CH_SPEED] < 260);
        TEST_ASSERT_TRUE(s.value[CH_GEAR] >= 0 && s.value[CH_GEAR] <= GEAR_COUNT);
        TEST_ASSERT_TRUE(s.value[CH_COOLANT] > 20 && s.value[CH_COOLANT] < 125);
        TEST_ASSERT_TRUE(s.value[CH_VOLTAGE] > 9 && s.value[CH_VOLTAGE] < 15.5f);
        if (s.value[CH_RPM] >= RPM_SHIFT) sawShift = true;
        if (s.value[CH_SPEED] > maxSpd) maxSpd = s.value[CH_SPEED];
    }
    TEST_ASSERT_TRUE_MESSAGE(sawShift, "scripted pull never reached the shift light");
    TEST_ASSERT_TRUE(maxSpd > 150);
}

// ---- renderer --------------------------------------------------------------------------------------
static void test_renderer_only_pushes_changes() {
    gauge_ui::begin(push, kThemes[0]);
    GaugeModel m;
    GaugeView v;
    bus::clear();
    g_host_ms = 90000;
    m.reset(g_host_ms);
    settle(m, v, 3000, 80, 90, 14, 30);
    gauge_ui::invalidate();
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(8, gauge_ui::lastPushedRegions());   // 7 live regions + SETUP button
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(0, gauge_ui::lastPushedRegions());   // nothing changed
    v.speed = 81;
    gauge_ui::render(v);
    TEST_ASSERT_EQUAL_INT(1, gauge_ui::lastPushedRegions());   // only the speed row
}

static void test_theme_switch_repaints_whole_screen() {
    gauge_ui::begin(push, kThemes[0]);
    for (int t = 0; t < THEME_COUNT; t++) {
        pushedPx = 0;
        memset(fb, 0, sizeof fb);
        gauge_ui::setTheme(kThemes[t]);
        TEST_ASSERT_TRUE(pushedPx >= 320 * 240);
        TEST_ASSERT_EQUAL_HEX16(kThemes[t].background[120 * 320 + 5], fb[120 * 320 + 5]);
        TEST_ASSERT_EQUAL_PTR(&kThemes[t], &gauge_ui::theme());
    }
}

// ---- splash ---------------------------------------------------------------------------------
static void test_credit_integrity() {
    TEST_ASSERT_FALSE(splash_ui::authentic());                     // splash not finished yet
    TEST_ASSERT_EQUAL_HEX32(0x6EEE0E43, splash_ui::creditCrc());   // logo/credit untouched
    gauge_ui::begin(push, kThemes[0]);
    splash_ui::draw(push, kThemes[0]);
    splash_ui::progress(push, kThemes[0], 0.99f);
    TEST_ASSERT_FALSE(splash_ui::authentic());
    splash_ui::progress(push, kThemes[0], 1.0f);
    TEST_ASSERT_TRUE(splash_ui::authentic());
}

static void test_splash_renders_every_theme() {
    TEST_ASSERT_TRUE(fontHas(font_title, "REDLINE"));
    TEST_ASSERT_TRUE(fontHas(font_small, "RACING DASH FOR THE CYD crafted by v0123456789."));
    TEST_ASSERT_TRUE(LOGO_BIRDLAB_W > 60 && LOGO_BIRDLAB_W < 200 && LOGO_BIRDLAB_H < 40);
    for (int t = 0; t < THEME_COUNT; t++) {
        pushedPx = 0;
        memset(fb, 0, sizeof fb);
        splash_ui::draw(push, kThemes[t]);
        TEST_ASSERT_EQUAL_INT(320 * 240, (int)pushedPx);            // exactly one full screen
        long before = pushedPx;
        splash_ui::progress(push, kThemes[t], 0.5f);
        TEST_ASSERT_TRUE(pushedPx - before < 320 * 30);             // progress = small region only
        splash_ui::progress(push, kThemes[t], -3.0f);               // out-of-range input is clamped
        splash_ui::progress(push, kThemes[t], 9.0f);
        // the credit logo actually landed on screen: bright pixels near the bottom
        int bright = 0;
        for (int y = 200; y < 230; y++)
            for (int x = 0; x < 320; x++) if ((fb[y * 320 + x] >> 11) > 26) bright++;
        TEST_ASSERT_TRUE_MESSAGE(bright > 150, kThemes[t].name);
    }
}

// ---- settings page -----------------------------------------------------------------------------------
static void test_settings_taps() {
    gauge_ui::begin(push, kThemes[0]);
    Settings s;
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(260, 80, s));   // 3rd theme card
    TEST_ASSERT_EQUAL_UINT8(2, s.theme);
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(260, 80, s));      // same again: no-op
    TEST_ASSERT_EQUAL(settings_ui::ACT_CHANGED, settings_ui::tap(270, 163, s));  // CUSTOM source
    TEST_ASSERT_EQUAL_UINT8(SRC_CUSTOM, s.source);
    TEST_ASSERT_EQUAL(settings_ui::ACT_CLOSE, settings_ui::tap(280, 14, s));     // DONE
    TEST_ASSERT_EQUAL(settings_ui::ACT_RESET_PEAK, settings_ui::tap(230, 229, s));
    TEST_ASSERT_EQUAL(settings_ui::ACT_NONE, settings_ui::tap(160, 144, s));     // between theme names and sources
}

static void test_settings_limits() {
    gauge_ui::begin(push, kThemes[0]);
    Settings s;
    for (int i = 0; i < 40; i++) settings_ui::tap(129, 205, s);                  // SHIFT +
    TEST_ASSERT_EQUAL_UINT16(Settings::kShiftMax, s.shiftRpm);
    for (int i = 0; i < 40; i++) settings_ui::tap(21, 205, s);                   // SHIFT -
    TEST_ASSERT_EQUAL_UINT16(Settings::kShiftMin, s.shiftRpm);
    for (int i = 0; i < 10; i++) settings_ui::tap(181, 205, s);                  // BRIGHTNESS -
    TEST_ASSERT_EQUAL_UINT8(Settings::kBrightMin, s.brightness);
    for (int i = 0; i < 10; i++) settings_ui::tap(289, 205, s);                  // BRIGHTNESS +
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
    bool beep = s.beep;
    settings_ui::tap(60, 229, s);                                                // BEEP toggle
    TEST_ASSERT_NOT_EQUAL(beep, s.beep);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_credit_integrity);             // first: needs a fresh boot state
    RUN_TEST(test_rgb565_and_blend);
    RUN_TEST(test_canvas_clips_and_rejects_oversize);
    RUN_TEST(test_tabular_digits_have_equal_width);
    RUN_TEST(test_fonts_cover_every_ui_string);
    RUN_TEST(test_font_tables_sorted);
    RUN_TEST(test_theme_backgrounds_match_slot_geometry);
    RUN_TEST(test_serial_key_value);
    RUN_TEST(test_serial_json_and_aliases);
    RUN_TEST(test_serial_ignores_garbage);
    RUN_TEST(test_obd_parse);
    RUN_TEST(test_gear_estimate);
    RUN_TEST(test_model_levels_and_shift);
    RUN_TEST(test_model_marks_stale_values);
    RUN_TEST(test_simulator_stays_physical);
    RUN_TEST(test_renderer_only_pushes_changes);
    RUN_TEST(test_theme_switch_repaints_whole_screen);
    RUN_TEST(test_splash_renders_every_theme);
    RUN_TEST(test_settings_taps);
    RUN_TEST(test_settings_limits);
    return UNITY_END();
}
