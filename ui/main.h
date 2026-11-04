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

#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <stdint.h>

enum center_line_t {
    CENTER_LINE_NONE = 0,
    CENTER_LINE_IN_USE,
    CENTER_LINE_AUDIO_BAR,
    CENTER_LINE_RSSI,
    CENTER_LINE_AM_FIX_DATA,
    CENTER_LINE_DTMF_DEC,
    CENTER_LINE_CHARGE_DATA
};

enum Vfo_txtr_mode{
    VFO_MODE_NONE = 0,
    VFO_MODE_TX = 1,
    VFO_MODE_RX = 2,
};

typedef enum center_line_t center_line_t;

extern center_line_t center_line;
extern const int8_t dBmCorrTable[7];

void UI_DisplayAudioBar(void);
void UI_MAIN_TimeSlice500ms(void);
#ifdef ENABLE_FEAT_SERVICE_MODES
void UI_MAIN_ShowSquelch(void);   // show "SQL n" briefly (side-key squelch)
bool UI_FullView(void);           // single (full screen) view: picked (F+2), or transmitting
extern uint8_t gRssiBars;         // receiving VFO's signal, 0-6 bars
void UI_DrawSignalBars(uint8_t *p, uint8_t level);   // 17 x 7
void UI_DrawModeIcon(uint8_t *p, uint8_t vfo);       // 7 x 7
struct VFO_Info_t;
void UI_PowerShort(const struct VFO_Info_t *v, char *out);   // "5W" .. "20mW", "OFF" (out[5])
bool UI_PowerSelected(unsigned int vfo);                      // the F editor's power, on this VFO
uint8_t UI_MemNo(unsigned int vfo, char *n);    // the memory number text (n[8]): its inverted width
void UI_SqlText(unsigned int vfo, char *s);     // "S0".."S9", "MO" (s[4])
bool UI_SqlSelected(unsigned int vfo);
#endif
void UI_DisplayMain(void);

#ifdef ENABLE_AGC_SHOW_DATA
void UI_MAIN_PrintAGC(bool force);
#endif

#endif
