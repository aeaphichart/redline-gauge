#include "ui/timer_ui.h"
#include "ui/canvas.h"
#include "ui/gauge_ui.h"
#include "ui/shift_slots.h"
#include "ui/splash_ui.h"
#include "ui/theme.h"
#include "config.h"
#include "fonts/font_timer.h"
#include "fonts/font_speed.h"
#include "fonts/font_value.h"
#include "fonts/font_label.h"
#include "fonts/font_small.h"
#include "fonts/font_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace timer_ui {

// Same geometry as gauge_ui.cpp (it is baked into the background art).
struct Rect { int x, y, w, h; };
static const Rect R_STATUS  = { 12,   7, 200, 15 };
static const Rect R_BAR     = {  8,  27, 200, 22 };
static const Rect R_BIG_LBL = { 14,  62, 196, 15 };
static const Rect R_BIG     = { 14,  77, 196, 51 };
static const Rect R_SPEED   = { 18, 183, 174, 28 };
static const int  PANEL_TOP[3] = { 33, 102, 171 };
static Rect panelRect(int i) { return { 226, PANEL_TOP[i] - 4, 88, 44 }; }
static const Rect R_LOGBTN  = { 204, 221, 54, 17 };
static const Rect R_BTN     = { 262, 221, 54, 17 };

// log page
static const Rect L_BACK  = { 250,   4,  64, 22 };
static const Rect L_CLEAR = {   8, 212, 150, 24 };
static const Rect L_NEW   = { 164, 212, 148, 24 };

enum { RG_STATUS, RG_BAR, RG_LBL, RG_BIG, RG_SPEED, RG_P0, RG_P1, RG_P2, RG_LOGBTN, RG_BTN, RG_COUNT };
static char s_key[RG_COUNT][72];

#define C_STATUS 0xd9e2e7
#define C_LABEL  0xaab7bf
#define C_DIM    0x55636d
#define C_MUTED  0x8796a0
#define C_TRACK  0x1a232b
#define C_EDGE   0x34434e

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }
static Canvas &cv() { return gauge_ui::canvas(); }
static const Theme &th() { return gauge_ui::theme(); }

static bool inside(const Rect &r, int x, int y) {
    return x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 3 && y < r.y + r.h + 3;
}

static bool changed(int rg, const char *key) {
    if (!strncmp(s_key[rg], key, sizeof s_key[rg])) return false;
    snprintf(s_key[rg], sizeof s_key[rg], "%s", key);
    return true;
}
static bool begin(const Rect &r) { return cv().begin(r.x, r.y, r.w, r.h, th().background); }
static void flush() { Canvas &c = cv(); gauge_ui::pushFn()(c.x0, c.y0, c.w, c.h, c.px); }

void invalidate() {
    for (auto &k : s_key) { k[0] = 1; k[1] = 0; }
}

// "12.34" or "--.--"
static void fmtSec(char *out, size_t n, float s) {
    if (s < 0) snprintf(out, n, "--.--");
    else snprintf(out, n, "%.2f", s > 99.99f ? 99.99f : s);
}
static void fmtCs(char *out, size_t n, uint16_t cs) {
    if (cs == RUN_NONE) snprintf(out, n, "--");
    else snprintf(out, n, "%u.%02u", cs / 100, cs % 100);
}

// ---- pieces ----------------------------------------------------------------------------
static void drawStatus(const char *state, uint32_t color) {
    char key[72];
    bool official = splash_ui::authentic();
    snprintf(key, sizeof key, "%s|%06x|%d", state, (unsigned)color, official);
    if (!changed(RG_STATUS, key) || !begin(R_STATUS)) return;
    Canvas &c = cv();
    c.fillCircle(21.5f, 15.5f, 2.3f, C(color));
    c.text(font_small, 28, 19, official ? STATUS_TITLE : "UNOFFICIAL", C(official ? C_STATUS : 0xff2e2e));
    c.text(font_small, 104, 19, "DRAG TIMER", C(th().accentBright));
    c.text(font_small, 206, 19, state, C(color), ALIGN_RIGHT);
    flush();
}

