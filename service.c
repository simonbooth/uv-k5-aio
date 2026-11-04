/* Service modes: per-VFO HAM / GMRS / MARINE state, persistence and VFO setup
 * for the built-in plans in svc_plans.c. See service.h.
 */

#include <string.h>

#include "service.h"
#include "dcs.h"
#include "external/printf/printf.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "frequencies.h"
#include "misc.h"
#include "settings.h"
#include "svc_plans.h"
#include "app/fm.h"
#include "functions.h"
#include "app/chFrScanner.h"
#include "app/app.h"

typedef struct {
    uint32_t rx;
    uint32_t txOffset;
    uint8_t  txDirection;
    uint8_t  flags;
} ServiceChannelInfo_t;

static uint8_t gMarinePlan;

static void SERVICE_GetInfo(uint16_t channel, ServiceChannelInfo_t *info)
{
    PlanChannelInfo_t plan;

    if (channel <= SVC_GMRS_LAST)   // (INTL: PMR446)
        (SERVICE_IsPmr() ? PLAN_PmrInfo : PLAN_GmrsInfo)(channel - SVC_GMRS_FIRST, &plan);
    else if (channel <= SVC_MARINE_LAST)
        PLAN_MarineInfo(gMarinePlan, channel - SVC_MARINE_FIRST, &plan);
    else if (channel <= SVC_AIR_LAST)
        PLAN_AirInfo(channel - SVC_AIR_FIRST, &plan);
    else
        PLAN_HamInfo(SERVICE_GetHamRegion(), channel - SVC_HAM_FIRST, &plan);

    info->rx          = plan.rx;
    info->flags       = plan.flags;
    info->txOffset    = (uint32_t)(plan.txShift < 0 ? -plan.txShift : plan.txShift);
    info->txDirection = plan.txShift > 0 ? TX_OFFSET_FREQUENCY_DIRECTION_ADD
                      : plan.txShift < 0 ? TX_OFFSET_FREQUENCY_DIRECTION_SUB
                      :                    TX_OFFSET_FREQUENCY_DIRECTION_OFF;
}

// ---------------------------------------------------------------------------
// Persistent state: our own EEPROM area (see settings.h), outside the range
// CHIRP uploads. Writes are read-modify-write of the EEPROM's 8-byte blocks.
// ---------------------------------------------------------------------------

#define SVC_STATE_ADDR  EEPROM_SERVICE_STATE                                 // 16 bytes
#define SVC_TONES_ADDR  (SVC_STATE_ADDR + 0x10u)                             // GMRS: 30 x SvcTones_t
#define SVC_GPOWER_ADDR (SVC_TONES_ADDR + PLAN_GMRS_SAVED * 4u)               // GMRS: 30 x 4-bit power
#define SVC_MPOWER_ADDR (SVC_GPOWER_ADDR + (PLAN_GMRS_SAVED + 1u) / 2u + 1u)  // marine: per plan, 1 bit per channel
#define SVC_MPOWER_LEN  ((PLAN_MARINE_MAX + 7u) / 8u)
#define SVC_END_ADDR    (SVC_MPOWER_ADDR + MARINE_PLAN_COUNT * SVC_MPOWER_LEN)

#define SVC_SQL_ADDR    SVC_END_ADDR                                         // squelch level per VFO, 2 bytes
#define SVC_HAMBAND_ADDR (SVC_SQL_ADDR + 2u)                                 // (once: last frequency per ham band)
#define SVC_LIC_ADDR    SVC_HAMBAND_ADDR                                     // licences (CHIRP): SvcLicence_t

#define SVC_MURS_ADDR   (SVC_HAMBAND_ADDR + PLAN_HAM_BANDS * 4u)              // MURS: 5 x SvcTones_t, 5 x 4-bit power
#define SVC_SLOTS_ADDR  (SVC_MURS_ADDR + (PLAN_GMRS_COUNT - PLAN_GMRS_SAVED) * 4u + 3u)   // VFO slots switched on, bit per slot
// (SVC_HAMBAND_ADDR: the last frequency per ham band, before the VFO slots; now the licences)

#define SVC_FM_ADDR     (SVC_SLOTS_ADDR + 1u)   // broadcast FM: volume (bits 0-3), stereo (bit 4); erased: 15, stereo

// This firmware set the radio up (a reset to our defaults): INSTALLED. PENDING: a first
// run was seen (another firmware's settings) and the reset screen not answered yet.
#define SVC_INSTALL_ADDR (SVC_FM_ADDR + 1u)
#define SVC_INSTALLED    0xA5u
#define SVC_PENDING      0x5Au

#define SVC_PUBID_ADDR   (SVC_INSTALL_ADDR + 1u)   // the public licence ID (CHIRP): GMRS callsign / Ofcom number, 10

#define SVC_HAMPWR_ADDR  (SVC_PUBID_ADDR + 10u)   // ham channels' TX power per band: 5 x 4 bits (8: OFF; erased: default)

_Static_assert(SVC_HAMPWR_ADDR + (PLAN_HAM_BANDS + 1u) / 2u <= EEPROM_OWN_END, "service state outgrew its EEPROM area");

// GMRS tones / power slots: 1-22, 15R-22R in the first area, MURS after the ham bands.
static uint16_t GmrsTonesAddr(uint8_t idx)
{
    return idx < PLAN_GMRS_SAVED ? SVC_TONES_ADDR + idx * 4u : SVC_MURS_ADDR + (idx - PLAN_GMRS_SAVED) * 4u;
}
static uint16_t GmrsPowerAddr(uint8_t idx)
{
    return idx < PLAN_GMRS_SAVED ? SVC_GPOWER_ADDR + idx / 2u
                                 : SVC_MURS_ADDR + (PLAN_GMRS_COUNT - PLAN_GMRS_SAVED) * 4u + (idx - PLAN_GMRS_SAVED) / 2u;
}

#define SVC_MAGIC       0x5Du   // 0x5D: AIR added (state layout changed)

typedef struct {
    uint8_t magic;
    uint8_t profile;
    uint8_t group[2];                     // [vfo] memory group (VFO/MR): 1-3 = scan list 1-3, 0 = all
    uint8_t channel[2][SERVICE_COUNT];    // [vfo][service] table index (HAM: its built-in channels)
    uint8_t marinePlan;                   // MarinePlan_t
    uint8_t reserved[1];                  // [0] 0xA5: side-key defaults taken
    uint8_t codeDetect : 4;               // T Live: CODE_DETECT_*
    uint8_t hamRegion  : 4;               // HamRgn: HamRegion_t
    uint8_t txAny      : 1;               // TX Any: the TX blocks lifted
    uint8_t abView     : 1;               // F+2: the A / B view
    uint8_t watch      : 2;               // Watch: WATCH_*
    uint8_t            : 4;               // (2 bits were WchHld: ScnRev holds for both now)
} SvcState_t;

_Static_assert(sizeof(SvcState_t) == 0x10, "SvcState_t must fill its 16-byte slot");

typedef struct {
    uint8_t rxType, rxCode, txType, txCode;
} SvcTones_t;

static SvcState_t gSvc;
static SvcState_t gSvcSaved;
static uint8_t    gSquelch[2];   // per VFO, 0-9
static uint8_t    gSlotMask;   // VFO slots switched on (CHIRP), bit per slot; erased: all
static uint8_t    gFmAudio;    // SVC_FM_ADDR

static void EepromWrite(uint16_t addr, const void *src, uint8_t len)
{
    const uint8_t *s = src;
    while (len) {
        const uint16_t block = addr & ~7u;
        const uint8_t  off   = (uint8_t)(addr - block);
        const uint8_t  n     = (len < 8u - off) ? len : (uint8_t)(8u - off);
        uint8_t        buf[8];

        EEPROM_ReadBuffer(block, buf, 8);
        if (memcmp(buf + off, s, n) != 0) {
            memcpy(buf + off, s, n);
            EEPROM_WriteBuffer(block, buf);
        }
        addr += n;
        s    += n;
        len  -= n;
    }
}

// The walkie-talkie mode's channels: US FRS / GMRS (+ repeaters) / MURS, Canada 1-22,
// INTL PMR446, UK PMR446 + Simple UK.
static uint8_t GmrsCount(void)
{
    static const uint8_t count[PLAN_REGION_COUNT] = { PLAN_GMRS_COUNT, PLAN_GMRS_SIMPLEX, PLAN_PMR_COUNT, PLAN_INTL_COUNT };
    return count[SERVICE_GetRegion()];
}

