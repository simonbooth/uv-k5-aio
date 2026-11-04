#ifdef ENABLE_FEAT_SERVICE_MODES

#include <string.h>

#include "ARMCM0.h"
#include "app/generic.h"
#include "app/sysinfo.h"
#include "audio.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "helper/battery.h"
#include "driver/eeprom.h"
#include "misc.h"
#include "service.h"
#include "settings.h"
#include "settings.h"
#include "svc_plans.h"
#include "bitmaps.h"
#include "ui/battery.h"
#include "app/param_edit.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/menu.h"
#include "ui/ui.h"
#include "version.h"

enum { SI_NONE, SI_REGION, SI_BATTXT, SI_HAM, SI_HAMCALL, SI_PUB, SI_PUBID,
       SI_CONTRAST, SI_INVERT, SI_MIC, SI_BATTYPE, SI_BATCAL, SI_BATSAVE, SI_COUNT };
static uint8_t gSiItem;            // the setting being changed (SI_NONE: none)
static uint8_t gSiCursor = 0xFF;   // a callsign: the character being changed (0xFF: the field)
static char    gSiText[11];        // ... the callsign / public ID being edited, space-padded

// The public licence of the region: US GMRS, UK Simple UK (none elsewhere).
static uint16_t PubLicence(void)
{
    return SERVICE_GetRegion() == HAM_REGION_US ? LIC_GMRS : SERVICE_GetRegion() == PLAN_REGION_UK ? LIC_UK_SIMPLE : 0u;
}

static bool    TextItem(void) { return gSiItem == SI_HAMCALL || gSiItem == SI_PUBID; }
static uint8_t TextLen(void)  { return gSiItem == SI_HAMCALL ? 8u : 10u; }

static void SiLoadText(void)   // arriving at a callsign: it as stored, padded
{
    if (!TextItem())
        return;
    const uint8_t n = TextLen();
    SERVICE_Callsign(gSiItem == SI_HAMCALL ? SERVICE_HAM : SERVICE_GMRS, gSiText);
    for (uint8_t i = (uint8_t)strlen(gSiText); i < n; i++)
        gSiText[i] = ' ';
    gSiText[n] = 0;
}

// The status row of SysInf / setup: the battery at the right (its x back).
static uint8_t StatusBattery(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
    return UI_StatusBattery(gStatusLine);
}

static void Val(const char *t, uint8_t x, uint8_t y, bool selected)
{
    GUI_DisplaySmallest(t, x, y, false, true);
    if (selected)
        for (uint8_t i = x - 1u, e = (uint8_t)(x + strlen(t) * 4u); i < e && i < LCD_WIDTH; i++)
            gFrameBuffer[y / 8u][i] ^= 0x7E;
}

