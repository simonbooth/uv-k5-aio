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

#include <string.h>

#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "bitmaps.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "service.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "ui/status.h"
#include "ui/main.h"
#include "app/param_edit.h"

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
#ifndef ENABLE_FEAT_F4HWN_DEBUG
static void convertTime(uint8_t *line, uint8_t type) 
{
    uint16_t t = (type == 0) ? (gTxTimerCountdown_500ms / 2) : (3600 - gRxTimerCountdown_500ms / 2);

    uint8_t m = t / 60;
    uint8_t s = t - (m * 60); // Replace modulo with subtraction for efficiency

    gStatusLine[0] = gStatusLine[7] = gStatusLine[14] = 0x00; // Quick fix on display (on scanning I, II, etc.)

    char str[6];
    sprintf(str, "%02u:%02u", m, s);
    UI_PrintStringSmallBufferNormal(str, line);

    gUpdateStatus = true;
}
#endif
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
// The A / B indicator is one framed item (so it reads apart from the other status
// icons); inside it the VFO PTT transmits on is a bold 5x5 letter, a VFO that is
// only listening a thin 3x5 one. Every column carries the frame's top / bottom.
static uint8_t *FrameCol(uint8_t *p, uint8_t bits)
{
    *p = (uint8_t)(bits | 0x41);
    return p + 1;
}

static uint8_t *DrawVfoLetter(uint8_t *p, uint8_t vfo, bool tx)
{
    static const uint8_t letters[2][2][5] = {   // [vfo][bold]
        { { 0x00, 0x3C, 0x0A, 0x3C, 0x00 }, { 0x3C, 0x3E, 0x0A, 0x3E, 0x3C } },
        { { 0x00, 0x3E, 0x2A, 0x14, 0x00 }, { 0x3E, 0x3E, 0x2A, 0x3E, 0x14 } },
    };
    for (uint8_t c = 0; c < 5; c++)
        p = FrameCol(p, letters[vfo][tx][c]);
    return FrameCol(p, 0);
}

static bool VfoCanTx(uint8_t vfo)
{
    return SERVICE_CanTx(&gEeprom.VfoInfo[vfo]);
}
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
// The battery at the right of a status row: the icon, or (BatTxt) the voltage /
// percentage in its place - one of the three; its x back.
uint8_t UI_StatusBattery(uint8_t *line)
{
    char str[8];
    if (gSetting_battery_text == 0 || gSetting_battery_text > 2) {
        const uint8_t x = LCD_WIDTH - sizeof(BITMAP_BatteryLevel1);
        UI_DrawBattery(line + x, gBatteryDisplayLevel, gLowBatteryBlink);
        return x;
    }
    const uint16_t v = gBatteryVoltageAverage <= 999 ? gBatteryVoltageAverage : 999;   // (9.99 V at most)
    if (gSetting_battery_text == 1)
        sprintf(str, "%u.%02u", v / 100, v % 100);
    else
        sprintf(str, "%02u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
    const uint8_t x = (uint8_t)(LCD_WIDTH - 7u * strlen(str));
    UI_PrintStringSmallBufferNormal(str, line + x);
    return x;
}
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
// The services status bar. Full screen: the A / B view's left column in a row - 1 the mode,
// 2 the signal / TX / scan, 3 the power (no TX), 4 the squelch, 5 the memory number; A / B
// view: the [A B] frame instead. Then from the right: the battery, lock / F / mute / light,
// the callsign, VOX - each where it fits.
static void StatusServices(void)
{
    uint8_t      *line = gStatusLine;
    const uint8_t vfo  = gEeprom.TX_VFO;
    char          s[12];
    unsigned int  x    = 0;

    if (UI_FullView()) {
        UI_DrawModeIcon(line, vfo);                       // 1
        if (PARAM_Current() == PE_BAND)
            for (uint8_t i = 0; i < 8u; i++)
                line[i] ^= 0x7F;
        if (FUNCTION_IsRx()                               // 2
#ifdef ENABLE_FMRADIO
            || (gFmRadioMode && !gFM_RestoreCountdown_10ms)
#endif
        )
            UI_DrawSignalBars(line + 9, gRssiBars);
        else if (gCurrentFunction == FUNCTION_TRANSMIT)
            UI_PrintStringSmallBufferNormal("TX", line + 9);
        else if (gScanStateDir != SCAN_OFF)
            GUI_DisplaySmallest("SCN", 10, 1, true, true);
        const bool psel = UI_PowerSelected(vfo);          // 3
        if (!VfoCanTx(vfo) && !psel)
            memcpy(line + 28, BITMAP_NoTx, sizeof(BITMAP_NoTx));
        else {
            UI_PowerShort(&gEeprom.VfoInfo[vfo], s);
            GUI_DisplaySmallest(s, 29, 1, true, true);
        }
        if (psel)
            for (uint8_t i = 28; i < 46u; i++)
                line[i] ^= 0x7F;
        UI_SqlText(vfo, s);                               // 4
        GUI_DisplaySmallest(s, 48, 1, true, true);
        if (UI_SqlSelected(vfo))
            for (uint8_t i = 47; i < 56u; i++)
                line[i] ^= 0x7F;
        const uint8_t w = UI_MemNo(vfo, s);               // 5
        GUI_DisplaySmallest(s, 59, 1, true, true);
        for (uint8_t i = 58; i < 58u + w; i++)
            line[i] ^= 0x7F;
        x = 59u + strlen(s) * 4u;
    } else {   // [A B]: the VFO PTT transmits on bold; both while the watch runs
        const bool txOk = VfoCanTx(vfo);
        uint8_t   *p    = line;
        *p++ = 0x7F;
        p    = FrameCol(p, 0);
        if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF && gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF) {
            p = DrawVfoLetter(p, vfo, txOk);
        } else {
            p = DrawVfoLetter(p, 0, vfo == 0 && txOk);
            if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) {   // an arrow, TX to RX
                static const uint8_t arrow[2][3] = { { 0x22, 0x14, 0x08 }, { 0x08, 0x14, 0x22 } };
                for (uint8_t c = 0; c < 3; c++)
                    p = FrameCol(p, arrow[vfo][c]);
                p = FrameCol(p, 0);
            }
            p = DrawVfoLetter(p, 1, vfo == 1 && txOk);
        }
        *p = 0x7F;
        x  = (unsigned int)(p - line) + 1u;
    }

    // from the right: the battery, then lock / F / mute / light, the callsign, VOX
    unsigned int xr = UI_StatusBattery(line);
    const uint8_t *src = gEeprom.KEY_LOCK ? gFontKeyLock : gWasFKeyPressed ? gFontF : gMute ? gFontMute
                       : gBackLight ? gFontLight : NULL;
    const uint8_t  len = gEeprom.KEY_LOCK ? sizeof(gFontKeyLock) : gWasFKeyPressed ? sizeof(gFontF)
                       : gMute ? sizeof(gFontMute) : sizeof(gFontLight);
    if (src && xr >= x + len + 2u) {
        xr -= len + 2u;
        memcpy(line + xr, src, len);
    }
    if (UI_FullView() && SERVICE_TxCallsign(s) && xr >= x + strlen(s) * 4u + 3u) {
        xr -= (unsigned int)(strlen(s) * 4u + 3u);
        GUI_DisplaySmallest(s, (uint8_t)xr, 1, true, true);
    }
