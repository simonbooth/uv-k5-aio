/* Built-in channel plans: GMRS/FRS and three VHF marine plans. Pure data and
 * lookups (no radio dependencies) so it can be checked on a host. See svc_plans.h.
 */

#include <string.h>

#include "svc_plans.h"
#include "misc.h"
#include "external/printf/printf.h"

// ---------------------------------------------------------------------------
// GMRS/FRS (47 CFR 95.1763): 1-7 and 15-22 at 462 MHz, 8-14 at 467 MHz
// (0.5 W, narrowband), 15R-22R repeater pairs: RX 462.xxx, TX +5 MHz.
// ---------------------------------------------------------------------------

#define GMRS_REPEATER_FIRST 22u
#define GMRS_REPEATER_SHIFT 500000u   // 5 MHz in 10 Hz units

void PLAN_GmrsInfo(uint8_t idx, PlanChannelInfo_t *info)
{
    if (idx >= PLAN_GMRS_SAVED) {   // MURS 1-3: 11.25 kHz (narrow), 4-5: 20 kHz (wide)
        static const uint16_t murs[5] = { 15182, 15188, 15194, 15457, 15460 };   // 10 kHz
        info->rx      = murs[idx - PLAN_GMRS_SAVED] * 1000u;
        info->txShift = 0;
        info->flags   = PLAN_F_MURS | (idx < PLAN_GMRS_SAVED + 3u ? PLAN_F_NARROW : 0u);
        return;
    }
    if (idx < 7u)
        info->rx = 46256250u + idx * 2500u;
    else if (idx < 14u)
        info->rx = 46756250u + (idx - 7u) * 2500u;
    else if (idx < GMRS_REPEATER_FIRST)
        info->rx = 46255000u + (idx - 14u) * 2500u;
    else
        info->rx = 46255000u + (idx - GMRS_REPEATER_FIRST) * 2500u;

    info->txShift = 0;
    info->flags   = 0;
    if (idx >= 7u && idx < 14u) {
        info->flags = PLAN_F_LOW | PLAN_F_NARROW;
    } else if (idx >= GMRS_REPEATER_FIRST) {
        info->flags   = PLAN_F_DUPLEX;
        info->txShift = (int32_t)GMRS_REPEATER_SHIFT;
    }
}

// Business Radio Simple UK Light (Ofcom): its 19 shared frequencies, 12.5 kHz,
// 5 W ERP, handheld / mobile only; low band, VHF mid, VHF high, UHF 1. In 6.25 kHz
// steps above 70 MHz.
static const uint16_t SimpleUk[PLAN_INTL_COUNT - PLAN_PMR_COUNT] = {
     1230,  2614,  2616,  2618,  2620,                  //  77.6875, 86.3375 - 86.3750
    15048, 15050,                                       // 164.0500, 164.0625
    15854, 15890, 16488, 16490, 16494,                  // 169.0875, 169.3125, 173.0500 - 173.0875
    60690, 60704, 60716, 62206, 62208, 62210, 62212,    // 449.3125 - 449.4750, 458.7875 - 458.8250
};

// PMR446 analog 1-16: 446.00625 MHz + n x 12.5 kHz, narrow, 0.5 W ERP; then Simple UK
void PLAN_PmrInfo(uint8_t idx, PlanChannelInfo_t *info)
{
    info->txShift = 0;
    info->flags   = PLAN_F_NARROW;
    if (idx >= PLAN_PMR_COUNT) {
        info->rx = 7000000u + SimpleUk[(idx - PLAN_PMR_COUNT) % ARRAY_SIZE(SimpleUk)] * 625u;
        return;
    }
    info->rx     = 44600625u + idx * 1250u;
    info->flags |= PLAN_F_LOW;   // (PLAN_F_LOW: capped at 0.5 W like GMRS 8-14)
}

void PLAN_PmrLabel(uint8_t idx, char *out)
{
    if (idx < PLAN_PMR_COUNT)
        sprintf(out, "%u", idx + 1u);
    else
        sprintf(out, "U%u", idx - PLAN_PMR_COUNT + 1u);
}

bool PLAN_IsPmrFrequency(uint32_t frequency, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        PlanChannelInfo_t info;
        PLAN_PmrInfo(i, &info);
        if (info.rx == frequency)
            return true;
    }
    return false;
}

void PLAN_GmrsLabel(uint8_t idx, char *out)
{
    if (idx < GMRS_REPEATER_FIRST)
        sprintf(out, "%u", idx + 1u);
    else if (idx < PLAN_GMRS_SAVED)
        sprintf(out, "%uR", idx - GMRS_REPEATER_FIRST + 15u);
    else
        sprintf(out, "M%u", idx - PLAN_GMRS_SAVED + 1u);
}