// The gauge's 13 slanted slots: `pos` of 13 lit in `color`, or `staged` amber lights.
static void drawBar(float pos, uint32_t color, int staged) {
    char key[72];
    snprintf(key, sizeof key, "%d|%06x|%d", (int)(pos * 52), (unsigned)color, staged);
    if (!changed(RG_BAR, key) || !begin(R_BAR)) return;
    Canvas &c = cv();
    for (int r = 0; r < SLOT_ROWS; r++) {
        int y = SLOT_Y0 + r;
        float t = (float)r / (SLOT_ROWS - 1);
        float sh = r == 0 ? 0.50f : r == 1 ? 0.25f : 0.10f - t * 0.22f;
        for (int s = 0; s < SLOT_COUNT; s++) {
            float a = SLOT_SPAN[r][s][0], b = SLOT_SPAN[r][s][1] + 1;
            if (s < staged) { c.hspan(a, b, y, shade565(C(th().warning), sh)); continue; }
            float f = pos - s;
            if (f <= 0) continue;
            if (f > 1) f = 1;
            c.hspan(a, a + f * (b - a), y, shade565(C(color), sh));
        }
    }
    flush();
}

static void drawBig(const char *label, const char *right, const char *tag, const char *num, uint32_t color) {
    auto paint = [&]() {
        Canvas &c = cv();
        c.text(font_small, 19, 75, label, C(C_LABEL), ALIGN_LEFT, 1);
        if (tag[0]) {
            int tw = c.textWidth(font_small, tag, 1) + 10;
            c.fillRect(206 - tw, 65, tw, 13, C(th().good));
            c.text(font_small, 206 - tw / 2, 75, tag, C(0x05140a), ALIGN_CENTER, 1);
        } else if (right[0]) {
            c.text(font_small, 206, 75, right, C(0x6f808b), ALIGN_RIGHT);
        }
        int w = c.text(font_timer, 18, 118, num, C(color), ALIGN_LEFT, 0, true);
        c.text(font_small, 18 + w + 6, 118, "SEC", C(th().accentBright), ALIGN_LEFT, 1);
    };
    char key[72];
    snprintf(key, sizeof key, "%s|%s|%s", label, right, tag);
    if (changed(RG_LBL, key) && begin(R_BIG_LBL)) { paint(); flush(); }
    snprintf(key, sizeof key, "%s|%06x", num, (unsigned)color);
    if (changed(RG_BIG, key) && begin(R_BIG)) { paint(); flush(); }
}

static void drawSpeed(bool valid, int kmh) {
    char num[8], key[72];
    if (valid) snprintf(num, sizeof num, "%03d", kmh < 0 ? 0 : kmh > 999 ? 999 : kmh);
    else snprintf(num, sizeof num, "---");
    snprintf(key, sizeof key, "%s", num);
    if (!changed(RG_SPEED, key) || !begin(R_SPEED)) return;
    Canvas &c = cv();
    c.text(font_label, 23, 203, "SPEED", C(th().accentBright), ALIGN_LEFT, 1);
    int w = c.text(font_speed, 68, 207, num, C(valid ? 0xffffff : C_DIM), ALIGN_LEFT, 0, true);
    c.text(font_small, 68 + w + 3, 207, "km/h", C(0xbac6cc));
    flush();
}

enum PanelState { PS_PENDING, PS_ACTIVE, PS_DONE, PS_BEST };