#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH && xr >= x + sizeof(gFontVox) + 2u)
        memcpy(line + xr - sizeof(gFontVox) - 2u, gFontVox, sizeof(gFontVox));
#endif
    ST7565_BlitStatusLine();
}
#endif

void UI_DisplayStatus()
{
    char str[8] = "";

    gUpdateStatus = false;
#ifdef ENABLE_FEAT_SERVICE_MODES
    if (gScreenToDisplay >= DISPLAY_SYSINFO)   // (SysInf, setup, confirm: their own)
        return;   // (SysInf draws the status row itself; setup: none)
    memset(gStatusLine, 0, sizeof(gStatusLine));
    StatusServices();
    (void)str;
    return;
#endif
    memset(gStatusLine, 0, sizeof(gStatusLine));

    uint8_t     *line = gStatusLine;
    unsigned int x    = 0;

#ifdef ENABLE_NOAA
    // NOAA indicator
    if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) { // NOASS SCAN indicator
        memcpy(line + x, BITMAP_NOAA, sizeof(BITMAP_NOAA));
    }
    // Power Save indicator
    else if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#elif defined(ENABLE_FEAT_SERVICE_MODES)
    {   // Mode indicator (replaces the power-save icon): HAM / GMRS-FRS / MARINE, "FM" for the FM radio

        if (UI_FullView()) {
            UI_DrawModeIcon(line + x, gEeprom.TX_VFO);
            if (PARAM_Current() == PE_BAND)   // selecting the band: the icon inverted
                for (uint8_t i = 0; i < 8u; i++)
                    line[x + i] ^= 0x7F;
            if (FUNCTION_IsRx()   // single view: the signal by the mode icon (broadcast FM's too)
#ifdef ENABLE_FMRADIO
                || (gFmRadioMode && !gFM_RestoreCountdown_10ms)
#endif
            )
                UI_DrawSignalBars(line + x + 9, gRssiBars);
            else if (gCurrentFunction == FUNCTION_TRANSMIT)   // (A / B view: in its block)
                UI_PrintStringSmallBufferNormal("TX", line + x + 9);
            x += 9 + 17;
        }
    }
    x += 8;
