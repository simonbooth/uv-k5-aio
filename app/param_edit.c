#ifdef ENABLE_FEAT_SERVICE_MODES

#include <string.h>

#include "app/param_edit.h"
#include "app/common.h"
#include "app/chFrScanner.h"
#include "audio.h"
#include "app/sysinfo.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "dcs.h"
#include "driver/bk4819.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "service.h"
#include "settings.h"
#include "subcode.h"
#include "external/printf/printf.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif

uint8_t gEditItem;
uint8_t gEditVfo;   // which VFO the item belongs to (A / B view: either)

#define EV (&gEeprom.VfoInfo[gEditVfo])

static uint8_t gOffsetDigits;   // digits typed into the offset since it was selected

static const uint8_t ItemsGmrs[]   = { PE_TUNE, PE_CODE, PE_SUBCODE, PE_POWER, PE_SQUELCH };   // power picks FRS / GMRS
static const uint8_t ItemsMarine[] = { PE_TUNE, PE_POWER, PE_SQUELCH };            // 1 W / 5 W where allowed
static const uint8_t ItemsTune[]   = { PE_TUNE, PE_SQUELCH };                      // air
static const uint8_t ItemsHam[]    = { PE_TUNE, PE_POWER, PE_SQUELCH };            // ham channels: their band's power
static const uint8_t ItemsMemory[] = { PE_TUNE, PE_MODE, PE_POWER, PE_RXTONE, PE_TXTONE, PE_SHIFT, PE_OFFSET, PE_STEP, PE_SQUELCH, PE_COMPAND, PE_NAME, PE_GROUPS, PE_SLOT };
static const uint8_t ItemsBfm[]    = { PE_TUNE, PE_VOLUME, PE_STEREO };            // FM memories and the FM VFO
static const uint8_t ItemsVfo[]    = { PE_TUNE, PE_BAND, PE_MODE, PE_POWER, PE_RXTONE, PE_TXTONE, PE_SHIFT, PE_OFFSET, PE_STEP, PE_SQUELCH, PE_COMPAND, PE_SLOT };

static uint8_t gCursor = PARAM_CURSOR_NONE;   // PE_NAME / PE_GROUPS: the character / flag
static uint8_t gSlotPick;                     // PE_SLOT: see PARAM_SlotPick
static char    gName[11];                     // PE_NAME: the memory's name, space-padded

uint8_t     PARAM_Cursor(void)   { return gCursor; }
uint8_t     PARAM_SlotPick(void) { return gSlotPick; }
const char *PARAM_NameText(void) { return gName; }

// The parameters a VFO's channel has, in F order.
static uint8_t Items(uint8_t vfo, const uint8_t **list)
{
    const VFO_Info_t *v = &gEeprom.VfoInfo[vfo];
    if (gScanStateDir != SCAN_OFF)
        return 0;
    if (SERVICE_OfChannel(v->CHANNEL_SAVE) == SERVICE_GMRS) {
        *list = ItemsGmrs;
        return sizeof(ItemsGmrs);
    }
    if (SERVICE_OfChannel(v->CHANNEL_SAVE) == SERVICE_MARINE) {
        *list = ItemsMarine;
        return sizeof(ItemsMarine);
    }
#ifdef ENABLE_FMRADIO
    if (v->Modulation == MODULATION_BFM) {   // broadcast FM: tune only
        *list = ItemsBfm;
        return sizeof(ItemsBfm);
    }
#endif
    if (IS_MR_CHANNEL(v->CHANNEL_SAVE)) {   // memories: everything but the band
        *list = ItemsMemory;
        return sizeof(ItemsMemory);
    }
    if (SERVICE_OfChannel(v->CHANNEL_SAVE) == SERVICE_HAM && IS_SVC_CHANNEL(v->CHANNEL_SAVE)) {
        *list = ItemsHam;
        return sizeof(ItemsHam);
    }
    if (!IS_FREQ_CHANNEL(v->CHANNEL_SAVE)) {
        *list = ItemsTune;
        return sizeof(ItemsTune);
    }
    *list = ItemsVfo;
    return sizeof(ItemsVfo);
}