static uint8_t ServiceCount(Service_t service)
{
    return service == SERVICE_GMRS ? GmrsCount()
         : service == SERVICE_AIR  ? PLAN_AIR_COUNT
         : service == SERVICE_HAM  ? PLAN_HAM_COUNT : PLAN_MarineCount(gMarinePlan);
}

static uint16_t ServiceFirst(Service_t service)
{
    return service == SERVICE_GMRS ? SVC_GMRS_FIRST
         : service == SERVICE_AIR  ? SVC_AIR_FIRST
         : service == SERVICE_HAM  ? SVC_HAM_FIRST : SVC_MARINE_FIRST;
}

static bool gFirstRun;   // another firmware's settings: the reset screen asks (SVC_PENDING)

// ---------------------------------------------------------------------------
// Licences (CHIRP writes them; the old per-band frequencies' area, so a magic)

#define LIC_MAGIC 0xC5u
typedef struct {
    uint8_t magic;     // LIC_MAGIC once CHIRP set them, else the region's defaults
    uint8_t flags[2];  // LIC_*, little-endian
    char    ham[8];    // the ham callsign: space / 0 / 0xFF padded (GMRS / Ofcom: SVC_PUBID_ADDR)
} SvcLicence_t;
_Static_assert(sizeof(SvcLicence_t) <= PLAN_HAM_BANDS * 4u, "licences outgrew their area");

static SvcLicence_t gLic;

uint16_t SERVICE_Licences(void)
{
    // the licence-free ones, and ham to listen to (its callsign, unset, keeps it receive
    // only; GMRS off: its repeaters only for a licensed user): US, Canada, INTL, UK
    static const uint16_t region[PLAN_REGION_COUNT] = {
        LIC_HAM | LIC_FRS | LIC_MURS | LIC_MARINE_RX | LIC_AIR,
        LIC_HAM | LIC_FRS | LIC_MARINE_RX | LIC_AIR,
        LIC_HAM | LIC_PMR446 | LIC_MARINE_RX | LIC_AIR,
        LIC_HAM | LIC_PMR446 | LIC_MARINE_RX | LIC_AIR,
    };
    return gLic.magic == LIC_MAGIC ? (uint16_t)(gLic.flags[0] | gLic.flags[1] << 8) : region[SERVICE_GetRegion()];
}

// HAM: the ham callsign; else the public licence ID (GMRS callsign, Ofcom licence number).
bool SERVICE_Callsign(Service_t service, char *out)
{
    char        id[10];
    const char *c = gLic.ham;
    uint8_t     n = 0, len = 8;
    if (service != SERVICE_HAM) {
        EEPROM_ReadBuffer(SVC_PUBID_ADDR, id, sizeof(id));
        c   = id;
        len = 10;
    }
    while (gLic.magic == LIC_MAGIC && n < len && c[n] > ' ' && c[n] < 0x7F) {
        out[n] = c[n];
        n++;
    }
    out[n] = 0;
    return n != 0;
}

static bool LicTx(Service_t service, uint16_t lic)   // ticked, and its callsign set
{
    char c[11];
    return (SERVICE_Licences() & lic) && SERVICE_Callsign(service, c);
}

// A plan channel's licence: what shows it (any), what lets it transmit.
static uint16_t ChannelLicence(uint16_t channel, bool tx)
{
    switch (SERVICE_OfChannel(channel)) {
        case SERVICE_GMRS: {
            if (SERVICE_IsPmr())
                return (uint16_t)(channel - SVC_GMRS_FIRST) < PLAN_PMR_COUNT ? LIC_PMR446 : LIC_UK_SIMPLE;
            ServiceChannelInfo_t info;
            SERVICE_GetInfo(channel, &info);
            return (info.flags & PLAN_F_MURS) ? LIC_MURS : (info.flags & PLAN_F_DUPLEX) ? LIC_GMRS : (LIC_FRS | LIC_GMRS);
        }
        case SERVICE_MARINE: return tx ? LIC_MARINE_TX : LIC_MARINE_TX | LIC_MARINE_RX;
        case SERVICE_AIR:    return LIC_AIR;
        default:             return LIC_HAM;
    }
}

bool SERVICE_GmrsLicensed(void) { return LicTx(SERVICE_GMRS, LIC_GMRS); }

bool SERVICE_ChannelVisible(uint16_t channel)
{
    return !IS_SVC_CHANNEL(channel) || (SERVICE_Licences() & ChannelLicence(channel, false));
}

bool SERVICE_Enabled(uint8_t mode)
{
    if (mode == SERVICE_HAM || mode >= SERVICE_COUNT)
        return true;   // (VFOs, memories; broadcast FM)
    for (uint16_t ch = ServiceFirst((Service_t)mode); ch < ServiceFirst((Service_t)mode) + ServiceCount((Service_t)mode); ch++)
        if (SERVICE_ChannelVisible(ch))
            return true;
    return false;
}

// TX under the licences (TX Any aside): GMRS with its callsign covers FRS, and is
// what repeaters / more than 2 W need; ham with its callsign.
static bool ChannelLicensedTx(uint16_t channel)
{
    const uint16_t lic = ChannelLicence(channel, true);
    if (lic == LIC_HAM)
        return LicTx(SERVICE_HAM, LIC_HAM);
    if (lic & LIC_GMRS)
        return (SERVICE_Licences() & (lic & LIC_FRS)) || LicTx(SERVICE_GMRS, LIC_GMRS);
    return (SERVICE_Licences() & lic) && lic != LIC_AIR;
}

// Set on the radio (SysInf) as CHIRP does: the first time from the region's defaults.
static void LicSave(void)
{
    if (gLic.magic != LIC_MAGIC) {
        const uint16_t f = SERVICE_Licences();
        char           sp[10];
        memset(sp, ' ', sizeof(sp));
        memset(gLic.ham, ' ', sizeof(gLic.ham));
        EepromWrite(SVC_PUBID_ADDR, sp, sizeof(sp));
        gLic.flags[0] = (uint8_t)f;
        gLic.flags[1] = (uint8_t)(f >> 8);
        gLic.magic    = LIC_MAGIC;
    }
    EepromWrite(SVC_LIC_ADDR, &gLic, sizeof(gLic));
}

void SERVICE_SetLicence(uint16_t lic, bool on)
{
    LicSave();
    const uint16_t f = on ? SERVICE_Licences() | lic : SERVICE_Licences() & ~lic;
    gLic.flags[0] = (uint8_t)f;
    gLic.flags[1] = (uint8_t)(f >> 8);
    LicSave();
}

void SERVICE_SetCallsign(Service_t service, const char *text)
{
    LicSave();
    if (service == SERVICE_HAM)
        memcpy(gLic.ham, text, sizeof(gLic.ham)), LicSave();
    else
        EepromWrite(SVC_PUBID_ADDR, text, 10);
}

// A reset keeping the licences: saved before the erase, written back after it.
void SERVICE_LicencesKeep(bool restore)
{
    static uint8_t save[sizeof(SvcLicence_t) + 10u];
    if (!restore) {
        EEPROM_ReadBuffer(SVC_LIC_ADDR, save, sizeof(SvcLicence_t));
        EEPROM_ReadBuffer(SVC_PUBID_ADDR, save + sizeof(SvcLicence_t), 10);
        return;
    }
    EepromWrite(SVC_LIC_ADDR, save, sizeof(SvcLicence_t));
    EepromWrite(SVC_PUBID_ADDR, save + sizeof(SvcLicence_t), 10);
}

bool SERVICE_TxCallsign(char *out)
{
    const uint8_t     vfo = gEeprom.TX_VFO;
    const VFO_Info_t *v   = &gEeprom.VfoInfo[vfo];
    const uint8_t     m   = SERVICE_ModeOf(vfo);
    if (!SERVICE_CanTx(v) || (m != SERVICE_HAM && m != SERVICE_GMRS))
        return false;
    if (m == SERVICE_GMRS) {   // a GMRS channel (not MURS / PMR446 / Simple UK)
        const uint16_t ch = IS_SVC_CHANNEL(v->CHANNEL_SAVE) ? v->CHANNEL_SAVE : SERVICE_PlanChannelOf(v);
        if (ch == 0xFFFF || !(ChannelLicence(ch, true) & LIC_GMRS))
            return false;
    }
    return LicTx((Service_t)m, m == SERVICE_HAM ? LIC_HAM : LIC_GMRS) && SERVICE_Callsign((Service_t)m, out);
}

bool SERVICE_IsFirstRun(void) { return gFirstRun; }

