#pragma once
// Boot splash: REDLINE wordmark, a mini shift bar used as a progress indicator,
// and the "crafted by birdlab.th" credit. Drawn on the active theme's artwork.
#include "ui/gauge_ui.h"
#include "ui/theme.h"

namespace splash_ui {
void draw(PushFn push, const Theme &theme);                  // full screen, progress 0
void progress(PushFn push, const Theme &theme, float p);     // p = 0..1, redraws the bar only
}