// Items that don't apply right now: power on a channel that can't transmit at all,
// the other TX ones also with power OFF, the offset without a shift.
static bool Skip(uint8_t vfo, uint8_t item)
{
    const VFO_Info_t *v = &gEeprom.VfoInfo[vfo];
    if (item == PE_SUBCODE)   // full screen, where brands number them (not PMR446 / UK: by the channel)
        return SERVICE_GetAbView() || SERVICE_IsPmr();
    if (item == PE_GROUPS)    // full screen
        return SERVICE_GetAbView();
    if (item == PE_COMPAND) {   // full screen, FM: ham; walkie memories with a GMRS licence
        const uint8_t m = SERVICE_ModeOf(vfo);   // (PMR446 / Simple UK: CHIRP only)
        return SERVICE_GetAbView() || v->Modulation != MODULATION_FM ||
               !(m == SERVICE_HAM || (m == SERVICE_GMRS && IS_MR_CHANNEL(v->CHANNEL_SAVE) &&
                                      !SERVICE_IsPmr() && SERVICE_GmrsLicensed()));
    }
    if (item == PE_POWER)
        return SERVICE_IsTxInhibited(v);
    if ((item == PE_TXTONE || item == PE_SHIFT || item == PE_OFFSET) && !SERVICE_CanTx(v))
        return true;
    return item == PE_OFFSET && v->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_OFF;
}

const char *PARAM_Name(uint8_t item)
{
    static const char * const names[] = {
        [PE_MODE]   = "MODULATION",
        [PE_POWER]  = "TX POWER",
        [PE_RXTONE] = "RX CTCSS / DCS",
        [PE_TXTONE] = "TX CTCSS / DCS",
        [PE_SHIFT]  = "TX SHIFT",
        [PE_OFFSET] = "TX OFFSET MHz",
        [PE_STEP]   = "TUNING STEP",
        [PE_SQUELCH] = "SQUELCH",
        [PE_VOLUME]  = "FM VOLUME",
        [PE_STEREO]  = "STEREO / MONO",
        [PE_NAME]    = "NAME",
        [PE_SLOT]    = "MEMORY",
    };
    if (item == PE_TUNE)
        return IS_FREQ_CHANNEL(EV->CHANNEL_SAVE) ? "FREQUENCY" : "CHANNEL";
    if (item == PE_BAND)
        return "BAND";
    static char s[24];
    if (item == PE_SUBCODE) {   // its value too: the row is what changes ("MOTO/UNIDEN/BCA":
        const char *b = gSubMenu_SUBCODE[SERVICE_GetSubCodeProfile()];   // the menu's lines on one,
        uint8_t     n = strlen(b) + 6u <= 18u ? 6u : 0u;                  // "CODES " where it fits)
        strcpy(s, "CODES ");
        for (; *b && n < sizeof(s) - 1u; b++)
            s[n++] = *b == '\n' ? '/' : *b;
        s[n] = 0;
        return s;
    }
    if (item == PE_COMPAND) {   // its value too
        static const char modes[][6] = { "OFF", "TX", "RX", "TX/RX" };
        sprintf(s, "COMPANDER %s", modes[EV->Compander & 3u]);
        return s;
    }
    if (item == PE_GROUPS) {    // "GROUPS 1 - 3 C": the ones it is in (C: Call)
        const VFO_Info_t *v = EV;
        const bool on[4] = { v->SCANLIST1_PARTICIPATION, v->SCANLIST2_PARTICIPATION,
                             v->SCANLIST3_PARTICIPATION, (v->SvcFlags & VFO_F_CALL) != 0 };
        strcpy(s, "GROUPS 1 2 3 C");
        for (uint8_t i = 0; i < 4; i++)
            if (!on[i])
                s[7 + 2 * i] = '-';
        return s;
    }
    return item >= PE_MODE && item < ARRAY_SIZE(names) ? names[item] : "";
}

