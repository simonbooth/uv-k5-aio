/* Built-in channel plans for the service modes (see service.h). Pure data: no
 * radio state, so the tables can be compiled and checked on a host.
 */

#ifndef SVC_PLANS_H
#define SVC_PLANS_H

#include <stdbool.h>
#include <stdint.h>

#define PLAN_GMRS_COUNT  35u   // 1-22 + 15R-22R + MURS M1-M5
#define PLAN_GMRS_SAVED  30u   // the ones with their own saved tones / power (not MURS)
#define PLAN_MARINE_MAX  68u   // largest marine plan (Canada); sizes the channel-index range
#define PLAN_AIR_COUNT   13u   // common VHF airband channels (receive only, AM)
#define PLAN_HAM_COUNT   10u   // ham calling / APRS channels, ahead of the memories

enum {
    PLAN_F_RX_ONLY = 1u << 0,   // no TX at all (receive-only, data-only or government-only)
    PLAN_F_LOW     = 1u << 1,   // low power by rule (the default; power stays user-editable)
    PLAN_F_NARROW  = 1u << 2,   // 12.5 kHz channel
    PLAN_F_DUPLEX  = 1u << 3,   // TX on a split frequency (GMRS repeater input / marine ship TX)
    PLAN_F_USB     = 1u << 4,   // SSB / CW: USB demodulation (this radio transmits FM only)
    PLAN_F_MURS    = 1u << 5,   // MURS (47 CFR 95 J): 2 W, never FRS
};

typedef enum {
    MARINE_PLAN_US = 0,
    MARINE_PLAN_CA,
    MARINE_PLAN_INTL,
    MARINE_PLAN_COUNT
} MarinePlan_t;

extern const char * const gSubMenu_MARINE_PLAN[MARINE_PLAN_COUNT];

typedef struct {
    uint32_t rx;        // 10 Hz units
    int32_t  txShift;   // TX = RX + txShift (10 Hz units)
    uint8_t  flags;
} PlanChannelInfo_t;

void    PLAN_GmrsInfo(uint8_t idx, PlanChannelInfo_t *info);
// The INTL region's walkie-talkie channels, in place of FRS / GMRS / MURS: PMR446
// 1-16 (licence-free), then the UK's Business Radio Simple UK Light U1-U19 (licensed)
#define PLAN_PMR_COUNT   16u
#define PLAN_INTL_COUNT  (PLAN_PMR_COUNT + 19u)
void    PLAN_PmrInfo(uint8_t idx, PlanChannelInfo_t *info);
void    PLAN_PmrLabel(uint8_t idx, char *out);
bool    PLAN_IsPmrFrequency(uint32_t frequency, uint8_t count);   // one of the first `count`
void    PLAN_GmrsLabel(uint8_t idx, char *out);

void        PLAN_AirInfo(uint8_t idx, PlanChannelInfo_t *info);
const char *PLAN_AirUse(uint8_t idx);

typedef enum {
    HAM_REGION_US = 0,
    HAM_REGION_CA,
    HAM_REGION_R1,   // IARU Region 1 (Europe, Africa, Middle East)
    HAM_REGION_COUNT
} HamRegion_t;

// The Region menu: the ham regions above plus the UK (ham: IARU R1, marine: INTL,
// the walkie-talkie mode: PMR446 and Simple UK)
#define PLAN_REGION_UK     3u
#define PLAN_REGION_COUNT  4u
extern const char * const gSubMenu_HAM_REGION[PLAN_REGION_COUNT];
#define PLAN_GMRS_SIMPLEX  22u   // FRS / GMRS 1-22 (Canada: these only)

void        PLAN_HamInfo(uint8_t region, uint8_t idx, PlanChannelInfo_t *info);
const char *PLAN_HamUse(uint8_t region, uint8_t idx);
// The region's ham bands (10 m, 6 m, 2 m, 1.25 m, 70 cm; R1 without 1.25 m): the
// TX limits (what F Lock did) and the F editor's BAND item.
#define PLAN_HAM_BANDS 5u
bool        PLAN_HamTxAllowed(uint8_t region, uint32_t frequency);
uint8_t     PLAN_HamBandCount(uint8_t region);
int8_t      PLAN_HamBandOf(uint8_t region, uint32_t frequency);   // -1: outside them
uint32_t    PLAN_HamBandCall(uint8_t region, uint8_t band);       // its calling frequency
char        PLAN_HamBandDigit(uint8_t region, uint32_t frequency); // icon digit, '-' outside

// VFO slots: each VFO keeps one state (frequency, offset, tones, mode, ..) per
// band of use; a VFO's slot always follows its frequency. GEN holds everything
// outside the others. Each slot is a FREQ channel (FREQ_CHANNEL_FIRST + slot).
enum { SLOT_10M, SLOT_6M, SLOT_FM, SLOT_AIR, SLOT_2M, SLOT_125CM, SLOT_70CM, SLOT_GEN, SLOT_COUNT };
uint8_t     PLAN_SlotOf(uint8_t region, uint32_t frequency);
bool        PLAN_SlotRange(uint8_t region, uint8_t slot, uint32_t *lo, uint32_t *hi);   // [lo, hi); false: GEN or not in the region
uint32_t    PLAN_SlotDefault(uint8_t region, uint8_t slot);   // ham: its calling frequency

uint8_t PLAN_MarineCount(uint8_t plan);
void    PLAN_MarineInfo(uint8_t plan, uint8_t idx, PlanChannelInfo_t *info);
void    PLAN_MarineLabel(uint8_t plan, uint8_t idx, char *out);
uint8_t PLAN_MarineFind(uint8_t plan, const char *label);
// Short channel use for the display: "CALLING", "BRIDGE", "WEATHER", ...
const char *PLAN_MarineUse(uint8_t plan, uint8_t idx);
// Index in `toPlan` of the channel matching `idx` of `fromPlan`: same label,
// else same number with/without an A/B suffix, else channel 16.
uint8_t PLAN_MarineEquivalent(uint8_t fromPlan, uint8_t idx, uint8_t toPlan);

#endif