void SYSINFO_Display(void)
{
    char s[22];

    // the status row (this screen's own): the firmware, the region, the battery
    const uint8_t xb     = StatusBattery();
    const char   *region = gSubMenu_HAM_REGION[SERVICE_GetRegion()];
    const uint8_t xr     = (uint8_t)(xb - 3u - strlen(region) * 4u);
    sprintf(s, "%s %s %s", AUTHOR_STRING_2, VERSION_STRING_2, Edition);
    if (2u + strlen(s) * 4u >= xr)
        sprintf(s, "%s %s", AUTHOR_STRING_2, VERSION_STRING_2);   // (the edition where it fits)
    GUI_DisplaySmallest(s, 2, 1, true, true);
    GUI_DisplaySmallest(region, xr, 1, true, true);
    if (gSiItem == SI_REGION || gSiItem == SI_BATTXT)   // selected: inverted
        for (uint8_t x = gSiItem == SI_REGION ? xr - 1u : xb - 1u; x < (gSiItem == SI_REGION ? xb - 2u : LCD_WIDTH); x++)
            gStatusLine[x] ^= 0x7F;
    ST7565_BlitStatusLine();

    UI_DisplayClear();

    // row 0: the ham callsign | the public licence ID, centred; a licence item selected:
    // that one, with its label
    if (gSiItem >= SI_HAM && gSiItem <= SI_PUBID) {
        const bool pub = gSiItem >= SI_PUB;
        UI_PrintStringSmallNormal(!pub ? "HAM" : PubLicence() == LIC_GMRS ? "GMRS" : gSiItem == SI_PUB ? "UK SIMPLE" : "OFCOM", 2, 0, 1);
        uint8_t x0, x1;
        if (TextItem()) {   // the field, or its character at the cursor
            UI_PrintStringSmallNormal(gSiText, 52, 0, 1);
            x0 = gSiCursor == 0xFF ? 51u : (uint8_t)(51u + gSiCursor * 7u);
            x1 = gSiCursor == 0xFF ? (uint8_t)(52u + TextLen() * 7u) : (uint8_t)(x0 + 7u);
        } else {
            UI_PrintStringSmallNormal((SERVICE_Licences() & (pub ? PubLicence() : LIC_HAM)) ? "ON" : "OFF", 100, 0, 1);
            x0 = 98;
            x1 = LCD_WIDTH;
        }
        for (uint8_t x = x0; x < x1 && x < LCD_WIDTH; x++)
            gFrameBuffer[1][x] ^= 0x7F;
    } else {
        char h[11], g[11];
        const bool hh = SERVICE_Callsign(SERVICE_HAM, h), gg = SERVICE_Callsign(SERVICE_GMRS, g);
        sprintf(s, "%s%s%s", h, hh && gg ? " | " : "", g);
        if (strlen(s) <= 18u)
            UI_PrintStringSmallNormal(s, 0, LCD_WIDTH, 1);
        else   // (two long ones: the 3x5 font, still centred)
            GUI_DisplaySmallest(s, (uint8_t)((LCD_WIDTH - strlen(s) * 4u) / 2u), 10, false, true);
    }

    // the settings (3x5 font): DISP: and MIC:; BATT: the reading, the type; calibration, saving
    GUI_DisplaySmallest("DISP:", 2, 26, false, true);
    sprintf(s, "%u", gSetting_set_ctr);
    Val(s, 26, 26, gSiItem == SI_CONTRAST);
    Val(gSetting_set_inv ? "INV" : "NORM", 40, 26, gSiItem == SI_INVERT);
    GUI_DisplaySmallest("MIC:", 70, 26, false, true);
    const uint8_t mic = gMicGain_dB2[gEeprom.MIC_SENSITIVITY % 5u];
    sprintf(s, "+%u.%01udB", mic / 2u, mic % 2u);   // (as the stock menu, CHIRP: +1.1 .. +15.1)
    Val(s, 90, 26, gSiItem == SI_MIC);

    GUI_DisplaySmallest("BATT:", 2, 34, false, true);
    sprintf(s, "%u.%02uV %u%%", gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
            BATTERY_VoltsToPercent(gBatteryVoltageAverage));   // (the reading: CAL moves it)
    GUI_DisplaySmallest(s, 26, 34, false, true);
    Val(gSubMenu_BATTYP[gEeprom.BATTERY_TYPE % 3u], 70, 34, gSiItem == SI_BATTYPE);
    GUI_DisplaySmallest("CAL", 26, 42, false, true);
    sprintf(s, "%u", gBatteryCalibration[3]);
    Val(s, 40, 42, gSiItem == SI_BATCAL);
    GUI_DisplaySmallest("SAVE", 70, 42, false, true);
    if (gEeprom.BATTERY_SAVE)
        sprintf(s, "1:%u", gEeprom.BATTERY_SAVE);
    else
        strcpy(s, "OFF");
    Val(s, 90, 42, gSiItem == SI_BATSAVE);

    if (gSiItem != SI_NONE)
        GUI_DisplaySmallest("F NEXT  F HOLD BACK  UP/DN CHANGE", 2, 50, false, true);   // (the helper line)
    ST7565_BlitFullScreen();
}

