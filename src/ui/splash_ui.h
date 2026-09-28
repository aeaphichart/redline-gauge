#pragma once
// Boot splash: REDLINE wordmark, a mini shift bar used as a progress indicator,
// and the "crafted by birdlab.th" credit. Drawn on the active theme's artwork.
#include "ui/gauge_ui.h"
#include "ui/theme.h"

namespace splash_ui {
static const uint32_t kDurationMs  = 1800;   // always shown at boot
static const uint32_t kSkipAfterMs = 1000;   // a tap can skip it only after this

void draw(PushFn push, const Theme &theme);                  // full screen, progress 0
void progress(PushFn push, const Theme &theme, float p);     // p = 0..1, redraws the bar only

// The credit is part of the licence terms (see NOTICE). The gauge checks that the
// splash ran to completion this boot and that the credit assets are unmodified;
// if not, its status bar reads "UNOFFICIAL" instead of the product name.
bool     authentic();
uint32_t creditCrc();
}