// ---------------------------------------------------------------------------
// VHF airband: nationwide US frequencies (47 CFR Part 87), receive only, AM.
// Tower / ground / ATIS frequencies are airport-specific: press VFO on any of
// these to tune the band. RX = 118.000 MHz + step x 25 kHz.
// ---------------------------------------------------------------------------

enum { A_GUARD = 0, A_SAR, A_AIRAIR, A_HELI, A_MULTICOM, A_UNICOM };

static const char AirUses[6][9] = { "GUARD", "SAR", "AIR-AIR", "HELI A-A", "MULTICOM", "UNICOM" };

#define AIR(khz, use) { (uint8_t)(((khz) - 118000u) / 25u), (use) }

static const uint8_t AirChannels[PLAN_AIR_COUNT][2] = {
    AIR(121500, A_GUARD),      // 87.217(b), 87.375(d): emergency and distress only
    AIR(123100, A_SAR),        // 87.375(a): search and rescue missions
    AIR(122750, A_AIRAIR),     // 87.187(j): private fixed-wing air-to-air
    AIR(123025, A_HELI),       // 87.187(j): helicopter air-to-air
    AIR(122900, A_MULTICOM),   // multicom (FAA AIM); 87.375(b): SAR training
    AIR(122950, A_UNICOM),     // 87.217(a)(1): unicom at towered airports
    AIR(122700, A_UNICOM),     // 87.217(a)(2): unicom at all other airports
    AIR(122725, A_UNICOM),
    AIR(122800, A_UNICOM),
    AIR(122975, A_UNICOM),
    AIR(123000, A_UNICOM),
    AIR(123050, A_UNICOM),
    AIR(123075, A_UNICOM),
};

void PLAN_AirInfo(uint8_t idx, PlanChannelInfo_t *info)
{
    info->rx      = 11800000u + AirChannels[idx < PLAN_AIR_COUNT ? idx : 0][0] * 2500u;
    info->txShift = 0;
    info->flags   = PLAN_F_RX_ONLY;   // aircraft / ground station licence needed to transmit
}

const char *PLAN_AirUse(uint8_t idx)
{
    return AirUses[AirChannels[idx < PLAN_AIR_COUNT ? idx : 0][1]];
}

// ---------------------------------------------------------------------------
// Ham: calling frequencies and APRS for the selected region (HamRgn).
// North America (US / Canada): ARRL band plan and regional coordinators; IARU
// Region 1: its band plan as the RSGB publishes it. SSB / CW ones are receive
// only here (the radio transmits FM only). North America has no designated VHF /
// UHF CW calling frequency: 50.090 sits in the 6 m CW activity (50.080-50.100).
// (The ISS is left for a feature of its own: split, tone, Doppler.)
// 4 bytes per channel: kHz | flags | name.
// ---------------------------------------------------------------------------

enum { H_2M, H_70CM, H_125M, H_6M, H_10M, H_APRS,
       H_2MSSB, H_70CMSSB, H_6MSSB, H_6MCW, H_2MCW, H_70CMCW };

static const char HamUses[][11] = {
    "2M CALL", "70CM CALL", "1.25M CALL", "6M CALL", "10M CALL", "APRS",
    "2M SSB", "70CM SSB", "6M SSB", "6M CW", "2M CW", "70CM CW",
};

#define HAM_RX  (1ul << 20)   // receive only
#define HAM_SSB (1ul << 21)   // USB, narrow, receive only
#define HAM(khz, use, f) ((uint32_t)(khz) | (f) | ((uint32_t)(use) << 24))

static const uint32_t HamChannels[2][PLAN_HAM_COUNT] = {
    {   // North America
        HAM(146520, H_2M, 0),       HAM(446000, H_70CM, 0),      HAM(223500, H_125M, 0),
        HAM( 52525, H_6M, 0),       HAM( 29600, H_10M, 0),       HAM(144390, H_APRS, 0),
        HAM(144200, H_2MSSB, HAM_SSB), HAM(432100, H_70CMSSB, HAM_SSB),
        HAM( 50125, H_6MSSB, HAM_SSB), HAM( 50090, H_6MCW, HAM_SSB),
    },
    {   // IARU Region 1 (no 1.25 m band; CW centres of activity)
        HAM(145500, H_2M, 0),       HAM(433500, H_70CM, 0),      HAM( 51510, H_6M, 0),
        HAM( 29600, H_10M, 0),      HAM(144800, H_APRS, 0),
        HAM(144300, H_2MSSB, HAM_SSB), HAM(432200, H_70CMSSB, HAM_SSB), HAM( 50150, H_6MSSB, HAM_SSB),
        HAM(144050, H_2MCW, HAM_SSB),  HAM(432050, H_70CMCW, HAM_SSB),
    },
};

