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

#include "app/action.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/common.h"
#include "app/dtmf.h"
#ifdef ENABLE_FLASHLIGHT
    #include "app/flashlight.h"
#endif
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "audio.h"
#include "bsp/dp32g030/gpio.h"
#ifdef ENABLE_FMRADIO
    #include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/backlight.h"
#include "functions.h"
#include "misc.h"
#include "service.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "ui/ui.h"
#ifdef ENABLE_REGA
    #include "app/rega.h"
#endif

#if defined(ENABLE_FMRADIO_SCREEN)
static void ACTION_Scan_FM(bool bRestart);
#endif

#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
static void ACTION_AlarmOr1750(bool b1750);
inline static void ACTION_Alarm() { ACTION_AlarmOr1750(false); }
inline static void ACTION_1750() { ACTION_AlarmOr1750(true); };
#endif

inline static void ACTION_ScanRestart() { ACTION_Scan(true); };

#ifdef ENABLE_FEAT_SERVICE_MODES
// Side-key squelch: one step per press, shown briefly on screen, saved.
static void ACTION_SquelchStep(int8_t direction)   // the active VFO only
{   // monitoring counts as 0 (open): up ends it at 1 on the VFO monitored, down does nothing
    uint8_t vfo = gEeprom.TX_VFO;
    if (gCurrentFunction == FUNCTION_MONITOR) {
        if (direction < 0)
            return;
        gMonitor  = false;   // (the step's reconfigure ends it: back to the foreground)
        vfo       = gEeprom.RX_VFO;
        direction = (int8_t)(1 - SERVICE_GetSquelch(vfo));
    }
    SERVICE_StepSquelch(vfo, direction);
    UI_MAIN_ShowSquelch();
}

// The SysInf screen (battery voltage / percent, firmware).
static void ACTION_SysInfo(void)
{
    gRequestDisplayScreen = DISPLAY_SYSINFO;
}

static void ACTION_SquelchUp(void)   { ACTION_SquelchStep(1); }
static void ACTION_SquelchDown(void) { ACTION_SquelchStep(-1); }
#endif

void (*action_opt_table[])(void) = {
#ifdef ENABLE_FEAT_SERVICE_MODES
    [ACTION_OPT_SQUELCH_UP]   = &ACTION_SquelchUp,
    [ACTION_OPT_SQUELCH_DOWN] = &ACTION_SquelchDown,
    [ACTION_OPT_SYSINFO]      = &ACTION_SysInfo,
#endif
    [ACTION_OPT_NONE] = &FUNCTION_NOP,
    [ACTION_OPT_POWER] = &ACTION_Power,
    [ACTION_OPT_MONITOR] = &ACTION_Monitor,
    [ACTION_OPT_SCAN] = &ACTION_ScanRestart,
    [ACTION_OPT_KEYLOCK] = &COMMON_KeypadLockToggle,
    [ACTION_OPT_A_B] = &COMMON_SwitchVFOs,
    [ACTION_OPT_VFO_MR] = &COMMON_SwitchVFOMode,
    [ACTION_OPT_SWITCH_DEMODUL] = &ACTION_SwitchDemodul,

#ifdef ENABLE_FLASHLIGHT
    [ACTION_OPT_FLASHLIGHT] = &ACTION_FlashLight,
#else
    [ACTION_OPT_FLASHLIGHT] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_VOX
    [ACTION_OPT_VOX] = &ACTION_Vox,
#else
    [ACTION_OPT_VOX] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_FMRADIO
    [ACTION_OPT_FM] = &ACTION_FM,
#else
    [ACTION_OPT_FM] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_ALARM
    [ACTION_OPT_ALARM] = &ACTION_Alarm,
#else
    [ACTION_OPT_ALARM] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_TX1750
    [ACTION_OPT_1750] = &ACTION_1750,
#else
    [ACTION_OPT_1750] = &FUNCTION_NOP,
#endif

#ifdef ENABLE_BLMIN_TMP_OFF
    [ACTION_OPT_BLMIN_TMP_OFF] = &ACTION_BlminTmpOff,
#else
    [ACTION_OPT_BLMIN_TMP_OFF] = &FUNCTION_NOP,
#endif

#if defined(ENABLE_FEAT_F4HWN) && defined(ENABLE_FEAT_SERVICE_MODES)
    [ACTION_OPT_RXMODE] = &FUNCTION_NOP,   // the A / B view and its watch replaced these
    [ACTION_OPT_MAINONLY] = &FUNCTION_NOP,
#endif
#ifdef ENABLE_FEAT_F4HWN
#ifndef ENABLE_FEAT_SERVICE_MODES
    [ACTION_OPT_RXMODE] = &ACTION_RxMode,
    [ACTION_OPT_MAINONLY] = &ACTION_MainOnly,
#endif
#ifdef ENABLE_FEAT_SERVICE_MODES   // one PTT mode (F + PTT latches): nothing to toggle
    [ACTION_OPT_PTT] = &FUNCTION_NOP,
#else
    [ACTION_OPT_PTT] = &ACTION_Ptt,
#endif
    [ACTION_OPT_WN] = &ACTION_Wn,
    [ACTION_OPT_BACKLIGHT] = &ACTION_BackLight,
    #if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
        [ACTION_OPT_MUTE] = &ACTION_Mute,
    #else
        [ACTION_OPT_MUTE] = &FUNCTION_NOP,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        [ACTION_OPT_POWER_HIGH] = &ACTION_Power_High,
        [ACTION_OPT_REMOVE_OFFSET] = &ACTION_Remove_Offset,
    #endif
#else
    [ACTION_OPT_RXMODE] = &FUNCTION_NOP,
#endif
#ifdef ENABLE_REGA
    [ACTION_OPT_REGA_ALARM] = &ACTION_RegaAlarm,
    [ACTION_OPT_REGA_TEST] = &ACTION_RegaTest,
#endif
};

