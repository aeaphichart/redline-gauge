#pragma once
// Colour themes (artwork in tools/art/): each has its own background art
// (same geometry, different accent lighting) plus the accent colours the live
// widgets use. Add a theme = add a background header + one row in theme.cpp.
#include <stdint.h>

struct Theme {
    const char     *name;          // shown in settings, ≤ 10 chars
    const char     *key;           // for the serial command theme=<key>
    const uint16_t *background;    // 320x240 RGB565 in flash
    uint32_t accent;               // shift bar segments, button highlights
    uint32_t accentBright;         // labels, SPEED caption, selected text
    uint32_t good;                 // voltage value, "live" status dot
    uint32_t warning;              // upper shift-bar segments
};

enum { THEME_ICE, THEME_LIME, THEME_AMBER, THEME_COUNT };
extern const Theme kThemes[THEME_COUNT];
