#ifndef APP_SYSINFO_H
#define APP_SYSINFO_H

#include <stdbool.h>

#include "driver/keyboard.h"

// The SysInf screen (DISPLAY_SYSINFO): battery voltage / percent, the firmware, and
// the radio's setup F-editor style - F selects Region, contrast, invert, battery type,
// battery calibration; Up / Down change it (saved, applied at once). Opened by the
// SYSTEM INFO side-key action; EXIT (nothing selected) / MENU go back. Its own status
// row (the firmware); also the power-on screen (in place of the welcome one).
void SYSINFO_Display(void);
void SYSINFO_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

// The reset screen (DISPLAY_RESET): to our defaults, settings or settings and memories.
// A first run of this firmware asks (no cancel); EXIT held at power-up opens it too.
// A warning to accept (DISPLAY_CONFIRM): MENU accepts, EXIT cancels, then back to the
// screen it was opened from. `lines`: 5 lines of the 3x5 font.
void CONFIRM_Open(const char * const *lines, void (*accept)(void), void (*cancel)(void));
void CONFIRM_Display(void);
void CONFIRM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

void RESET_Open(bool firstRun);
void RESET_Display(void);
void RESET_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