uint8_t PARAM_Current(void)
{
    const uint8_t *list;
    if (gEditItem != PE_NONE && (gEditVfo == gEeprom.TX_VFO || SERVICE_GetAbView())) {
        const uint8_t n = Items(gEditVfo, &list);
        for (uint8_t i = 0; i < n; i++)
            if (list[i] == gEditItem)
                return gEditItem;
    }
    gEditItem = PE_NONE;   // the channel / view changed under it
    return PE_NONE;
}

// The active VFO's items, then (A / B view) the other VFO's: F walks all of them.
static void TakeSlot(void);
static void Apply(VFO_Info_t *v, uint8_t vfo);

// A ham band outside the radio's design switched on (the warning accepted): that level.
static uint8_t gHamPending;
static void HamAccept(void)
{
    VFO_Info_t *v = EV;
    v->TX_OFF       = false;
    v->OUTPUT_POWER = gHamPending - 1u;
    Apply(v, gEditVfo);
}

void PARAM_Next(void)
{
    const uint8_t  active = gEeprom.TX_VFO;
    const uint8_t  cur    = PARAM_Current();
    if (gCursor != PARAM_CURSOR_NONE && (cur == PE_NAME || cur == PE_GROUPS) &&
        ++gCursor < (cur == PE_NAME ? 10u : 4u)) {
        gUpdateDisplay = true;   // the next character / flag
        return;
    }
    gCursor = PARAM_CURSOR_NONE;
    if (cur == PE_SLOT && gSlotPick) {
        TakeSlot();
        gEditItem = PE_NONE;
        gWasFKeyPressed = false;
        gUpdateDisplay = true;
        gUpdateStatus  = true;
        return;
    }
    uint8_t        vfo    = cur ? gEditVfo : active;
    const uint8_t *list;
    uint8_t        n = Items(vfo, &list);
    uint8_t        i = 0;

    if (cur != PE_NONE)
        while (list[i++] != cur)
            ;
    for (;;) {
        while (i < n && Skip(vfo, list[i]))
            i++;
        if (i < n) {
            gEditVfo  = vfo;
            gEditItem = list[i];
            break;
        }
        if (vfo != active || !SERVICE_GetAbView()) {
            gEditItem = PE_NONE;
            break;
        }
        vfo = !active;
        n   = Items(vfo, &list);
        i   = 0;
    }
    gOffsetDigits  = 0;
    gSlotPick      = 0;
    if (gEditItem == PE_NAME) {   // the name as stored, padded to its 10 characters
        SETTINGS_FetchChannelName(gName, EV->CHANNEL_SAVE);
        for (uint8_t i = (uint8_t)strlen(gName); i < 10u; i++)
            gName[i] = ' ';
        gName[10] = 0;
    }
    if (gEditItem == PE_OFFSET || gEditItem == PE_NAME)
        gWasFKeyPressed = false;   // digits type the offset / name (not F + digit)
    gUpdateDisplay = true;
    gUpdateStatus  = true;   // (BAND inverts the status-bar icon in single view)
}

// F held: the previous parameter (or character) - found by walking F's own order from
// the start, so it skips what F skips. The first one: stays.
void PARAM_Prev(void)
{
    const uint8_t cur = PARAM_Current(), vfo = gEditVfo;
    if (cur == PE_NONE)
        return;
    if (gCursor != PARAM_CURSOR_NONE && gCursor) {   // a name / groups character back
        gCursor--;
        gUpdateDisplay = true;
        return;
    }
    uint8_t pi = PE_NONE, pv = vfo;
    gEditItem = PE_NONE;
    gCursor   = PARAM_CURSOR_NONE;
    for (uint8_t n = 0; n < 32u; n++) {
        PARAM_Next();
        if (gEditItem == PE_NONE || (gEditItem == cur && gEditVfo == vfo))
            break;
        pi = gEditItem;
        pv = gEditVfo;
    }
    gEditItem = PE_NONE;   // forward again to the one before (as F would arrive at it)
    for (uint8_t n = 0; n < 32u; n++) {
        PARAM_Next();
        if (gEditItem == PE_NONE || (gEditItem == (pi ? pi : cur) && gEditVfo == (pi ? pv : vfo)))
            break;
    }
}

