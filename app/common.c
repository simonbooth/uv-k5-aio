#include "app/action.h"
#include "app/app.h"
#include "driver/bk4819.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
    #include "driver/bk1080.h"
#endif
#include "app/chFrScanner.h"
#include "audio.h"
#include "functions.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "service.h"
#include "settings.h"
#include "ui/ui.h"

void COMMON_KeypadLockToggle() 
{

    if (gScreenToDisplay != DISPLAY_MENU &&
        gCurrentFunction != FUNCTION_TRANSMIT)
    {   // toggle the keyboad lock

        #ifdef ENABLE_VOICE
            gAnotherVoiceID = gEeprom.KEY_LOCK ? VOICE_ID_UNLOCK : VOICE_ID_LOCK;
        #endif

        gEeprom.KEY_LOCK = !gEeprom.KEY_LOCK;

        gRequestSaveSettings = true;
    }
}

void COMMON_SwitchVFOs()
{
#ifdef ENABLE_SCAN_RANGES    
    gScanRangeStart = 0;
#endif
    gEeprom.TX_VFO ^= 1;

    if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF)
        gEeprom.CROSS_BAND_RX_TX = gEeprom.TX_VFO + 1;
    if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
        gEeprom.DUAL_WATCH = gEeprom.TX_VFO + 1;

    gRequestSaveSettings  = 1;
    gFlagReconfigureVfos  = true;
    gScheduleDualWatch = true;

    gRequestDisplayScreen = DISPLAY_MAIN;
}

#ifdef ENABLE_FEAT_SERVICE_MODES
// MR/service -> VFO always carries the current channel into the VFO for its band.
static void COMMON_CopyChannelToVfo(void)
{
    const uint8_t  vfo         = gEeprom.TX_VFO;
    VFO_Info_t    *pVfo        = &gEeprom.VfoInfo[vfo];
    const uint16_t freqChannel = FREQ_CHANNEL_FIRST + SERVICE_SlotOf(pVfo->freq_config_RX.Frequency);

    gEeprom.ScreenChannel[vfo] = freqChannel;
    gEeprom.FreqChannel[vfo]   = freqChannel;
    pVfo->CHANNEL_SAVE         = freqChannel;
    pVfo->Band                 = FREQUENCY_GetBand(pVfo->freq_config_RX.Frequency);
    pVfo->TX_LOCK              = true;   // in VFO mode TX follows HamRgn's ham bands again
    pVfo->Name[0]              = 0;

    RADIO_SelectVfos();
    RADIO_ApplyOffset(pVfo);
    RADIO_ConfigureSquelchAndOutputPower(pVfo);
    RADIO_SetupRegisters(true);

    gRequestSaveChannel = 1;   // writes the VFO slot of its band
    gRequestSaveVFO     = true;
    gUpdateDisplay      = true;
}

