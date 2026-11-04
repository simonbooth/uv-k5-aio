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
#include <stdlib.h>  // abs()

#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/dtmf.h"
#include "app/main.h"
#include "app/param_edit.h"
#ifdef ENABLE_AM_FIX
    #include "am_fix.h"
#endif
#include "bitmaps.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "service.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"
#include "audio.h"

#ifdef ENABLE_FEAT_F4HWN
    #include "driver/system.h"
#endif

center_line_t center_line = CENTER_LINE_NONE;

// Info row under a VFO's frequency (small 3x5 font): x of each field
#ifdef ENABLE_FEAT_SERVICE_MODES
    #define IR_SHIFT_X 53
    #define IR_REV_X   61
    #define IR_TYPE_X  65
    #define IR_TONE_X  75
#else
    #define IR_SHIFT_X 41
    #define IR_REV_X   51
    #define IR_TYPE_X  58
    #define IR_TONE_X  68
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
    #define UI_CENTER_DTMF 0   // live DTMF shows in the VFO's text row instead
#else
    #define UI_CENTER_DTMF 1
#endif

#ifdef ENABLE_FEAT_F4HWN
    static int8_t RxBlink;
    static int8_t RxBlinkLed = 0;
    static int8_t RxBlinkLedCounter;
    static int8_t RxLine;
    static uint32_t RxOnVfofrequency;

    bool isMainOnlyInputDTMF = false;

#ifdef ENABLE_FEAT_SERVICE_MODES
    bool UI_FullView(void)
    {   // transmitting: the TX VFO full screen, whichever view is picked
        return !SERVICE_GetAbView() || gCurrentFunction == FUNCTION_TRANSMIT;
    }
#endif

    static bool isMainOnly()
    {
#ifdef ENABLE_FEAT_SERVICE_MODES
        return UI_FullView();   // F+2 picks the view (scans too); the watch is separate
#else
        return (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) && (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF);
#endif
    }
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
// Privacy code heard on a GMRS channel that has none selected (the receiver
// still opens on any signal there): read from the BK4819's CTCSS / CDCSS
// detector while receiving, shown in brackets next to the channel number.
static uint8_t        gShowSquelch_500ms;   // > 0: show "SQL n" in place of the channel tag
static DCS_CodeType_t gHeardType;
static uint8_t        gHeardCode;
static uint8_t        gHeardHits;

// F's first item (tuning): the frequency / channel inverted (rows row..row+rows-1)
static void UI_TuneMark(unsigned int row, uint8_t rows, uint8_t x0, uint8_t x1)
{
    for (uint8_t r = 0; r < rows; r++)
        for (uint8_t x = x0; x < x1; x++)
            gFrameBuffer[row + r][x] ^= 0xFF;
}

// A memory's number at the left of its text row (3x5 font): "M12", or its group's
// digit inverted ("[1]12"); the info row's fields start IR_MEM_W further right.
#define IR_MEM_W 16u

#ifdef ENABLE_FMRADIO
static bool gFmStereoRx;   // broadcast FM playing, received in stereo (UI_DrawBlock sets it)
#endif

static uint8_t UI_InfoOx(unsigned int vfo)
{   // A / B view, memories and VFOs (the VFO's M+ there while the slot is selected)
    const uint16_t ch = gEeprom.ScreenChannel[vfo];
    return !UI_FullView() && (IS_MR_CHANNEL(ch) || IS_FREQ_CHANNEL(ch)) ? IR_MEM_W : 0u;
}

// A memory's number (3x5 font): "M12", its group's digit inverted ("[1]12"), "M12-" being
// typed, the slot being picked (M+ / a free memory / M-, inverted). Its inverted width back
// (0: none); n[0] 0: nothing (not a memory).
uint8_t UI_MemNo(unsigned int vfo, char *n)
{
    const uint16_t ch      = gEeprom.ScreenChannel[vfo];
    const bool     picking = vfo == gEditVfo && PARAM_Current() == PE_SLOT;
    const uint8_t  pick    = picking ? PARAM_SlotPick() : 0u;
    n[0] = 0;
    if (!IS_MR_CHANNEL(ch) && !picking)
        return 0;
    const bool    inputting = gInputBoxIndex != 0 && gEeprom.TX_VFO == vfo;
    const uint8_t group     = inputting || pick ? 0u : SERVICE_GroupOf(vfo);
    if (pick == PARAM_SLOT_DELETE)
        strcpy(n, "M-");
    else if (pick)
        sprintf(n, "M%u", pick);
    else if (!IS_MR_CHANNEL(ch))
        strcpy(n, "M+");
    else if (group)
        sprintf(n, "%u%u", group, ch + 1u);
    else if (!inputting)
        sprintf(n, "M%u", ch + 1u);
    else
        sprintf(n, "M%.3s", INPUTBOX_GetAscii());
    return picking ? (uint8_t)(strlen(n) * 4u + 1u) : group ? 5u : 0u;
}

// A / B view: at the left of the text row (full screen: the status bar)
static void UI_DrawMemNo(unsigned int vfo, unsigned int row)
{
    char          n[8];
    const uint8_t w = UI_MemNo(vfo, n);
    if (UI_FullView() || !n[0])
        return;
    GUI_DisplaySmallest(n, 2, (uint8_t)(row * 8u + 1u), false, true);
    for (uint8_t i = 1; i < w + 1u; i++)
        gFrameBuffer[row][i] ^= 0x7F;
}

// The squelch for slot 4 (3x5 font): "S0".."S9", "MO" monitoring.
void UI_SqlText(unsigned int vfo, char *s)
{
    if (gMonitor && vfo == gEeprom.TX_VFO)
        strcpy(s, "MO");
    else
        sprintf(s, "S%u", SQUELCH_OF(vfo));
}

bool UI_SqlSelected(unsigned int vfo)
{
    return vfo == gEditVfo && PARAM_Current() == PE_SQUELCH;
}

// The name being edited (10 characters from x, small font): the field inverted, or
// (editing) the character at the cursor.
static void UI_DrawNameEdit(uint8_t x, unsigned int row)
{
    UI_PrintStringSmallNormal(PARAM_NameText(), x, 0, row);
    const uint8_t c  = PARAM_Cursor();
    const uint8_t x0 = (uint8_t)(c == PARAM_CURSOR_NONE ? x - 1u : x + c * 7u - 1u);
    const uint8_t x1 = (uint8_t)(c == PARAM_CURSOR_NONE ? x + 70u : x0 + 7u);
    for (uint8_t i = x0; i < x1 && i < LCD_WIDTH; i++)
        gFrameBuffer[row][i] ^= 0x7F;
}

// The info-row item being selected on this VFO, inverted (row `row`, 3x5 font).
static void UI_HighlightInfo(unsigned int vfo, unsigned int row)
{
    static const uint8_t ranges[][2] = {   // [item - PE_MODE] = { x from, x to }
        [PE_MODE   - PE_MODE] = {  2, 18 },
        [PE_POWER  - PE_MODE] = { 0, 0 },     // (its own spot)
        [PE_RXTONE - PE_MODE] = { 64, 100 },
        [PE_TXTONE - PE_MODE] = { 60, 100 },   // with a "T" in front
        [PE_SHIFT  - PE_MODE] = { 52, 60 },
        [PE_OFFSET - PE_MODE] = { 64, 100 },
        [PE_STEP   - PE_MODE] = { 64, 100 },
        [PE_SQUELCH - PE_MODE] = { 0, 0 },     // (slot 4)
        [PE_VOLUME - PE_MODE] = {  2, 30 },    // broadcast FM
        [PE_STEREO - PE_MODE] = { 34, 62 },
    };
    const uint8_t item = PARAM_Current();
    const uint8_t ox   = item == PE_SQUELCH ? 0u : UI_InfoOx(vfo);
    if (vfo == gEditVfo && item >= PE_MODE && item <= PE_STEREO)
        for (uint8_t x = ranges[item - PE_MODE][0] + ox; x < ranges[item - PE_MODE][1] + ox; x++)
            gFrameBuffer[row][x] ^= 0x7F;
}


// The power, compact (3x5 font, under the mode icon / in the status bar): "5W" .. "20mW",
// "OFF" (power off: shown while it is being selected; else the no-TX icon).
void UI_PowerShort(const struct VFO_Info_t *v, char *out)
{
    static const char w[][5] = { "20mW", "1/8W", "1/4W", "1/2W", "1W", "2W", "5W" };
    const uint8_t p   = v->OUTPUT_POWER % 8u;
    const uint8_t idx = (p == OUTPUT_POWER_USER) ? gSetting_set_pwr : (uint8_t)(p - 1u);
    strcpy(out, v->TX_OFF ? "OFF" : idx < ARRAY_SIZE(w) ? w[idx] : "");
}

bool UI_PowerSelected(unsigned int vfo)
{
    return vfo == gEditVfo && PARAM_Current() == PE_POWER;
}

#if 0   // (the power: under the mode icon / in the status bar, selected there too)
static void UI_DrawPower(const VFO_Info_t *v, unsigned int vfo, unsigned int row)
{
    const bool selecting = vfo == gEditVfo && PARAM_Current() == PE_POWER;
    if (!selecting)
        return;

    static const char names[][3] = { "U", "L1", "L2", "L3", "L4", "L5", "M", "H" };
    char          s[10];
    const uint8_t p   = v->OUTPUT_POWER % 8u;
    const uint8_t idx = (p == OUTPUT_POWER_USER) ? gSetting_set_pwr : (uint8_t)(p - 1u);
    if (v->TX_OFF)
        strcpy(s, "OFF");
    else
        sprintf(s, "%.2s %s", names[p], idx < ARRAY_SIZE(gWatts) ? gWatts[idx] : "");
    GUI_DisplaySmallest(s, (uint8_t)(20u + UI_InfoOx(vfo)), (uint8_t)(row * 8u + 1u), false, true);
}
#endif

// The tone the info row shows: the one being selected, else TX while transmitting.
static const FREQ_Config_t *UI_ShownTone(const VFO_Info_t *v, bool tx, unsigned int vfo)
{
    if (vfo == gEditVfo && gEditItem == PE_TXTONE)
        tx = true;
    else if (vfo == gEditVfo && gEditItem == PE_RXTONE)
        tx = false;
    else if (v->pRX->CodeType == CODE_TYPE_OFF)
        tx = true;   // a TX-only tone (repeater access): that one, marked T
    return tx ? v->pTX : v->pRX;
}

// T Live: listen for codes on this VFO (it has none of its own set)
static bool UI_DetectCodes(const VFO_Info_t *vfo)
{
    const uint8_t m = SERVICE_GetCodeDetect();
    return vfo->pRX->CodeType == CODE_TYPE_OFF &&
           (m == CODE_DETECT_ALL || (m == CODE_DETECT_AUTO && SERVICE_OfChannel(vfo->CHANNEL_SAVE) == SERVICE_GMRS));
}

static void UI_SniffSubCode(void)
{
    if (!UI_DetectCodes(gRxVfo))
        return;

    uint32_t       cdcss;
    uint16_t       ctcss;
    DCS_CodeType_t type = CODE_TYPE_OFF;
    uint8_t        code = 0xFF;

    switch (BK4819_GetCxCSSScanResult(&cdcss, &ctcss)) {
        case BK4819_CSS_RESULT_CTCSS:
            code = DCS_GetCtcssCode(ctcss);   // 0xFF unless within 5 Hz of a standard tone
            type = CODE_TYPE_CONTINUOUS_TONE;
            break;
        case BK4819_CSS_RESULT_CDCSS:
            code = DCS_GetCdcssCode(cdcss);
            type = CODE_TYPE_DIGITAL;
            break;
        default:
            break;
    }
    if (code == 0xFF)
        return;

    // the same reading twice in a row before it is shown (CTCSS measurement wobbles)
    gHeardHits = (type == gHeardType && code == gHeardCode) ? (gHeardHits ? 2u : 1u) : 0u;
    gHeardType = type;
    gHeardCode = code;
    if (gHeardHits == 1u)
        gUpdateDisplay = true;
}