static void Step(int8_t d)
{
    switch (gSiItem) {
        case SI_REGION:   // the ham channels and bands, the marine plan, the VFO slots
            SERVICE_SetHamRegion((uint8_t)((SERVICE_GetRegion() + PLAN_REGION_COUNT + d) % PLAN_REGION_COUNT));
            gRequestSaveVFO   = true;
            gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
            gFlagResetVfos    = true;
            return;
        case SI_HAM:
        case SI_PUB: {   // on / off (the licence: CHIRP's tick)
            const uint16_t lic = gSiItem == SI_HAM ? LIC_HAM : PubLicence();
            SERVICE_SetLicence(lic, !(SERVICE_Licences() & lic));
            gVfoConfigureMode = VFO_CONFIGURE_RELOAD;   // (what shows, what transmits)
            gFlagResetVfos    = true;
            return;
        }
        case SI_HAMCALL:
        case SI_PUBID:   // from the first character, then the one at the cursor
            if (gSiCursor == 0xFF)
                gSiCursor = 0;
            gSiText[gSiCursor] = PARAM_StepChar(gSiText[gSiCursor], d);
            SERVICE_SetCallsign(gSiItem == SI_HAMCALL ? SERVICE_HAM : SERVICE_GMRS, gSiText);
            return;
        case SI_BATTXT:   // the status bar's battery: icon, voltage, percentage
            gSetting_battery_text = (uint8_t)((gSetting_battery_text + 3u + d) % 3u);
            gRequestSaveSettings  = true;
            return;
        case SI_CONTRAST:
            if ((d > 0 && gSetting_set_ctr < 15u) || (d < 0 && gSetting_set_ctr > 1u))
                gSetting_set_ctr = (uint8_t)(gSetting_set_ctr + d);
            break;
        case SI_INVERT:
            gSetting_set_inv = !gSetting_set_inv;
            break;
        case SI_BATTYPE:
            gEeprom.BATTERY_TYPE = (uint8_t)((gEeprom.BATTERY_TYPE + 3u + d) % 3u);
            gRequestSaveSettings = true;
            BATTERY_GetReadings(true);
            return;
        case SI_BATSAVE:   // receive power saving: off, 1:1 .. 1:5
            gEeprom.BATTERY_SAVE = (uint8_t)((gEeprom.BATTERY_SAVE + 6u + d) % 6u);
            gRequestSaveSettings = true;
            return;
        case SI_MIC:
            gEeprom.MIC_SENSITIVITY = (uint8_t)((gEeprom.MIC_SENSITIVITY + 5u + d) % 5u);
            SETTINGS_LoadCalibration();
            gFlagReconfigureVfos = true;
            gRequestSaveSettings = true;
            return;
        case SI_BATCAL: {   // the 7.6 V point (1600-2200): the reading beside it follows
            const uint16_t c = (uint16_t)(gBatteryCalibration[3] + d);
            if (c < 1600u || c > 2200u)
                return;
            gBatteryCalibration[3] = c;
            SETTINGS_SaveBatteryCalibration(gBatteryCalibration);
            BATTERY_GetReadings(true);
            return;
        }
        default:
            return;
    }
    ST7565_ContrastAndInv();   // (contrast, invert)
    gRequestSaveSettings = true;
}

void SYSINFO_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key == KEY_PTT) {
        GENERIC_Key_PTT(bKeyPressed);
        return;
    }
    if (Key == KEY_F) {   // on release: forward; held: back (a character, an item)
        if (bKeyPressed && !bKeyHeld)
            return;
        if (!bKeyPressed && bKeyHeld)
            return;   // (the release after a hold)
        if (bKeyHeld) {
            if (TextItem() && gSiCursor != 0xFF && gSiCursor)
                gSiCursor--;
            else {
                gSiCursor = 0xFF;
                do
                    gSiItem = (uint8_t)((gSiItem + SI_COUNT - 1u) % SI_COUNT);
                while ((gSiItem == SI_PUB || gSiItem == SI_PUBID) && !PubLicence());
                SiLoadText();
            }
            gUpdateDisplay = true;
            return;
        }
    }
    if ((!bKeyPressed && Key != KEY_F) || (bKeyHeld && Key != KEY_UP && Key != KEY_DOWN))
        return;   // (Up / Down repeat while held)
    if (Key <= KEY_9 && TextItem()) {   // a digit: it, then the next character
        if (gSiCursor == 0xFF)
            gSiCursor = 0;
        gSiText[gSiCursor] = (char)('0' + Key - KEY_0);
        SERVICE_SetCallsign(gSiItem == SI_HAMCALL ? SERVICE_HAM : SERVICE_GMRS, gSiText);
        Key = KEY_F;
    }
    switch (Key) {
        case KEY_F:
            if (TextItem() && gSiCursor != 0xFF && ++gSiCursor < TextLen())
                break;   // the next character
            gSiCursor = 0xFF;
            do   // (no public licence in the region: its two skipped)
                gSiItem = (uint8_t)((gSiItem + 1u) % SI_COUNT);
            while ((gSiItem == SI_PUB || gSiItem == SI_PUBID) && !PubLicence());
            SiLoadText();
            break;
        case KEY_UP:
        case KEY_DOWN:
            if (gSiItem == SI_NONE)
                return;
            Step(Key == KEY_UP ? 1 : -1);
            gRequestDisplayScreen = DISPLAY_SYSINFO;   // (a reload would go back to the main screen)
            break;
        case KEY_EXIT:
            gSiCursor = 0xFF;
            if (gSiItem != SI_NONE) {
                gSiItem = SI_NONE;
                break;
            }
            /* fall through */
        case KEY_MENU:
            gSiItem               = SI_NONE;
            gRequestDisplayScreen = DISPLAY_MAIN;
            break;
        default:
            return;
    }
    if (!bKeyHeld)
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    gUpdateDisplay = true;
}