static_assert(ARRAY_SIZE(action_opt_table) == ACTION_OPT_LEN);

void ACTION_Power(void)
{
#ifdef ENABLE_FEAT_SERVICE_MODES
    if (IS_SVC_CHANNEL(gTxVfo->CHANNEL_SAVE))   // plan limits; on GMRS this also picks FRS / GMRS
        gTxVfo->OUTPUT_POWER = SERVICE_NextPower(gTxVfo->CHANNEL_SAVE, gTxVfo->OUTPUT_POWER);
    else
#endif
    if (++gTxVfo->OUTPUT_POWER > OUTPUT_POWER_HIGH)
        gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW1;

    gRequestSaveChannel = 1;

    gRequestDisplayScreen = gScreenToDisplay;

#ifdef ENABLE_VOICE
    gAnotherVoiceID   = VOICE_ID_POWER;
#endif

}

void ACTION_Monitor(void)
{
    if (gCurrentFunction != FUNCTION_MONITOR) { // enable the monitor
        RADIO_SelectVfos();
#ifdef ENABLE_NOAA
        if (IS_NOAA_CHANNEL(gRxVfo->CHANNEL_SAVE) && gIsNoaaMode)
            gNoaaChannel = gRxVfo->CHANNEL_SAVE - NOAA_CHANNEL_FIRST;
#endif
        RADIO_SetupRegisters(true);
        APP_StartListening(FUNCTION_MONITOR);
        return;
    }

    gMonitor = false;

    if (gScanStateDir != SCAN_OFF) {
        gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
        gScheduleScanListen    = false;
        gScanPauseMode         = true;
    }

#ifdef ENABLE_NOAA
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF && gIsNoaaMode) {
        gNOAA_Countdown_10ms = NOAA_countdown_10ms;
        gScheduleNOAA        = false;
    }
#endif

    RADIO_SetupRegisters(true);

#ifdef ENABLE_FMRADIO_SCREEN
    if (gFmRadioMode) {
        FM_Start();
        gRequestDisplayScreen = DISPLAY_FM;
    }
    else