// A privacy code as the selected brand numbers it ("12", "102"), returning the
// Midland-style "CTCSS" / "DCS" label to show above it. Tones the brand does not
// number keep their raw form ("67.0", "D023N") and get no label.
static const char *UI_FormatCode(uint16_t channel, DCS_CodeType_t type, uint8_t code, char *out)
{
    bool          separate;
    const uint8_t n = SUBCODE_FromTone(channel, type, code, &separate);
    if (n == 0 || n == 0xFF || SERVICE_SubCodeProfileFor(channel) == SUBCODE_OFCOM) {
        SUBCODE_Format(channel, type, code, out);   // (Ofcom: "C13", "D023" say what they are)
        return NULL;
    }
    sprintf(out, "%u", n);
    return type == CODE_TYPE_CONTINUOUS_TONE ? "CTCSS" : "DCS";
}

// What was decoded on a VFO, for its text row: live DTMF ("DTMF 123#", D Live) or
// a CTCSS / DCS code heard while receiving ("RX DCS 102", "RX 88.5"; T Live); on the
// TX VFO a DTMF string being typed in or sent (">123", shown as TX starts).
static bool UI_DecodedText(const VFO_Info_t *vfo, char *out)
{
    if (vfo == gTxVfo && (gDTMF_InputMode || gDTMF_ReplyState == DTMF_REPLY_ANI)) {
        sprintf(out, ">%s", gDTMF_InputMode ? gDTMF_InputBox : gDTMF_String);
        return true;
    }
    if (gSetting_live_DTMF_decoder && gDTMF_RX_live[0] && vfo == &gEeprom.VfoInfo[gDTMF_RX_live_vfo]) {
        const unsigned int len = strlen(gDTMF_RX_live);
        sprintf(out, "DTMF %s", gDTMF_RX_live + (len > 9u ? len - 9u : 0u));
        return true;
    }
    if (!FUNCTION_IsRx()) {
        gHeardHits = 0;
        return false;
    }
    if (vfo != gRxVfo || gHeardHits == 0 || !UI_DetectCodes(vfo))
        return false;

    char        c[7];
    const char *kind = NULL;   // GMRS: the brand's numbering; elsewhere the tone itself
    if (SERVICE_OfChannel(vfo->CHANNEL_SAVE) == SERVICE_GMRS)
        kind = UI_FormatCode(vfo->CHANNEL_SAVE, gHeardType, gHeardCode, c);
    else
        SUBCODE_Format(vfo->CHANNEL_SAVE, gHeardType, gHeardCode, c);
    sprintf(out, "RX %s%s%s", kind ? kind : "", kind ? " " : "", c);
    return true;
}

// Texts of a service channel: `code` beside the number ("12" selected, "(12)"
// heard on a GMRS channel with none selected) with its CTCSS / DCS `kind`, and
// `tag` under it ("FRS 2W", "CALLING 5W", "GUARD", or "SQL 3" after a squelch change).
static const char *gFavName;     // drawing a favorite: the memory's name, for the tag
static bool        gFavOffPlan;  // ... on no channel of its plan: labelled by its frequency

static void UI_ServiceTexts(const VFO_Info_t *vfo, uint16_t channel, char *code, const char **kind, char *tag)
{
    const Service_t s = SERVICE_OfChannel(channel);

    code[0] = 0;
    *kind   = NULL;
    if (s == SERVICE_GMRS) {
        if (PARAM_Current() == PE_CODE && vfo == &gEeprom.VfoInfo[gEditVfo] && vfo->freq_config_RX.CodeType == CODE_TYPE_OFF) {
            strcpy(code, "OFF");   // selecting: show where the cursor is
        } else if (vfo->freq_config_RX.CodeType != CODE_TYPE_OFF) {
            *kind = UI_FormatCode(channel, vfo->freq_config_RX.CodeType, vfo->freq_config_RX.Code, code);
        }
    }

    // which service the selected power makes this (the power itself, no TX: beside the
    // mode icon / in the status bar)
    const uint8_t pwr = vfo->OUTPUT_POWER;

    const uint8_t n = (uint8_t)(vfo - gEeprom.VfoInfo);
    (void)n;
    if (gFavName && (gFavName[0] || gFavOffPlan))   // off the plan: never another channel's use
        sprintf(tag, "%.15s", gFavName);
    else if (s == SERVICE_AIR)
        strcpy(tag, SERVICE_ChannelUse(channel));
    else if (s == SERVICE_GMRS)
        strcpy(tag, SERVICE_IsPmr() ? (channel >= SVC_GMRS_FIRST + PLAN_PMR_COUNT ? "SIMPLE UK" : "PMR446")
                    : channel >= SVC_GMRS_FIRST + PLAN_GMRS_SAVED ? "MURS"
                    : SERVICE_IsFrs(channel, pwr) ? "FRS" : "GMRS");
    else
        strcpy(tag, SERVICE_ChannelUse(channel));
}

// Big text: digits in the big-digit font, letters in the main font, '.' a dot;
// scale 1 (16 px, pages page..page+1) or 2 (32 px, pages page..page+3, every
// pixel doubled). Returns the width it took.
static uint8_t UI_DrawBigText(const char *s, uint8_t x, uint8_t page, uint8_t scale, bool draw)
{
    const uint8_t x0 = x;
    for (; *s; s++) {
        if (*s == '.') {   // at the baseline, scaled
            if (draw)
                memset(gFrameBuffer[page + 2u * scale - 1u] + x, scale == 2 ? 0x3C : 0x60, scale == 2 ? 6 : 3);
            x += scale == 2 ? 7u : 4u;
            continue;
        }
        const bool     digit = *s >= '0' && *s <= '9';
        const uint8_t *g     = digit ? gFontBigDigits[*s - '0'] : gFontBig[*s - ' ' - 1];
        const uint8_t  w     = digit ? 10u : 7u;
        for (uint8_t c = 0; draw && c < w; c++) {
            if (scale == 1) {
                gFrameBuffer[page][x + c]     = g[c];
                gFrameBuffer[page + 1][x + c] = g[w + c];
                continue;
            }
            const uint16_t col = g[c] | (uint16_t)(g[w + c] << 8);
            uint32_t       d   = 0;
            for (uint8_t r = 0; r < 16; r++)
                if (col >> r & 1u)
                    d |= 3ul << (2u * r);
            for (uint8_t p = 0; p < 4; p++)
                gFrameBuffer[page + p][x + 2u * c] = gFrameBuffer[page + p][x + 2u * c + 1u] = (uint8_t)(d >> (8u * p));
        }
        x += (uint8_t)((digit ? 12u : 9u) * scale);
    }
    return (uint8_t)(x - x0);
}

#endif

const int8_t dBmCorrTable[7] = {
            -15, // band 1
            -25, // band 2
            -20, // band 3
            -4, // band 4
            -7, // band 5
            -6, // band 6
             -1  // band 7
        };

const char *VfoStateStr[] = {
       [VFO_STATE_NORMAL]="",
       [VFO_STATE_BUSY]="BUSY",
       [VFO_STATE_BAT_LOW]="BAT LOW",
       [VFO_STATE_TX_DISABLE]="TX DISABLE",
       [VFO_STATE_TIMEOUT]="TIMEOUT",
       [VFO_STATE_ALARM]="ALARM",
       [VFO_STATE_VOLTAGE_HIGH]="VOLT HIGH"
};

// ***************************************************************************

static void DrawSmallAntennaAndBars(uint8_t *p, unsigned int level)
{
    if(level>6)
        level = 6;

    memcpy(p, BITMAP_Antenna, ARRAY_SIZE(BITMAP_Antenna));

    for(uint8_t i = 1; i <= level; i++) {
        char bar = (0xff << (6-i)) & 0x7F;
        memset(p + 2 + i*3, bar, 2);
    }
}
#if defined ENABLE_AUDIO_BAR || defined ENABLE_RSSI_BAR

static void DrawLevelBar(uint8_t xpos, uint8_t line, uint8_t level, uint8_t bars)
{
#ifndef ENABLE_FEAT_F4HWN
    const char hollowBar[] = {
        0b01111111,
        0b01000001,
        0b01000001,
        0b01111111
    };
#endif
    
    uint8_t *p_line = gFrameBuffer[line];
    level = MIN(level, bars);

    for(uint8_t i = 0; i < level; i++) {
#ifdef ENABLE_FEAT_F4HWN
        if(gSetting_set_met)
        {
            const char hollowBar[] = {
                0b01111111,
                0b01000001,
                0b01000001,
                0b01111111
            };

            if(i < bars - 4) {
                for(uint8_t j = 0; j < 4; j++)
                    p_line[xpos + i * 5 + j] = (~(0x7F >> (i + 1))) & 0x7F;
            }
            else {
                memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
            }
        }
        else
        {
            const char hollowBar[] = {
                0b00111110,
                0b00100010,
                0b00100010,
                0b00111110
            };

            const char simpleBar[] = {
                0b00111110,
                0b00111110,
                0b00111110,
                0b00111110
            };

            if(i < bars - 4) {
                memcpy(p_line + (xpos + i * 5), &simpleBar, ARRAY_SIZE(simpleBar));
            }
            else {
                memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
            }
        }
#else
        if(i < bars - 4) {
            for(uint8_t j = 0; j < 4; j++)
                p_line[xpos + i * 5 + j] = (~(0x7F >> (i+1))) & 0x7F;
        }
        else {
            memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
        }
#endif
    }
}
#endif

#ifdef ENABLE_AUDIO_BAR

// Approximation of a logarithmic scale using integer arithmetic
uint8_t log2_approx(unsigned int value) {
    uint8_t log = 0;
    while (value >>= 1) {
        log++;
    }
    return log;
}

void UI_DisplayAudioBar(void)
{
    if (gSetting_mic_bar)
    {
        if(gLowBattery && !gLowBatteryConfirmed)
            return;

#ifdef ENABLE_FEAT_F4HWN
        RxBlinkLed = 0;
        RxBlinkLedCounter = 0;
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        unsigned int line;
        if (isMainOnly())
        {
            line = 5;
        }
        else
        {
            line = 3;
        }
#else
        const unsigned int line = 3;
#endif

        if (gCurrentFunction != FUNCTION_TRANSMIT ||
            gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
            || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
            )
        {
            return;  // screen is in use
        }

#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
        if (gAlarmState != ALARM_STATE_OFF)
            return;
#endif
        static uint8_t barsOld = 0;
        const uint8_t thresold = 18; // arbitrary thresold
        //const uint8_t barsList[] = {0, 0, 0, 1, 2, 3, 4, 5, 6, 8, 10, 13, 16, 20, 25, 25};
        const uint8_t barsList[] = {0, 0, 0, 1, 2, 3, 5, 7, 9, 12, 15, 18, 21, 25, 25, 25};
        uint8_t logLevel;
        uint8_t bars;

        unsigned int voiceLevel  = BK4819_GetVoiceAmplitudeOut();  // 15:0

        voiceLevel = (voiceLevel >= thresold) ? (voiceLevel - thresold) : 0;
        logLevel = log2_approx(MIN(voiceLevel * 16, 32768u) + 1);
        bars = barsList[logLevel];
        barsOld = (barsOld - bars > 1) ? (barsOld - 1) : bars;

        uint8_t *p_line = gFrameBuffer[line];
        memset(p_line, 0, LCD_WIDTH);

        DrawLevelBar(2, line, barsOld, 25);

        if (gCurrentFunction == FUNCTION_TRANSMIT)
            ST7565_BlitFullScreen();
    }
}
#endif

