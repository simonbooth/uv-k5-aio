/* GMRS/FRS privacy sub-code numbering per radio make/model. See subcode.h. */

#include <stdbool.h>
#include <string.h>

#include "subcode.h"
#include "dcs.h"
#include "misc.h"
#include "service.h"
#include "external/printf/printf.h"


// The firmware's tone tables in the order every brand scheme is built from: the
// "approved" FRS/GMRS set is the full table minus these non-standard entries.
static const uint8_t CtcssExtraIdx[12] = {
     1, 26, 28, 30, 32, 34, 36, 38, 39, 41,
    45, 49
};

static const uint8_t DcsExtraIdx[21] = {
     5,  9, 19, 25, 34, 36, 41, 43, 44, 48,
    50, 54, 56, 60, 71, 72, 73, 74, 75, 82,
    83
};

// Position of `option` in `extra` (0xFF if absent).
static uint8_t ExtraPos(uint8_t option, const uint8_t *extra, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
        if (extra[i] == option)
            return i;
    return 0xFF;
}

// 0-based index among the approved tones, 0xFF for a non-standard tone.
static uint8_t ApprovedIndex(uint8_t option, const uint8_t *extra, uint8_t count)
{
    uint8_t below = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (extra[i] == option)
            return 0xFF;
        if (extra[i] < option)
            below++;
    }
    return option - below;
}

#define DCS_GetCtcssApprovedIndex(o) ApprovedIndex((o), CtcssExtraIdx, ARRAY_SIZE(CtcssExtraIdx))
#define DCS_GetDcsApprovedIndex(o)   ApprovedIndex((o), DcsExtraIdx,   ARRAY_SIZE(DcsExtraIdx))
#define DCS_GetCtcssExtraPos(o)      ExtraPos((o), CtcssExtraIdx, ARRAY_SIZE(CtcssExtraIdx))
#define DCS_GetDcsExtraPos(o)        ExtraPos((o), DcsExtraIdx,   ARRAY_SIZE(DcsExtraIdx))
#define DCS_GetOption(o)             DCS_Options[(o)]

// ---------------------------------------------------------------------------
// Privacy sub-codes
//
// Numbering source: K0TFU "FRS/GMRS Privacy Codes Demystified" and its sheet.
// Every scheme is a permutation/subset of the firmware's own tone tables:
//   CTCSS Azure   = CTCSS_Options minus the 12 CTCSS_ExtraIdx tones (approved index)
//   CTCSS Indigo  = Azure, 39 = 62.5 Hz (unsupported), 40-51 = CTCSS_ExtraIdx in order
//   CTCSS Crimson = all 50 CTCSS_Options in order
//   DCS   Lime    = DCS options minus the 21 DCS_ExtraIdx codes (approved index)
//   DCS   Jade    = Lime, then 84-104 = DCS_ExtraIdx in order
//   DCS   Lilac   = all 104 in order, plus D645 at 94 (unsupported)
// ---------------------------------------------------------------------------

const char * const gSubMenu_SUBCODE[SUBCODE_BRAND_COUNT] = {
    "MOTO\nUNIDEN\nBCA",
    "COBRA",
    "MIDLAND",
    "RUGGED",
    "BAOFENG\nRETEVIS",
};

typedef struct {
    uint8_t ctMax;
    uint8_t dcsMax;
    bool    combined;   // DCS numbers continue after the CTCSS range (39..121)
} SubCodeScheme_t;

static const SubCodeScheme_t SubCodeSchemes[SUBCODE_PROFILE_COUNT] = {
    [SUBCODE_MOTO]    = { 38,  83, true  },
    [SUBCODE_COBRA]   = { 38,  83, false },
    [SUBCODE_MIDLAND] = { 38, 104, false },
    [SUBCODE_RUGGED]  = { 51, 104, false },
    [SUBCODE_BAOFENG] = { 50, 105, false },
    [SUBCODE_PMR446]  = { 38,   0, false },
    [SUBCODE_OFCOM]   = { 42,  51, false },
};

// Ofcom (OfW164): the CTCSS table without these (100.0, 159.8 .. 199.5 Hz), its own
// 198 Hz at C32 (not a standard tone: unsupported); DCS only the 51 of table 4.
static const uint8_t OfcomCtExtraIdx[9] = { 12, 26, 28, 30, 32, 34, 36, 38, 39 };
static const uint8_t OfcomDcsMask[13]   = {
    0x7F, 0xFE, 0xFF, 0x7B, 0x26, 0x3A, 0xF1, 0x87, 0x30, 0x40, 0xC0, 0x22, 0x00,
};

static uint8_t SUBCODE_CtNumber(uint8_t profile, uint8_t option)
{
    if (profile == SUBCODE_BAOFENG)
        return option + 1u;
    if (profile == SUBCODE_OFCOM) {
        const uint8_t n = ApprovedIndex(option, OfcomCtExtraIdx, ARRAY_SIZE(OfcomCtExtraIdx));
        return n == 0xFF ? 0 : n + (n >= 31u ? 2u : 1u);   // past C32 (198 Hz)
    }

    const uint8_t approved = DCS_GetCtcssApprovedIndex(option);
    if (approved != 0xFF)
        return approved + 1u;

    if (profile == SUBCODE_RUGGED)
        return 40u + DCS_GetCtcssExtraPos(option);

    return 0;
}