void PARAM_Cancel(void)
{
    if (gEditItem != PE_NONE)
        gUpdateDisplay = true;
    gEditItem = PE_NONE;
    gCursor   = PARAM_CURSOR_NONE;
    gSlotPick = 0;   // (EXIT: no slot taken)
}

// ---------------------------------------------------------------------------
// The memory slot, the name, the groups.

static bool SlotOk(uint8_t idx, bool memory)
{
    return idx == 0 || (idx == PARAM_SLOT_DELETE && memory) ||
           (idx <= MR_CHANNEL_LAST + 1u && !RADIO_CheckValidChannel(idx - 1u, false, 0));
}

static void StepSlot(int8_t direction)
{
    const bool memory = IS_MR_CHANNEL(EV->CHANNEL_SAVE);
    uint8_t    idx    = gSlotPick;
    do
        idx = (uint8_t)((idx + PARAM_SLOT_DELETE + 1u + direction) % (PARAM_SLOT_DELETE + 1u));
    while (!SlotOk(idx, memory));
    gSlotPick = idx;
}

// The slot picked: the VFO stored in a free memory, a memory moved there (name and
// groups with it), or deleted; the VFO then shows it (deleted: the next one, none: VFO).
static void TakeSlot(void)
{
    VFO_Info_t    *v      = EV;
    const uint8_t  vfo    = gEditVfo;
    const uint16_t own    = v->CHANNEL_SAVE;
    const bool     memory = IS_MR_CHANNEL(own);
    uint16_t       to = 0xFFFF;
    char           name[17];
    gRequestSaveChannel = 0;   // (written below; a pending save would bring the old one back)
    SETTINGS_FetchChannelName(name, own);   // (a VFO: none)
    if (gSlotPick != PARAM_SLOT_DELETE) {
        to = gSlotPick - 1u;
        SETTINGS_SaveChannel(to, vfo, v, 2);
        SETTINGS_SaveChannelName(to, name);
    }
    if (memory)   // moved / deleted: the old one goes
        SETTINGS_UpdateChannel(own, NULL, false, false, true);
    if (gSlotPick == PARAM_SLOT_DELETE)
        to = RADIO_FindNextChannel(own, 1, false, 0);
    gSlotPick = 0;
    if (to == 0xFFFF)
        to = gEeprom.FreqChannel[vfo];   // no memories left: the VFO
    else
        gEeprom.MrChannel[vfo] = (uint8_t)to;
    gEeprom.ScreenChannel[vfo] = to;
    gRequestSaveVFO   = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
    gFlagResetVfos    = true;
    gBeepToPlay       = BEEP_1KHZ_60MS_OPTIONAL;
}

static void SaveName(void)
{
    char s[11];
    memcpy(s, gName, sizeof(s));
    for (int8_t i = 9; i >= 0 && s[i] == ' '; i--)
        s[i] = 0;   // (trailing spaces off)
    SETTINGS_SaveChannelName(EV->CHANNEL_SAVE, s);
    gUpdateDisplay = true;
}

char PARAM_StepChar(char c, int8_t direction)
{
    static const char set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.+";
    const char   *p = strchr(set, c);
    const uint8_t n = sizeof(set) - 1u;
    const uint8_t i = p && *p ? (uint8_t)(p - set) : 0u;
    return set[(i + n + direction) % n];
}

static void StepName(int8_t direction)
{
    if (gCursor == PARAM_CURSOR_NONE)
        gCursor = 0;   // editing: from the first character
    gName[gCursor] = PARAM_StepChar(gName[gCursor], direction);
    SaveName();
}