void DisplayRSSIBar(const bool now)
{
#if defined(ENABLE_RSSI_BAR)

    const unsigned int txt_width    = 7 * 8;                 // 8 text chars
    const unsigned int bar_x        = 2 + txt_width + 4;     // X coord of bar graph

#ifdef ENABLE_FEAT_F4HWN
    /*
    const char empty[] = {
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    };
    */

    unsigned int line;
    if (isMainOnly())
    {
        line = 5;
    }
    else
    {
        line = 3;
    }

#ifdef ENABLE_FEAT_SERVICE_MODES
    if (now) {
        UI_SniffSubCode();
        if (!isMainOnly()) {   // A / B view: redrawn whole (its blocks are shifted for the border)
            gUpdateDisplay = true;
            return;
        }
    }
#endif

    //char rx[4];
    //sprintf(String, "%d", RxBlink);
    //UI_PrintStringSmallBold(String, 80, 0, RxLine);

#ifndef ENABLE_FEAT_SERVICE_MODES   // signal bars instead: A / B view by the icon, single view in the status bar
    if(RxLine >= 0 && center_line != CENTER_LINE_IN_USE)
    {
        if (RxBlink == 0 || RxBlink == 1) {
            UI_PrintStringSmallBold("RX", 10, 0, RxLine);
            if (RxBlink == 1) RxBlink = 2;
        } else {
            for (uint8_t i = 10; i < 26; i++)
            {
                gFrameBuffer[RxLine][i] = 0x00;
            }
            RxBlink = 1;
        }
        if (now)
            ST7565_BlitLine(RxLine);
    }
#endif
#else
    const unsigned int line = 3;
#endif
    uint8_t           *p_line        = gFrameBuffer[line];
    char               str[16];

#ifndef ENABLE_FEAT_F4HWN
    const char plus[] = {
        0b00011000,
        0b00011000,
        0b01111110,
        0b01111110,
        0b01111110,
        0b00011000,
        0b00011000,
    };
#endif

    if ((gEeprom.KEY_LOCK && gKeypadLocked > 0) || center_line != CENTER_LINE_RSSI)
        return;     // display is in use

    if (gCurrentFunction == FUNCTION_TRANSMIT ||
        gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
        || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
        )
        return;     // display is in use

    if (now)
        memset(p_line, 0, LCD_WIDTH);

#ifdef ENABLE_FEAT_F4HWN
    int16_t rssi_dBm =
        BK4819_GetRSSI_dBm()
#ifdef ENABLE_AM_FIX
        + ((gSetting_AM_fix && gRxVfo->Modulation == MODULATION_AM) ? AM_fix_get_gain_diff() : 0)
#endif
        + dBmCorrTable[gRxVfo->Band];

    rssi_dBm = -rssi_dBm;

    if(rssi_dBm > 141) rssi_dBm = 141;
    if(rssi_dBm < 53) rssi_dBm = 53;

    uint8_t s_level = 0;
    uint8_t overS9dBm = 0;
    uint8_t overS9Bars = 0;

    if(rssi_dBm >= 93) {
        s_level = map(rssi_dBm, 141, 93, 1, 9);
    }
    else {
        s_level = 9;
        overS9dBm = map(rssi_dBm, 93, 53, 0, 40);
        overS9Bars = map(overS9dBm, 0, 40, 0, 4);
    }
#else
    const int16_t s0_dBm   = -gEeprom.S0_LEVEL;                  // S0 .. base level
    const int16_t rssi_dBm =
        BK4819_GetRSSI_dBm()
#ifdef ENABLE_AM_FIX
        + ((gSetting_AM_fix && gRxVfo->Modulation == MODULATION_AM) ? AM_fix_get_gain_diff() : 0)
#endif
        + dBmCorrTable[gRxVfo->Band];

    int s0_9 = gEeprom.S0_LEVEL - gEeprom.S9_LEVEL;
    const uint8_t s_level = MIN(MAX((int32_t)(rssi_dBm - s0_dBm)*100 / (s0_9*100/9), 0), 9); // S0 - S9
    uint8_t overS9dBm = MIN(MAX(rssi_dBm + gEeprom.S9_LEVEL, 0), 99);
    uint8_t overS9Bars = MIN(overS9dBm/10, 4);
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
    {   // signal bars (6: S1-S9, +10..+40 dB): A / B view right of the mode icon, single
        // view in the status bar (the stock bar row below is left out)
        const uint8_t bars = (uint8_t)((s_level + overS9Bars) * 6u / 13u);
        if (!isMainOnly()) {
            if (RxLine >= 0) {
                memset(gFrameBuffer[RxLine] + 10, 0, 17);
                UI_DrawSignalBars(gFrameBuffer[RxLine] + 10, bars);
            }
            return;
        }
        if (bars != gRssiBars) {
            gRssiBars     = bars;
            gUpdateStatus = true;
        }
        return;
    }
#endif

#ifdef ENABLE_FEAT_F4HWN
    if (gSetting_set_gui)
    {
        sprintf(str, "%3d", -rssi_dBm);
        UI_PrintStringSmallNormal(str, LCD_WIDTH + 8, 0, line - 1);
    }
    else
    {
        sprintf(str, "% 4d %s", -rssi_dBm, "dBm");
        if(isMainOnly())
            GUI_DisplaySmallest(str, 2, 41, false, true);
        else
            GUI_DisplaySmallest(str, 2, 25, false, true);
    }

    if(overS9Bars == 0) {
        sprintf(str, "S%d", s_level);
    }
    else {
        sprintf(str, "+%02d", overS9dBm);
    }

    UI_PrintStringSmallNormal(str, LCD_WIDTH + 38, 0, line - 1);
#else
    if(overS9Bars == 0) {
        sprintf(str, "% 4d S%d", -rssi_dBm, s_level);
    }
    else {
        sprintf(str, "% 4d  %2d", -rssi_dBm, overS9dBm);
        memcpy(p_line + 2 + 7*5, &plus, ARRAY_SIZE(plus));
    }

    UI_PrintStringSmallNormal(str, 2, 0, line);
#endif
    DrawLevelBar(bar_x, line, s_level + overS9Bars, 13);
    if (now)
        ST7565_BlitLine(line);
#else
    int16_t rssi = BK4819_GetRSSI();
    uint8_t Level;

    if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][3]) {
        Level = 6;
    } else if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][2]) {
        Level = 4;
    } else if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][1]) {
        Level = 2;
    } else if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][0]) {
        Level = 1;
    } else {
        Level = 0;
    }

    uint8_t *pLine = (gEeprom.RX_VFO == 0)? gFrameBuffer[2] : gFrameBuffer[6];
    if (now)
        memset(pLine, 0, 23);
    DrawSmallAntennaAndBars(pLine, Level);
    if (now)
        ST7565_BlitFullScreen();
#endif

}

#ifdef ENABLE_AGC_SHOW_DATA
void UI_MAIN_PrintAGC(bool now)
{
    char buf[20];
    memset(gFrameBuffer[3], 0, 128);
    union {
        struct {
            uint16_t _ : 5;
            uint16_t agcSigStrength : 7;
            int16_t gainIdx : 3;
            uint16_t agcEnab : 1;
        };
        uint16_t __raw;
    } reg7e;
    reg7e.__raw = BK4819_ReadRegister(0x7E);
    uint8_t gainAddr = reg7e.gainIdx < 0 ? 0x14 : 0x10 + reg7e.gainIdx;
    union {
        struct {
            uint16_t pga:3;
            uint16_t mixer:2;
            uint16_t lna:3;
            uint16_t lnaS:2;
        };
        uint16_t __raw;
    } agcGainReg;
    agcGainReg.__raw = BK4819_ReadRegister(gainAddr);
    int8_t lnaShortTab[] = {-28, -24, -19, 0};
    int8_t lnaTab[] = {-24, -19, -14, -9, -6, -4, -2, 0};
    int8_t mixerTab[] = {-8, -6, -3, 0};
    int8_t pgaTab[] = {-33, -27, -21, -15, -9, -6, -3, 0};
    int16_t agcGain = lnaShortTab[agcGainReg.lnaS] + lnaTab[agcGainReg.lna] + mixerTab[agcGainReg.mixer] + pgaTab[agcGainReg.pga];

    sprintf(buf, "%d%2d %2d %2d %3d", reg7e.agcEnab, reg7e.gainIdx, -agcGain, reg7e.agcSigStrength, BK4819_GetRSSI());
    UI_PrintStringSmallNormal(buf, 2, 0, 3);
    if(now)
        ST7565_BlitLine(3);
}
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
uint8_t gRssiBars;   // the receiving VFO's signal, 0-6 bars, for the status bar

// Antenna + 6 rising one-pixel bars, 17 px wide.
void UI_DrawSignalBars(uint8_t *p, uint8_t level)
{
    memcpy(p, BITMAP_Antenna, ARRAY_SIZE(BITMAP_Antenna));
    for (uint8_t i = 1; i <= level && i <= 6u; i++)
        p[4u + 2u * i] = (uint8_t)((0xFFu << (6u - i)) & 0x7Fu);
}

// A VFO's mode icon (7 x 7); a ham VFO frequency shows a slim antenna and its ham
// band (1 = 10 m, 6, 2, 3 = 1.25 m, 7 = 70 cm, - outside them).
void UI_DrawModeIcon(uint8_t *p, uint8_t vfo)
{
    const uint8_t  m  = SERVICE_ModeOf(vfo);
    const uint16_t ch = gEeprom.ScreenChannel[vfo];
    if (m == SERVICE_HAM && IS_FREQ_CHANNEL(ch)) {
        const uint8_t *d = gFont3x5[PLAN_HamBandDigit(SERVICE_GetHamRegion(), gEeprom.VfoInfo[vfo].freq_config_RX.Frequency) - ' '];
        p[0] = 0x01; p[1] = 0x7E; p[2] = 0x01; p[3] = 0x00;
        memcpy(p + 4, d, 3);
    } else {
        memcpy(p, BITMAP_ModeIcons[m], 7);
    }
}

void UI_MAIN_ShowSquelch(void)
{   // (slot 4 shows it: the block, the status bar)
    gUpdateDisplay = true;
    gUpdateStatus  = true;
}
#endif

void UI_MAIN_TimeSlice500ms(void)
{
#ifdef ENABLE_FEAT_SERVICE_MODES
    if (gShowSquelch_500ms && --gShowSquelch_500ms == 0)
        gUpdateDisplay = true;
#endif
    if(gScreenToDisplay==DISPLAY_MAIN) {
#ifdef ENABLE_AGC_SHOW_DATA
        UI_MAIN_PrintAGC(true);
        return;
#endif

        if(FUNCTION_IsRx()) {
            DisplayRSSIBar(true);
        }
#ifdef ENABLE_FEAT_F4HWN // Blink Green Led for white...
        else if(gSetting_set_eot > 0 && RxBlinkLed == 2)
        {
            if(RxBlinkLedCounter <= 8)
            {
                if(RxBlinkLedCounter % 2 == 0)
                {
                    if(gSetting_set_eot > 1 )
                    {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    }
                }
                else
                {
                    if(gSetting_set_eot > 1 )
                    {
                        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                    }

                    if(gSetting_set_eot == 1 || gSetting_set_eot == 3)
                    {
                        switch(RxBlinkLedCounter)
                        {
                            case 1:
                            AUDIO_PlayBeep(BEEP_400HZ_30MS);
                            break;

                            case 3:
                            AUDIO_PlayBeep(BEEP_400HZ_30MS);
                            break;

                            case 5:
                            AUDIO_PlayBeep(BEEP_500HZ_30MS);
                            break;

                            case 7:
                            AUDIO_PlayBeep(BEEP_600HZ_30MS);
                            break;
                        }
                    }
                }
                RxBlinkLedCounter += 1;
            }
            else
            {
                RxBlinkLed = 0;
            }
        }
#endif
    }
}

// ***************************************************************************