void SERVICE_SetInstalled(void)
{
    const uint8_t m = SVC_INSTALLED;
    EepromWrite(SVC_INSTALL_ADDR, &m, 1);
    gFirstRun = false;
}

void SERVICE_Init(void)
{
    EEPROM_ReadBuffer(SVC_STATE_ADDR, &gSvc, sizeof(gSvc));
    EEPROM_ReadBuffer(SVC_LIC_ADDR, &gLic, sizeof(gLic));

    // First run: no marker and none of our state (an earlier build of ours had the
    // state but no marker: it is ours, marked so). Pending until the reset screen answers.
    uint8_t m;
    EEPROM_ReadBuffer(SVC_INSTALL_ADDR, &m, 1);
    if (m != SVC_INSTALLED) {
        gFirstRun = m == SVC_PENDING || gSvc.magic != SVC_MAGIC;
        m         = gFirstRun ? SVC_PENDING : SVC_INSTALLED;
        EepromWrite(SVC_INSTALL_ADDR, &m, 1);
    }

    if (gSvc.magic != SVC_MAGIC) {
        memset(&gSvc, 0, sizeof(gSvc));
        gSvc.magic = SVC_MAGIC;
        gSvc.channel[0][SERVICE_MARINE] = gSvc.channel[1][SERVICE_MARINE] = PLAN_MarineFind(MARINE_PLAN_US, "16");
        gSvc.channel[0][SERVICE_AIR] = 2;   // A: 122.750 air-to-air
        gSvc.channel[1][SERVICE_AIR] = 0;   // B: 121.500 guard
    }

    if (gSvc.profile >= SUBCODE_BRAND_COUNT)
        gSvc.profile = SUBCODE_MOTO;
    if (gSvc.hamRegion >= PLAN_REGION_COUNT)
        gSvc.hamRegion = HAM_REGION_US;
    gSvc.marinePlan = SERVICE_GetHamRegion();   // one region for ham and marine (the same order)
    gMarinePlan     = gSvc.marinePlan;

    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        if (gSvc.group[vfo] > 3u)   // (was lastService)
            gSvc.group[vfo] = 0;
        for (uint8_t s = SERVICE_GMRS; s < SERVICE_COUNT; s++)
            if (gSvc.channel[vfo][s] >= ServiceCount(s))
                gSvc.channel[vfo][s] = 0;
        if (gSvc.channel[vfo][SERVICE_HAM] >= PLAN_HAM_COUNT)
            gSvc.channel[vfo][SERVICE_HAM] = 0;
    }

    gSvcSaved = gSvc;

    EEPROM_ReadBuffer(SVC_SLOTS_ADDR, &gSlotMask, 1);
    EEPROM_ReadBuffer(SVC_FM_ADDR, &gFmAudio, 1);
#ifdef ENABLE_FMRADIO
    FM_ApplyAudio();
#endif
    EEPROM_ReadBuffer(SVC_SQL_ADDR, gSquelch, sizeof(gSquelch));
    for (uint8_t vfo = 0; vfo < 2; vfo++)
        if (gSquelch[vfo] > 9u)   // erased: the old one-for-both level
            gSquelch[vfo] = gEeprom.SQUELCH_LEVEL;
}

uint8_t SERVICE_GetSquelch(uint8_t vfo)
{
    return gSquelch[vfo & 1u];
}

// ---------------------------------------------------------------------------
// VFO slots (PLAN_Slot*): the only way a VFO changes band is into that band's slot,
// with its own saved state; switched-off slots (CHIRP) are left out, all of them
// back on with TX Any.
// ---------------------------------------------------------------------------
uint8_t SERVICE_FmVolume(void) { return gFmAudio & 0x0Fu; }
bool    SERVICE_FmStereo(void) { return gFmAudio & 0x10u; }

void SERVICE_SetFmAudio(uint8_t volume, bool stereo)
{
    gFmAudio = (uint8_t)(0xE0u | (volume & 0x0Fu) | (stereo ? 0x10u : 0u));
    EepromWrite(SVC_FM_ADDR, &gFmAudio, 1);
}

uint8_t SERVICE_SlotOf(uint32_t frequency)
{
    return PLAN_SlotOf(SERVICE_GetHamRegion(), frequency);
}

bool SERVICE_SlotEnabled(uint8_t slot)
{
    uint32_t lo, hi;
    return (slot == SLOT_GEN || PLAN_SlotRange(SERVICE_GetHamRegion(), slot, &lo, &hi)) &&
           (gSvc.txAny || (gSlotMask >> slot & 1u));
}

// A slot from scratch: its band's default (ham: the calling frequency; FM: 100.0 on
// broadcast FM; air: 121.5 AM); one the region doesn't have: the general slot.
void SERVICE_InitSlotVfo(uint8_t vfo, uint16_t channel)
{
    const uint8_t  region = SERVICE_GetHamRegion();
    const uint32_t f      = PLAN_SlotDefault(region, channel - FREQ_CHANNEL_FIRST);
    VFO_Info_t    *v      = &gEeprom.VfoInfo[vfo];

    channel = FREQ_CHANNEL_FIRST + PLAN_SlotOf(region, f);
    gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo] = channel;
    RADIO_InitInfo(v, channel, f);   // (AM in the airband)
    v->TX_OFF = SERVICE_OutOfSpec(f) && SERVICE_ModeOf(vfo) == SERVICE_HAM;   // (10 m / 6 m / 1.25 m: until set, with the warning)
#ifdef ENABLE_FMRADIO
    if (channel == FREQ_CHANNEL_FIRST + SLOT_FM) {
        v->Modulation        = MODULATION_BFM;
        v->StepFrequency     = 10000;
        v->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
    }
#endif
}

bool SERVICE_TuneVfo(uint8_t vfo, uint32_t frequency)
{
    const uint8_t slot = SERVICE_SlotOf(frequency);
    if (!SERVICE_SlotEnabled(slot))
        return false;
    VFO_Info_t *v = &gEeprom.VfoInfo[vfo];
    if (gEeprom.ScreenChannel[vfo] != FREQ_CHANNEL_FIRST + slot) {
        gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo] = FREQ_CHANNEL_FIRST + slot;
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);   // that band's saved state
        gRequestSaveVFO = true;
    }
    v->freq_config_RX.Frequency = frequency;
    v->Band                     = FREQUENCY_GetBand(frequency);
    return true;
}

uint32_t SERVICE_NextVfoFrequency(VFO_Info_t *v, int8_t direction)
{
    const uint8_t region = SERVICE_GetHamRegion();
    uint32_t      lo, hi;
    if (PLAN_SlotRange(region, PLAN_SlotOf(region, v->freq_config_RX.Frequency), &lo, &hi))
        return APP_SetFreqByStepAndLimits(v, direction, lo, hi - 1u);   // wrapping in its band

    // general: over the whole range, across the bands that have their own slots
    const int32_t step = (int32_t)v->StepFrequency;
    int32_t       f    = (int32_t)v->freq_config_RX.Frequency;
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        f = (int32_t)FREQUENCY_RoundToStep((uint32_t)(f + direction * step), (uint32_t)step);
        if (f > (int32_t)frequencyBandTable[BAND_N_ELEM - 1].upper)
            f = (int32_t)frequencyBandTable[0].lower;
        else if (f < (int32_t)frequencyBandTable[0].lower)
            f = (int32_t)frequencyBandTable[BAND_N_ELEM - 1].upper;
        if (!PLAN_SlotRange(region, PLAN_SlotOf(region, (uint32_t)f), &lo, &hi))
            break;
        f = direction > 0 ? (int32_t)hi - step : (int32_t)lo;
    }
    return (uint32_t)f;
}

void SERVICE_StepHamBand(uint8_t vfo, int8_t direction)
{
    // the next / previous ham band switched on: its slot, where this VFO was on it
    const uint8_t region = SERVICE_GetHamRegion();
    const uint8_t n      = PLAN_HamBandCount(region);
    int8_t        b      = PLAN_HamBandOf(region, gEeprom.VfoInfo[vfo].freq_config_RX.Frequency);
    for (uint8_t i = 0; i < n; i++) {
        b = b < 0 ? (direction > 0 ? 0 : (int8_t)(n - 1u)) : (int8_t)((b + n + direction) % n);
        const uint8_t slot = PLAN_SlotOf(region, PLAN_HamBandCall(region, (uint8_t)b));
        if (!SERVICE_SlotEnabled(slot))
            continue;
        gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo] = FREQ_CHANNEL_FIRST + slot;
        gRequestSaveVFO = true;
        if (vfo == gEeprom.TX_VFO) {
            gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
        } else {
            RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
            gFlagReconfigureVfos = true;
        }
        return;
    }
}