static void drawPanel(int i, const char *label, const char *value, PanelState ps, float prog) {
    int fill = (int)lroundf((prog < 0 ? 0 : prog > 1 ? 1 : prog) * 76);
    char key[72];
    snprintf(key, sizeof key, "%s|%s|%d|%d", label, value, ps, fill);
    if (!changed(RG_P0 + i, key) || !begin(panelRect(i))) return;
    Canvas &c = cv();
    int top = PANEL_TOP[i];
    c.text(font_label, 230, top + 8, label, C(ps == PS_PENDING ? 0x6f808b : th().accentBright), ALIGN_LEFT, 1);
    uint32_t vc = ps == PS_PENDING ? C_DIM : ps == PS_ACTIVE ? th().accentBright : ps == PS_BEST ? th().good : 0xffffff;
    int w = c.text(font_value, 230, top + 29, value, C(vc), ALIGN_LEFT, 0, true);
    c.text(font_small, 230 + w + 3, top + 29, "s", C(0xd1d9de));
    if (ps == PS_BEST) c.text(font_small, 312, top + 29, "BEST", C(th().good), ALIGN_RIGHT);
    c.fillRect(230, top + 34, 76, 3, C(C_TRACK));
    if (fill > 0) c.fillRect(230, top + 34, fill, 3, C(ps >= PS_DONE ? th().good : th().accent));
    flush();
}

static void smallButton(int rg, const Rect &r, const char *label, bool show) {
    char key[72];
    snprintf(key, sizeof key, "%s|%d", label, show);
    if (!changed(rg, key) || !begin(r)) return;
    Canvas &c = cv();
    if (show) {
        c.blendRect(r.x, r.y, r.w, r.h, 0x0000, 190);
        uint16_t a = C(th().accent);
        c.fillRect(r.x, r.y, r.w, 1, a);
        c.fillRect(r.x, r.y + r.h - 1, r.w, 1, shade565(a, -0.5f));
        c.fillRect(r.x, r.y, 1, r.h, shade565(a, -0.3f));
        c.fillRect(r.x + r.w - 1, r.y, 1, r.h, shade565(a, -0.3f));
        c.text(font_small, r.x + r.w / 2, r.y + 12, label, C(C_STATUS), ALIGN_CENTER);
    }
    flush();
}