#ifdef ENABLE_FEAT_SERVICE_MODES
// The A / B view's border round a VFO's block (A: y 1-27, B: y 29-55), solid or
// dashed (2 on, 2 off).
static void UI_DrawBlockBorder(uint8_t vfo, bool dashed)
{
    const uint8_t y0 = vfo ? 29u : 1u;
    for (uint8_t y = y0; y <= y0 + 26u; y++) {
        uint8_t      *p   = gFrameBuffer[y / 8u];
        const uint8_t bit = (uint8_t)(1u << (y % 8u));
        if (!dashed || (y & 2u)) {
            p[0] |= bit;
            p[LCD_WIDTH - 1] |= bit;
        }
        if (y == y0 || y == y0 + 26u)
            for (uint8_t x = 1; x < LCD_WIDTH - 1; x++)
                if (!dashed || (x & 2u))
                    p[x] |= bit;
    }
}
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
// ---------------------------------------------------------------------------
// One VFO's block, the same for whatever it shows (VFO, memory, plan channel,
// favorite) and in both views: a label in the big font (the frequency, or a plan's
// channel name: "15R", "16", "WX1") with a privacy code beside it, then the tag (a
// memory's name, a channel's use) and the info row (mode, power, shift, tone,
// SQL). The A / B view has one row under the label: the tag, else (or while one
// of its items is selected) the info row. Full screen draws the same block, the
// label at double size wherever it fits, the tag and the info row both, and the
// selected parameter's description under them.
// ---------------------------------------------------------------------------
#define BLOCK_X0  24u    // the label's field: x 24 .. 127 (the mode icon, TX / RX left of it;
                         // full screen: the whole width, TX in the status bar)

// The info row (3x5 font): mode, power, shift, tone (or the step), reverse, squelch;
// while transmitting the power bars in place of the mode.
static void UI_DrawInfoRow(unsigned int vfo_num, unsigned int row, enum Vfo_txtr_mode mode)
{
    const VFO_Info_t *v  = &gEeprom.VfoInfo[vfo_num];
    const uint8_t     y  = (uint8_t)(row * 8u + 1u);
    const uint8_t     ox = UI_InfoOx(vfo_num);   // a memory: its number first
    char              s[12];

    UI_DrawMemNo(vfo_num, row);
    if (mode == VFO_MODE_TX)
        DrawSmallAntennaAndBars(gFrameBuffer[row] + ox, (unsigned int)(v->OUTPUT_POWER - 1u));
#ifdef ENABLE_FMRADIO
    if (v->Modulation == MODULATION_BFM) {   // broadcast FM: MONO (set), STEREO (set, and received), else
        const uint8_t item = vfo_num == gEditVfo ? PARAM_Current() : PE_NONE;   // blank; the volume
        if (item == PE_VOLUME) {                                                  // while selected
            sprintf(s, "VOL %u", SERVICE_FmVolume());
            GUI_DisplaySmallest(s, (uint8_t)(3u + ox), y, false, true);
        }
        GUI_DisplaySmallest(!SERVICE_FmStereo() ? "MONO" : gFmStereoRx || item == PE_STEREO ? "STEREO" : "",
                            (uint8_t)(36u + ox), y, false, true);
        UI_HighlightInfo(vfo_num, row);
        return;
    }
#endif
    if (mode != VFO_MODE_TX)
        GUI_DisplaySmallest(v->Modulation != MODULATION_FM ? gModulationStr[v->Modulation]
                            : v->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE ? "FM" : NARROWER_ON(v) ? "N+" : "NFM", (uint8_t)(3u + ox), y, false, true);   // CHIRP's names

    // the tone (the one being selected; TX while transmitting), else the step
    const FREQ_Config_t *c     = UI_ShownTone(v, mode == VFO_MODE_TX, vfo_num);
    static const char    types[][3] = { "", "CT", "DC", "DC" };
    const char          *type  = c->CodeType < ARRAY_SIZE(types) ? types[c->CodeType] : "";
    int8_t               shift = 0;
    switch (c->CodeType) {
        case CODE_TYPE_CONTINUOUS_TONE:
            sprintf(s, "%u.%u", CTCSS_Options[c->Code] / 10, CTCSS_Options[c->Code] % 10);
            break;
        case CODE_TYPE_DIGITAL:
        case CODE_TYPE_REVERSE_DIGITAL:
            sprintf(s, "%03o%c", DCS_Options[c->Code], c->CodeType == CODE_TYPE_DIGITAL ? 'N' : 'I');
            break;
        default:
            sprintf(s, "%d.%02uK", v->StepFrequency / 100, v->StepFrequency % 100);
            shift = -10;
    }
    if (vfo_num == gEditVfo) {   // the tone slot shows what is being selected
        const uint8_t item = PARAM_Current();
        if (item == PE_OFFSET) {
            const uint32_t o = v->TX_OFFSET_FREQUENCY;   // 10 Hz
            sprintf(s, "%u.%03u", (unsigned)(o / 100000u), (unsigned)(o / 100u % 1000u));
            type  = "";
            shift = -10;
        } else if ((item == PE_RXTONE || item == PE_TXTONE) && c->CodeType == CODE_TYPE_OFF) {
            strcpy(s, "OFF");   // (no type prefix, shifted: as for the step)
        }
    }
    if (c == v->pTX && (c->CodeType != CODE_TYPE_OFF || PARAM_Current() == PE_TXTONE))
        GUI_DisplaySmallest("T", (uint8_t)(IR_REV_X + ox), y, false, true);
    if (type[0])
        GUI_DisplaySmallest(type, (uint8_t)(IR_TYPE_X + ox), y, false, true);
    GUI_DisplaySmallest(s, (uint8_t)(IR_TONE_X + ox + shift), y, false, true);


    if (v->freq_config_RX.Frequency != v->freq_config_TX.Frequency) {   // the TX offset
        static const char dirs[][2] = { "", "+", "-" };
        UI_PrintStringSmallNormal(dirs[v->TX_OFFSET_FREQUENCY_DIRECTION % 3u], (uint8_t)(IR_SHIFT_X + ox), 0, row);
    }
    if (v->FrequencyReverse)
        GUI_DisplaySmallest("R", (uint8_t)(IR_REV_X + ox), y, false, true);

    // full screen: the compander (the squelch: slot 4)
    const uint16_t ch = gEeprom.ScreenChannel[vfo_num];
    if (UI_FullView() && ch <= FREQ_CHANNEL_LAST && gMR_ChannelAttributes[ch].compander) {
        static const char cmp[][4] = { "", "CTX", "CRX", "CTR" };
        GUI_DisplaySmallest(cmp[gMR_ChannelAttributes[ch].compander & 3u], 114, y, false, true);
    }
    UI_HighlightInfo(vfo_num, row);
}

// The decimals (of MHz) a value in 10 Hz units needs: 1 (100 kHz) .. 5 (10 Hz).
static uint8_t UI_Decimals(uint32_t v)
{
    uint8_t d = 5;
    for (uint32_t m = 10; d > 1 && v % m == 0; m *= 10)
        d--;
    return d;
}

// Widths at a scale: the big font (16 px; 2: 32 px), one size down for the label's
// suffix and the code (scale 1: the small font).
static uint8_t UI_BigWidth(const char *s, uint8_t scale)
{
    return UI_DrawBigText(s, 0, 0, scale, false);
}

static uint8_t UI_SmallerWidth(const char *s, uint8_t scale)
{
    return scale == 2 ? UI_DrawBigText(s, 0, 0, 1, false) : (uint8_t)(strlen(s) * 7u);
}

static void UI_DrawSmallerRow(const char *s, uint8_t x, uint8_t page, uint8_t scale)
{   // one size down at `page` (scale 1: the small font's row, 2: the 16 px font's two)
    if (scale == 2)
        UI_DrawBigText(s, x, page, 1, true);
    else
        UI_PrintStringSmallNormal(s, x, 0, page);
}

// Moves `w` columns of two pages one pixel down / up: the stacked part lined up
// with the big digits (their rows 1-14 of 16; the small fonts start at row 0).
// (shr 0: down, 1: unmoved, 2: up)
static void UI_NudgeRows(uint8_t x, uint8_t w, uint8_t page, uint8_t shr)
{
    for (uint8_t *p = gFrameBuffer[page] + x; w--; p++) {
        const uint32_t n = (uint32_t)(p[0] | p[LCD_WIDTH] << 8) << 1 >> shr;
        p[0]         = (uint8_t)n;
        p[LCD_WIDTH] = (uint8_t)(n >> 8);
    }
}