void SERVICE_StepSquelch(uint8_t vfo, int8_t direction)
{
    const int8_t level = (int8_t)gSquelch[vfo & 1u] + direction;
    if (level < 0 || level > 9)
        return;
    gSquelch[vfo & 1u] = (uint8_t)level;
    EepromWrite(SVC_SQL_ADDR, gSquelch, sizeof(gSquelch));
    RADIO_ConfigureSquelchAndOutputPower(&gEeprom.VfoInfo[vfo & 1u]);
    gFlagReconfigureVfos = true;   // the receiver's thresholds
}

Service_t SERVICE_OfChannel(uint16_t channel)
{
    if (channel >= SVC_GMRS_FIRST && channel <= SVC_GMRS_LAST)
        return SERVICE_GMRS;
    if (channel >= SVC_MARINE_FIRST && channel <= SVC_MARINE_LAST)
        return SERVICE_MARINE;
    if (channel >= SVC_AIR_FIRST && channel <= SVC_AIR_LAST)
        return SERVICE_AIR;
    return SERVICE_HAM;   // memories, VFOs and the built-in ham channels
}

// The service a frequency belongs to (10 Hz units): the airband, marine (incl.
// weather), FRS / GMRS, else HAM. VFOs and memories (favorites) go by it.
static Service_t ModeOfFrequency(uint32_t f)
{
    if (f >= 10800000u && f < 13700000u)
        return SERVICE_AIR;
    if ((f >= 15600000u && f <= 15742500u) || (f >= 16060000u && f <= 16255000u))
        return SERVICE_MARINE;
    if (SERVICE_IsPmr() ? PLAN_IsPmrFrequency(f, GmrsCount())                          // PMR446, Simple UK
        : (f >= 46255000u && f <= 46272500u) || (f >= 46755000u && f <= 46772500u) ||   // FRS / GMRS
          (SERVICE_GetRegion() == HAM_REGION_US &&                                      // MURS (US only)
           ((f >= 15182000u && f <= 15194000u) || (f >= 15457000u && f <= 15460000u))))
        return SERVICE_GMRS;
    return SERVICE_HAM;
}

uint8_t SERVICE_ModeOf(uint8_t vfo)
{
    const uint16_t ch = gEeprom.ScreenChannel[vfo];
#ifdef ENABLE_FMRADIO   // broadcast FM, and the FM slot in any mode
    if (gEeprom.VfoInfo[vfo].Modulation == MODULATION_BFM || ch == FREQ_CHANNEL_FIRST + SLOT_FM)
        return MODE_ICON_FM;
#endif
    if (IS_SVC_CHANNEL(ch))
        return SERVICE_OfChannel(ch);
    return ModeOfFrequency(gEeprom.VfoInfo[vfo].freq_config_RX.Frequency);   // VFOs and memories
}

uint16_t SERVICE_PlanChannelOf(const VFO_Info_t *pVfo)
{
    const Service_t s = ModeOfFrequency(pVfo->freq_config_RX.Frequency);
    uint16_t        found = 0xFFFF;
    if (s == SERVICE_HAM)
        return found;
    for (uint8_t i = 0; i < ServiceCount(s); i++) {   // same RX, the same shift if there is one (15 / 15R)
        ServiceChannelInfo_t info;
        const uint16_t       ch = ServiceFirst(s) + i;
        SERVICE_GetInfo(ch, &info);
        if (info.rx != pVfo->freq_config_RX.Frequency)
            continue;
        if (info.txDirection == pVfo->TX_OFFSET_FREQUENCY_DIRECTION)
            return ch;
        if (found == 0xFFFF)
            found = ch;
    }
    return found;
}

// A memory's mode: broadcast FM by its modulation, else the service of its
// frequency (GMRS / marine / air memories are those services' favorites). BFM
// outside the broadcast band is FM, as RADIO_ConfigureChannel loads it.
static uint8_t MemoryMode(uint16_t channel)
{
    uint8_t d[12];   // RX frequency (4), offset (4), codes, types, modulation << 4 | shift
    EEPROM_ReadBuffer(channel * 16u, d, sizeof(d));
    uint32_t f;
    memcpy(&f, d, sizeof(f));
#ifdef ENABLE_FMRADIO
    if ((d[11] >> 4) == MODULATION_BFM && FM_IS_BROADCAST(f))
        return MODE_ICON_FM;
#endif
    return ModeOfFrequency(f);
}

static uint16_t FindMemory(uint16_t from, int8_t direction, uint8_t mode, bool scanList, uint8_t list)
{
    uint16_t ch = from;
    for (uint8_t i = 0; i <= MR_CHANNEL_LAST; i++) {
        ch = RADIO_FindNextChannel(ch, direction, scanList, list);
        if (ch == 0xFFFF)
            break;
        if (MemoryMode(ch) == mode)
            return ch;
        ch += direction;
    }
    return 0xFFFF;
}

uint16_t SERVICE_NextMemory(uint16_t from, int8_t direction, uint8_t mode, uint8_t group)
{
    return FindMemory(from, direction, mode, group != 0, group);
}

// Memory groups are the scan lists (a memory's ScAdd1-3 bits, as CHIRP writes them).
// A VFO is in a group only as the VFO/MR cycle put it there: on a memory outside it
// (picked by number, ..) it is back on all memories for good, so stepping through
// them never drops into a group again.
uint8_t SERVICE_GroupOf(uint8_t vfo)
{
    const uint8_t  g  = gSvc.group[vfo];
    const uint16_t ch = gEeprom.ScreenChannel[vfo];
    if (!g || !IS_MR_CHANNEL(ch))
        return 0;
    const ChannelAttributes_t a = gMR_ChannelAttributes[ch];
    if (g == 1u ? a.scanlist1 : g == 2u ? a.scanlist2 : a.scanlist3)
        return g;
    gSvc.group[vfo] = 0;   // (saved with the rest of the state)
    return 0;
}

void SERVICE_SetGroup(uint8_t vfo, uint8_t group)
{
    gSvc.group[vfo] = group;
    SERVICE_SaveState();
}

static uint8_t gScanMode;    // a memory scan: the memories of the mode it started in,
static uint8_t gScanGroup;   // ... in its group (5: all)

uint16_t SERVICE_NextScanMemory(uint16_t from, int8_t direction)
{
    return FindMemory(from, direction, gScanMode, true, gScanGroup);
}

uint16_t SERVICE_ChannelFor(uint8_t vfo, Service_t service)
{
    const uint16_t ch = ServiceFirst(service) + gSvc.channel[vfo][service];   // HAM: its built-in channels
    return SERVICE_ChannelVisible(ch) ? ch : SERVICE_StepChannel(ch, 1);       // (one licensed)
}

static void SERVICE_Track(uint8_t vfo, uint16_t channel)
{
    if (IS_SVC_CHANNEL(channel)) {
        const Service_t s = SERVICE_OfChannel(channel);
        gSvc.channel[vfo][s] = (uint8_t)(channel - ServiceFirst(s));
    }
}

// Weather watch: a WX channel on the VFO you are not transmitting on, while dual
// watching, opens only on the 1050 Hz Warning Alarm Tone (the BK4819's 1050 Hz
// detector, as the stock NOAA feature used), so dual watch keeps checking 16.
// Once the alarm is heard the channel stays open (latched) for the message.
static bool gWxAlertLatched;

bool SERVICE_WxAlertGate(const VFO_Info_t *pVfo)
{
    const uint16_t ch = pVfo->CHANNEL_SAVE;
    return !gWxAlertLatched && gEeprom.DUAL_WATCH != DUAL_WATCH_OFF &&
           pVfo != &gEeprom.VfoInfo[gEeprom.TX_VFO] &&
           SERVICE_OfChannel(ch) == SERVICE_MARINE &&
           strcmp(PLAN_MarineUse(gMarinePlan, ch - SVC_MARINE_FIRST), "WEATHER") == 0;
}

bool SERVICE_WxAlertReset(void)
{
    const bool was = gWxAlertLatched;
    gWxAlertLatched = false;
    return was;
}

bool SERVICE_OnRxStart(const VFO_Info_t *pVfo)
{
    if (SERVICE_WxAlertGate(pVfo)) {   // a gated WX channel only opens on the alarm tone
        gWxAlertLatched = true;
        return true;
    }
    return pVfo->CHANNEL_SAVE == SERVICE_MarineCallingChannel();   // activity on 16
}