// ---- timer screen ------------------------------------------------------------------------
void render(const DragTimer &t, const RunLog &log, uint32_t now, bool speedValid) {
    const Theme &T = th();
    TimerState st = t.state();
    char num[12], right[24] = "", label[32];
    const char *stateText = "";
    uint32_t stateColor = T.accentBright, bigColor = 0xffffff;
    const char *tag = "";

    switch (st) {
        case TS_NO_SPEED: stateText = "NO SPEED"; stateColor = 0xff4741; break;
        case TS_MOVING:   stateText = "HOLD STILL"; stateColor = T.warning; break;
        case TS_STAGED:   stateText = "STAGED"; stateColor = T.warning; break;
        case TS_RUN:      stateText = "RUN"; stateColor = T.good; break;
        case TS_FINISH:   stateText = "FINISH"; stateColor = T.good; break;
        case TS_SAVED:    stateText = "SAVED"; stateColor = T.good; break;
        case TS_NO_RESULT:stateText = "TOO SHORT"; stateColor = T.warning; break;
    }
    drawStatus(stateText, stateColor);

    // big number
    if (st == TS_RUN) {
        fmtSec(num, sizeof num, t.elapsed(now));
        snprintf(label, sizeof label, "TIME");
        int next = 0;
        while (next < 3 && t.segDone(next == 0 ? SEG_0_100 : next == 1 ? SEG_100_120 : SEG_120_160)) next++;
        snprintf(right, sizeof right, "TO %d KM/H", next == 0 ? 100 : next == 1 ? 120 : next == 2 ? 160 : 200);
    } else if (st == TS_FINISH) {
        fmtSec(num, sizeof num, t.segTime(SEG_0_200, now));
        snprintf(label, sizeof label, "0-200 KM/H");
        bigColor = T.good;
        if (t.newBest(SEG_0_200)) tag = "NEW BEST";
    } else if (st == TS_SAVED) {                         // didn't reach 200: time to its top speed
        fmtSec(num, sizeof num, t.toMax());
        snprintf(label, sizeof label, "0-%d KM/H  (TOP SPEED)", (int)lroundf(t.maxKmh()));
        bigColor = 0xffffff;
    } else if (st == TS_NO_RESULT) {
        fmtSec(num, sizeof num, -1);
        snprintf(label, sizeof label, "UNDER 30 KM/H - NOT LOGGED");
        bigColor = C_DIM;
    } else {
        snprintf(num, sizeof num, "0.00");
        bigColor = C_DIM;
        snprintf(label, sizeof label, st == TS_STAGED ? "LAUNCH TO START" : st == TS_MOVING ? "STOP THE CAR TO ARM"
                                                                            : "WAITING FOR SPEED");
        uint16_t b200 = log.best(SEG_0_200), b100 = log.best(SEG_0_100);
        if (b200 != RUN_NONE) snprintf(right, sizeof right, "BEST 0-200 %u.%02u", b200 / 100, b200 % 100);
        else if (b100 != RUN_NONE) snprintf(right, sizeof right, "BEST 0-100 %u.%02u", b100 / 100, b100 % 100);
    }
    drawBig(label, right, tag, num, bigColor);

    // bar: staging lights, live speed toward 200, or the result
    if (st == TS_STAGED) drawBar(0, T.accent, 3);
    else if (st == TS_FINISH) drawBar(SLOT_COUNT, T.good, 0);
    else if (st == TS_RUN || st == TS_SAVED || st == TS_NO_RESULT) {
        float v = st == TS_RUN ? t.speed() : t.maxKmh();
        drawBar(SLOT_COUNT * (v > 200 ? 1 : v / 200), T.accent, 0);
    } else drawBar(0, T.accent, 0);

    drawSpeed(speedValid, (int)lroundf(t.speed()));

    static const Seg kPanelSeg[3] = { SEG_0_100, SEG_100_120, SEG_120_160 };
    static const char *const kPanelLbl[3] = { "0-100", "100-120", "120-160" };
    bool showing = st == TS_RUN || t.hasResult() || st == TS_NO_RESULT;
    for (int i = 0; i < 3; i++) {
        Seg s = kPanelSeg[i];
        PanelState ps = PS_PENDING;
        float tm = -1;
        if (showing) {
            if (t.segDone(s)) { ps = t.newBest(s) ? PS_BEST : PS_DONE; tm = t.segTime(s, now); }
            else if (t.segActive(s)) { ps = PS_ACTIVE; tm = t.segTime(s, now); }
        }
        fmtSec(num, sizeof num, tm);
        drawPanel(i, kPanelLbl[i], num, ps, showing ? t.segProgress(s) : 0);
    }

    smallButton(RG_LOGBTN, R_LOGBTN, "LOG", st != TS_RUN);
    smallButton(RG_BTN, R_BTN, st == TS_RUN ? "ABORT" : t.hasResult() || st == TS_NO_RESULT ? "AGAIN" : "EXIT", true);
}

Action tapTimer(int x, int y, const DragTimer &t) {
    if (inside(R_BTN, x, y)) {
        if (t.state() == TS_RUN) return ACT_ABORT;
        return t.hasResult() || t.state() == TS_NO_RESULT ? ACT_AGAIN : ACT_EXIT;
    }
    if (inside(R_LOGBTN, x, y) && t.state() != TS_RUN) return ACT_LOG;
    return ACT_NONE;
}

// ---- log page ------------------------------------------------------------------------------
static void button(const Rect &r, const char *label, uint32_t edge, bool hot, float fillFrac = 0) {
    Canvas &c = cv();
    c.blendRect(r.x, r.y, r.w, r.h, 0x0000, 200);
    if (fillFrac > 0) c.blendRect(r.x, r.y, (int)(r.w * (fillFrac > 1 ? 1 : fillFrac)), r.h, C(edge), 110);
    uint16_t e = C(edge);
    c.fillRect(r.x, r.y, r.w, 1, e);
    c.fillRect(r.x, r.y + r.h - 1, r.w, 1, e);
    c.fillRect(r.x, r.y, 1, r.h, e);
    c.fillRect(r.x + r.w - 1, r.y, 1, r.h, e);
    c.text(font_ui, r.x + r.w / 2, r.y + (r.h + font_ui.ascent) / 2, label, C(hot ? edge : C_STATUS), ALIGN_CENTER, 1);
}