const char * const gSubMenu_HAM_REGION[PLAN_REGION_COUNT] = { "US", "CANADA", "INTL", "UK" };   // (INTL: IARU R1)

static uint32_t HamEntry(uint8_t region, uint8_t idx)
{
    return HamChannels[region == HAM_REGION_R1][idx < PLAN_HAM_COUNT ? idx : 0];
}

void PLAN_HamInfo(uint8_t region, uint8_t idx, PlanChannelInfo_t *info)
{
    const uint32_t e = HamEntry(region, idx);
    info->rx      = (e & 0xFFFFFu) * 100u;
    info->txShift = 0;
    info->flags   = (e & HAM_SSB) ? (PLAN_F_RX_ONLY | PLAN_F_USB | PLAN_F_NARROW)
                  : (e & HAM_RX)  ? PLAN_F_RX_ONLY : 0;
}

const char *PLAN_HamUse(uint8_t region, uint8_t idx)
{
    return HamUses[HamEntry(region, idx) >> 24];
}

// The region's ham bands this radio reaches (10 m, 6 m, 2 m, 1.25 m, 70 cm): edges
// in 10 kHz, the band's calling channel (HamChannels index) and its icon digit.
// They are the TX limits (HamRgn) and what the F editor's BAND item steps through.
typedef struct {
    uint16_t lo, hi;
    uint8_t  call;
    char     digit;
} HamBand_t;

static const HamBand_t HamBands[HAM_REGION_COUNT][PLAN_HAM_BANDS] = {
    {   // US (FCC Part 97)
        { 2800, 2970, 4, '1' }, { 5000, 5400, 3, '6' }, { 14400, 14800, 0, '2' },
        { 22200, 22500, 2, '3' }, { 42000, 45000, 1, '7' },
    },
    {   // Canada (RBR-4)
        { 2800, 2970, 4, '1' }, { 5000, 5400, 3, '6' }, { 14400, 14800, 0, '2' },
        { 22200, 22500, 2, '3' }, { 43000, 45000, 1, '7' },
    },
    {   // IARU R1 (no 1.25 m)
        { 2800, 2970, 3, '1' }, { 5000, 5200, 2, '6' }, { 14400, 14600, 0, '2' },
        { 43000, 44000, 1, '7' },
    },
};

uint8_t PLAN_HamBandCount(uint8_t region)
{
    return region == HAM_REGION_R1 ? 4u : 5u;
}

int8_t PLAN_HamBandOf(uint8_t region, uint32_t frequency)
{
    region = region < HAM_REGION_COUNT ? region : 0;
    for (uint8_t i = 0; i < PLAN_HamBandCount(region); i++)
        if (frequency >= HamBands[region][i].lo * 1000ul && frequency < HamBands[region][i].hi * 1000ul)
            return (int8_t)i;
    return -1;
}

bool PLAN_HamTxAllowed(uint8_t region, uint32_t frequency)
{
    return PLAN_HamBandOf(region, frequency) >= 0;
}

uint32_t PLAN_HamBandCall(uint8_t region, uint8_t band)
{
    PlanChannelInfo_t info;
    PLAN_HamInfo(region, HamBands[region < HAM_REGION_COUNT ? region : 0][band % PLAN_HAM_BANDS].call, &info);
    return info.rx;
}

char PLAN_HamBandDigit(uint8_t region, uint32_t frequency)
{
    const int8_t b = PLAN_HamBandOf(region, frequency);
    return b < 0 ? '-' : HamBands[region < HAM_REGION_COUNT ? region : 0][b].digit;
}

static const char SlotDigit[SLOT_COUNT] = { '1', '6', 0, 0, '2', '3', '7', 0 };   // the ham slots' bands

// The region's ham band of a slot, -1: not a ham slot (or not in the region)
static int8_t SlotHamBand(uint8_t region, uint8_t slot)
{
    region = region < HAM_REGION_COUNT ? region : 0;
    for (uint8_t i = 0; SlotDigit[slot] && i < PLAN_HamBandCount(region); i++)
        if (HamBands[region][i].digit == SlotDigit[slot])
            return (int8_t)i;
    return -1;
}