static void UI_DrawBlock(unsigned int vfo_num, unsigned int line, unsigned int activeTxVFO)
{
    VFO_Info_t        *v      = &gEeprom.VfoInfo[vfo_num];
    const uint16_t     ch     = gEeprom.ScreenChannel[vfo_num];
    const bool         full   = isMainOnly();
    const uint8_t      item   = vfo_num == gEditVfo ? PARAM_Current() : PE_NONE;
    enum Vfo_txtr_mode mode   = VFO_MODE_NONE;
    uint32_t           f      = v->pRX->Frequency;
    char               label[12], code[8] = "", tag[17] = "";
    char               dec23[3] = "", dec45[6] = "";   // one size down, stacked: a frequency's
                                                       // decimals 2-3 / 4-5, a name's letters ("15R")
    uint32_t           shown  = f;                       // the frequency the label shows (air: its name)
    const char        *kind   = NULL;

    if (!full) {   // A / B view, under the mode icon: the power, or no TX (single view: the status
        const bool sel = UI_PowerSelected(vfo_num);   // bar; right of the icon: the signal / TX)
        if (!SERVICE_CanTx(v) && !sel)
            memcpy(gFrameBuffer[line + 1] + 1, BITMAP_NoTx, sizeof(BITMAP_NoTx));
        else {
            char p[5];
            UI_PowerShort(v, p);
            GUI_DisplaySmallest(p, 2, (uint8_t)((line + 1u) * 8u + 1u), false, true);
            if (sel)   // being selected: inverted
                for (uint8_t x = 1; x < 18u; x++)
                    gFrameBuffer[line + 1][x] ^= 0x7F;
        }
        char q[4];   // 4: the squelch
        UI_SqlText(vfo_num, q);
        GUI_DisplaySmallest(q, 19, (uint8_t)((line + 1u) * 8u + 1u), false, true);
        if (UI_SqlSelected(vfo_num))
            for (uint8_t x = 18; x < 27u; x++)
                gFrameBuffer[line + 1][x] ^= 0x7F;
        if (gScanStateDir != SCAN_OFF && vfo_num == gEeprom.TX_VFO && !FUNCTION_IsRx())
            GUI_DisplaySmallest("SCN", 11, (uint8_t)(line * 8u + 1u), false, true);   // 2: scanning
    }

    // TX / RX, at the label's left (RX: its signal bars, DisplayRSSIBar)
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        if (activeTxVFO == vfo_num) {
            mode = VFO_MODE_TX;
            f    = v->pTX->Frequency;
            if (!full)   // (full screen: in the status bar, by the mode icon)
                UI_PrintStringSmallBold("TX", 10, 0, line);
        }
    } else {
        mode = VFO_MODE_RX;
        if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num && VfoState[vfo_num] == VFO_STATE_NORMAL) {
            RxBlinkLed        = 1;
            RxBlinkLedCounter = 0;
            RxLine            = (int8_t)line;
            RxOnVfofrequency  = f;
            RxBlink           = vfo_num != gEeprom.TX_VFO;
        } else if (RxBlinkLed == 1) {
            RxBlinkLed = 2;
        }
    }

    // the plan channel it shows: a channel, or the one a favorite is on
    uint16_t plan = IS_SVC_CHANNEL(ch) ? ch : 0xFFFF;
    gFavOffPlan   = false;
    if (IS_MR_CHANNEL(ch)) {
        const uint8_t m = SERVICE_ModeOf(vfo_num);
        if (m != SERVICE_HAM && m < SERVICE_COUNT) {
            plan = SERVICE_PlanChannelOf(v);
            if (plan == 0xFFFF) {   // off the plan's channels: shown by its frequency
                plan        = SERVICE_ChannelFor(vfo_num, (Service_t)m);
                gFavOffPlan = true;
            }
        }
    }

    // the label: the channel's name where its plan names them, else the frequency;
    // a frequency being typed in
    label[0] = 0;
    if (gInputBoxIndex > 0 && IS_FREQ_CHANNEL(ch) && gEeprom.TX_VFO == vfo_num) {
        const char *ascii = INPUTBOX_GetAscii();
        const bool  giga  = f >= _1GHz_in_KHz;
        sprintf(label, "%.*s.%.3s", 3 + giga, ascii, ascii + 3 + giga);
    } else if (plan != 0xFFFF && !gFavOffPlan && SERVICE_ChannelLabel(plan, label)) {
        uint8_t last = 0;   // "15R": the letters after the number one size down (bottom row)
        for (uint8_t i = 0; label[i]; i++)
            if (label[i] >= '0' && label[i] <= '9')
                last = i;
        strcpy(dec45, label + last + 1u);
        label[last + 1u] = 0;
    } else {
        // A frequency: MHz and the first decimal big, then the rest small and stacked,
        // decimals 2-3 on top, 4-5 under them, as many as it needs: a VFO what its step
        // resolves (steady while tuning), else at least kHz (broadcast FM: 100 kHz).
        // Air: the channel's name (8.33 kHz: 118.005 for 118.0000, .010, .015).
        uint8_t dec = UI_Decimals(f);
        if (SERVICE_ModeOf(vfo_num) == SERVICE_AIR) {
            const uint32_t off = f % 2500u;   // in its 25 kHz block
            const uint8_t  k   = (uint8_t)((off + 416u) / 833u);
            if (k || v->StepFrequency == 833u)
                shown = f - off + (k + 1u) * 500u;
            dec = 3;
        } else if (IS_FREQ_CHANNEL(ch)) {
            const uint8_t ds = UI_Decimals(v->StepFrequency);
            dec = ds > dec ? ds : dec;
        } else if (v->Modulation != MODULATION_BFM && dec < 3u) {
            dec = 3;
        }
        sprintf(label, "%u.%05u", shown / 100000u, shown % 100000u);
        char *d = label + strlen(label) - 4u;   // the decimals after the first
        d[dec - 1u] = 0;                         // as many as it shows
        memcpy(dec23, d, 2);
        if (dec > 3u)
            strcpy(dec45, d + 2);
        *d = 0;
    }

    // the tag and the code: a channel's / favorite's (its name), a memory's name
    if (plan != 0xFFFF) {
        char name[17];
        if (IS_MR_CHANNEL(ch)) {
            SETTINGS_FetchChannelName(name, ch);
            gFavName = name;
        }
        UI_ServiceTexts(v, plan, code, &kind, tag);
        gFavName = NULL;
    } else if (IS_MR_CHANNEL(ch)) {
        SETTINGS_FetchChannelName(tag, ch);
    }

    // the stacked part's width: a frequency's always two digits (the label doesn't move)
    const char *small = dec23[0] ? "00" : dec45;

    // double size where it all fits (full screen; a VFO's label there has the whole width)
    // (from x 2: the last digit's trailing gap past the edge; a code after the label
    // ends in its own gap)
    const bool    wide = full;
    const uint8_t bx   = wide ? 2u : BLOCK_X0, bw = wide ? LCD_WIDTH : LCD_WIDTH - BLOCK_X0;
    uint8_t scale = full ? 2u : 1u, w, wl;
    for (;; scale = 1) {
        w = UI_BigWidth(label, scale);
        if (small[0])
            w = (uint8_t)(w + 1u + UI_SmallerWidth(small, scale));
        wl = w;   // the label alone (the tune highlight)
        if (code[0])
            w = (uint8_t)(w + 4u + UI_SmallerWidth(code, scale));
        if (scale == 1 || w <= bw)
            break;
    }
    const uint8_t x0 = (uint8_t)(bx + (w < bw ? (bw - w) / 2u : 0u));

    if (VfoState[vfo_num] != VFO_STATE_NORMAL) {   // BUSY, TX DISABLE, ..
        if (VfoState[vfo_num] < ARRAY_SIZE(VfoStateStr))
            UI_PrintString(VfoStateStr[VfoState[vfo_num]], 31, 0, line, 8);
    } else {
        const uint8_t x = (uint8_t)(x0 + UI_DrawBigText(label, x0, line, scale, true) + 1u);
        const uint8_t sw = small[0] ? (uint8_t)(x0 + wl - x) : 0u;   // the stacked part's width (none: 0)
        UI_DrawSmallerRow(dec23, x, (uint8_t)line, scale);   // (either may be empty)
        UI_NudgeRows(x, sw, (uint8_t)line, 0);               // top: with the digits' top (before
                                                              // the bottom row is drawn under it)
        UI_DrawSmallerRow(dec45, x, (uint8_t)(line + scale), scale);
        UI_NudgeRows(x, sw, (uint8_t)(line + scale), scale);  // bottom: with their bottom (full
                                                              // screen: up; 1: shr 1, unmoved)
        if (code[0]) {   // the privacy code, its CTCSS / DCS label above it
            const uint8_t cx   = (uint8_t)(x0 + wl + 4u);
            const uint8_t page = (uint8_t)(line + (scale == 2 ? 2u : 1u));
            if (kind)
                GUI_DisplaySmallest(kind, cx, (uint8_t)(page * 8u - 6u), false, true);
            UI_DrawSmallerRow(code, cx, (uint8_t)(line + scale), scale);   // bottom-aligned with the label
            if (item == PE_CODE)   // selecting the code: inverted
                for (uint8_t r = page; r < page + scale; r++)
                    for (uint8_t i = cx - 1u; i < cx + UI_SmallerWidth(code, scale) && i < LCD_WIDTH; i++)
                        gFrameBuffer[r][i] ^= scale == 2 ? 0xFF : 0x7F;
        }
        if (item == PE_TUNE)   // tuning: the label inverted
            UI_TuneMark(line, (uint8_t)(2u * scale), x0 - 2u, (uint8_t)(x0 + wl + 2u < LCD_WIDTH ? x0 + wl + 2u : LCD_WIDTH));
    }
    if (item == PE_BAND && !full)   // the band: the mode icon (with its digit) inverted
        UI_TuneMark(line, 1, 1, 10);
#ifdef ENABLE_FMRADIO
    // broadcast FM playing: its signal on the meter (A / B view by the icon, single
    // view in the status bar); full screen also dBuV, SNR and stereo at the bottom
    uint8_t rssi, snr;
    bool    stereo;
    const bool fmLive = v->Modulation == MODULATION_BFM && vfo_num == gEeprom.TX_VFO && FM_Signal(&rssi, &snr, &stereo);
    gFmStereoRx = fmLive && stereo;   // (the info row's stereo field)
    if (fmLive) {
        const uint8_t bars = rssi >= 60u ? 6u : (uint8_t)(rssi / 10u);
        if (!full)
            UI_DrawSignalBars(gFrameBuffer[line] + 10, bars);
        else if (bars != gRssiBars) {
            gRssiBars     = bars;
            gUpdateStatus = true;
        }
    }
#endif

    // under the label
    char          heard[20];
    const bool    decoded = UI_DecodedText(v, heard);   // live DTMF, a heard code
    const bool    info    = item >= PE_MODE && item <= PE_STEREO && item != PE_POWER && item != PE_SQUELCH;   // (their own spots)
    uint8_t       row     = (uint8_t)(line + 2u * scale);
    if (!full) {
        if (decoded)
            UI_PrintStringSmallNormal(heard, 2, 0, row);
        else if (item == PE_NAME) {
            UI_DrawNameEdit(BLOCK_X0, row);
            UI_DrawMemNo(vfo_num, row);
        }
        else if (tag[0] && !info) {
            UI_PrintStringSmallNormal(tag, BLOCK_X0, LCD_WIDTH, row);
            UI_DrawMemNo(vfo_num, row);
        }
        else
            UI_DrawInfoRow(vfo_num, row, mode);
        return;
    }
    if (item == PE_NAME)
        UI_DrawNameEdit(2, row++);
    else if (tag[0])
        UI_PrintStringSmallNormal(tag, 0, LCD_WIDTH, row++);
    UI_DrawInfoRow(vfo_num, row, mode);
    if (item != PE_NONE && row < 6u) {   // what is being selected (SubCod: its value, inverted)
        const char *nm = PARAM_Name(item);
        UI_PrintStringSmallNormal(nm, 0, LCD_WIDTH, row + 1u);
        const uint8_t c = PARAM_Cursor();
        if (item == PE_SUBCODE || item == PE_COMPAND || (item == PE_GROUPS && c == PARAM_CURSOR_NONE))
            UI_TuneMark(row + 1u, 1, 0, LCD_WIDTH);
        else if (item == PE_GROUPS) {   // the flag at the cursor ("GROUPS 1 2 3 C", centred)
            const uint8_t x = (uint8_t)((LCD_WIDTH - strlen(nm) * 7u + 1u) / 2u + (7u + 2u * c) * 7u);
            UI_TuneMark(row + 1u, 1, x - 1u, x + 6u);
        }
    }
    if (decoded)
        UI_PrintStringSmallNormal(heard, 2, 0, 6);
#ifdef ENABLE_FMRADIO
    if (fmLive) {   // full screen: the volume, the signal
        sprintf(heard, "VOL %u  %udBuV SNR %u", SERVICE_FmVolume(), rssi, snr);
        UI_PrintStringSmallNormal(heard, 0, LCD_WIDTH, 6);
    }
#endif
    if (shown != f && !decoded) {   // an 8.33 kHz air channel's name: its frequency too
        sprintf(heard, "%u.%05u", f / 100000u, f % 100000u);   // (the label's format: one string)
        UI_PrintStringSmallNormal(heard, 0, LCD_WIDTH, 6);
    }
}
#endif