static void StepGroups(void)
{
    VFO_Info_t *v = EV;
    if (gCursor == PARAM_CURSOR_NONE)
        gCursor = 0;
    switch (gCursor) {
        case 0:  v->SCANLIST1_PARTICIPATION ^= 1u; break;
        case 1:  v->SCANLIST2_PARTICIPATION ^= 1u; break;
        case 2:  v->SCANLIST3_PARTICIPATION ^= 1u; break;
        default: v->SvcFlags ^= VFO_F_CALL;        break;
    }
}

// ---------------------------------------------------------------------------

// OFF, then all CTCSS, then DCS normal, then DCS inverted.
#define TONE_COUNT (1u + ARRAY_SIZE(CTCSS_Options) + 2u * DCS_OPTION_COUNT)

static void StepTone(FREQ_Config_t *config, int8_t direction)
{
    uint16_t idx;
    switch (config->CodeType) {
        case CODE_TYPE_CONTINUOUS_TONE: idx = 1u + config->Code; break;
        case CODE_TYPE_DIGITAL:         idx = 1u + ARRAY_SIZE(CTCSS_Options) + config->Code; break;
        case CODE_TYPE_REVERSE_DIGITAL: idx = 1u + ARRAY_SIZE(CTCSS_Options) + DCS_OPTION_COUNT + config->Code; break;
        default:                        idx = 0; break;
    }

    idx = (uint16_t)((idx + TONE_COUNT + direction) % TONE_COUNT);

    if (idx == 0) {
        config->CodeType = CODE_TYPE_OFF;
        config->Code     = 0;
    } else if (idx <= ARRAY_SIZE(CTCSS_Options)) {
        config->CodeType = CODE_TYPE_CONTINUOUS_TONE;
        config->Code     = idx - 1u;
    } else if (idx <= ARRAY_SIZE(CTCSS_Options) + DCS_OPTION_COUNT) {
        config->CodeType = CODE_TYPE_DIGITAL;
        config->Code     = idx - 1u - ARRAY_SIZE(CTCSS_Options);
    } else {
        config->CodeType = CODE_TYPE_REVERSE_DIGITAL;
        config->Code     = idx - 1u - ARRAY_SIZE(CTCSS_Options) - DCS_OPTION_COUNT;
    }
}

// NFM, FM (wide), AM, USB, and broadcast FM ("WFM", as CHIRP names them) in the broadcast band.
static void StepMode(int8_t direction)
{
    VFO_Info_t *v = EV;
    uint8_t     m;
    switch (v->Modulation) {   // FM (wide), NFM, N+ (the narrower filter), AM, USB, WFM
        case MODULATION_AM:  m = 3; break;
        case MODULATION_USB: m = 4; break;
#ifdef ENABLE_FMRADIO
        case MODULATION_BFM: m = 5; break;
#endif
        default:             m = v->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE ? 0 : NARROWER_ON(v) ? 2 : 1; break;
    }

    uint8_t n = 5;
#ifdef ENABLE_FMRADIO
    if (FM_IS_BROADCAST(v->freq_config_RX.Frequency))
        n = 6;
#endif
    m = (uint8_t)((m + n + direction) % n);

    static const uint8_t mods[] = { MODULATION_FM, MODULATION_FM, MODULATION_FM, MODULATION_AM, MODULATION_USB,
#ifdef ENABLE_FMRADIO
                                    MODULATION_BFM
#endif
    };
    v->Modulation = mods[m];
    if (m < 3)
        v->CHANNEL_BANDWIDTH = m ? BK4819_FILTER_BW_NARROW : BK4819_FILTER_BW_WIDE;
    v->SvcFlags = (uint8_t)((v->SvcFlags & ~VFO_F_NARROWER) | (m == 2 ? VFO_F_NARROWER : 0u));
}