#endif
        gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Scan(bool bRestart)
{
    (void)bRestart;

#if defined(ENABLE_FEAT_SERVICE_MODES) && defined(ENABLE_FMRADIO)
    if (gTxVfo->Modulation == MODULATION_BFM) {   // broadcast FM: seek the next station
        FM_Seek();
        return;
    }
#endif

#ifdef ENABLE_FMRADIO_SCREEN
    if (gFmRadioMode) {
        ACTION_Scan_FM(bRestart);
        return;
    }
#endif

    if (SCANNER_IsScanning()) {
        return;
    }

    // not scanning
    gMonitor = false;

#ifdef ENABLE_DTMF_CALLING
    DTMF_clear_RX();
#endif
    gDTMF_RX_live_timeout = 0;
    memset(gDTMF_RX_live, 0, sizeof(gDTMF_RX_live));

    RADIO_SelectVfos();

#ifdef ENABLE_NOAA
    if (IS_NOAA_CHANNEL(gRxVfo->CHANNEL_SAVE)) {
        return;
    }
#endif

    GUI_SelectNextDisplay(DISPLAY_MAIN);

    if (gScanStateDir != SCAN_OFF) {
        // already scanning

#ifdef ENABLE_FEAT_SERVICE_MODES   // the scan covers the VFO's group: no list cycling
        {
#else
        if (!IS_MR_CHANNEL(gNextMrChannel)) {
#endif
            CHFRSCANNER_Stop();
#ifdef ENABLE_VOICE
            gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
#endif
            return;
        }

        // channel mode. Keep scanning but toggle between scan lists
        gEeprom.SCAN_LIST_DEFAULT = (gEeprom.SCAN_LIST_DEFAULT + 1) % 6;
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
            SETTINGS_WriteCurrentState();
        #endif

        // jump to the next channel
        CHFRSCANNER_Start(false, gScanStateDir);
        gScanPauseDelayIn_10ms = 1;
        gScheduleScanListen    = false;
    } else {
        #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        if(gScanRangeStart == 0) // No ScanRange
        {
            gEeprom.CURRENT_STATE = 1;
        }
        else // ScanRange
        {
            gEeprom.CURRENT_STATE = 2;
        }
        SETTINGS_WriteCurrentState();
        #endif
        // start scanning
        CHFRSCANNER_Start(true, SCAN_FWD);

#ifdef ENABLE_VOICE
        AUDIO_SetVoiceID(0, VOICE_ID_SCANNING_BEGIN);
        AUDIO_PlaySingleVoice(true);
#endif

        // clear the other vfo's rssi level (to hide the antenna symbol)
        gVFO_RSSI_bar_level[(gEeprom.RX_VFO + 1) & 1U] = 0;

        // let the user see DW is not active
        gDualWatchActive = false;
    }

    gUpdateStatus = true;
}


void ACTION_SwitchDemodul(void)
{
    gRequestSaveChannel = 1;

    gTxVfo->Modulation++;

    if(gTxVfo->Modulation == MODULATION_UKNOWN)
        gTxVfo->Modulation = MODULATION_FM;
}


void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (gScreenToDisplay == DISPLAY_MAIN && gDTMF_InputMode){
         // entering DTMF code

        gPttWasReleased = true;

        if (Key != KEY_SIDE1 || bKeyHeld || !bKeyPressed){
            return;
        }

        // side1 btn pressed

        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        gRequestDisplayScreen = DISPLAY_MAIN;

        if (gDTMF_InputBox_Index <= 0) {
            // turn off DTMF input box if no codes left
            gDTMF_InputMode = false;
            return;
        }

        // DTMF codes are in the input box
        gDTMF_InputBox[--gDTMF_InputBox_Index] = '-'; // delete one code

#ifdef ENABLE_VOICE
        gAnotherVoiceID   = VOICE_ID_CANCEL;
#endif
        return;
    }

    enum ACTION_OPT_t funcShort = ACTION_OPT_NONE;
    enum ACTION_OPT_t funcLong  = ACTION_OPT_NONE;
    switch(Key) {
        case KEY_SIDE1:
            funcShort = gEeprom.KEY_1_SHORT_PRESS_ACTION;
            funcLong  = gEeprom.KEY_1_LONG_PRESS_ACTION;
            break;
        case KEY_SIDE2:
            funcShort = gEeprom.KEY_2_SHORT_PRESS_ACTION;
            funcLong  = gEeprom.KEY_2_LONG_PRESS_ACTION;
            break;
        case KEY_MENU:
            funcLong  = gEeprom.KEY_M_LONG_PRESS_ACTION;
            break;
        default:
            break;
    }

    if (!bKeyHeld && bKeyPressed) // button pushed
    {
        return;
    }

    // held or released beyond this point

    if(!(bKeyHeld && !bKeyPressed)) // don't beep on released after hold
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    if (bKeyHeld || bKeyPressed) // held
    {
        funcShort = funcLong;

        if (!bKeyPressed) //ignore release if held
            return;
    }

    // held or released after short press beyond this point

    if (action_opt_table[funcShort])   // (an action not in this build - a key mapped by another firmware: none)
        action_opt_table[funcShort]();
}


#if defined(ENABLE_FMRADIO) && !defined(ENABLE_FMRADIO_SCREEN)
void ACTION_FM(void)   // the selected VFO to broadcast FM and back
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && gScanStateDir == SCAN_OFF) {
        gInputBoxIndex        = 0;
        gRequestDisplayScreen = DISPLAY_MAIN;
        COMMON_ToggleFm();
    }
}
#endif