void UI_DisplayMain(void)
{
    char               String[22];

    center_line = CENTER_LINE_NONE;

#ifdef ENABLE_FEAT_SERVICE_MODES
    {
        static bool wasRx;
        if (FUNCTION_IsRx() != wasRx) {   // the status bar's signal bars come and go
            wasRx         = !wasRx;
            gRssiBars     = 0;
            gUpdateStatus = true;
        }
    }
#endif

    // clear the screen
    UI_DisplayClear();

    if(gLowBattery && !gLowBatteryConfirmed) {
        UI_DisplayPopup("LOW BATTERY");
        ST7565_BlitFullScreen();
        return;
    }

#ifndef ENABLE_FEAT_F4HWN
    if (gEeprom.KEY_LOCK && gKeypadLocked > 0)
    {   // tell user how to unlock the keyboard
        UI_PrintString("Long press #", 0, LCD_WIDTH, 1, 8);
        UI_PrintString("to unlock",    0, LCD_WIDTH, 3, 8);
        ST7565_BlitFullScreen();
        return;
    }
#else
    if (gEeprom.KEY_LOCK && gKeypadLocked > 0)
    {   // tell user how to unlock the keyboard
        uint8_t shift = 3;

        /*
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
        SYSTEM_DelayMs(50);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        SYSTEM_DelayMs(50);
        */

        if(isMainOnly())
        {
            shift = 5;
        }
        //memcpy(gFrameBuffer[shift] + 2, gFontKeyLock, sizeof(gFontKeyLock));
        UI_PrintStringSmallBold("UNLOCK KEYBOARD", 12, 0, shift);
        //memcpy(gFrameBuffer[shift] + 120, gFontKeyLock, sizeof(gFontKeyLock));

        /*
        for (uint8_t i = 12; i < 116; i++)
        {
            gFrameBuffer[shift][i] ^= 0xFF;
        }
        */
    }
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
    // the selected VFO; the other one only while it is receiving (in single view it
    // listens on the other VFO when the selected one plays broadcast FM)
    unsigned int activeTxVFO = FUNCTION_IsRx() ? gEeprom.RX_VFO : gEeprom.TX_VFO;
#else
    unsigned int activeTxVFO = gRxVfoIsActive ? gEeprom.RX_VFO : gEeprom.TX_VFO;
#endif

    for (unsigned int vfo_num = 0; vfo_num < 2; vfo_num++)
    {
#ifdef ENABLE_FEAT_F4HWN
        const unsigned int line0 = 0;  // text screen line
        const unsigned int line1 = 4;
        unsigned int line;
        if (isMainOnly())
        {
            line       = 0;
        }
        else
        {
            line       = (vfo_num == 0) ? line0 : line1;
        }
        const bool         isMainVFO  = (vfo_num == gEeprom.TX_VFO);
        uint8_t           *p_line0    = gFrameBuffer[line + 0];
        uint8_t           *p_line1    = gFrameBuffer[line + 1];
        enum Vfo_txtr_mode mode       = VFO_MODE_NONE;      
#else
        const unsigned int line0 = 0;  // text screen line
        const unsigned int line1 = 4;
        const unsigned int line       = (vfo_num == 0) ? line0 : line1;
        const bool         isMainVFO  = (vfo_num == gEeprom.TX_VFO);
        uint8_t           *p_line0    = gFrameBuffer[line + 0];
        uint8_t           *p_line1    = gFrameBuffer[line + 1];
        enum Vfo_txtr_mode mode       = VFO_MODE_NONE;
#endif

#ifdef ENABLE_FEAT_F4HWN
    if (isMainOnly())
    {
        if (activeTxVFO != vfo_num)
        {
            continue;
        }
    }
#endif

#ifndef ENABLE_FEAT_SERVICE_MODES   // (services: no scan ranges, DTMF in the text row, no markers)
#ifdef ENABLE_FEAT_F4HWN
        if (activeTxVFO != vfo_num || isMainOnly())
#else
        if (activeTxVFO != vfo_num) // this is not active TX VFO
#endif
        {
#ifdef ENABLE_SCAN_RANGES
            if(gScanRangeStart) {

#ifdef ENABLE_FEAT_F4HWN
                //if(IS_FREQ_CHANNEL(gEeprom.ScreenChannel[0]) && IS_FREQ_CHANNEL(gEeprom.ScreenChannel[1])) {
                if(IS_FREQ_CHANNEL(gEeprom.ScreenChannel[activeTxVFO])) {

                    uint8_t shift = 0;

                    if (isMainOnly())
                    {
                        shift = 3;
                    }

                    UI_PrintString("ScnRng", 5, 0, line + shift, 8);
                    sprintf(String, "%3u.%05u", gScanRangeStart / 100000, gScanRangeStart % 100000);
                    UI_PrintStringSmallNormal(String, 56, 0, line + shift);
                    sprintf(String, "%3u.%05u", gScanRangeStop / 100000, gScanRangeStop % 100000);
                    UI_PrintStringSmallNormal(String, 56, 0, line + shift + 1);

                    if (!isMainOnly())
                        continue;
                }
                else
                {
                    gScanRangeStart = 0;
                }
#else
                UI_PrintString("ScnRng", 5, 0, line, 8);
                sprintf(String, "%3u.%05u", gScanRangeStart / 100000, gScanRangeStart % 100000);
                UI_PrintStringSmallNormal(String, 56, 0, line);
                sprintf(String, "%3u.%05u", gScanRangeStop / 100000, gScanRangeStop % 100000);
                UI_PrintStringSmallNormal(String, 56, 0, line + 1);
                continue;
#endif
            }
#endif


#ifndef ENABLE_FEAT_SERVICE_MODES   // (services: in the VFO's text row, UI_DecodedText)
            if (gDTMF_InputMode
#ifdef ENABLE_DTMF_CALLING
                || gDTMF_CallState != DTMF_CALL_STATE_NONE || gDTMF_IsTx
#endif
            ) {
                char *pPrintStr = "";
                // show DTMF stuff
#ifdef ENABLE_DTMF_CALLING
                char Contact[16];
                if (!gDTMF_InputMode) {
                    if (gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT) {
                        pPrintStr = DTMF_FindContact(gDTMF_String, Contact) ? Contact : gDTMF_String;
                    } else if (gDTMF_CallState == DTMF_CALL_STATE_RECEIVED || gDTMF_CallState == DTMF_CALL_STATE_RECEIVED_STAY){
                        pPrintStr = DTMF_FindContact(gDTMF_Callee, Contact) ? Contact : gDTMF_Callee;
                    }else if (gDTMF_IsTx) {
                        pPrintStr = gDTMF_String;
                    }
                }

                UI_PrintString(pPrintStr, 2, 0, 2 + (vfo_num * 3), 8);

                pPrintStr = "";
                if (!gDTMF_InputMode) {
                    if (gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT) {
                        pPrintStr = (gDTMF_State == DTMF_STATE_CALL_OUT_RSP) ? "CALL OUT(RSP)" : "CALL OUT";
                    } else if (gDTMF_CallState == DTMF_CALL_STATE_RECEIVED || gDTMF_CallState == DTMF_CALL_STATE_RECEIVED_STAY) {
                        sprintf(String, "CALL FRM:%s", (DTMF_FindContact(gDTMF_Caller, Contact)) ? Contact : gDTMF_Caller);
                        pPrintStr = String;
                    } else if (gDTMF_IsTx) {
                        pPrintStr = (gDTMF_State == DTMF_STATE_TX_SUCC) ? "DTMF TX(SUCC)" : "DTMF TX";
                    }
                }
                else
#endif
                {
                    sprintf(String, ">%s", gDTMF_InputBox);
                    pPrintStr = String;
                }

#ifdef ENABLE_FEAT_F4HWN
                if (isMainOnly())
                {
#ifdef ENABLE_FEAT_SERVICE_MODES
                    UI_PrintStringSmallNormal(pPrintStr, 2, 0, 6);
#else
                    UI_PrintString(pPrintStr, 2, 0, 5, 8);
#endif
                    isMainOnlyInputDTMF = true;
                    center_line = CENTER_LINE_IN_USE;
                }
                else
                {
                    UI_PrintString(pPrintStr, 2, 0, 0 + (vfo_num * 3), 8);
                    isMainOnlyInputDTMF = false;
                    center_line = CENTER_LINE_IN_USE;
                    continue;
                }
#else
                UI_PrintString(pPrintStr, 2, 0, 0 + (vfo_num * 3), 8);
                center_line = CENTER_LINE_IN_USE;
                continue;
#endif
            }
#endif

            // highlight the selected/used VFO with a marker
            if (isMainVFO)
                memcpy(p_line0 + 0, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
        }
        else // active TX VFO
        {   // highlight the selected/used VFO with a marker
            if (isMainVFO)
                memcpy(p_line0 + 0, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
            else
                memcpy(p_line0 + 0, BITMAP_VFO_NotDefault, sizeof(BITMAP_VFO_NotDefault));
        }
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
        memset(p_line0, 0, 8);   // no arrow: a border marks the selected VFO (A / B view)
        if (!isMainOnly())       // A / B view: each VFO shows its mode icon
            UI_DrawModeIcon(p_line0 + 2, vfo_num);
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
        UI_DrawBlock(vfo_num, line, activeTxVFO);
        (void)isMainVFO;
        (void)p_line1;
        (void)mode;
#else
        uint32_t frequency = gEeprom.VfoInfo[vfo_num].pRX->Frequency;

        if(TX_freq_check(frequency) != 0 && gEeprom.VfoInfo[vfo_num].TX_LOCK == true)
        {
            if(isMainOnly())
                memcpy(p_line0 + 14, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
            else
                memcpy(p_line0 + 24, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
        }

        if (gCurrentFunction == FUNCTION_TRANSMIT)
        {   // transmitting

#ifdef ENABLE_ALARM
            if (gAlarmState == ALARM_STATE_SITE_ALARM)
                mode = VFO_MODE_RX;
            else
#endif
            {
                if (activeTxVFO == vfo_num)
                {   // show the TX symbol
                    mode = VFO_MODE_TX;
                    UI_PrintStringSmallBold("TX", 10, 0, line);
                }
            }
        }
        else
        {   // receiving .. show the RX symbol
            mode = VFO_MODE_RX;
            //if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num) {
            if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num && VfoState[vfo_num] == VFO_STATE_NORMAL) {
#ifdef ENABLE_FEAT_F4HWN
                RxBlinkLed = 1;
                RxBlinkLedCounter = 0;
                RxLine = line;
                RxOnVfofrequency = frequency;
                if(!isMainVFO)
                {
                    RxBlink = 1;
                }
                else
                {
                    RxBlink = 0;
                }
#else
                UI_PrintStringSmallBold("RX", 8, 0, line);
#endif
            }
#ifdef ENABLE_FEAT_F4HWN
            else
            {
                if(RxOnVfofrequency == frequency && !isMainOnly())
                {
                    UI_PrintStringSmallNormal(">>", 10, 0, line);
                    //memcpy(p_line0 + 14, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
                }

                if(RxBlinkLed == 1)
                    RxBlinkLed = 2;
            }
#endif
        }

        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
        {   // channel mode
            const unsigned int x = 2;
            const bool inputting = gInputBoxIndex != 0 && gEeprom.TX_VFO == vfo_num;
            if (!inputting)
                sprintf(String, "M%u", gEeprom.ScreenChannel[vfo_num] + 1);
            else
                sprintf(String, "M%.3s", INPUTBOX_GetAscii());  // show the input text
            UI_PrintStringSmallNormal(String, x, 0, line + 1);
        }
        else if (IS_FREQ_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
        {   // frequency mode
            // show the frequency band number
            const unsigned int x = 2;
            char * buf = gEeprom.VfoInfo[vfo_num].pRX->Frequency < _1GHz_in_KHz ? "" : "+";
            sprintf(String, "F%u%s", 1 + gEeprom.ScreenChannel[vfo_num] - FREQ_CHANNEL_FIRST, buf);
            UI_PrintStringSmallNormal(String, x, 0, line + 1);
        }
#ifdef ENABLE_NOAA
        else
        {
            if (gInputBoxIndex == 0 || gEeprom.TX_VFO != vfo_num)
            {   // channel number
                sprintf(String, "N%u", 1 + gEeprom.ScreenChannel[vfo_num] - NOAA_CHANNEL_FIRST);
            }
            else
            {   // user entering channel number
                sprintf(String, "N%u%u", '0' + gInputBox[0], '0' + gInputBox[1]);
            }
            UI_PrintStringSmallNormal(String, 7, 0, line + 1);
        }
#endif

        // ************

        enum VfoState_t state = VfoState[vfo_num];

#ifdef ENABLE_ALARM
        if (gCurrentFunction == FUNCTION_TRANSMIT && gAlarmState == ALARM_STATE_SITE_ALARM) {
            if (activeTxVFO == vfo_num)
                state = VFO_STATE_ALARM;
        }
#endif
        if (state != VFO_STATE_NORMAL)
        {
            if (state < ARRAY_SIZE(VfoStateStr))
                UI_PrintString(VfoStateStr[state], 31, 0, line, 8);
        }
        else if (gInputBoxIndex > 0 && IS_FREQ_CHANNEL(gEeprom.ScreenChannel[vfo_num]) && gEeprom.TX_VFO == vfo_num)
        {   // user entering a frequency
            const char * ascii = INPUTBOX_GetAscii();
            bool isGigaF = frequency>=_1GHz_in_KHz;
            sprintf(String, "%.*s.%.3s", 3 + isGigaF, ascii, ascii + 3 + isGigaF);
#ifdef ENABLE_BIG_FREQ
            if(!isGigaF) {
                // show the remaining 2 small frequency digits
                UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                String[7] = 0;
                // show the main large frequency digits
                UI_DisplayFrequency(String, 32, line, false);
            }
            else
#endif
            {
                // show the frequency in the main font
                UI_PrintString(String, 32, 0, line, 8);
            }

            continue;
        }
        else
        {
            if (gCurrentFunction == FUNCTION_TRANSMIT)
            {   // transmitting
                if (activeTxVFO == vfo_num)
                    frequency = gEeprom.VfoInfo[vfo_num].pTX->Frequency;
            }

            if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num]))
            {   // it's a channel

                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                    if(gEeprom.MENU_LOCK == false) {
                #endif
                uint8_t countList = 0;
                uint8_t shiftList = 0;

                if(gMR_ChannelExclude[gEeprom.ScreenChannel[vfo_num]] == false)
                {
                    // show the scan list assigment symbols
                    const ChannelAttributes_t att = gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];

                    countList = att.scanlist1 + att.scanlist2 + att.scanlist3;

                    if(countList == 0)
                    {
                        memcpy(p_line0 + 125 - (1 * 6), BITMAP_ScanList0, sizeof(BITMAP_ScanList0));
                    }
                    else
                    {
                        shiftList = countList;

                        if (att.scanlist1)
                        {
                            memcpy(p_line0 + 125 - (shiftList * 6), BITMAP_ScanList1, sizeof(BITMAP_ScanList1));
                            shiftList--;
                        }
                        if (att.scanlist2)
                        {
                            memcpy(p_line0 + 125 - (shiftList * 6), BITMAP_ScanList2, sizeof(BITMAP_ScanList2));
                            shiftList--;
                        }
                        if (att.scanlist3)
                        {
                            memcpy(p_line0 + 125 - (shiftList * 6), BITMAP_ScanList3, sizeof(BITMAP_ScanList3));
                        }
                    }
                }
                else
                {
                    memcpy(p_line0 + 125 - (1 * 6), BITMAP_ScanListE, sizeof(BITMAP_ScanListE));
                }

                #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                {
                    }
                }
                #endif

                // compander symbol
#ifndef ENABLE_BIG_FREQ
                if (att.compander)
                    memcpy(p_line0 + 120 + LCD_WIDTH, BITMAP_compand, sizeof(BITMAP_compand));
#else
                // TODO:  // find somewhere else to put the symbol
#endif

                switch (gEeprom.CHANNEL_DISPLAY_MODE)
                {
                    case MDF_FREQUENCY: // show the channel frequency
                        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
#ifdef ENABLE_BIG_FREQ
                        if(frequency < _1GHz_in_KHz) {
                            // show the remaining 2 small frequency digits
                            UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                            String[7] = 0;
                            // show the main large frequency digits
                            UI_DisplayFrequency(String, 32, line, false);
                        }
                        else
#endif
                        {
                            // show the frequency in the main font
                            UI_PrintString(String, 32, 0, line, 8);
                        }

                        break;

                    case MDF_CHANNEL:   // show the channel number
                        sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
                        UI_PrintString(String, 32, 0, line, 8);
                        break;

                    case MDF_NAME:      // show the channel name
                    case MDF_NAME_FREQ: // show the channel name and frequency

                        SETTINGS_FetchChannelName(String, gEeprom.ScreenChannel[vfo_num]);
                        if (String[0] == 0)
                        {   // no channel name, show the channel number instead
                            sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
                        }

                        if (gEeprom.CHANNEL_DISPLAY_MODE == MDF_NAME) {
                            UI_PrintString(String, 32, 0, line, 8);
                        }
                        else {
#ifdef ENABLE_FEAT_F4HWN
                            if (isMainOnly())
                            {
                                UI_PrintString(String, 32, 0, line, 8);
                            }
                            else
                            {
                                if(activeTxVFO == vfo_num) {
                                    UI_PrintStringSmallBold(String, 32 + 4, 0, line);
                                }
                                else
                                {
                                    UI_PrintStringSmallNormal(String, 32 + 4, 0, line);     
                                }
                            }
#else
                            UI_PrintStringSmallBold(String, 32 + 4, 0, line);
#endif

#ifdef ENABLE_FEAT_F4HWN
                            if (isMainOnly())
                            {
                                sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
                                if(frequency < _1GHz_in_KHz) {
                                    // show the remaining 2 small frequency digits
                                    if (strlen(String) > 7)
                                        UI_PrintStringSmallNormal(String + 7, 113, 0, line + 4);
                                    String[7] = 0;
                                    // show the main large frequency digits
                                    UI_DisplayFrequency(String, 32, line + 3, false);
                                }
                                else
                                {
                                    // show the frequency in the main font
                                    UI_PrintString(String, 32, 0, line + 3, 8);
                                }
                            }
                            else
                            {
                                sprintf(String, "%03u.%05u", frequency / 100000, frequency % 100000);
                                UI_PrintStringSmallNormal(String, 32 + 4, 0, line + 1);
                            }
#else                           // show the channel frequency below the channel number/name
                            sprintf(String, "%03u.%05u", frequency / 100000, frequency % 100000);
                            UI_PrintStringSmallNormal(String, 32 + 4, 0, line + 1);
#endif
                        }

                        break;
                }
            }
            else
            {   // frequency mode
#ifdef ENABLE_FMRADIO
                if (gEeprom.VfoInfo[vfo_num].Modulation == MODULATION_BFM)   // broadcast FM: "98.1"
                    sprintf(String, "%3u.%u", frequency / 100000, frequency / 10000 % 10);
                else
#endif
                sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);

#ifdef ENABLE_BIG_FREQ
                if(frequency < _1GHz_in_KHz) {
                    if (strlen(String) > 7) {
                        // show the remaining 2 small frequency digits
                        UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
                        String[7] = 0;
                    }
                    // show the main large frequency digits
                    UI_DisplayFrequency(String, 32, line, false);
                }
                else
#endif
                {
                    // show the frequency in the main font
                    UI_PrintString(String, 32, 0, line, 8);
                }

                // show the channel symbols (attributes only exist up to the VFO channels;
                // built-in service channels have none)
                const uint16_t ch = gEeprom.ScreenChannel[vfo_num];
                if (ch <= FREQ_CHANNEL_LAST && gMR_ChannelAttributes[ch].compander)
#ifdef ENABLE_BIG_FREQ
                    memcpy(p_line0 + 120, BITMAP_compand, sizeof(BITMAP_compand));
#else
                    memcpy(p_line0 + 120 + LCD_WIDTH, BITMAP_compand, sizeof(BITMAP_compand));
#endif
            }
        }

        // ************

        {   // show the TX/RX level
            int8_t Level = -1;

            if (mode == VFO_MODE_TX)
            {   // TX power level
                /*
                switch (gRxVfo->OUTPUT_POWER)
                {
                    case OUTPUT_POWER_LOW1:     Level = 2; break;
                    case OUTPUT_POWER_LOW2:     Level = 2; break;
                    case OUTPUT_POWER_LOW3:     Level = 2; break;
                    case OUTPUT_POWER_LOW4:     Level = 2; break;
                    case OUTPUT_POWER_LOW5:     Level = 2; break;
                    case OUTPUT_POWER_MID:      Level = 4; break;
                    case OUTPUT_POWER_HIGH:     Level = 6; break;
                }

                if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_MID) {
                    Level = 4;
                } else if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_HIGH) {
                    Level = 6;
                } else {
                    Level = 2;
                }
                */
                Level = gRxVfo->OUTPUT_POWER - 1;
            }
            else
            if (mode == VFO_MODE_RX)
            {   // RX signal level
                #ifndef ENABLE_RSSI_BAR
                    // bar graph
                    if (gVFO_RSSI_bar_level[vfo_num] > 0)
                        Level = gVFO_RSSI_bar_level[vfo_num];
                #endif
            }
            if(Level >= 0)
                DrawSmallAntennaAndBars(p_line1 + LCD_WIDTH, Level);
        }

        // ************


        String[0] = '\0';
        const VFO_Info_t *vfoInfo = &gEeprom.VfoInfo[vfo_num];

        // show the modulation symbol
        const char * s = "";