uint16_t SERVICE_MarineCallingChannel(void)
{
    return SVC_MARINE_FIRST + PLAN_MarineFind(gMarinePlan, "16");
}

uint16_t SERVICE_MarineWatchChannel(void)
{
    uint8_t idx = PLAN_MarineFind(gMarinePlan, "WX1");
    if (idx == 0xFF)                                   // International: no WX broadcasts
        idx = PLAN_MarineFind(gMarinePlan, "13");      // bridge-to-bridge, worldwide (App.18 note k)
    return SVC_MARINE_FIRST + idx;
}

uint16_t SERVICE_ValidateChannel(uint16_t channel)
{
    const Service_t s = SERVICE_OfChannel(channel);
    if (channel - ServiceFirst(s) < ServiceCount(s))
        return channel;
    // past the end of this plan (the region changed): marine 16, else the first one
    return s == SERVICE_MARINE ? SERVICE_MarineCallingChannel() : ServiceFirst(s);
}

uint8_t SERVICE_PlanPosition(uint16_t channel, uint16_t *first)
{
    const Service_t s = SERVICE_OfChannel(channel);
    *first = ServiceFirst(s);
    return ServiceCount(s);
}

bool SERVICE_ScanSkip(uint16_t channel)
{
    const Service_t s = SERVICE_OfChannel(channel);
    if (!SERVICE_ChannelVisible(channel))
        return true;   // (not licensed)
    if (s == SERVICE_GMRS) {   // 15R-22R receive on the same frequency as 15-22
        ServiceChannelInfo_t info;
        SERVICE_GetInfo(channel, &info);
        return info.flags & PLAN_F_DUPLEX;
    }
    if (s == SERVICE_MARINE) {   // data bursts / continuous broadcasts would hold the scan
        const char *use = PLAN_MarineUse(gMarinePlan, channel - SVC_MARINE_FIRST);
        return strcmp(use, "AIS") == 0 || strcmp(use, "DSC") == 0 ||
               strcmp(use, "WEATHER") == 0 || strcmp(use, "BROADCST") == 0;
    }
    return false;
}

uint16_t SERVICE_StepChannel(uint16_t channel, int8_t direction)
{
    const Service_t s     = SERVICE_OfChannel(channel);
    const uint16_t  first = ServiceFirst(s);
    const uint8_t   count = ServiceCount(s);
    uint16_t        ch    = channel;
    for (uint8_t i = 0; i < count; i++) {   // the next licensed one (none: stays)
        ch = first + (uint16_t)((ch - first + count + direction) % count);
        if (SERVICE_ChannelVisible(ch))
            return ch;
    }
    return channel;
}

void SERVICE_SaveState(void)
{
    SERVICE_Track(0, gEeprom.ScreenChannel[0]);
    SERVICE_Track(1, gEeprom.ScreenChannel[1]);

    if (memcmp(&gSvc, &gSvcSaved, sizeof(gSvc)) == 0)
        return;

    EepromWrite(SVC_STATE_ADDR, &gSvc, sizeof(gSvc));
    gSvcSaved = gSvc;
}

uint8_t SERVICE_TakeKeyDefaultsOnce(void)
{
    // reserved[0]: 0xA5 = the side-key defaults taken, 0xA6 = also long-press MENU
    const uint8_t take = gSvc.reserved[0] == 0xA6 ? 0u : gSvc.reserved[0] == 0xA5 ? 2u : 3u;
    if (take) {
        gSvc.reserved[0] = 0xA6;
        SERVICE_SaveState();
    }
    return take;
}

uint8_t SERVICE_GetSubCodeProfile(void)
{
    return gSvc.profile;
}

uint8_t SERVICE_SubCodeProfileFor(uint16_t channel)
{   // INTL: PMR446 and Simple UK number their codes their own way; else the brand picked
    if (SERVICE_IsPmr() && SERVICE_OfChannel(channel) == SERVICE_GMRS)
        return channel < SVC_GMRS_FIRST + PLAN_PMR_COUNT ? SUBCODE_PMR446 : SUBCODE_OFCOM;
    return gSvc.profile;
}

void SERVICE_SetSubCodeProfile(uint8_t profile)
{
    if (profile < SUBCODE_BRAND_COUNT)
        gSvc.profile = profile;
    SERVICE_SaveState();
}

uint8_t SERVICE_GetCodeDetect(void) { return gSvc.codeDetect <= CODE_DETECT_OFF ? gSvc.codeDetect : CODE_DETECT_AUTO; }
void    SERVICE_SetCodeDetect(uint8_t mode)   { gSvc.codeDetect = mode;   SERVICE_SaveState(); }
uint8_t SERVICE_GetRegion(void)     { return gSvc.hamRegion; }   // (checked in SERVICE_Init)
uint8_t SERVICE_GetHamRegion(void)  { return gSvc.hamRegion == PLAN_REGION_UK ? HAM_REGION_R1 : gSvc.hamRegion; }
bool    SERVICE_IsPmr(void)         { return gSvc.hamRegion >= HAM_REGION_R1; }   // INTL, UK: PMR446
void    SERVICE_SetHamRegion(uint8_t region)   // Region: ham channels and bands, the marine plan
{
    SERVICE_SetMarinePlan(region == PLAN_REGION_UK ? MARINE_PLAN_INTL : region);
    gSvc.hamRegion = region;
    SERVICE_SaveState();
}

uint8_t SERVICE_GetMarinePlan(void)
{
    return gSvc.marinePlan;
}

void SERVICE_SetMarinePlan(uint8_t plan)
{
    if (plan >= MARINE_PLAN_COUNT || plan == gSvc.marinePlan)
        return;

    SERVICE_Track(0, gEeprom.ScreenChannel[0]);
    SERVICE_Track(1, gEeprom.ScreenChannel[1]);

    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        // stay on the same channel where the new plan has it (16 -> 16, 22A -> 22, AIS1 -> 87B)
        const uint8_t idx = PLAN_MarineEquivalent(gSvc.marinePlan, gSvc.channel[vfo][SERVICE_MARINE], plan);
        gSvc.channel[vfo][SERVICE_MARINE] = idx;
        if (SERVICE_OfChannel(gEeprom.ScreenChannel[vfo]) == SERVICE_MARINE)
            gEeprom.ScreenChannel[vfo] = SVC_MARINE_FIRST + idx;
    }

    gSvc.marinePlan = plan;
    gMarinePlan     = plan;
    SERVICE_SaveState();
}

static void SetCode(FREQ_Config_t *config, uint8_t type, uint8_t code)
{
    // Erased EEPROM (0xFF) and anything out of range reads as "no tone".
    config->CodeType = (type <= CODE_TYPE_REVERSE_DIGITAL) ? type : CODE_TYPE_OFF;
    const uint8_t limit = (config->CodeType == CODE_TYPE_CONTINUOUS_TONE) ? ARRAY_SIZE(CTCSS_Options)
                        : (config->CodeType == CODE_TYPE_OFF)             ? 1u : ARRAY_SIZE(DCS_Options);
    config->Code = (code < limit) ? code : 0;
}

static void SERVICE_LoadTones(uint16_t channel, VFO_Info_t *pVfo)
{
    SvcTones_t t;
    EEPROM_ReadBuffer(GmrsTonesAddr(channel - SVC_GMRS_FIRST), &t, sizeof(t));
    SetCode(&pVfo->freq_config_RX, t.rxType, t.rxCode);
    SetCode(&pVfo->freq_config_TX, t.txType, t.txCode);
}

// Power levels per F4HWN: LOW1 <20 mW, LOW2 125 mW, LOW3 250 mW, LOW4 500 mW,
// LOW5 1 W, MID 2 W, HIGH 5 W (USER: whatever SetPwr selects, same scale).
#define POWER_GMRS_LOW_CAP OUTPUT_POWER_LOW4   // GMRS/FRS 8-14: 0.5 W (47 CFR 95.567 / 95.1767)
#define POWER_MARINE_LOW   OUTPUT_POWER_LOW5   // marine "low" = 1 W (47 CFR 80.215(e))
#define POWER_FRS_MAX      OUTPUT_POWER_MID    // FRS 1-7 / 15-22: 2 W