// Up / Down on a VFO: its frequency (VFO mode), or the next channel of its list
// (a plan's channels; the memories of its mode). The selected VFO retunes at once;
// the other one (the F editor in the A / B view) is saved and reconfigured here.
// False if there is nowhere to go (frequency out of range, no channel).
bool COMMON_StepChannel(uint8_t vfo, int8_t dir)
{
    VFO_Info_t *v  = &gEeprom.VfoInfo[vfo];
    const bool  tx = vfo == gEeprom.TX_VFO;
    uint16_t    ch = gEeprom.ScreenChannel[vfo];

    if (tx && SERVICE_WatchStop())   // tuning the active VFO pauses the watch on it
        gFlagReconfigureVfos = true;      // (EXIT starts it again), so it doesn't fall back

    if (IS_FREQ_CHANNEL(ch)) {   // its slot's band, wrapping (general: across the others)
#ifdef ENABLE_FMRADIO
        const bool bfm = v->Modulation == MODULATION_BFM;
#else
        const bool bfm = false;
#endif
        const uint32_t f = SERVICE_NextVfoFrequency(v, dir);
        if (RX_freq_check(f) < 0)
            return false;
        v->freq_config_RX.Frequency = f;
        if (tx && bfm) {   // retune the FM chip and save; a full reconfigure would click the speaker
            gEeprom.FM_FrequencyPlaying = (uint16_t)(f / 10000u);
            if (gFmRadioMode && !gFM_RestoreCountdown_10ms)
                BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band);
            SETTINGS_SaveChannel(ch, vfo, v, 1);
            gUpdateDisplay = true;
        } else if (tx) {
            BK4819_SetFrequency(f);
            BK4819_RX_TurnOn();
            gRequestSaveChannel = 1;
        } else {
            SETTINGS_SaveChannel(ch, vfo, v, 1);
            RADIO_ConfigureChannel(vfo, VFO_CONFIGURE);
            gFlagReconfigureVfos = true;
        }
        return true;
    }

    if (IS_SVC_CHANNEL(ch)) {
        ch = SERVICE_StepChannel(ch, dir);
    } else {   // memories of the same mode (ham, FM, a service's favorites), wrapping
        const uint16_t next = SERVICE_NextMemory(ch + dir, dir, SERVICE_ModeOf(vfo), SERVICE_GroupOf(vfo));   // in its group
        if (next == 0xFFFF)
            return false;
        ch = next;
    }

    if (IS_MR_CHANNEL(ch))
        gEeprom.MrChannel[vfo] = ch;
    gEeprom.ScreenChannel[vfo] = ch;
    gRequestSaveVFO = true;
    if (tx) {
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    } else {
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
        gFlagReconfigureVfos = true;
    }
    return true;
}

// Modes are per VFO: F+1 (in channel modes) moves the selected VFO to the next
// mode and the other VFO keeps its channel. Single / dual watch stays the
// user's choice (F+2); only the F+5 marine watch sets both VFOs.
void COMMON_SetService(Service_t service)
{
    SERVICE_SaveState();   // remember where each VFO was, for coming back to this mode
    SERVICE_WxAlertReset();

    for (uint8_t vfo = 0; vfo < 2; vfo++)   // HAM: memory channels (VFO if none programmed)
        gEeprom.ScreenChannel[vfo] = SERVICE_ChannelFor(vfo, service);

    gRequestSaveVFO   = true;
    gFlagResetVfos    = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}