#ifdef ENABLE_FEAT_F4HWN
        const char * t = "";
#endif
        const ModulationMode_t mod = vfoInfo->Modulation;
        switch (mod){
            case MODULATION_FM: {
                const FREQ_Config_t *pConfig = (mode == VFO_MODE_TX) ? vfoInfo->pTX : vfoInfo->pRX;
                const unsigned int code_type = pConfig->CodeType;
#ifdef ENABLE_FEAT_F4HWN
                const char *code_list[] = {"", "CT", "DC", "DC"};
#else
                const char *code_list[] = {"", "CT", "DCS", "DCR"};
#endif
                if (code_type < ARRAY_SIZE(code_list))
                    s = code_list[code_type];
#ifdef ENABLE_FEAT_F4HWN
                if(gCurrentFunction != FUNCTION_TRANSMIT || activeTxVFO != vfo_num)
                    t = gModulationStr[mod];
#endif
                break;
            }
            default:
                t = gModulationStr[mod];
            break;
        }

#if ENABLE_FEAT_F4HWN
        const FREQ_Config_t *pConfig = (mode == VFO_MODE_TX) ? vfoInfo->pTX : vfoInfo->pRX;
        int8_t shift = 0;

        switch((int)pConfig->CodeType)
        {
            case 1:
            sprintf(String, "%u.%u", CTCSS_Options[pConfig->Code] / 10, CTCSS_Options[pConfig->Code] % 10);
            break;

            case 2:
            sprintf(String, "%03oN", DCS_Options[pConfig->Code]);
            break;

            case 3:
            sprintf(String, "%03oI", DCS_Options[pConfig->Code]);
            break;

            default:
            sprintf(String, "%d.%02uK", vfoInfo->StepFrequency / 100, vfoInfo->StepFrequency % 100);
            shift = -10;
        }


        if (gSetting_set_gui)
        {
            UI_PrintStringSmallNormal(s, LCD_WIDTH + 22, 0, line + 1);
            UI_PrintStringSmallNormal(t, LCD_WIDTH + 2, 0, line + 1);

            if (isMainOnly() && !gDTMF_InputMode)
            {
                if(shift == 0)
                {
                    UI_PrintStringSmallNormal(String, 2, 0, 6);
                }

                if((vfoInfo->StepFrequency / 100) < 100)
                {
                    sprintf(String, "%d.%02uK", vfoInfo->StepFrequency / 100, vfoInfo->StepFrequency % 100);
                }
                else
                {
                    sprintf(String, "%dK", vfoInfo->StepFrequency / 100);               
                }
                UI_PrintStringSmallNormal(String, 46, 0, 6);
            }
        }
        else
        {
            if ((s != NULL) && (s[0] != '\0')) {
                GUI_DisplaySmallest(s, IR_TYPE_X, line == 0 ? 17 : 49, false, true);
            }

            if ((t != NULL) && (t[0] != '\0')) {
                GUI_DisplaySmallest(t, 3, line == 0 ? 17 : 49, false, true);
            }

            GUI_DisplaySmallest(String, IR_TONE_X + shift, line == 0 ? 17 : 49, false, true);

            //sprintf(String, "%d.%02u", vfoInfo->StepFrequency / 100, vfoInfo->StepFrequency % 100);
            //GUI_DisplaySmallest(String, 91, line == 0 ? 2 : 34, false, true);
        }
#else
        UI_PrintStringSmallNormal(s, LCD_WIDTH + 24, 0, line + 1);