uint8_t SERVICE_PowerLimit(uint16_t channel)
{
    ServiceChannelInfo_t info;
    SERVICE_GetInfo(channel, &info);
    // GMRS 8-14 are capped at 0.5 W; marine "low" channels only default low.
    if (SERVICE_OfChannel(channel) != SERVICE_GMRS)
        return OUTPUT_POWER_HIGH;
    return (info.flags & PLAN_F_LOW) ? POWER_GMRS_LOW_CAP
         : ((info.flags & PLAN_F_MURS) || SERVICE_GetRegion() == HAM_REGION_CA ||   // MURS, Canada,
            (!SERVICE_IsPmr() && !LicTx(SERVICE_GMRS, LIC_GMRS))) ? OUTPUT_POWER_MID   // FRS only: 2 W
         : OUTPUT_POWER_HIGH;
}

// Plan default. GMRS simplex channels start at 2 W, i.e. FRS (no licence
// needed); HIGH makes them GMRS. Repeater pairs are GMRS-only: HIGH.
static uint8_t DefaultPower(uint16_t channel)
{
    ServiceChannelInfo_t info;
    SERVICE_GetInfo(channel, &info);
    if (SERVICE_OfChannel(channel) == SERVICE_GMRS) {
        if (info.flags & PLAN_F_LOW)
            return POWER_GMRS_LOW_CAP;
        return (info.flags & PLAN_F_DUPLEX) ? OUTPUT_POWER_HIGH : POWER_FRS_MAX;
    }
    return (info.flags & PLAN_F_LOW) ? POWER_MARINE_LOW : OUTPUT_POWER_HIGH;
}

uint8_t SERVICE_NextPower(uint16_t channel, uint8_t power)
{
    if (SERVICE_OfChannel(channel) == SERVICE_AIR)   // receive only
        return power;
    if (SERVICE_OfChannel(channel) == SERVICE_MARINE)   // 1 W / 5 W switch, like a marine radio
        return (power == OUTPUT_POWER_HIGH) ? POWER_MARINE_LOW : OUTPUT_POWER_HIGH;
    return (power >= SERVICE_PowerLimit(channel) || power < OUTPUT_POWER_LOW1) ? OUTPUT_POWER_LOW1 : power + 1u;
}

bool SERVICE_IsFrs(uint16_t channel, uint8_t power)
{
    if (SERVICE_OfChannel(channel) != SERVICE_GMRS)
        return false;
    ServiceChannelInfo_t info;
    SERVICE_GetInfo(channel, &info);
    if (info.flags & (PLAN_F_DUPLEX | PLAN_F_MURS))   // repeater pairs: GMRS only; MURS
        return false;
    if (info.flags & PLAN_F_LOW)      // 8-14: 0.5 W, shared by both services
        return true;
    // USER power: SetPwr index 0..6 = <20 mW .. 5 W, 5 = 2 W
    return (power == OUTPUT_POWER_USER) ? gSetting_set_pwr <= 5u : power <= POWER_FRS_MAX;
}

// GMRS keeps any power level (4 bits; 0 / 0xF = default). Marine channels are
// LOW1 / HIGH like a marine radio's 1 W / 25 W switch, stored as one
// "flipped from the default" bit per channel and plan.
// The built-in ham channels: one power per band (the F editor sets it). Default: HIGH,
// but 10 m / 6 m (below 100 MHz, far outside the radio's design: spurious) receive only.
#define HAM_PWR_OFF 8u

// (fixed slots, whatever the region: 10 m, 6 m, 2 m, 1.25 m, 70 cm - IARU R1 has no 1.25 m)
static uint16_t HamPowerAddr(const VFO_Info_t *pVfo, uint8_t *nibble)
{
    int8_t band = PLAN_HamBandOf(SERVICE_GetHamRegion(), pVfo->freq_config_RX.Frequency);
    if (band == 3 && SERVICE_GetHamRegion() == HAM_REGION_R1)
        band = 4;   // (its 70 cm)
    *nibble = (uint8_t)(band & 1);
    return band < 0 ? 0u : (uint16_t)(SVC_HAMPWR_ADDR + band / 2);
}

static void LoadHamPower(VFO_Info_t *pVfo)
{
    uint8_t        hi, b = 0xFF;
    const uint16_t a = HamPowerAddr(pVfo, &hi);
    if (a)
        EEPROM_ReadBuffer(a, &b, 1);
    uint8_t p = hi ? b >> 4 : b & 0x0Fu;
    if (p > HAM_PWR_OFF)   // (erased)
        p = pVfo->freq_config_RX.Frequency < 10000000u ? HAM_PWR_OFF : OUTPUT_POWER_HIGH;
    pVfo->TX_OFF       = p == HAM_PWR_OFF;
    pVfo->OUTPUT_POWER = p == HAM_PWR_OFF ? OUTPUT_POWER_HIGH : p;
}

static void SaveHamPower(const VFO_Info_t *pVfo)
{
    uint8_t        hi, b;
    const uint16_t a = HamPowerAddr(pVfo, &hi);
    if (!a)
        return;
    const uint8_t p = pVfo->TX_OFF ? HAM_PWR_OFF : pVfo->OUTPUT_POWER & 7u;
    EEPROM_ReadBuffer(a, &b, 1);
    b = hi ? (uint8_t)((b & 0x0Fu) | p << 4) : (uint8_t)((b & 0xF0u) | p);
    EepromWrite(a, &b, 1);
}

// The radio's PA / filters: 136-174 and 400-470 MHz (outside: spurious, weak).
bool SERVICE_OutOfSpec(uint32_t frequency)
{
    return frequency < 13600000u || (frequency > 17400000u && frequency < 40000000u) || frequency > 47000000u;
}

static uint8_t LoadPower(uint16_t channel)
{
    uint8_t b;
    if (SERVICE_OfChannel(channel) == SERVICE_GMRS) {
        const uint8_t idx = channel - SVC_GMRS_FIRST;
        EEPROM_ReadBuffer(GmrsPowerAddr(idx), &b, 1);
        const uint8_t p = (idx & 1u) ? (b >> 4) : (b & 0x0Fu);
        return (p >= OUTPUT_POWER_LOW1 && p <= SERVICE_PowerLimit(channel)) ? p : DefaultPower(channel);
    }
    const uint8_t idx = channel - SVC_MARINE_FIRST;
    EEPROM_ReadBuffer(SVC_MPOWER_ADDR + gMarinePlan * SVC_MPOWER_LEN + idx / 8u, &b, 1);
    const bool    flipped = (b != 0xFF) && (b & (1u << (idx & 7u)));   // erased = all defaults
    const uint8_t def     = DefaultPower(channel);
    return flipped ? (def == OUTPUT_POWER_HIGH ? POWER_MARINE_LOW : OUTPUT_POWER_HIGH) : def;
}

static void SavePower(uint16_t channel, uint8_t power)
{
    uint8_t b;
    if (SERVICE_OfChannel(channel) == SERVICE_GMRS) {
        const uint8_t  idx  = channel - SVC_GMRS_FIRST;
        const uint16_t addr = GmrsPowerAddr(idx);
        EEPROM_ReadBuffer(addr, &b, 1);
        b = (idx & 1u) ? (uint8_t)((b & 0x0Fu) | (power << 4)) : (uint8_t)((b & 0xF0u) | power);
        EepromWrite(addr, &b, 1);
        return;
    }
    const uint8_t  idx  = channel - SVC_MARINE_FIRST;
    const uint16_t addr = SVC_MPOWER_ADDR + gMarinePlan * SVC_MPOWER_LEN + idx / 8u;
    EEPROM_ReadBuffer(addr, &b, 1);
    if (b == 0xFF)
        b = 0;
    const uint8_t bit = (uint8_t)(1u << (idx & 7u));
    b = (power != DefaultPower(channel)) ? (uint8_t)(b | bit) : (uint8_t)(b & ~bit);
    EepromWrite(addr, &b, 1);
}

void SERVICE_SaveChannelTones(uint16_t channel, const VFO_Info_t *pVfo)
{
    if (SERVICE_OfChannel(channel) == SERVICE_HAM) {   // the band's power
        SaveHamPower(pVfo);
        return;
    }
    if (SERVICE_OfChannel(channel) == SERVICE_AIR)
        return;   // nothing kept per channel

    if (LoadPower(channel) != pVfo->OUTPUT_POWER)
        SavePower(channel, pVfo->OUTPUT_POWER);

    if (SERVICE_OfChannel(channel) != SERVICE_GMRS)
        return;

    const SvcTones_t t = {
        pVfo->freq_config_RX.CodeType, pVfo->freq_config_RX.Code,
        pVfo->freq_config_TX.CodeType, pVfo->freq_config_TX.Code,
    };
    EepromWrite(GmrsTonesAddr(channel - SVC_GMRS_FIRST), &t, sizeof(t));
}