#else
    // Power Save indicator
    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#endif

    unsigned int x1 = x;

#ifdef ENABLE_DTMF_CALLING
    if (gSetting_KILLED) {
        memset(line + x, 0xFF, 10);
        x1 = x + 10;
    }
    else
#endif
    { // SCAN indicator
        if (gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) {
#ifdef ENABLE_FEAT_SERVICE_MODES   // one S: the scan covers the VFO's group (shown in its M slot)
            if (false) {
#else
            if (IS_MR_CHANNEL(gNextMrChannel) && !SCANNER_IsScanning()) { // channel mode
#endif
                switch(gEeprom.SCAN_LIST_DEFAULT) {
                    case 0:
                        memcpy(line + 0, BITMAP_ScanList0, sizeof(BITMAP_ScanList0));
                        break;
                    case 1: 
                        memcpy(line + 0, BITMAP_ScanList1, sizeof(BITMAP_ScanList1));
                        break;
                    case 2:
                        memcpy(line + 0, BITMAP_ScanList2, sizeof(BITMAP_ScanList2));
                        break;
                    case 3:
                        memcpy(line + 0, BITMAP_ScanList3, sizeof(BITMAP_ScanList3));
                        break;
                    case 4:
                        memcpy(line + 0, BITMAP_ScanList123, sizeof(BITMAP_ScanList123));
                        break;
                    case 5:
                        memcpy(line + 0, BITMAP_ScanListAll, sizeof(BITMAP_ScanListAll));
                        break;
                }
            }
            else {  // frequency mode
                memcpy(line + x + 1, gFontS, sizeof(gFontS));
                //UI_PrintStringSmallBufferNormal("S", line + x + 1);
            }
            x1 = x + 10;
        }
    }
    x += 10;  // font character width

    #ifdef ENABLE_FEAT_F4HWN_DEBUG
        // Only for debug
        // Only for debug
        // Only for debug

        sprintf(str, "%d", gDebug);
        UI_PrintStringSmallBufferNormal(str, line + x + 1);
        x += 16;
    #else
        #ifdef ENABLE_VOICE
        // VOICE indicator
        if (gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF){
            memcpy(line + x, BITMAP_VoicePrompt, sizeof(BITMAP_VoicePrompt));
            x1 = x + sizeof(BITMAP_VoicePrompt);
        }
        x += sizeof(BITMAP_VoicePrompt);
        #endif

        if(!SCANNER_IsScanning()) {
        #ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
            if(gCurrentFunction == FUNCTION_TRANSMIT && gSetting_set_tmr == true)
            {
                convertTime(line, 0);
            }
            else if(FUNCTION_IsRx() && gSetting_set_tmr == true)
            {
                convertTime(line, 1);
            }
            else
        #endif
            {
                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                if(gEeprom.MENU_LOCK == true) {
                    memcpy(line + x + 2, gFontRO, sizeof(gFontRO));
                }
                else
                {
                #endif
                    uint8_t dw = (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) + (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) * 2;
#ifdef ENABLE_FEAT_SERVICE_MODES
                    // Which VFOs are listened to, framed; bold = the VFO PTT transmits on
                    // (thin when it cannot transmit): [A] that VFO only, [A B] the watch
                    // is running (the screen's solid border is the TX VFO too)
                    const uint8_t tx   = gEeprom.TX_VFO;
                    const bool    txOk = VfoCanTx(tx);
                    uint8_t      *p    = line + x;
                    *p++ = 0x7F;
                    p    = FrameCol(p, 0);
                    if (dw == 0) {
                        p = DrawVfoLetter(p, tx, txOk);
                    } else {
                        p = DrawVfoLetter(p, 0, tx == 0 && txOk);
                        if (dw == 2) {   // arrow from TX to RX
                            static const uint8_t arrow[2][3] = { { 0x22, 0x14, 0x08 }, { 0x08, 0x14, 0x22 } };
                            for (uint8_t c = 0; c < 3; c++)
                                p = FrameCol(p, arrow[tx][c]);
                            p = FrameCol(p, 0);
                        }
                        p = DrawVfoLetter(p, 1, tx == 1 && txOk);
                    }
                    *p = 0x7F;
#else
                    if(dw == 1 || dw == 3) { // DWR - dual watch + respond
                        if(gDualWatchActive)
                            memcpy(line + x + (dw==1?0:2), gFontDWR, sizeof(gFontDWR) - (dw==1?0:5));
                        else
                            memcpy(line + x + 3, gFontHold, sizeof(gFontHold));
                    }
                    else if(dw == 2) { // XB - crossband
                        memcpy(line + x + 2, gFontXB, sizeof(gFontXB));
                    }
                    else
                    {
                        memcpy(line + x + 2, gFontMO, sizeof(gFontMO));
                    }
#endif
                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                }
                #endif
            }
        }
        x += sizeof(gFontDWR) + 3;
    #endif

#ifdef ENABLE_VOX
    // VOX indicator
    if (gEeprom.VOX_SWITCH) {
        memcpy(line + x, gFontVox, sizeof(gFontVox));
        x1 = x + sizeof(gFontVox) + 1;
    }
    x += sizeof(gFontVox) + 3;
#endif

#ifdef ENABLE_FEAT_F4HWN
    // PTT indicator
#ifdef ENABLE_FEAT_SERVICE_MODES
    if (UI_FullView()) {   // full screen: the power, or no TX (A / B view: beside each mode icon)
        const bool sel = UI_PowerSelected(gEeprom.TX_VFO);
        if (!VfoCanTx(gEeprom.TX_VFO) && !sel) {
            memcpy(line + x, BITMAP_NoTx, sizeof(BITMAP_NoTx));
            x1 = x + sizeof(BITMAP_NoTx) + 1;
        } else {
            UI_PowerShort(&gEeprom.VfoInfo[gEeprom.TX_VFO], str);
            GUI_DisplaySmallest(str, (uint8_t)(x + 1u), 1, true, true);
            x1 = x + 18u;
            if (sel)   // being selected (F editor): inverted
                for (unsigned int i = x; i < x1; i++)
                    line[i] ^= 0x7F;
        }
    }
#else
    if (gSetting_set_ptt_session) {
        memcpy(line + x, gFontPttOnePush, sizeof(gFontPttOnePush));
        x1 = x + sizeof(gFontPttOnePush) + 1;
    }
    else
    {
        memcpy(line + x, gFontPttClassic, sizeof(gFontPttClassic));
        x1 = x + sizeof(gFontPttClassic) + 1;       
    }
#endif
    x += sizeof(gFontPttClassic) + 3;
#endif

    x = MAX(x1, 69u);

    const void *src = NULL;   // Pointer to the font/bitmap to copy
    size_t size = 0;          // Size of the font/bitmap

    // Determine the source and size based on conditions
    if (gEeprom.KEY_LOCK) {
        src = gFontKeyLock;
        size = sizeof(gFontKeyLock);
    }
    else if (gWasFKeyPressed) {
        #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        if (!gEeprom.MENU_LOCK) {
            src = gFontF;
            size = sizeof(gFontF);
        }
        #else
        src = gFontF;
        size = sizeof(gFontF);
        #endif
    }
    #ifdef ENABLE_FEAT_F4HWN
        else if (gMute) {
            src = gFontMute;
            size = sizeof(gFontMute);
        }
    #endif
    else if (gBackLight) {
        src = gFontLight;
        size = sizeof(gFontLight);
    }
    #ifdef ENABLE_FEAT_F4HWN_CHARGING_C
    else if (gChargingWithTypeC) {
        src = BITMAP_USB_C;
        size = sizeof(BITMAP_USB_C);
    }
    #endif

    // Perform the memcpy if a source was selected
    if (src) {
        memcpy(line + x + 1, src, size);
    }

    // Battery
