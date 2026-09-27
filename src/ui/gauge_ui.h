#pragma once
// Renders a GaugeView onto the themed REDLINE background.
// The screen is split into independent regions (status, shift bar, rpm, speed,
// three side panels). Each frame, a region is re-composed and pushed ONLY if what
// it shows changed — so a steady cruise costs almost no SPI time.
#include <stdint.h>
#include "ui/gauge_model.h"
#include "ui/theme.h"

class Canvas;

// Pushes a w*h block of plain RGB565 pixels to the screen at (x,y).
typedef void (*PushFn)(int x, int y, int w, int h, const uint16_t *pixels);

namespace gauge_ui {
void begin(PushFn push, const Theme &theme);
void setTheme(const Theme &theme);   // swap background + accents, full redraw
const Theme &theme();
void redrawAll();                    // repaint background, then every region next render()
void invalidate();                   // force every region to redraw next frame
void render(const GaugeView &v);     // redraw regions whose content changed
int  lastPushedRegions();            // how many regions the last render() pushed
Canvas &canvas();                    // shared scratch canvas (also used by the settings screen)
PushFn  pushFn();

// Touch zones (screen coordinates)
bool hitStatus(int x, int y);
bool hitMain(int x, int y);
bool hitSetup(int x, int y);         // the SETUP button, bottom-right
}