// ---------------------------------------------------------------------------
// VFO configuration
// ---------------------------------------------------------------------------

void SERVICE_ConfigureVfo(VFO_Info_t *pVfo, uint16_t channel)
{
    ServiceChannelInfo_t info;
    SERVICE_GetInfo(channel, &info);

    RADIO_InitInfo(pVfo, channel, info.rx);

    const STEP_Setting_t step = (SERVICE_OfChannel(channel) == SERVICE_GMRS) ? STEP_12_5kHz : STEP_25kHz;
    pVfo->STEP_SETTING                  = step;
    pVfo->StepFrequency                 = gStepFrequencyTable[step];
    pVfo->TX_OFFSET_FREQUENCY           = info.txOffset;
    pVfo->TX_OFFSET_FREQUENCY_DIRECTION = info.txDirection;
    pVfo->CHANNEL_BANDWIDTH             = (info.flags & PLAN_F_NARROW) ? BK4819_FILTER_BW_NARROW : BK4819_FILTER_BW_WIDE;
    pVfo->OUTPUT_POWER                  = (SERVICE_OfChannel(channel) == SERVICE_AIR) ? OUTPUT_POWER_LOW1
                                        : (SERVICE_OfChannel(channel) == SERVICE_HAM) ? OUTPUT_POWER_HIGH : LoadPower(channel);
    if (SERVICE_OfChannel(channel) == SERVICE_HAM)
        LoadHamPower(pVfo);   // (its band's)
    if (SERVICE_OfChannel(channel) == SERVICE_AIR)
        pVfo->Modulation = MODULATION_AM;   // AM fix applies (AM Fix setting)
    if (info.flags & PLAN_F_USB)
        pVfo->Modulation = MODULATION_USB;
    if (SERVICE_OfChannel(channel) == SERVICE_GMRS && !(info.flags & PLAN_F_NARROW))   // FRS is narrowband
        pVfo->CHANNEL_BANDWIDTH = SERVICE_IsFrs(channel, pVfo->OUTPUT_POWER) ? BK4819_FILTER_BW_NARROW
                                                                             : BK4819_FILTER_BW_WIDE;
    // TX_LOCK=false lets the service's own plan decide TX instead of F Lock;
    // receive-only channels are refused separately by SERVICE_IsTxInhibited().
    pVfo->TX_LOCK                       = (info.flags & PLAN_F_RX_ONLY) ? true : false;
    pVfo->BUSY_CHANNEL_LOCK             = true;   // (shared channels: not over someone)
    pVfo->SvcFlags                      = 0;      // (no N+, not a Call channel)
    pVfo->SCANLIST1_PARTICIPATION       = 0;
    pVfo->SCANLIST2_PARTICIPATION       = 0;
    pVfo->SCANLIST3_PARTICIPATION       = 0;

    if (SERVICE_OfChannel(channel) == SERVICE_GMRS)
        SERVICE_LoadTones(channel, pVfo);

    RADIO_ApplyOffset(pVfo);
    RADIO_ConfigureSquelchAndOutputPower(pVfo);
}

bool    SERVICE_GetTxAny(void)           { return gSvc.txAny; }
void    SERVICE_SetTxAny(bool on)        { gSvc.txAny = on;        SERVICE_SaveState(); }
bool    SERVICE_GetAbView(void)          { return gSvc.abView; }
uint8_t SERVICE_GetWatchLevel(void)      { return gSvc.watch <= WATCH_OFF ? gSvc.watch : WATCH_PRIORITY; }
void    SERVICE_SetWatchLevel(uint8_t l) { gSvc.watch = l;         SERVICE_SaveState(); }

// ---------------------------------------------------------------------------
// A / B view and the watch. F+2 only switches the view; entering the A / B view
// starts the watch on the other VFO (Watch: PRIORITY = stay on the TX VFO and
// look at the other one every 2 s, WATCH = alternate evenly, OFF = no watch).
// The solid border is always the VFO PTT transmits on: when the watched VFO is
// heard TX moves there (never to one that can't transmit), and goes back to the
// main VFO the ScnRev time after the signal ends unless you answer (STOP: it
// stays and the watch ends, like a scan hit). Transmitting or picking A / B
// (long-press 2) makes that VFO exclusive (the watch ends); EXIT starts it again.
// The running watch is the stock dual watch (DUAL_WATCH on, cross band off).
// ---------------------------------------------------------------------------

#define WATCH_PENDING 0xFFu   // TX on the watched VFO, its signal not over yet

static uint8_t gWatchMain;             // the VFO the user picked
static uint8_t gWatchFallback_500ms;   // TX on the watched VFO: back to main when it runs out

static void WatchSetTx(uint8_t vfo)
{
    gEeprom.TX_VFO = vfo;
    gTxVfo         = &gEeprom.VfoInfo[vfo];
    gUpdateStatus  = true;
    gUpdateDisplay = true;
}

void SERVICE_SetAbView(bool on, bool forceWatch)
{
    gSvc.abView = on;
    SERVICE_SaveState();
    if (on)
        SERVICE_WatchStart(forceWatch);
    else if (SERVICE_WatchStop())   // the selected (solid-border) VFO goes full screen
        gFlagReconfigureVfos = true;     // and is the only one listened to
}

void SERVICE_WatchStart(bool force)
{
    const bool run = force || SERVICE_GetWatchLevel() != WATCH_OFF;
    gWatchMain               = gEeprom.TX_VFO;
    gWatchFallback_500ms     = 0;
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
    gEeprom.DUAL_WATCH       = run ? gEeprom.TX_VFO + 1u : DUAL_WATCH_OFF;
    gFlagReconfigureVfos     = true;
    gUpdateStatus            = true;
}

bool SERVICE_WatchStop(void)
{
    const bool was = gEeprom.DUAL_WATCH != DUAL_WATCH_OFF || gWatchFallback_500ms;
    gWatchMain           = gEeprom.TX_VFO;
    gWatchFallback_500ms = 0;
    gEeprom.DUAL_WATCH   = DUAL_WATCH_OFF;   // (no reconfigure here: PTT calls this mid-TX)
    gUpdateStatus        = true;
    return was;
}

bool SERVICE_WatchExit(void)
{
    if (!SERVICE_GetAbView())
        return false;
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) {   // one VFO exclusive: back to watching
        if (SERVICE_GetWatchLevel() == WATCH_OFF)
            return false;
        SERVICE_WatchStart(false);
        return true;
    }
    if (!gWatchFallback_500ms)
        return false;
    WatchSetTx(gWatchMain);   // watching, PTT on the watched VFO: back to main now
    gWatchFallback_500ms = 0;
    return true;
}

void SERVICE_WatchBoot(void)
{
    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
    gEeprom.DUAL_WATCH       = DUAL_WATCH_OFF;
    gWatchMain               = gEeprom.TX_VFO;
    if (SERVICE_GetAbView() && SERVICE_GetWatchLevel() != WATCH_OFF)
        gEeprom.DUAL_WATCH = gEeprom.TX_VFO + 1u;
}

void SERVICE_WatchOnRx(void)
{
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF)
        return;
    const uint8_t rx = gEeprom.RX_VFO;
    if (rx == gEeprom.TX_VFO) {   // the TX VFO heard again: the fall-back waits
        if (gWatchFallback_500ms)
            gWatchFallback_500ms = WATCH_PENDING;
        return;
    }
    if (!SERVICE_CanTx(&gEeprom.VfoInfo[rx]))
        return;   // e.g. weather: PTT stays where it is
    WatchSetTx(rx);
    if (rx == gWatchMain) {
        gWatchFallback_500ms = 0;
    } else if (gEeprom.SCAN_RESUME_MODE == 0) {   // ScnRev STOP
        gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;   // parked on it, like a scan hit (mid-RX: no reconfigure)
        gWatchMain         = rx;
    } else {
        gWatchFallback_500ms = WATCH_PENDING;
    }
}

// ---------------------------------------------------------------------------
// Listening: the watch and the scan as one rotation on the single receiver, and
// the only code that moves RX_VFO between the VFOs. HOME is the VFO picked (the
// scan's, PTT's home); OTHER the other one.
//   watching (A / B view)  HOME, OTHER, ..  WATCH: 100 ms each; PRIORITY: 2 s on
//                                           HOME, a 200 ms look at OTHER
//   scanning               STEP, STEP, ..   on HOME; while watching, the PRIORITY
//                                           look at OTHER every 2 s (channel and
//                                           frequency scans alike)
// Heard on OTHER: PTT follows (SERVICE_WatchOnRx). One hold for every hit, ScnRev:
// STOP = stay there (the scan / watch ends), CARRIER / TIMEOUT = that long after
// the signal before going on, and PTT going back HOME.
// ---------------------------------------------------------------------------
static bool    gLook, gLookHeard;   // the scan's look at OTHER; something heard there
static uint8_t gScanHome;           // the scanned VFO
static uint8_t gLookClock_500ms;    // since the last look