#ifdef ENABLE_FEAT_SERVICE_MODES
    const unsigned int x2 = UI_StatusBattery(line);
    (void)str;
    char call[11];   // full screen: the callsign it transmits under (ham / GMRS), left of the battery
    if (UI_FullView() && SERVICE_TxCallsign(call))
        GUI_DisplaySmallest(call, (uint8_t)(x2 - strlen(call) * 4u - 2u), 1, true, true);
#else
    unsigned int x2 = LCD_WIDTH - sizeof(BITMAP_BatteryLevel1) - 0;

    UI_DrawBattery(line + x2, gBatteryDisplayLevel, gLowBatteryBlink);

    bool BatTxt = true;

    switch (gSetting_battery_text) {
        default:
        case 0:
            BatTxt = false;
            break;

        case 1:    // voltage
            const uint16_t voltage = (gBatteryVoltageAverage <= 999) ? gBatteryVoltageAverage : 999; // limit to 9.99V
            sprintf(str, "%u.%02u", voltage / 100, voltage % 100);
            break;

        case 2:     // percentage
            //gBatteryVoltageAverage = 999;
            sprintf(str, "%02u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
            break;
    }

    if (BatTxt) {
        x2 -= (7 * strlen(str));
        UI_PrintStringSmallBufferNormal(str, line + x2);
        /*
        uint8_t shift = (strlen(str) < 5) ? 92 : 88;
        GUI_DisplaySmallest(str, shift, 1, true, true);

        for (uint8_t i = shift - 2; i < 110; i++) {
            gStatusLine[i] ^= 0x7F; // invert
        }
        */
    }
#endif

    // **************

    ST7565_BlitStatusLine();
}