// ---------------------------------------------------------------------------

static const char * const *gCfLines;
static void              (*gCfAccept)(void);
static void              (*gCfCancel)(void);
static GUI_DisplayType_t   gCfBack;

void CONFIRM_Open(const char * const *lines, void (*accept)(void), void (*cancel)(void))
{
    gCfLines  = lines;
    gCfAccept = accept;
    gCfCancel = cancel;
    gCfBack   = gScreenToDisplay;
    GUI_SelectNextDisplay(DISPLAY_CONFIRM);
}

void CONFIRM_Display(void)
{
    StatusBattery();
    ST7565_BlitStatusLine();
    UI_DisplayClear();
    UI_PrintString("WARNING", 0, LCD_WIDTH, 0, 8);
    for (uint8_t i = 0; i < 5u; i++)
        GUI_DisplaySmallest(gCfLines[i], 2, (uint8_t)(17u + i * 6u), false, true);
    GUI_DisplaySmallest("MENU: ACCEPT    EXIT: CANCEL", 2, 50, false, true);
    ST7565_BlitFullScreen();
}

void CONFIRM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyPressed || bKeyHeld || (Key != KEY_MENU && Key != KEY_EXIT))
        return;
    GUI_SelectNextDisplay(gCfBack);
    if (Key == KEY_MENU)
        gCfAccept();
    else if (gCfCancel)
        gCfCancel();
}

// ---------------------------------------------------------------------------

// F selects a row, Up / Down change it, MENU applies, EXIT cancels (not on a first run).
enum { RS_REGION, RS_RESET, RS_LICENCES, RS_TXANY };
static bool    gResetFirst;   // a first run: a reset must be chosen, licences not kept
static uint8_t gRsRow;
static uint8_t gRsRegion;     // the region to set
static uint8_t gRsReset;      // 0: none, 1: settings, 2: settings + memories
static bool    gRsClearLic;   // clear the licences / callsigns too
static bool    gRsTxAny;      // TX Any (break glass: here only, and CHIRP)

void RESET_Open(bool firstRun)
{
    gResetFirst           = firstRun;
    gRsRow                = RS_REGION;
    gRsRegion             = SERVICE_GetRegion();
    gRsReset              = firstRun ? 1u : 0u;
    gRsClearLic           = false;
    gRsTxAny              = SERVICE_GetTxAny();
    GUI_SelectNextDisplay(DISPLAY_RESET);   // (now: at boot no key event would apply a request)
}

void RESET_Display(void)
{
    static const char * const resets[] = { "NONE", "SETTINGS", "+ MEMORIES" };
    static const char * const labels[] = { "REGION", "RESET", "LICENCES", "TX ANY" };
    const char *values[] = { gSubMenu_HAM_REGION[gRsRegion], resets[gRsReset], gRsClearLic ? "CLEAR" : "KEEP",
                             gRsTxAny ? "ON" : "OFF" };

    StatusBattery();   // (no status bar here: the battery only)
    ST7565_BlitStatusLine();
    UI_DisplayClear();

    UI_PrintString(gResetFirst ? "NEW FIRMWARE" : "SETUP", 0, LCD_WIDTH, 0, 8);
    for (uint8_t r = 0; r < (gResetFirst ? 2u : 4u); r++) {
        UI_PrintStringSmallNormal(labels[r], 2, 0, 2u + r);
        UI_PrintStringSmallNormal(values[r], 64, 0, 2u + r);
        if (r == gRsRow)
            for (uint8_t x = 62; x < LCD_WIDTH; x++)
                gFrameBuffer[2u + r][x] ^= 0x7F;
    }
    GUI_DisplaySmallest(gResetFirst ? "F SELECT  UP/DN CHANGE  MENU APPLY" : "F SELECT  UP/DN CHANGE  MENU APPLY  EXIT", 2, 50, false, true);
    ST7565_BlitFullScreen();
}