static void composeLog(const RunLog &log, float clearHold) {
    const Theme &T = th();
    Canvas &c = cv();
    for (int i = 0; i < c.w * c.h; i++) c.px[i] = blend565(215, 0, c.px[i]);
    c.text(font_ui, 12, 20, "RUN LOG", C(T.accentBright), ALIGN_LEFT, 2);
    char buf[48];
    snprintf(buf, sizeof buf, "%d run%s saved", log.count, log.count == 1 ? "" : "s");
    c.text(font_small, 100, 19, buf, C(C_MUTED));
    button(L_BACK, "BACK", T.accent, true);
    for (int x = 8; x < 312; x++) c.blendPixel(x, 30, C(T.accent), (uint8_t)(255 - (x - 8) * 200 / 304));

    static const int cx[6] = { 12, 36, 82, 128, 174, 220 };
    static const char *const hdr[6] = { "#", "0-100", "100-120", "120-160", "0-200", "TOP @TIME" };
    static const Seg cols[4] = { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200 };
    for (int k = 0; k < 6; k++) c.text(font_small, cx[k], 44, hdr[k], C(C_MUTED));
    c.fillRect(8, 48, 304, 1, C(C_EDGE));
    uint16_t best[4];
    for (int k = 0; k < 4; k++) best[k] = log.best(cols[k]);

    const int rows = log.count < 6 ? log.count : 6;     // newest 6 on screen; `timerlog` prints all
    if (!rows) c.text(font_small, 160, 110, "No runs yet - drive one from STAGED", C(C_MUTED), ALIGN_CENTER);
    for (int r = 0; r < rows; r++) {
        const RunRecord &rec = log.runs[r];
        int y = 64 + r * 21;
        if (r == 0) c.blendRect(8, y - 12, 304, 18, C(T.accent), 40);
        snprintf(buf, sizeof buf, "%u", rec.seq);
        c.text(font_small, cx[0], y, buf, C(C_MUTED));
        for (int k = 0; k < 4; k++) {
            uint16_t v = rec.cs[cols[k]];
            fmtCs(buf, sizeof buf, v);
            uint32_t col = v == RUN_NONE ? C_DIM : v == best[k] ? T.good : 0xffffff;
            c.text(font_small, cx[k + 1], y, buf, C(col));
        }
        snprintf(buf, sizeof buf, "%u @%u.%01u", rec.maxKmh, rec.toMaxCs / 100, rec.toMaxCs % 100 / 10);
        c.text(font_small, cx[5], y, buf, C(0xffffff));
        c.fillRect(8, y + 7, 304, 1, C(0x1c252d));
    }
    if (log.count) {
        c.text(font_small, 12, 199, "BEST", C(T.good));
        for (int k = 0; k < 4; k++) {
            fmtCs(buf, sizeof buf, best[k]);
            c.text(font_small, cx[k + 1], 199, buf, C(T.good));
        }
    }
    button(L_CLEAR, clearHold > 0 ? "KEEP HOLDING" : "HOLD TO CLEAR", 0xff2e2e, clearHold > 0, clearHold);
    button(L_NEW, "NEW RUN", T.accent, true);
}

void drawLog(const RunLog &log, float clearHold) {
    Canvas &c = cv();
    for (int y = 0; y < 240; y += 40) {
        c.begin(0, y, 320, 40, th().background);
        composeLog(log, clearHold);
        gauge_ui::pushFn()(0, y, 320, 40, c.px);
    }
}

Action tapLog(int x, int y) {
    if (inside(L_BACK, x, y)) return ACT_BACK;
    if (inside(L_NEW, x, y)) return ACT_NEW_RUN;
    return ACT_NONE;
}

bool hitClear(int x, int y) { return inside(L_CLEAR, x, y); }

}  // namespace timer_ui
