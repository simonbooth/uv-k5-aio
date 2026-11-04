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

#ifndef APP_FM_H
#define APP_FM_H

#ifdef ENABLE_FMRADIO

#include "driver/keyboard.h"

#define FM_CHANNEL_UP   0x01
#define FM_CHANNEL_DOWN 0xFF

enum {
    FM_SCAN_OFF = 0U,
};

#ifdef ENABLE_FMRADIO_SCREEN
extern uint16_t          gFM_Channels[20];
#endif
extern bool              gFmRadioMode;
extern uint8_t           gFmRadioCountdown_500ms;
extern volatile uint16_t gFmPlayCountdown_10ms;
extern volatile int8_t   gFM_ScanState;
extern bool              gFM_AutoScan;
extern uint8_t           gFM_ChannelPosition;
// Doubts about          whether this should be signed or not
extern uint16_t          gFM_FrequencyDeviation;
extern bool              gFM_FoundFrequency;
extern uint16_t          gFM_RestoreCountdown_10ms;

bool    FM_CheckValidChannel(uint8_t Channel);
// returns first valid channel starting at Channel
uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction);
int     FM_ConfigureChannelState(void);
void    FM_TurnOff(void);
void    FM_EraseChannels(void);

void    FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag);
void    FM_PlayAndUpdate(void);
int     FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit);

void    FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

void    FM_Play(void);
void    FM_Start(void);
void    FM_Sync(void);   // broadcast FM as a VFO modulation (no FM screen)

void    FM_Seek(void);   // scan on broadcast FM: the next station up
void    FM_ApplyAudio(void);   // the saved volume / mono to the FM chip (now, if it plays)
// While a station plays: its signal (dBuV), SNR, and whether it is received in stereo.
bool    FM_Signal(uint8_t *rssi, uint8_t *snr, bool *stereo);

#define FM_IS_BROADCAST(f) ((f) >= 8750000u && (f) < 10800000u)   // 10 Hz units

#endif

#endif