// Erases settings (and memories) to their defaults: calibration (0x1E00 on), F Lock
// and the DTMF codes / contacts kept, the licences unless cleared. Our own area too;
// then the region set, marked ours; reboots.
static void Reset(bool memories)
{
    const bool keepLic = !gResetFirst && !gRsClearLic;
    if (keepLic)
        SERVICE_LicencesKeep(false);
    uint8_t ff[8];
    memset(ff, 0xFF, sizeof(ff));
    UI_DisplayClear();
    UI_PrintString("RESETTING", 0, LCD_WIDTH, 3, 8);
    ST7565_BlitFullScreen();
    for (uint16_t a = 0; a < EEPROM_OWN_END; a += 8) {
        const bool memory = a < 0x0C80u || (a >= 0x0D60u && a < 0x0E28u) || (a >= 0x0F50u && a < 0x1C00u);
        const bool keep   = (a >= 0x0EE0u && a < 0x0F18u) || (a >= 0x0F30u && a < 0x0F50u) || (a >= 0x1C00u && a < 0x1D00u);
        if (!keep && (memories || !memory))
            EEPROM_WriteBuffer(a, ff);
    }
    if (memories)
        EEPROM_WriteBuffer(0x1FF0, ff);   // (as the stock reset: F4HWN's own settings)
    if (keepLic)
        SERVICE_LicencesKeep(true);
    SERVICE_Init();                  // (the erased state: our defaults)
    SERVICE_SetHamRegion(gRsRegion);
    SERVICE_SetTxAny(gRsTxAny);
    SERVICE_SetInstalled();
    NVIC_SystemReset();
}

static void TxAnyCancel(void) { gRsTxAny = false; }   // (back on the setup screen)

static void Apply(void)
{
    SERVICE_SetTxAny(gRsTxAny);
    if (gRsReset)
        Reset(gRsReset == 2u);   // (reboots)
    if (gRsRegion != SERVICE_GetRegion()) {   // the region alone
        SERVICE_SetHamRegion(gRsRegion);
        gRequestSaveVFO   = true;
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
        gFlagResetVfos    = true;
        gUpdateStatus     = true;
    }
    gRequestDisplayScreen = DISPLAY_MAIN;
}

void RESET_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyPressed || bKeyHeld)
        return;
    const int8_t d = Key == KEY_UP ? 1 : -1;
    switch (Key) {
        case KEY_F:
            gRsRow = (uint8_t)((gRsRow + 1u) % (gResetFirst ? 2u : 4u));
            break;
        case KEY_UP:
        case KEY_DOWN:
            if (gRsRow == RS_REGION)
                gRsRegion = (uint8_t)((gRsRegion + PLAN_REGION_COUNT + d) % PLAN_REGION_COUNT);
            else if (gRsRow == RS_RESET)   // (a first run: settings / + memories only)
                gRsReset = gResetFirst ? (uint8_t)(3u - gRsReset) : (uint8_t)((gRsReset + 3u + d) % 3u);
            else if (gRsRow == RS_LICENCES)
                gRsClearLic = !gRsClearLic;
            else
                gRsTxAny = !gRsTxAny;
            break;
        case KEY_MENU:
            if (gRsTxAny && !SERVICE_GetTxAny()) {   // switching it on: the warning first
                static const char * const lines[] = {
                    "TX ANY LIFTS EVERY TX LIMIT:", "BANDS, LICENCES, RX-ONLY CH.", "TX OUTSIDE YOUR LICENCE MAY",
                    "BE ILLEGAL; OUT-OF-BAND TX", "CAN DAMAGE IT. AT YOUR RISK.",
                };
                CONFIRM_Open(lines, Apply, TxAnyCancel);
                return;
            }
            Apply();
            return;
        case KEY_EXIT:
            if (!gResetFirst)
                gRequestDisplayScreen = DISPLAY_MAIN;
            return;
        default:
            return;
    }
    gUpdateDisplay = true;
}

#endif