#ifdef ENABLE_FMRADIO_SCREEN
void ACTION_FM(void)
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR)
    {
        gInputBoxIndex = 0;

        if (gFmRadioMode) {
            FM_TurnOff();
            gFlagReconfigureVfos  = true;
            gRequestDisplayScreen = DISPLAY_MAIN;

#ifdef ENABLE_VOX
            gVoxResumeCountdown = 80;
#endif
            return;
        }

        gMonitor = false;

        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);

        FM_Start();

        gRequestDisplayScreen = DISPLAY_FM;
    }
}

static void ACTION_Scan_FM(bool bRestart)
{
    if (FUNCTION_IsRx())
        return;

    GUI_SelectNextDisplay(DISPLAY_FM);

    gMonitor = false;

    if (gFM_ScanState != FM_SCAN_OFF) {
        FM_PlayAndUpdate();

#ifdef ENABLE_VOICE
        gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
#endif
        return;
    }

    uint16_t freq;

    if (bRestart) {
        gFM_AutoScan = true;
        gFM_ChannelPosition = 0;
        FM_EraseChannels();
        freq = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    } else {
        gFM_AutoScan = false;
        gFM_ChannelPosition = 0;
        freq = gEeprom.FM_FrequencyPlaying;
    }

    BK1080_GetFrequencyDeviation(freq);
    FM_Tune(freq, 1, bRestart);

#ifdef ENABLE_VOICE
    gAnotherVoiceID = VOICE_ID_SCANNING_BEGIN;
#endif

}

#endif


#if defined(ENABLE_ALARM) || defined(ENABLE_TX1750)
static void ACTION_AlarmOr1750(const bool b1750)
{

    if(gEeprom.KEY_LOCK && gEeprom.KEY_LOCK_PTT)
        return;

    #if defined(ENABLE_ALARM)
        const AlarmState_t alarm_mode = (gEeprom.ALARM_MODE == ALARM_MODE_TONE) ? ALARM_STATE_TXALARM : ALARM_STATE_SITE_ALARM;
        gAlarmRunningCounter = 0;
    #endif

    #if defined(ENABLE_ALARM) && defined(ENABLE_TX1750)
        gAlarmState = b1750 ? ALARM_STATE_TX1750 : alarm_mode;
    #elif defined(ENABLE_ALARM)
        gAlarmState = alarm_mode;
    #else
        gAlarmState = ALARM_STATE_TX1750;
    #endif

    (void)b1750;
    gInputBoxIndex = 0;

    gFlagPrepareTX = gAlarmState != ALARM_STATE_OFF;

    if (gScreenToDisplay != DISPLAY_MENU)     // 1of11 .. don't close the menu
        gRequestDisplayScreen = DISPLAY_MAIN;
}


#endif

#ifdef ENABLE_VOX
void ACTION_Vox(void)
{
    gEeprom.VOX_SWITCH   = !gEeprom.VOX_SWITCH;
    gRequestSaveSettings = true;
    gFlagReconfigureVfos = true;
    gUpdateStatus        = true;

    #ifdef ENABLE_VOICE
        gAnotherVoiceID  = VOICE_ID_VOX;
    #endif
}
#endif

#ifdef ENABLE_BLMIN_TMP_OFF
void ACTION_BlminTmpOff(void)
{
    if(++gEeprom.BACKLIGHT_MIN_STAT == BLMIN_STAT_UNKNOWN) {
        gEeprom.BACKLIGHT_MIN_STAT = BLMIN_STAT_ON;
        BACKLIGHT_SetBrightness(gEeprom.BACKLIGHT_MIN);
    } else {
        BACKLIGHT_SetBrightness(0);
    }
}
#endif

#ifdef ENABLE_FEAT_F4HWN
void ACTION_Update(void)
{
    gSaveRxMode          = false;
    gFlagReconfigureVfos = true;
    gUpdateStatus        = true;
}

#ifndef ENABLE_FEAT_SERVICE_MODES   // the A / B view and its watch replaced RxMode
void ACTION_RxMode(void)
{
    static bool cycle = 0;

    switch(cycle) {
        case 0:
            gEeprom.DUAL_WATCH = !gEeprom.DUAL_WATCH;
            cycle = 1;
            break;
        case 1:
            gEeprom.CROSS_BAND_RX_TX = !gEeprom.CROSS_BAND_RX_TX;
            cycle = 0;
            break;
    }

    ACTION_Update();
}