#ifdef ENABLE_FMRADIO
// Broadcast FM as a mode: the selected VFO to the FM slot (its own saved station
// and mode) and back to HAM.
void COMMON_ToggleFm(void)
{
    const uint8_t vfo = gEeprom.TX_VFO;
    const bool    fm  = SERVICE_ModeOf(vfo) == MODE_ICON_FM;   // (the FM slot in any mode)

    if (!fm && !SERVICE_SlotEnabled(SLOT_FM)) {   // broadcast FM switched off (CHIRP)
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
    SERVICE_SaveState();
    SERVICE_WxAlertReset();
    gEeprom.ScreenChannel[vfo] = fm ? SERVICE_ChannelFor(vfo, SERVICE_HAM) : FREQ_CHANNEL_FIRST + SLOT_FM;
    gRequestSaveVFO   = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}
#endif

void COMMON_CycleService(void)
{
    const uint8_t vfo = gEeprom.TX_VFO;
    const uint8_t cur = SERVICE_ModeOf(vfo);   // channels, favorites and VFOs alike

    uint8_t next = cur;   // the next mode with anything licensed (HAM always)
    do
        next = (uint8_t)((next + 1u) % SERVICE_COUNT);
    while (!SERVICE_Enabled(next));

#ifdef ENABLE_FMRADIO   // ... -> FM -> HAM
    if (cur == MODE_ICON_FM || (next == SERVICE_HAM && SERVICE_SlotEnabled(SLOT_FM))) {
        COMMON_ToggleFm();
        return;
    }
#endif

    SERVICE_SaveState();
    SERVICE_WxAlertReset();

    gEeprom.ScreenChannel[vfo] = SERVICE_ChannelFor(vfo, (Service_t)next);

    gRequestSaveVFO   = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}
#endif



void COMMON_SwitchVFOMode()
{
#ifdef ENABLE_NOAA
    if (gEeprom.VFO_OPEN && !IS_NOAA_CHANNEL(gTxVfo->CHANNEL_SAVE))
#else
    if (gEeprom.VFO_OPEN)
#endif
    {
#ifdef ENABLE_FEAT_SERVICE_MODES
        // The VFO's mode's stops: VFO -> channels -> group 1 -> 2 -> 3 -> all memories -> VFO
        // (GMRS / marine: no VFO; FM: no channels, its VFO is the FM channel); a group
        // with no memories of the mode is skipped, and so are all memories with none.
        const uint8_t  vfo   = gEeprom.TX_VFO;
        const uint8_t  mode  = SERVICE_ModeOf(vfo);
        const uint16_t ch    = gTxVfo->CHANNEL_SAVE;
        const uint8_t  group = SERVICE_GroupOf(vfo);
        const uint8_t  from  = IS_MR_CHANNEL(ch) ? (group ? 1u + group : 5u) : IS_SVC_CHANNEL(ch) ? 1u : 0u;
        uint8_t        stop  = from;
        uint16_t       mr    = 0xFFFF;
        do {
            stop = (uint8_t)((stop + 1u) % 6u);
            if (stop == 0)
                mr = (mode == SERVICE_GMRS || mode == SERVICE_MARINE) ? 0xFFFF : 0u;
            else if (stop == 1)   // (ham channels: with a ham licence)
                mr = (mode == MODE_ICON_FM || !SERVICE_ChannelVisible(SERVICE_ChannelFor(vfo, (Service_t)(mode < SERVICE_COUNT ? mode : 0)))) ? 0xFFFF : 0u;
            else
                mr = SERVICE_NextMemory(gEeprom.MrChannel[vfo], 1, mode, stop == 5u ? 0u : stop - 1u);
        } while (mr == 0xFFFF && stop != from);

        if (stop == from) {   // nowhere else to go
            gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
            return;
        }
        if (stop == 0) {   // the VFO, on this channel / memory (an FM memory: the FM channel)
            COMMON_CopyChannelToVfo();
            return;
        }
        if (stop == 1) {   // the channels of the mode, where that side was
            gEeprom.ScreenChannel[vfo] = SERVICE_ChannelFor(vfo, mode < SERVICE_COUNT ? (Service_t)mode : SERVICE_HAM);
        } else {           // a group / all memories, from the last memory on
            SERVICE_SetGroup(vfo, stop == 5u ? 0u : stop - 1u);
            gEeprom.ScreenChannel[vfo] = gEeprom.MrChannel[vfo] = mr;
        }
        gRequestSaveVFO   = true;
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
        return;
#else
        if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        {   // swap to frequency mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = gEeprom.FreqChannel[gEeprom.TX_VFO];
            #ifdef ENABLE_VOICE
                gAnotherVoiceID        = VOICE_ID_FREQUENCY_MODE;
            #endif
            gRequestSaveVFO            = true;
            gVfoConfigureMode          = VFO_CONFIGURE_RELOAD;
            return;
        }
#endif

#ifndef ENABLE_FEAT_SERVICE_MODES
        uint16_t Channel = RADIO_FindNextChannel(gEeprom.MrChannel[gEeprom.TX_VFO], 1, false, 0);
        if (Channel != 0xFFFF)
        {   // swap to channel mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = Channel;
            #ifdef ENABLE_VOICE
                AUDIO_SetVoiceID(0, VOICE_ID_CHANNEL_MODE);
                AUDIO_SetDigitVoice(1, Channel + 1);
                gAnotherVoiceID = (VOICE_ID_t)0xFE;
            #endif
            gRequestSaveVFO     = true;
            gVfoConfigureMode   = VFO_CONFIGURE_RELOAD;
            return;
        }
#endif
    }
}