uint8_t PLAN_SlotOf(uint8_t region, uint32_t frequency)
{
    if (frequency >= 8750000u && frequency < 10800000u)
        return SLOT_FM;
    if (frequency >= 10800000u && frequency < 13700000u)
        return SLOT_AIR;
    const char d = PLAN_HamBandDigit(region, frequency);
    for (uint8_t s = 0; s < SLOT_GEN; s++)
        if (SlotDigit[s] == d)
            return s;
    return SLOT_GEN;
}

bool PLAN_SlotRange(uint8_t region, uint8_t slot, uint32_t *lo, uint32_t *hi)
{
    if (slot == SLOT_FM || slot == SLOT_AIR) {
        *lo = slot == SLOT_FM ? 8750000u : 10800000u;
        *hi = slot == SLOT_FM ? 10800000u : 13700000u;
        return true;
    }
    const int8_t b = SlotHamBand(region, slot);
    if (b < 0)
        return false;
    *lo = HamBands[region < HAM_REGION_COUNT ? region : 0][b].lo * 1000ul;
    *hi = HamBands[region < HAM_REGION_COUNT ? region : 0][b].hi * 1000ul;
    return true;
}

uint32_t PLAN_SlotDefault(uint8_t region, uint8_t slot)
{
    if (slot == SLOT_FM)
        return 10000000u;   // 100.0 MHz
    if (slot == SLOT_AIR)
        return 12150000u;   // 121.5 MHz guard
    const int8_t b = SlotHamBand(region, slot);
    return b < 0 ? 15500000u : PLAN_HamBandCall(region, (uint8_t)b);   // general: 155.0 MHz
}

// ---------------------------------------------------------------------------
// VHF marine, 2 bytes per channel. Frequencies follow from the channel number
// (ITU-R M.1084 numbering): ship TX = 156.050 + (n-1) x 50 kHz for 1-28 and
// 156.025 + (n-60) x 50 kHz for 60-88; the coast frequency is 4.6 MHz higher.
// RX is on the coast side for duplex, "B" (coast-only) and AIS channels.
//   bits 0-6  channel number (101-107 = WX1-WX7)
//   bits 7-8  label suffix: none / A / B / "AIS" (87 -> AIS1, 88 -> AIS2)
//   bit  9    duplex, bit 10 low power by default, bit 11 receive only
//   bits 12-15 channel use (MarineUses)
// RX-only covers receive-only broadcasts, DSC/AIS data channels and channels
// reserved to government / coast guard stations.
// ---------------------------------------------------------------------------

enum { SFX_NONE = 0, SFX_A, SFX_B, SFX_AIS };
#define M_DUP (1u << 9)
#define M_LOW (1u << 10)
#define M_RXO (1u << 11)
#define MCH(num, sfx, fl) (uint16_t)((num) | ((sfx) << 7) | (fl))

// Channel use (bits 12-15), shown under the channel number.
enum {
    U_DISTRESS = 0u << 12,
    U_CALLING = 1u << 12,
    U_BRIDGE = 2u << 12,
    U_SAFETY = 3u << 12,
    U_PORTOPS = 4u << 12,
    U_COMMERCL = 5u << 12,
    U_NONCOMM = 6u << 12,
    U_INTERSHP = 7u << 12,
    U_PHONE = 8u << 12,
    U_CGONLY = 9u << 12,
    U_CGLIAIS = 10u << 12,
    U_GOV = 11u << 12,
    U_WEATHER = 12u << 12,
    U_BROADCST = 13u << 12,
    U_DSC = 14u << 12,
    U_AIS = 15u << 12,
};

static const char MarineUses[16][9] = {
    "DISTRESS",
    "CALLING",
    "BRIDGE",
    "SAFETY",
    "PORT OPS",
    "COMMERCL",
    "NONCOMM",
    "INTERSHP",
    "PHONE",
    "CG ONLY",
    "CG LIAIS",
    "GOV",
    "WEATHER",
    "BROADCST",
    "DSC",
    "AIS",
};

#define MCH_NUM(e) ((e) & 0x7Fu)
#define MCH_SFX(e) (((e) >> 7) & 3u)
#define WX_FIRST   101u

// WX1-WX7 RX frequencies, 25 kHz steps above 162.400 MHz.
static const uint8_t WxSteps[7] = { 6, 0, 3, 1, 2, 4, 5 };