static uint8_t SUBCODE_DcsNumber(uint8_t profile, uint8_t option)
{
    if (profile == SUBCODE_PMR446)
        return 0;   // CTCSS only
    if (profile == SUBCODE_OFCOM) {   // its place among table 4's codes
        if (!(OfcomDcsMask[option / 8u] >> (option % 8u) & 1u))
            return 0;
        uint8_t n = 1;
        for (uint8_t o = 0; o < option; o++)
            n += OfcomDcsMask[o / 8u] >> (o % 8u) & 1u;
        return n;
    }
    if (profile == SUBCODE_BAOFENG)
        return option + (option <= 92u ? 1u : 2u);   // skip 94 = D645, not in the radio's table

    const uint8_t approved = DCS_GetDcsApprovedIndex(option);
    if (approved != 0xFF)
        return approved + 1u;

    if (profile == SUBCODE_MIDLAND || profile == SUBCODE_RUGGED)
        return 84u + DCS_GetDcsExtraPos(option);

    return 0;
}

// Unified key: 0 = off, 1..ctMax = CTCSS, ctMax+1..ctMax+dcsMax = DCS.
static uint8_t SUBCODE_Key(uint8_t profile, DCS_CodeType_t type, uint8_t code)
{
    if (type == CODE_TYPE_CONTINUOUS_TONE)
        return SUBCODE_CtNumber(profile, code);
    if (type == CODE_TYPE_DIGITAL) {
        const uint8_t n = SUBCODE_DcsNumber(profile, code);
        return n ? SubCodeSchemes[profile].ctMax + n : 0;
    }
    return 0;
}

static bool SUBCODE_ToneForKey(uint8_t profile, uint8_t key, DCS_CodeType_t *type, uint8_t *code)
{
    if (key == 0) {
        *type = CODE_TYPE_OFF;
        *code = 0;
        return true;
    }

    for (uint8_t opt = 0; opt < ARRAY_SIZE(CTCSS_Options); opt++)
        if (SUBCODE_CtNumber(profile, opt) == key) {
            *type = CODE_TYPE_CONTINUOUS_TONE;
            *code = opt;
            return true;
        }

    for (uint8_t opt = 0; opt < DCS_OPTION_COUNT; opt++)
        if (SUBCODE_Key(profile, CODE_TYPE_DIGITAL, opt) == key) {
            *type = CODE_TYPE_DIGITAL;
            *code = opt;
            return true;
        }

    return false;
}

uint8_t SUBCODE_FromTone(uint16_t channel, DCS_CodeType_t type, uint8_t code, bool *isSeparateDcs)
{
    const uint8_t          profile = SERVICE_SubCodeProfileFor(channel);
    const SubCodeScheme_t *scheme  = &SubCodeSchemes[profile];

    *isSeparateDcs = false;

    if (type == CODE_TYPE_OFF)
        return 0;
    if (SERVICE_OfChannel(channel) != SERVICE_GMRS)
        return 0xFF;   // numbered only in the walkie-talkie mode; elsewhere the tone itself

    const uint8_t key = SUBCODE_Key(profile, type, code);
    if (key == 0)
        return 0xFF;

    if (key > scheme->ctMax && !scheme->combined) {
        *isSeparateDcs = true;
        return key - scheme->ctMax;
    }
    return key;
}

void SUBCODE_Format(uint16_t channel, DCS_CodeType_t type, uint8_t code, char *out)
{
    bool separate;
    const uint8_t n = SUBCODE_FromTone(channel, type, code, &separate);

    if (n == 0)
        strcpy(out, "OFF");
    else if (n != 0xFF) {   // Ofcom: as a UK licence schedule lists them (C13, D023)
        const bool ofcom = SERVICE_SubCodeProfileFor(channel) == SUBCODE_OFCOM;
        sprintf(out, separate ? (ofcom ? "D%03o" : "d%u") : (ofcom ? "C%u" : "%u"),
                separate && ofcom ? DCS_GetOption(code) : n);
    }
    else if (type == CODE_TYPE_CONTINUOUS_TONE)
        sprintf(out, "%u.%u", CTCSS_Options[code] / 10u, CTCSS_Options[code] % 10u);
    else
        sprintf(out, "D%03o%c", DCS_GetOption(code), type == CODE_TYPE_REVERSE_DIGITAL ? 'I' : 'N');
}

void SUBCODE_Step(uint16_t channel, int8_t direction, DCS_CodeType_t *type, uint8_t *code)
{
    const uint8_t profile = SERVICE_SubCodeProfileFor(channel);
    const uint8_t count   = SubCodeSchemes[profile].ctMax + SubCodeSchemes[profile].dcsMax + 1u;
    uint8_t       key     = SUBCODE_Key(profile, *type, *code);

    // Gaps (62.5 Hz, D645) have no tone in this radio: keep stepping past them.
    for (uint8_t tries = 0; tries < count; tries++) {
        key = (uint8_t)((key + count + direction) % count);
        if (SUBCODE_ToneForKey(profile, key, type, code))
            return;
    }
}