static void ListenOn(uint8_t vfo)
{
    gEeprom.RX_VFO = vfo;
    gRxVfo         = &gEeprom.VfoInfo[vfo];
}

// After a hit's signal: the ScnRev hold, 10 ms (0: STOP).
static uint16_t Hold_10ms(void)
{
    const uint8_t m = gEeprom.SCAN_RESUME_MODE;
    return m < 81u ? m * 25u : (m - 80u) * 500u;   // CARRIER n x 250 ms, TIMEOUT n x 5 s
}

static void LookEnd(void);

void SERVICE_WatchTimeSlice500ms(void)
{
    if (gLookClock_500ms < 0xFFu)
        gLookClock_500ms++;
    if (gLook && gLookHeard && !FUNCTION_IsRx())
        LookEnd();   // OTHER's signal ended: back to the scan

    if (!gWatchFallback_500ms)
        return;
    if (FUNCTION_IsRx() || gCurrentFunction == FUNCTION_TRANSMIT) {
        gWatchFallback_500ms = WATCH_PENDING;
        return;
    }
    if (gWatchFallback_500ms == WATCH_PENDING)
        gWatchFallback_500ms = (uint8_t)(Hold_10ms() / 50u + 1u);   // <= 2 min
    if (--gWatchFallback_500ms == 0)
        WatchSetTx(gWatchMain);
}

// Back on HOME; also before the scan stops, which keeps or restores its result on
// RX_VFO (else OTHER got HOME's channel).
void SERVICE_ScanStop(void)
{
    if (!gLook)
        return;
    gLook = false;
    ListenOn(gScanHome);
}

static void LookEnd(void)
{
    SERVICE_ScanStop();
    if (gLookHeard) {
        gLookHeard = false;
        const uint16_t hold = Hold_10ms();
        if (!hold) {   // STOP: the scan ends, parked on OTHER (PTT is there)
            CHFRSCANNER_Stop();
            ListenOn(gEeprom.TX_VFO);
        } else {
            gScanPauseDelayIn_10ms = hold;
            gScheduleScanListen    = false;
        }
    }
    RADIO_SetupRegisters(false);
}

bool SERVICE_WatchPointless(void)
{
#ifdef ENABLE_FMRADIO   // the other VFO on broadcast FM: the BK4819 is muted there, a look only clicks
    return gEeprom.VfoInfo[!gEeprom.TX_VFO].Modulation == MODULATION_BFM;
#else
    return false;
#endif
}

bool SERVICE_ScanLook(void)
{
    if (gLook) {
        if (gLookHeard) {   // still hearing OTHER (the scan timer runs during RX):
            gScanPauseDelayIn_10ms = 50;   // stay; its end is picked up by the 500 ms slice
            gScheduleScanListen    = false;
            return true;
        }
        LookEnd();   // nothing there: the next step
        return false;
    }
    if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF || !SERVICE_GetAbView() || SERVICE_WatchPointless() ||
        gLookClock_500ms < 4u)
        return false;
    gLookClock_500ms = 0;
    gLook            = true;
    ListenOn(!gScanHome);
    RADIO_SetupRegisters(false);
    gScanPauseDelayIn_10ms = 20;   // 200 ms on it, as the PRIORITY watch's look
    gScheduleScanListen    = false;
    return true;
}

bool SERVICE_ScanLookHeard(void)
{
    if (!gLook)
        return false;
    gLookHeard = true;
    SERVICE_WatchOnRx();
    return true;
}

void SERVICE_ScanStart(void)
{
    gScanMode  = SERVICE_ModeOf(gEeprom.TX_VFO);
    gScanGroup = SERVICE_GroupOf(gEeprom.TX_VFO);   // the scan covers what the VFO is on
    if (!gScanGroup)
        gScanGroup = 5;   // all (RADIO_CheckValidChannel's "all lists")
    gLook = gLookHeard = false;
    gLookClock_500ms   = 0;
    gScanHome          = gEeprom.TX_VFO;   // A or B: the other one is watched
    gWatchMain         = gScanHome;
    gWatchFallback_500ms = 0;
}

// The scan started or restarted (Up / Down, ..): on HOME, wherever PTT followed a
// signal meanwhile (RADIO_SelectVfos puts RX on the TX VFO).
void SERVICE_ScanResume(void)
{
    gLook = gLookHeard = false;
    ListenOn(gScanHome);
    gCurrentVfo = gRxVfo;
}

// The watch's next turn: listening on the other VFO, for how long (10 ms).
uint16_t SERVICE_WatchNext_10ms(void)
{
    ListenOn(!gEeprom.RX_VFO);
    if (SERVICE_GetWatchLevel() != WATCH_PRIORITY)
        return 10;   // 100 ms each
    return gEeprom.RX_VFO == gWatchMain ? 200u : 20u;   // 2 s HOME, 200 ms on OTHER
}

bool SERVICE_CanTx(const VFO_Info_t *pVfo)
{
    return !pVfo->TX_OFF && !SERVICE_IsTxInhibited(pVfo);
}

// The hard TX blocks, all lifted by TX Any: receive-only plan channels, NOAA weather
// frequencies, broadcast FM, and outside the region's ham bands (VFO / memories).
bool SERVICE_IsTxInhibited(const VFO_Info_t *pVfo)
{
#ifdef ENABLE_FMRADIO
    if (pVfo->Modulation == MODULATION_BFM)
        return true;   // broadcast FM: receive only, TX Any or not (nothing transmits there)
#endif
    if (SERVICE_GetTxAny())
        return false;
    const uint32_t f = pVfo->pTX->Frequency;
    if (f >= 16240000u && f <= 16255000u)
        return true;   // NOAA weather radio
    uint16_t ch = pVfo->CHANNEL_SAVE;
    if (!IS_SVC_CHANNEL(ch)) {   // a VFO / memory: on a plan frequency it follows that plan
        if (ModeOfFrequency(pVfo->freq_config_RX.Frequency) == SERVICE_HAM)   // no ham licence / callsign,
            return !LicTx(SERVICE_HAM, LIC_HAM) ||                             // or outside the region's
                   (TX_freq_check(f) != 0 && pVfo->TX_LOCK);                   // ham bands (HamRgn)
        ch = SERVICE_PlanChannelOf(pVfo);
        if (ch == 0xFFFF)
            return true;   // a service band, but no channel of its plan
    }

    if (!ChannelLicensedTx(ch))
        return true;   // not licensed to transmit there
    ServiceChannelInfo_t info;
    SERVICE_GetInfo(ch, &info);
    return info.flags & PLAN_F_RX_ONLY;
}

bool SERVICE_ChannelLabel(uint16_t channel, char *out)
{   // air and the built-in ham channels go by their frequency
    const Service_t s = SERVICE_OfChannel(channel);
    if (s == SERVICE_AIR || s == SERVICE_HAM)
        return false;
    SERVICE_FormatLabel(channel, out);
    return true;
}

void SERVICE_FormatLabel(uint16_t channel, char *out)
{
    if (SERVICE_OfChannel(channel) == SERVICE_GMRS)
        (SERVICE_IsPmr() ? PLAN_PmrLabel : PLAN_GmrsLabel)(channel - SVC_GMRS_FIRST, out);
    else if (SERVICE_OfChannel(channel) == SERVICE_AIR)
        sprintf(out, "%u", channel - SVC_AIR_FIRST + 1u);   // the frequency is what gets shown
    else if (SERVICE_OfChannel(channel) == SERVICE_HAM)
        sprintf(out, "%u", channel - SVC_HAM_FIRST + 1u);
    else
        PLAN_MarineLabel(gMarinePlan, channel - SVC_MARINE_FIRST, out);
}

const char *SERVICE_ChannelUse(uint16_t channel)
{
    if (SERVICE_OfChannel(channel) == SERVICE_AIR)
        return PLAN_AirUse(channel - SVC_AIR_FIRST);
    if (SERVICE_OfChannel(channel) == SERVICE_HAM)
        return PLAN_HamUse(SERVICE_GetHamRegion(), channel - SVC_HAM_FIRST);
    return PLAN_MarineUse(gMarinePlan, channel - SVC_MARINE_FIRST);
}