// US: USCG NAVCEN "US VHF Channel Information". 13 and 67 default to 1 W
// (47 CFR 80.215(e)). 21A/23A/81A/82A/83A are US Government / Coast Guard only.
// Canada: ISED RBR-2 Schedule I. 15/17/20/75/76 are 1 W in every area. 21B/23B/
// 25B/28B/83B are Continuous Marine Broadcast (receive only); 87B/88B are AIS.
// International: ITU Radio Regulations Appendix 18 (Rev. WRC-15). 15/17/75/76 are
// 1 W (notes g, n); 70 is DSC only. Ch 87 is simplex 157.375 (161.975 = AIS 1).
static const uint16_t MarineChannelsUS[] = {
    MCH(  1, SFX_A   , U_PORTOPS),
    MCH(  5, SFX_A   , U_PORTOPS),
    MCH(  6, SFX_NONE, U_SAFETY),
    MCH(  7, SFX_A   , U_COMMERCL),
    MCH(  8, SFX_NONE, U_COMMERCL),
    MCH(  9, SFX_NONE, U_CALLING),
    MCH( 10, SFX_NONE, U_COMMERCL),
    MCH( 11, SFX_NONE, U_COMMERCL),
    MCH( 12, SFX_NONE, U_PORTOPS),
    MCH( 13, SFX_NONE, M_LOW | U_BRIDGE),
    MCH( 14, SFX_NONE, U_PORTOPS),
    MCH( 15, SFX_NONE, M_RXO | U_BROADCST),
    MCH( 16, SFX_NONE, U_DISTRESS),
    MCH( 17, SFX_NONE, U_GOV),
    MCH( 18, SFX_A   , U_COMMERCL),
    MCH( 19, SFX_A   , U_COMMERCL),
    MCH( 20, SFX_NONE, M_DUP | U_PORTOPS),
    MCH( 20, SFX_A   , U_PORTOPS),
    MCH( 21, SFX_A   , M_RXO | U_CGONLY),
    MCH( 22, SFX_A   , U_CGLIAIS),
    MCH( 23, SFX_A   , M_RXO | U_CGONLY),
    MCH( 24, SFX_NONE, M_DUP | U_PHONE),
    MCH( 25, SFX_NONE, M_DUP | U_PHONE),
    MCH( 26, SFX_NONE, M_DUP | U_PHONE),
    MCH( 27, SFX_NONE, M_DUP | U_PHONE),
    MCH( 28, SFX_NONE, M_DUP | U_PHONE),
    MCH( 63, SFX_A   , U_PORTOPS),
    MCH( 65, SFX_A   , U_PORTOPS),
    MCH( 66, SFX_A   , U_PORTOPS),
    MCH( 67, SFX_NONE, M_LOW | U_COMMERCL),
    MCH( 68, SFX_NONE, U_NONCOMM),
    MCH( 69, SFX_NONE, U_NONCOMM),
    MCH( 70, SFX_NONE, M_RXO | U_DSC),
    MCH( 71, SFX_NONE, U_NONCOMM),
    MCH( 72, SFX_NONE, U_NONCOMM),
    MCH( 73, SFX_NONE, U_PORTOPS),
    MCH( 74, SFX_NONE, U_PORTOPS),
    MCH( 77, SFX_NONE, U_PORTOPS),
    MCH( 78, SFX_A   , U_NONCOMM),
    MCH( 79, SFX_A   , U_COMMERCL),
    MCH( 80, SFX_A   , U_COMMERCL),
    MCH( 81, SFX_A   , M_RXO | U_GOV),
    MCH( 82, SFX_A   , M_RXO | U_GOV),
    MCH( 83, SFX_A   , M_RXO | U_CGONLY),
    MCH( 84, SFX_NONE, M_DUP | U_PHONE),
    MCH( 85, SFX_NONE, M_DUP | U_PHONE),
    MCH( 86, SFX_NONE, M_DUP | U_PHONE),
    MCH( 87, SFX_NONE, U_PHONE),
    MCH( 88, SFX_NONE, U_COMMERCL),
    MCH( 87, SFX_AIS , M_RXO | U_AIS),
    MCH( 88, SFX_AIS , M_RXO | U_AIS),
    MCH(101, SFX_NONE, M_RXO | U_WEATHER),
    MCH(102, SFX_NONE, M_RXO | U_WEATHER),
    MCH(103, SFX_NONE, M_RXO | U_WEATHER),
    MCH(104, SFX_NONE, M_RXO | U_WEATHER),
    MCH(105, SFX_NONE, M_RXO | U_WEATHER),
    MCH(106, SFX_NONE, M_RXO | U_WEATHER),
    MCH(107, SFX_NONE, M_RXO | U_WEATHER),
};