// Repeater offset sizes in 10 Hz units: 600 kHz, 1, 1.6, 5, 7.6 MHz.
static const uint32_t Offsets[] = { 60000, 100000, 160000, 500000, 760000 };

static void StepOffset(int8_t direction)
{
    const uint8_t  n   = ARRAY_SIZE(Offsets);
    const uint32_t cur = EV->TX_OFFSET_FREQUENCY;
    uint8_t        i   = 0;
    while (i < n && Offsets[i] < cur)   // the first size >= the current one
        i++;
    if (i < n && Offsets[i] == cur)
        i = (uint8_t)((i + n + direction) % n);
    else if (direction < 0)             // a custom size (from the menu): the one below
        i = (uint8_t)((i + n - 1u) % n);
    else if (i == n)
        i = 0;
    EV->TX_OFFSET_FREQUENCY = Offsets[i];
}

// After a change: saved (memories in full, mode 2, keeping the name) and the VFO
// reconfigured; the selected VFO through the main loop, the other one here.
static void Apply(VFO_Info_t *v, uint8_t vfo)
{
    const uint8_t mode = IS_MR_CHANNEL(v->CHANNEL_SAVE) ? 2u : 1u;
    RADIO_ApplyOffset(v);
    RADIO_ConfigureSquelchAndOutputPower(v);
    if (vfo == gEeprom.TX_VFO) {
        gRequestSaveChannel = mode;
    } else {
        SETTINGS_SaveChannel(v->CHANNEL_SAVE, vfo, v, mode);
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE);
        gFlagReconfigureVfos = true;
    }
    gUpdateDisplay = true;
}

