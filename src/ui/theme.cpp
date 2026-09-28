#include "ui/theme.h"
#include "assets/bg_ice.h"
#include "assets/bg_lime.h"
#include "assets/bg_amber.h"

// Based on the original theme design palette, pushed to higher
// saturation: the ILI9341 at wide-ish viewing angles washes colours out noticeably.
//                                           accent    bright    good      warning
const Theme kThemes[THEME_COUNT] = {
    { "ICE BLUE",  "ice",   bg_ice,   0x0A9DFF, 0x4FD2FF, 0x2EFF6E, 0xFFEA00 },
    { "ACID LIME", "lime",  bg_lime,  0xB6FF00, 0xD4FF2A, 0xB6FF00, 0xFFF200 },
    { "AMBER",     "amber", bg_amber, 0xFF8A00, 0xFFB42A, 0x7CFF2E, 0xFFE000 },
};

// CRC32 of the boot-splash credit (birdlab.th logo mask + credit text + wordmark).
// Checked by splash_ui::authentic(); must be updated only by the copyright holder.
namespace splash_ui {
extern const uint32_t kCreditCrc = 0x6EEE0E43;
}