static const uint16_t MarineChannelsCA[] = {
    MCH(  1, SFX_NONE, M_DUP | U_PHONE),
    MCH(  2, SFX_NONE, M_DUP | U_PHONE),
    MCH(  3, SFX_NONE, M_DUP | U_PHONE),
    MCH(  4, SFX_A   , U_COMMERCL),
    MCH(  5, SFX_A   , U_PORTOPS),
    MCH(  6, SFX_NONE, U_SAFETY),
    MCH(  7, SFX_A   , U_COMMERCL),
    MCH(  8, SFX_NONE, U_COMMERCL),
    MCH(  9, SFX_NONE, U_COMMERCL),
    MCH( 10, SFX_NONE, U_COMMERCL),
    MCH( 11, SFX_NONE, U_PORTOPS),
    MCH( 12, SFX_NONE, U_PORTOPS),
    MCH( 13, SFX_NONE, U_BRIDGE),
    MCH( 14, SFX_NONE, U_PORTOPS),
    MCH( 15, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 16, SFX_NONE, U_DISTRESS),
    MCH( 17, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 18, SFX_A   , U_COMMERCL),
    MCH( 19, SFX_A   , U_COMMERCL),
    MCH( 20, SFX_NONE, M_DUP | M_LOW | U_PORTOPS),
    MCH( 21, SFX_A   , M_RXO | U_CGONLY),
    MCH( 21, SFX_B   , M_RXO | U_BROADCST),
    MCH( 22, SFX_A   , U_CGLIAIS),
    MCH( 23, SFX_NONE, M_DUP | U_PHONE),
    MCH( 23, SFX_B   , M_RXO | U_BROADCST),
    MCH( 24, SFX_NONE, M_DUP | U_PHONE),
    MCH( 25, SFX_NONE, M_DUP | U_PHONE),
    MCH( 25, SFX_B   , M_RXO | U_BROADCST),
    MCH( 26, SFX_NONE, M_DUP | U_PHONE),
    MCH( 27, SFX_NONE, M_DUP | U_PHONE),
    MCH( 28, SFX_NONE, M_DUP | U_PHONE),
    MCH( 28, SFX_B   , M_RXO | U_BROADCST),
    MCH( 60, SFX_NONE, M_DUP | U_PHONE),
    MCH( 61, SFX_A   , U_COMMERCL),
    MCH( 62, SFX_A   , U_COMMERCL),
    MCH( 63, SFX_A   , U_COMMERCL),
    MCH( 64, SFX_NONE, M_DUP | U_PHONE),
    MCH( 64, SFX_A   , U_COMMERCL),
    MCH( 65, SFX_A   , U_PORTOPS),
    MCH( 66, SFX_A   , U_PORTOPS),
    MCH( 67, SFX_NONE, U_INTERSHP),
    MCH( 68, SFX_NONE, U_NONCOMM),
    MCH( 69, SFX_NONE, U_INTERSHP),
    MCH( 70, SFX_NONE, M_RXO | U_DSC),
    MCH( 71, SFX_NONE, U_PORTOPS),
    MCH( 72, SFX_NONE, U_INTERSHP),
    MCH( 73, SFX_NONE, U_INTERSHP),
    MCH( 74, SFX_NONE, U_PORTOPS),
    MCH( 75, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 76, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 77, SFX_NONE, U_PORTOPS),
    MCH( 78, SFX_A   , U_COMMERCL),
    MCH( 79, SFX_A   , U_COMMERCL),
    MCH( 80, SFX_A   , U_COMMERCL),
    MCH( 81, SFX_A   , M_RXO | U_CGONLY),
    MCH( 82, SFX_A   , M_RXO | U_CGONLY),
    MCH( 83, SFX_A   , M_RXO | U_GOV),
    MCH( 83, SFX_B   , M_RXO | U_BROADCST),
    MCH( 84, SFX_NONE, M_DUP | U_PHONE),
    MCH( 85, SFX_NONE, M_DUP | U_PHONE),
    MCH( 86, SFX_NONE, M_DUP | U_PHONE),
    MCH( 87, SFX_NONE, U_PORTOPS),
    MCH( 87, SFX_B   , M_RXO | U_AIS),
    MCH( 88, SFX_NONE, U_PORTOPS),
    MCH( 88, SFX_B   , M_RXO | U_AIS),
    MCH(101, SFX_NONE, M_RXO | U_WEATHER),
    MCH(102, SFX_NONE, M_RXO | U_WEATHER),
    MCH(103, SFX_NONE, M_RXO | U_WEATHER),
};

