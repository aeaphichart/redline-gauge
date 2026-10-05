#pragma once
// Drag timer screen and run log page. Draws on the gauge's themed art with the same
// region layout (status bar, shift bar, big number, speed, three side panels), and
// like the gauge only re-pushes a region when what it shows changed.
#include <stdint.h>
#include "ui/drag_timer.h"

namespace timer_ui {

enum Action : uint8_t { ACT_NONE, ACT_EXIT, ACT_ABORT, ACT_AGAIN, ACT_LOG, ACT_BACK, ACT_CLEAR, ACT_NEW_RUN };

void   invalidate();                                  // full repaint next render()
void   render(const DragTimer &t, const RunLog &log, uint32_t now, bool speedValid);
Action tapTimer(int x, int y, const DragTimer &t);

void   drawLog(const RunLog &log, float clearHold = 0);   // clearHold 0..1: hold-to-clear fill
Action tapLog(int x, int y);
bool   hitClear(int x, int y);                        // for the hold-to-clear button

}