#endif

        if ((state == VFO_STATE_NORMAL || state == VFO_STATE_ALARM)
        )
        {   // show the TX power
            uint8_t currentPower = vfoInfo->OUTPUT_POWER % 8;
            uint8_t arrowPos = 19;
            bool userPower = false;

            if(currentPower == OUTPUT_POWER_USER)
            {
                currentPower = gSetting_set_pwr;
                userPower = true;
            }
            else
            {
                currentPower--;
                userPower = false;
            }

            if (gSetting_set_gui)
            {
                const char pwr_short[][3] = {"L1", "L2", "L3", "L4", "L5", "M", "H"};
                //sprintf(String, "%s", pwr_short[currentPower]);
                //UI_PrintStringSmallNormal(String, LCD_WIDTH + 42, 0, line + 1);
                UI_PrintStringSmallNormal(pwr_short[currentPower], LCD_WIDTH + 42, 0, line + 1);

                arrowPos = 38;
            }
            else
            {
                const char pwr_long[][5] = {"LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH"};
                //sprintf(String, "%s", pwr_long[currentPower]);
                //GUI_DisplaySmallest(String, 24, line == 0 ? 17 : 49, false, true);
                GUI_DisplaySmallest(pwr_long[currentPower], 24, line == 0 ? 17 : 49, false, true);
            }

            if(userPower == true)
            {
                memcpy(p_line0 + 256 + arrowPos, BITMAP_PowerUser, sizeof(BITMAP_PowerUser));
            }
        }

        if (vfoInfo->freq_config_RX.Frequency != vfoInfo->freq_config_TX.Frequency)
        {   // show the TX offset symbol
            int i = vfoInfo->TX_OFFSET_FREQUENCY_DIRECTION % 3;

            #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                const char dir_list[][2] = {"", "+", "-", "D"};

                if(gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION != 0 && gTxVfo->pTX == &gTxVfo->freq_config_RX && !vfoInfo->FrequencyReverse)
                {
                    i = 3;
                }
            #else
                const char dir_list[][2] = {"", "+", "-"};
            #endif

#if ENABLE_FEAT_F4HWN
        if (gSetting_set_gui)
        {
            UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + 60, 0, line + 1);
        }
        else
        {
            #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
            if(i == 3)
            {
                GUI_DisplaySmallest(dir_list[i], 43, line == 0 ? 17 : 49, false, true);
            }
            else
            {
            #endif
            UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + IR_SHIFT_X, 0, line + 1);
            #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
            }
            #endif
        }
#else
            UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + 54, 0, line + 1);
#endif
        }

        // show the TX/RX reverse symbol
        if (vfoInfo->FrequencyReverse)
#if ENABLE_FEAT_F4HWN
        {
            if (gSetting_set_gui)
            {
                UI_PrintStringSmallNormal("R", LCD_WIDTH + 68, 0, line + 1);
            }
            else
            {
                GUI_DisplaySmallest("R", IR_REV_X, line == 0 ? 17 : 49, false, true);
            }
        }
#else
            UI_PrintStringSmallNormal("R", LCD_WIDTH + 62, 0, line + 1);
#endif

#if ENABLE_FEAT_F4HWN
        #ifdef ENABLE_FEAT_F4HWN_NARROWER
            bool narrower = 0;

            if(vfoInfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW && gSetting_set_nfm == 1)
            {
                narrower = 1;
            }

            if (gSetting_set_gui)
            {
                const char *bandWidthNames[] = {"W", "N", "N+"};
                UI_PrintStringSmallNormal(bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH + narrower], LCD_WIDTH + 80, 0, line + 1);
            }
            else
            {
                const char *bandWidthNames[] = {"WIDE", "NAR", "NAR+"};
                GUI_DisplaySmallest(bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH + narrower], 91, line == 0 ? 17 : 49, false, true);
            }
        #else
            if (gSetting_set_gui)
            {
                const char *bandWidthNames[] = {"W", "N"};
                UI_PrintStringSmallNormal(bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH], LCD_WIDTH + 80, 0, line + 1);
            }
            else
            {
                const char *bandWidthNames[] = {"WIDE", "NAR"};
                GUI_DisplaySmallest(bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH], 91, line == 0 ? 17 : 49, false, true);
            }
        #endif
#else
        if (vfoInfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW)
            UI_PrintStringSmallNormal("N", LCD_WIDTH + 70, 0, line + 1);
#endif

#ifdef ENABLE_DTMF_CALLING
        // show the DTMF decoding symbol
        if (vfoInfo->DTMF_DECODING_ENABLE || gSetting_KILLED)
            UI_PrintStringSmallNormal("DTMF", LCD_WIDTH + 78, 0, line + 1);
#endif

#ifndef ENABLE_FEAT_F4HWN
        // show the audio scramble symbol
        if (vfoInfo->SCRAMBLING_TYPE > 0 && gSetting_ScrambleEnable)
            UI_PrintStringSmallNormal("SCR", LCD_WIDTH + 106, 0, line + 1);
#endif

#ifdef ENABLE_FEAT_F4HWN
        /*
        if(isMainVFO)   
        {
            if(gMonitor)
            {
                sprintf(String, "%s", "MONI");
            }
            
            if (gSetting_set_gui)
            {
                if(!gMonitor)
                {
                    sprintf(String, "SQL%d", gEeprom.SQUELCH_LEVEL);
                }
                UI_PrintStringSmallNormal(String, LCD_WIDTH + 98, 0, line + 1);
            }
            else
            {
                if(!gMonitor)
                {
                    sprintf(String, "SQL%d", gEeprom.SQUELCH_LEVEL);
                }
                GUI_DisplaySmallest(String, 110, line == 0 ? 17 : 49, false, true);
            }
        }
        */
        if (isMainVFO
        ) {
           if (gMonitor) {
                strcpy(String, "MONI");
           } else {
                sprintf(String, "SQL%d", SQUELCH_OF(vfo_num));
           }

           if (gSetting_set_gui) {
                UI_PrintStringSmallNormal(String, LCD_WIDTH + 98, 0, line + 1);
           } else {
                GUI_DisplaySmallest(String, 110, line == 0 ? 17 : 49, false, true);
           }
        }
#endif
#endif
    }

#ifdef ENABLE_AGC_SHOW_DATA
    center_line = CENTER_LINE_IN_USE;
    UI_MAIN_PrintAGC(false);
#endif

#ifdef ENABLE_FEAT_SERVICE_MODES
    // nothing in the middle line: the signal goes to the bars (A / B: the block's,
    // single view: the status bar's)
    center_line = CENTER_LINE_RSSI;
    if (FUNCTION_IsRx())
        DisplayRSSIBar(false);
    (void)String;
#else
    if (center_line == CENTER_LINE_NONE)
    {   // we're free to use the middle line

        const bool rx = FUNCTION_IsRx();

#ifdef ENABLE_AUDIO_BAR
        if (gSetting_mic_bar && gCurrentFunction == FUNCTION_TRANSMIT) {
            center_line = CENTER_LINE_AUDIO_BAR;
            UI_DisplayAudioBar();
        }
        else
#endif

#if defined(ENABLE_AM_FIX) && defined(ENABLE_AM_FIX_SHOW_DATA)
        if (rx && gEeprom.VfoInfo[gEeprom.RX_VFO].Modulation == MODULATION_AM && gSetting_AM_fix)
        {
            if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                )
                return;

            center_line = CENTER_LINE_AM_FIX_DATA;
            AM_fix_print_data(gEeprom.RX_VFO, String);
            UI_PrintStringSmallNormal(String, 2, 0, 3);
        }
        else
#endif

#ifdef ENABLE_RSSI_BAR
        if (rx) {
            center_line = CENTER_LINE_RSSI;
            DisplayRSSIBar(false);
        }
        else
#endif
        if (rx || gCurrentFunction == FUNCTION_FOREGROUND || gCurrentFunction == FUNCTION_POWER_SAVE)
        {
            #if 1
                if (UI_CENTER_DTMF && gSetting_live_DTMF_decoder && gDTMF_RX_live[0] != 0)
                {   // show live DTMF decode
                    const unsigned int len = strlen(gDTMF_RX_live);
                    const unsigned int idx = (len > (17 - 5)) ? len - (17 - 5) : 0;  // limit to last 'n' chars

                    if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                        || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                        )
                        return;

                    center_line = CENTER_LINE_DTMF_DEC;

                    sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
#ifdef ENABLE_FEAT_F4HWN
                    if (isMainOnly())
                    {
                        UI_PrintStringSmallNormal(String, 2, 0, 5);
                    }
                    else
                    {
                        UI_PrintStringSmallNormal(String, 2, 0, 3);
                    }
#else
                    UI_PrintStringSmallNormal(String, 2, 0, 3);

#endif
                }
            #else
                if (gSetting_live_DTMF_decoder && gDTMF_RX_index > 0)
                {   // show live DTMF decode
                    const unsigned int len = gDTMF_RX_index;
                    const unsigned int idx = (len > (17 - 5)) ? len - (17 - 5) : 0;  // limit to last 'n' chars

                    if (gScreenToDisplay != DISPLAY_MAIN ||
                        gDTMF_CallState != DTMF_CALL_STATE_NONE)
                        return;

                    center_line = CENTER_LINE_DTMF_DEC;

                    sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
                    UI_PrintStringSmallNormal(String, 2, 0, 3);
                }
            #endif

#ifdef ENABLE_SHOW_CHARGE_LEVEL
            else if (gChargingWithTypeC)
            {   // charging .. show the battery state
                if (gScreenToDisplay != DISPLAY_MAIN
#ifdef ENABLE_DTMF_CALLING
                    || gDTMF_CallState != DTMF_CALL_STATE_NONE
#endif
                    )
                    return;

                center_line = CENTER_LINE_CHARGE_DATA;

                sprintf(String, "Charge %u.%02uV %u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
                UI_PrintStringSmallNormal(String, 2, 0, 3);
            }
#endif
        }
    }
#endif

#ifdef ENABLE_FEAT_F4HWN
    //#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    //if(gEeprom.MENU_LOCK == false)
    //{
    //#endif
#ifndef ENABLE_FEAT_SERVICE_MODES   // the status bar's A / B indicator already says which VFO
    if (isMainOnly() && !gDTMF_InputMode)
    {
        sprintf(String, "VFO %s", activeTxVFO ? "B" : "A");
        UI_PrintStringSmallBold(String, 92, 0, 6);
        for (uint8_t i = 92; i < 128; i++)
        {
            gFrameBuffer[6][i] ^= 0x7F;
        }
    }
#endif
    //#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    //}
    //#endif
#endif


#ifdef ENABLE_FEAT_SERVICE_MODES
    if (!isMainOnly() && !(gEeprom.KEY_LOCK && gKeypadLocked > 0)) {
        // A / B view: A moves down 3 px and B up 1 px (into the middle row), making
        // room for a thin border round the selected VFO with a pixel of air each side
        for (uint8_t x = 0; x < LCD_WIDTH; x++) {
            const uint32_t a = (gFrameBuffer[0][x] | gFrameBuffer[1][x] << 8 | (uint32_t)gFrameBuffer[2][x] << 16) << 3;
            const uint32_t b = ((gFrameBuffer[3][x] | gFrameBuffer[4][x] << 8 | (uint32_t)gFrameBuffer[5][x] << 16 |
                                 (uint32_t)gFrameBuffer[6][x] << 24) >> 1) | a >> 24;
            for (uint8_t r = 0; r < 4; r++) {
                if (r < 3)
                    gFrameBuffer[r][x] = (uint8_t)(a >> (8u * r));
                gFrameBuffer[3 + r][x] = (uint8_t)(b >> (8u * r));
            }
        }
        // solid: the selected VFO; dashed: the other one while it receives
        UI_DrawBlockBorder(gEeprom.TX_VFO, false);
        if (FUNCTION_IsRx() && gEeprom.RX_VFO != gEeprom.TX_VFO)
            UI_DrawBlockBorder(gEeprom.RX_VFO, true);
    }

    if (gScanStateDir != SCAN_OFF && IS_SVC_CHANNEL(gNextMrChannel) && isMainOnly()) {
        // Plan scan (full screen): one tick per scanned channel along the bottom row, a tall bar
        // on the channel being listened to.
        uint16_t      first;
        const uint8_t count = SERVICE_PlanPosition(gNextMrChannel, &first);
        for (uint8_t i = 0; i < count; i++) {
            if (SERVICE_ScanSkip(first + i))
                continue;
            gFrameBuffer[6][2u + i * 123u / (count - 1u)] |= (first + i == gNextMrChannel) ? 0x7E : 0x60;
        }
    }
#endif

    ST7565_BlitFullScreen();
}

// ***************************************************************************