void PARAM_Step(int8_t direction)
{
    VFO_Info_t   *v   = EV;
    const uint8_t vfo = gEditVfo;

    switch (PARAM_Current()) {
        case PE_BAND:   // the next / previous ham band
            SERVICE_StepHamBand(vfo, direction);
            gUpdateStatus  = true;
            gUpdateDisplay = true;
            return;
        case PE_SQUELCH:
            SERVICE_StepSquelch(vfo, direction);
            gUpdateDisplay = true;
            return;
#ifdef ENABLE_FMRADIO
        case PE_VOLUME:
        case PE_STEREO: {   // the FM chip's audio, for every station
            uint8_t vol = SERVICE_FmVolume();
            bool    st  = SERVICE_FmStereo();
            if (PARAM_Current() == PE_VOLUME) {
                vol = (uint8_t)(vol + direction);
                if (vol > 15u)
                    vol = direction > 0 ? 15u : 0u;
            } else {
                st = !st;
            }
            SERVICE_SetFmAudio(vol, st);
            FM_ApplyAudio();
            gUpdateDisplay = true;
            return;
        }
#endif
        case PE_TUNE:   // the other VFO (the active one tunes the usual way)
            COMMON_StepChannel(vfo, direction);
            gUpdateDisplay = true;
            return;
        case PE_CODE:   // privacy codes are both ways
            SUBCODE_Step(v->CHANNEL_SAVE, direction, &v->freq_config_RX.CodeType, &v->freq_config_RX.Code);
            v->freq_config_TX.CodeType = v->freq_config_RX.CodeType;
            v->freq_config_TX.Code     = v->freq_config_RX.Code;
            break;
        case PE_MODE:
            StepMode(direction);
            break;
        case PE_SLOT:
            StepSlot(direction);
            gUpdateDisplay = true;
            return;
        case PE_NAME:
            StepName(direction);
            return;
        case PE_GROUPS:
            StepGroups();
            break;
        case PE_COMPAND:
            v->Compander = (uint8_t)((v->Compander + 4u + direction) % 4u);
            break;
        case PE_SUBCODE:   // the brands, wrapping (every walkie channel's numbering)
            SERVICE_SetSubCodeProfile((uint8_t)((SERVICE_GetSubCodeProfile() + SUBCODE_BRAND_COUNT + direction) % SUBCODE_BRAND_COUNT));
            break;
        case PE_POWER: {
            if (IS_SVC_CHANNEL(v->CHANNEL_SAVE) && SERVICE_OfChannel(v->CHANNEL_SAVE) != SERVICE_HAM) {   // the plan's levels (GMRS: also FRS / GMRS)
                uint8_t p = v->OUTPUT_POWER, prev = p;
                if (direction > 0) {
                    p = SERVICE_NextPower(v->CHANNEL_SAVE, p);
                } else {
                    for (uint8_t i = 0; i < 8u && (p = SERVICE_NextPower(v->CHANNEL_SAVE, p)) != v->OUTPUT_POWER; i++)
                        prev = p;   // the level before the current one
                    p = prev;
                }
                v->OUTPUT_POWER = p;
                break;
            }
            // OFF, USER (the SetPwr level), LOW1-5, MID, HIGH
            uint8_t i = v->TX_OFF ? 0u : (uint8_t)(v->OUTPUT_POWER + 1u);
            i = (uint8_t)((i + 9u + direction) % 9u);
            if (v->TX_OFF && i && !IS_MR_CHANNEL(v->CHANNEL_SAVE) && SERVICE_OutOfSpec(v->freq_config_RX.Frequency)) {   // (ham channels, VFOs)
                static const char * const lines[] = {   // outside the radio's design: accept first
                    "OUTSIDE THIS RADIO'S TX DESIGN", "(136-174, 400-470 MHZ): WEAK,", "SPURIOUS OUTPUT. TX THERE MAY",
                    "BE ILLEGAL AND CAN DAMAGE IT.", "ENABLE TX HERE?",
                };
                gHamPending = i;
                CONFIRM_Open(lines, HamAccept, NULL);
                return;
            }
            v->TX_OFF = i == 0;
            if (i)
                v->OUTPUT_POWER = i - 1u;
            break;
        }
        case PE_RXTONE:
            StepTone(&v->freq_config_RX, direction);
            break;
        case PE_TXTONE:
            StepTone(&v->freq_config_TX, direction);
            break;
        case PE_SHIFT:
            v->TX_OFFSET_FREQUENCY_DIRECTION = (uint8_t)((v->TX_OFFSET_FREQUENCY_DIRECTION + 3u + direction) % 3u);
            if (v->TX_OFFSET_FREQUENCY == 0)
                v->TX_OFFSET_FREQUENCY = Offsets[0];
            break;
        case PE_OFFSET:
            StepOffset(direction);
            break;
        case PE_STEP: {
            const int8_t idx = (int8_t)FREQUENCY_GetSortedIdxFromStepIdx(v->STEP_SETTING) + direction;
            if (idx < 0 || idx >= (int8_t)STEP_N_ELEM)
                return;
            v->STEP_SETTING  = FREQUENCY_GetStepIdxFromSortedIdx(idx);
            v->StepFrequency = gStepFrequencyTable[v->STEP_SETTING];
            break;
        }
        default:
            return;
    }

    Apply(v, vfo);
}

// Typing digits with OFFSET selected: the offset in kHz (600 = 0.6 MHz, 291810 =
// 291.810 MHz); six digits, then the next one starts over.
void PARAM_Digit(uint8_t digit)
{
    VFO_Info_t *v = EV;
    if (PARAM_Current() == PE_NAME) {   // the digit, then the next character
        if (gCursor == PARAM_CURSOR_NONE)
            gCursor = 0;
        gName[gCursor] = (char)('0' + digit);
        SaveName();
        PARAM_Next();
        return;
    }
    if (gOffsetDigits >= 6u)
        gOffsetDigits = 0;
    const uint32_t khz = (gOffsetDigits++ ? v->TX_OFFSET_FREQUENCY / 100u : 0u) * 10u + digit;
    v->TX_OFFSET_FREQUENCY = khz * 100u;
    Apply(v, gEditVfo);
}

#endif