static const uint16_t MarineChannelsINTL[] = {
    MCH(  1, SFX_NONE, M_DUP | U_PHONE),
    MCH(  2, SFX_NONE, M_DUP | U_PHONE),
    MCH(  3, SFX_NONE, M_DUP | U_PHONE),
    MCH(  4, SFX_NONE, M_DUP | U_PHONE),
    MCH(  5, SFX_NONE, M_DUP | U_PHONE),
    MCH(  6, SFX_NONE, U_SAFETY),
    MCH(  7, SFX_NONE, M_DUP | U_PHONE),
    MCH(  8, SFX_NONE, U_INTERSHP),
    MCH(  9, SFX_NONE, U_INTERSHP),
    MCH( 10, SFX_NONE, U_INTERSHP),
    MCH( 11, SFX_NONE, U_PORTOPS),
    MCH( 12, SFX_NONE, U_PORTOPS),
    MCH( 13, SFX_NONE, U_BRIDGE),
    MCH( 14, SFX_NONE, U_PORTOPS),
    MCH( 15, SFX_NONE, M_LOW | U_INTERSHP),
    MCH( 16, SFX_NONE, U_DISTRESS),
    MCH( 17, SFX_NONE, M_LOW | U_INTERSHP),
    MCH( 18, SFX_NONE, M_DUP | U_PHONE),
    MCH( 19, SFX_NONE, M_DUP | U_PHONE),
    MCH( 20, SFX_NONE, M_DUP | U_PHONE),
    MCH( 21, SFX_NONE, M_DUP | U_PHONE),
    MCH( 22, SFX_NONE, M_DUP | U_PHONE),
    MCH( 23, SFX_NONE, M_DUP | U_PHONE),
    MCH( 24, SFX_NONE, M_DUP | U_PHONE),
    MCH( 25, SFX_NONE, M_DUP | U_PHONE),
    MCH( 26, SFX_NONE, M_DUP | U_PHONE),
    MCH( 27, SFX_NONE, M_DUP | U_PHONE),
    MCH( 28, SFX_NONE, M_DUP | U_PHONE),
    MCH( 60, SFX_NONE, M_DUP | U_PHONE),
    MCH( 61, SFX_NONE, M_DUP | U_PHONE),
    MCH( 62, SFX_NONE, M_DUP | U_PHONE),
    MCH( 63, SFX_NONE, M_DUP | U_PHONE),
    MCH( 64, SFX_NONE, M_DUP | U_PHONE),
    MCH( 65, SFX_NONE, M_DUP | U_PHONE),
    MCH( 66, SFX_NONE, M_DUP | U_PHONE),
    MCH( 67, SFX_NONE, U_INTERSHP),
    MCH( 68, SFX_NONE, U_INTERSHP),
    MCH( 69, SFX_NONE, U_INTERSHP),
    MCH( 70, SFX_NONE, M_RXO | U_DSC),
    MCH( 71, SFX_NONE, U_PORTOPS),
    MCH( 72, SFX_NONE, U_INTERSHP),
    MCH( 73, SFX_NONE, U_INTERSHP),
    MCH( 74, SFX_NONE, U_PORTOPS),
    MCH( 75, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 76, SFX_NONE, M_LOW | U_PORTOPS),
    MCH( 77, SFX_NONE, U_INTERSHP),
    MCH( 78, SFX_NONE, M_DUP | U_PHONE),
    MCH( 79, SFX_NONE, M_DUP | U_PHONE),
    MCH( 80, SFX_NONE, M_DUP | U_PHONE),
    MCH( 81, SFX_NONE, M_DUP | U_PHONE),
    MCH( 82, SFX_NONE, M_DUP | U_PHONE),
    MCH( 83, SFX_NONE, M_DUP | U_PHONE),
    MCH( 84, SFX_NONE, M_DUP | U_PHONE),
    MCH( 85, SFX_NONE, M_DUP | U_PHONE),
    MCH( 86, SFX_NONE, M_DUP | U_PHONE),
    MCH( 87, SFX_NONE, U_PORTOPS),
    MCH( 88, SFX_NONE, U_INTERSHP),
    MCH( 87, SFX_AIS , M_RXO | U_AIS),
    MCH( 88, SFX_AIS , M_RXO | U_AIS),
};


typedef struct {
    const uint16_t *channels;
    uint8_t         count;
} MarinePlanTable_t;

static const MarinePlanTable_t MarinePlans[MARINE_PLAN_COUNT] = {
    [MARINE_PLAN_US]   = { MarineChannelsUS,   ARRAY_SIZE(MarineChannelsUS)   },
    [MARINE_PLAN_CA]   = { MarineChannelsCA,   ARRAY_SIZE(MarineChannelsCA)   },
    [MARINE_PLAN_INTL] = { MarineChannelsINTL, ARRAY_SIZE(MarineChannelsINTL) },
};