void ACTION_MainOnly(void)
{
    static bool cycle = 0;
    static uint8_t dw = 0;
    static uint8_t cb = 0;

    switch(cycle) {
        case 0:
            dw = gEeprom.DUAL_WATCH;
            cb = gEeprom.CROSS_BAND_RX_TX;

            gEeprom.DUAL_WATCH = 0;
            gEeprom.CROSS_BAND_RX_TX = 0;
            cycle = 1;
            break;
        case 1:
            gEeprom.DUAL_WATCH = dw;
            gEeprom.CROSS_BAND_RX_TX = cb;
            cycle = 0;
            break;
    }

    ACTION_Update();
}
#endif

#ifndef ENABLE_FEAT_SERVICE_MODES
void ACTION_Ptt(void)
{
    gSetting_set_ptt_session = !gSetting_set_ptt_session;
}
#endif

void ACTION_Wn(void)
{
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        bool narrower = 0;
        if (FUNCTION_IsRx())
        {
            gRxVfo->CHANNEL_BANDWIDTH = (gRxVfo->CHANNEL_BANDWIDTH == 0) ? 1: 0;
            if(gRxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW && NARROWER_ON(gRxVfo))
            {
                narrower = 1;
            }

            #ifdef ENABLE_AM_FIX
                BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH + narrower, true);
            #else
                BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH + narrower, false);
            #endif
        }
        else
        {
            gTxVfo->CHANNEL_BANDWIDTH = (gTxVfo->CHANNEL_BANDWIDTH == 0) ? 1: 0;
            if(gTxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW && NARROWER_ON(gTxVfo))
            {
                narrower = 1;
            }

            #ifdef ENABLE_AM_FIX
                BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, true);
            #else
                BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, false);
            #endif
        }
    #else
        if (FUNCTION_IsRx())
        {
            gRxVfo->CHANNEL_BANDWIDTH = (gRxVfo->CHANNEL_BANDWIDTH == 0) ? 1: 0;
            #ifdef ENABLE_AM_FIX
                BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH, true);
            #else
                BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH, false);
            #endif
        }
        else
        {
            gTxVfo->CHANNEL_BANDWIDTH = (gTxVfo->CHANNEL_BANDWIDTH == 0) ? 1: 0;
            #ifdef ENABLE_AM_FIX
                BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, true);
            #else
                BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, false);
            #endif
        }
    #endif
}

void ACTION_BackLight(void)
{
    if(gBackLight)
    {
        gEeprom.BACKLIGHT_TIME = gBacklightTimeOriginal;
    }
    gBackLight = false;
    BACKLIGHT_TurnOn();
}

void ACTION_BackLightOnDemand(void)
{
    if(gBackLight == false)
    {
        gBacklightTimeOriginal = gEeprom.BACKLIGHT_TIME;
        gEeprom.BACKLIGHT_TIME = 61;
        gBackLight = true;
    }
    else
    {
        if(gBacklightBrightnessOld == gEeprom.BACKLIGHT_MAX)
        {
            gEeprom.BACKLIGHT_TIME = 0;
        }
        else
        {
            gEeprom.BACKLIGHT_TIME = 61;
        }
    }
    
    BACKLIGHT_TurnOn();
}

    #if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    void ACTION_Mute(void)
    {
        // Toggle mute state
        gMute = !gMute;

        // Update the registers
        #ifdef ENABLE_FMRADIO
            BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, gMute ? 0x0A10 : 0x0A1F);
        #endif
        gEeprom.VOLUME_GAIN = gMute ? 0 : gEeprom.VOLUME_GAIN_BACKUP;
        BK4819_WriteRegister(BK4819_REG_48,
            (11u << 12)                |  // ??? .. 0 ~ 15, doesn't seem to make any difference
            (0u << 10)                 |  // AF Rx Gain-1
            (gEeprom.VOLUME_GAIN << 4) |  // AF Rx Gain-2
            (gEeprom.DAC_GAIN << 0));     // AF DAC Gain (after Gain-1 and Gain-2)

        gUpdateStatus = true;
    }
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    void ACTION_Power_High(void)
    {
        gPowerHigh = !gPowerHigh;
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    }

    void ACTION_Remove_Offset(void)
    {
        gRemoveOffset = !gRemoveOffset;
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    }
    #endif
#endif