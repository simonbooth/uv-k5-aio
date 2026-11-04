/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <assert.h>
#include <string.h>

#include "app/chFrScanner.h"
#include "app/dtmf.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "driver/keyboard.h"
#include "misc.h"
#ifdef ENABLE_AIRCOPY
    #include "ui/aircopy.h"
#endif
#ifdef ENABLE_FMRADIO
    #include "ui/fmradio.h"
#endif
#ifdef ENABLE_REGA
    #include "app/rega.h"
#endif
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "ui/scanner.h"
#include "ui/ui.h"
#include "../misc.h"
#ifdef ENABLE_FEAT_SERVICE_MODES
    #include "app/sysinfo.h"
#endif

GUI_DisplayType_t gScreenToDisplay;
GUI_DisplayType_t gRequestDisplayScreen = DISPLAY_INVALID;

uint8_t           gAskForConfirmation;
bool              gAskToSave;
bool              gAskToDelete;


void (*UI_DisplayFunctions[])(void) = {
    [DISPLAY_MAIN] = &UI_DisplayMain,
    [DISPLAY_MENU] = &UI_DisplayMenu,
#ifdef ENABLE_FEAT_FINDER
    [DISPLAY_SCANNER] = &UI_DisplayScanner,
#else
    [DISPLAY_SCANNER] = &UI_DisplayMain,     // never shown; keeps the table whole
#endif

#ifdef ENABLE_FMRADIO_SCREEN
    [DISPLAY_FM] = &UI_DisplayFM,
#endif

#ifdef ENABLE_AIRCOPY
    [DISPLAY_AIRCOPY] = &UI_DisplayAircopy,
#endif

#ifdef ENABLE_REGA
    [DISPLAY_REGA] = &UI_DisplayREGA,
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
    [DISPLAY_SYSINFO] = &SYSINFO_Display,
    [DISPLAY_RESET]   = &RESET_Display,
    [DISPLAY_CONFIRM] = &CONFIRM_Display,
#endif
};

static_assert(ARRAY_SIZE(UI_DisplayFunctions) == DISPLAY_N_ELEM);

void GUI_DisplayScreen(void)
{
    if (gScreenToDisplay != DISPLAY_INVALID) {
        UI_DisplayFunctions[gScreenToDisplay]();
    }
}

void GUI_SelectNextDisplay(GUI_DisplayType_t Display)
{
    if (Display == DISPLAY_INVALID)
        return;

    if (gScreenToDisplay != Display)
    {
        DTMF_clear_input_box();

        gInputBoxIndex       = 0;
        gIsInSubMenu         = false;
        gCssBackgroundScan   = false;
        gScanStateDir        = SCAN_OFF;
        #ifdef ENABLE_FMRADIO_SCREEN
            gFM_ScanState    = FM_SCAN_OFF;
        #endif
        gAskForConfirmation  = 0;
        gAskToSave           = false;
        gAskToDelete         = false;
        gWasFKeyPressed      = false;

        gUpdateStatus        = true;
    }

    gScreenToDisplay = Display;
    gUpdateDisplay   = true;
}