_Static_assert(ARRAY_SIZE(MarineChannelsUS)   <= PLAN_MARINE_MAX, "PLAN_MARINE_MAX too small");
_Static_assert(ARRAY_SIZE(MarineChannelsCA)   <= PLAN_MARINE_MAX, "PLAN_MARINE_MAX too small");
_Static_assert(ARRAY_SIZE(MarineChannelsINTL) <= PLAN_MARINE_MAX, "PLAN_MARINE_MAX too small");

const char * const gSubMenu_MARINE_PLAN[MARINE_PLAN_COUNT] = {
    "US",
    "CANADA",
    "INTL",
};

uint8_t PLAN_MarineCount(uint8_t plan)
{
    return MarinePlans[plan].count;
}

static uint16_t MarineEntry(uint8_t plan, uint8_t idx)
{
    const MarinePlanTable_t *p = &MarinePlans[plan];
    return p->channels[idx < p->count ? idx : 0];
}

void PLAN_MarineInfo(uint8_t plan, uint8_t idx, PlanChannelInfo_t *info)
{
    const uint16_t e   = MarineEntry(plan, idx);
    const uint8_t  num = MCH_NUM(e);

    info->flags   = ((e & M_DUP) ? PLAN_F_DUPLEX : 0) | ((e & M_LOW) ? PLAN_F_LOW : 0) | ((e & M_RXO) ? PLAN_F_RX_ONLY : 0);
    info->txShift = (e & M_DUP) ? -460000 : 0;   // ship TX is 4.6 MHz below the coast side

    if (num >= WX_FIRST) {
        info->rx = 16240000u + WxSteps[num - WX_FIRST] * 2500u;
        return;
    }

    const uint32_t ship = (num <= 28u) ? 15605000u + (num - 1u) * 5000u : 15602500u + (num - 60u) * 5000u;
    const bool     coast = (e & M_DUP) || MCH_SFX(e) == SFX_B || MCH_SFX(e) == SFX_AIS;
    info->rx = ship + (coast ? 460000u : 0u);
}

void PLAN_MarineLabel(uint8_t plan, uint8_t idx, char *out)
{
    const uint16_t e   = MarineEntry(plan, idx);
    const uint8_t  num = MCH_NUM(e);

    if (num >= WX_FIRST)
        sprintf(out, "WX%u", num - WX_FIRST + 1u);
    else if (MCH_SFX(e) == SFX_AIS)
        sprintf(out, "AIS%u", num - 86u);
    else
        sprintf(out, "%02u%s", num, MCH_SFX(e) == SFX_A ? "A" : MCH_SFX(e) == SFX_B ? "B" : "");
}

uint8_t PLAN_MarineFind(uint8_t plan, const char *label)
{
    char other[6];
    for (uint8_t i = 0; i < PLAN_MarineCount(plan); i++) {
        PLAN_MarineLabel(plan, i, other);
        if (strcmp(other, label) == 0)
            return i;
    }
    return 0xFF;
}

uint8_t PLAN_MarineEquivalent(uint8_t fromPlan, uint8_t idx, uint8_t toPlan)
{
    // Match on the channel number: same suffix first ("16" -> "16"), then any
    // suffix ("22A" <-> "22", "AIS1" <-> "87B" - the same 161.975 MHz channel).
    const uint16_t e   = MarineEntry(fromPlan, idx);
    const uint8_t  num = MCH_NUM(e);
    uint8_t        any = 0xFF;

    for (uint8_t i = 0; i < PLAN_MarineCount(toPlan); i++) {
        const uint16_t o = MarineEntry(toPlan, i);
        if (MCH_NUM(o) != num)
            continue;
        if (MCH_SFX(o) == MCH_SFX(e))
            return i;
        // prefer a coast-side match for coast-side channels (87B/AIS1 vs voice 87)
        const bool eCoast = MCH_SFX(e) >= SFX_B, oCoast = MCH_SFX(o) >= SFX_B;
        if (any == 0xFF || eCoast == oCoast)
            any = i;
    }
    if (any != 0xFF)
        return any;

    const uint8_t found = PLAN_MarineFind(toPlan, "16");
    return found != 0xFF ? found : 0;
}

const char *PLAN_MarineUse(uint8_t plan, uint8_t idx)
{
    return MarineUses[MarineEntry(plan, idx) >> 12];
}
