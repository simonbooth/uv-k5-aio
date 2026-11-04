/* Service modes: HAM / GMRS-FRS / MARINE / AIR, plus broadcast FM as a VFO mode.
 *
 * The built-in plans (GMRS, marine, air, and the ham calling channels that come
 * before the memories) are fixed tables in flash (svc_plans.c), addressed through
 * their own channel-index ranges after the VFO channels (SVC_GMRS_FIRST ..
 * SVC_HAM_LAST, see misc.h). They never touch the 200 memory channels. Also here:
 * the TX blocks (SERVICE_IsTxInhibited), the weather alert gate, and the A / B
 * view's watch.
 *
 * GMRS privacy "sub-codes" are stored as the real tone (CodeType + Code option
 * index), never as a brand number: the selected SubCode profile only changes how
 * the tone is numbered on screen and in what order it is stepped through.
 */

#ifndef SERVICE_H
#define SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "misc.h"

#ifdef ENABLE_FEAT_SERVICE_MODES

#ifdef ENABLE_NOAA
    #error "ENABLE_NOAA and ENABLE_FEAT_SERVICE_MODES are exclusive: the marine plans carry the WX channels and F+5 goes to Marine"
#endif

#include "radio.h"
#include "subcode.h"

typedef enum {
    SERVICE_HAM = 0,
    SERVICE_GMRS,
    SERVICE_MARINE,
    SERVICE_AIR,       // airband, receive only (AM)
    SERVICE_COUNT
} Service_t;

#define IS_SVC_CHANNEL(x)     ((x) >= SVC_GMRS_FIRST && (x) <= SVC_HAM_LAST)

void      SERVICE_Init(void);
Service_t SERVICE_OfChannel(uint16_t channel);

// The channel the MR side of `vfo` returns to for `service` (HAM: the memory channel).
uint16_t  SERVICE_ChannelFor(uint8_t vfo, Service_t service);
// The next memory from `from` (inclusive) in `direction` of a mode (SERVICE_ModeOf
// values: a service by its frequency, MODE_ICON_FM by broadcast FM); 0xFFFF if none.
uint16_t  SERVICE_NextMemory(uint16_t from, int8_t direction, uint8_t mode, uint8_t group);   // group 0: all
uint8_t   SERVICE_GroupOf(uint8_t vfo);   // its memory's group (VFO/MR): 1-3, 0 = all memories
void      SERVICE_SetGroup(uint8_t vfo, uint8_t group);
// A memory scan's next channel (scan list `list`), on memories of the mode it started in.
uint16_t  SERVICE_NextScanMemory(uint16_t from, int8_t direction);   // a memory scan: in its group
// The plan channel a VFO / memory is on by its frequency (and shift); 0xFFFF if none.
uint16_t  SERVICE_PlanChannelOf(const VFO_Info_t *pVfo);

uint16_t  SERVICE_StepChannel(uint16_t channel, int8_t direction);
// Number of channels in `channel`'s plan; *first = the plan's first channel.
uint8_t   SERVICE_PlanPosition(uint16_t channel, uint16_t *first);
// Channels a scan passes over: GMRS repeater pairs (same RX as 15-22), marine
// AIS / DSC data and continuous broadcasts (WX, Canadian CMB).
bool      SERVICE_ScanSkip(uint16_t channel);
// Weather watch (see service.c): true while `pVfo` should open only on the
// 1050 Hz alarm tone. Reset re-arms it after an alert (returns true if latched).
bool      SERVICE_WxAlertGate(const VFO_Info_t *pVfo);
bool      SERVICE_WxAlertReset(void);
// Receive started on `pVfo`: latches a weather alert; true = flash the LED
// (weather alarm, or activity on marine channel 16).
bool      SERVICE_OnRxStart(const VFO_Info_t *pVfo);
// Marine channel 16 in the active plan (F+5).
uint16_t  SERVICE_MarineCallingChannel(void);
// Second marine watch channel for VFO B: WX1, or 13 where the plan has no WX.
uint16_t  SERVICE_MarineWatchChannel(void);
// Clamp a stored service channel to the active plan (marine plans differ in size).
uint16_t  SERVICE_ValidateChannel(uint16_t channel);

void      SERVICE_ConfigureVfo(VFO_Info_t *pVfo, uint16_t channel);
bool      SERVICE_IsTxInhibited(const VFO_Info_t *pVfo);
// Can transmit: power not OFF, and no TX block (see SERVICE_IsTxInhibited).
bool      SERVICE_CanTx(const VFO_Info_t *pVfo);
// Squelch level (0-9) per VFO: the side keys, the Sql menu and the F editor change
// the one VFO they act on.
// The F editor's BAND item: the next / previous ham band (HamRgn), on its last
// frequency (else its calling frequency).
void      SERVICE_StepHamBand(uint8_t vfo, int8_t direction);
// VFO slots (PLAN_Slot*): a VFO onto a frequency goes into that band's slot (its
// saved state), false if the slot is switched off; Up / Down wraps in the band.
uint8_t   SERVICE_SlotOf(uint32_t frequency);
// Broadcast FM's audio (the F editor in FM mode): the FM chip's volume 0-15, stereo / mono.
uint8_t   SERVICE_FmVolume(void);
bool      SERVICE_FmStereo(void);
void      SERVICE_SetFmAudio(uint8_t volume, bool stereo);
bool      SERVICE_SlotEnabled(uint8_t slot);
void      SERVICE_InitSlotVfo(uint8_t vfo, uint16_t channel);
bool      SERVICE_TuneVfo(uint8_t vfo, uint32_t frequency);
uint32_t  SERVICE_NextVfoFrequency(VFO_Info_t *v, int8_t direction);
uint8_t   SERVICE_GetSquelch(uint8_t vfo);
void      SERVICE_StepSquelch(uint8_t vfo, int8_t direction);
#define   SQUELCH_OF(vfo)  SERVICE_GetSquelch(vfo)

// TX Any (menu, double confirmed): lift every TX block.
bool      SERVICE_GetTxAny(void);
void      SERVICE_SetTxAny(bool on);

// A / B view (F+2) and the watch it starts (see service.c).
enum { WATCH_PRIORITY, WATCH_EVEN, WATCH_OFF };                     // Watch menu
bool      SERVICE_GetAbView(void);
void      SERVICE_SetAbView(bool on, bool forceWatch);   // forceWatch: even with Watch OFF (F+5)
uint8_t   SERVICE_GetWatchLevel(void);
void      SERVICE_SetWatchLevel(uint8_t level);
void      SERVICE_WatchStart(bool force);        // force: even with Watch OFF
bool      SERVICE_WatchStop(void);    // true if a watch was running; no reconfigure
// EXIT: start the watch again (one VFO was made exclusive), or PTT back to main.
bool      SERVICE_WatchExit(void);
void      SERVICE_WatchBoot(void);
void      SERVICE_WatchOnRx(void);               // reception started (dual watch)
void      SERVICE_WatchTimeSlice500ms(void);
uint16_t  SERVICE_WatchNext_10ms(void);          // the watch: on to the other VFO, for how long
// Scanning in the A / B view with the watch running (see service.c).
void      SERVICE_ScanStart(void);
void      SERVICE_ScanResume(void);   // after RADIO_SelectVfos in CHFRSCANNER_Start: on the scanned VFO
void      SERVICE_ScanStop(void);   // before CHFRSCANNER_Stop's result: back on the scanned VFO
bool      SERVICE_WatchPointless(void);  // nothing to watch (the other VFO is on broadcast FM)
bool      SERVICE_ScanLook(void);        // true: this scan step was a look at the watched VFO
bool      SERVICE_ScanLookHeard(void);   // reception started: true if during a look

// Channel label alone: "15R", "16", "22A", "WX1", "AIS1".
void      SERVICE_FormatLabel(uint16_t channel, char *out);
bool      SERVICE_ChannelLabel(uint16_t channel, char *out);   // false: the plan goes by frequency (air, ham)
// Channel use: marine ("CALLING", "BRIDGE", ...) in the active plan, or airband ("GUARD", "UNICOM", ...).
const char *SERVICE_ChannelUse(uint16_t channel);
// A VFO's mode, for its icon (BITMAP_ModeIcons): its channel's service, or for a
// frequency (VFO) channel the band it is in: broadcast FM (MODE_ICON_FM), air,
// marine (incl. weather), FRS / GMRS, else HAM.
#define MODE_ICON_FM SERVICE_COUNT
uint8_t SERVICE_ModeOf(uint8_t vfo);
// Short channel label for the channel box: "G15R", "M16", "MWX1".
// Full label: "GMRS 15R-12", "MAR 16".

// Next power level for the power key: GMRS steps up to the channel's limit and
// wraps; marine toggles 1 W / 5 W.
uint8_t   SERVICE_NextPower(uint16_t channel, uint8_t power);
// True when a GMRS-plan channel at this power is FRS operation (simplex, within
// FRS power; narrowband is set to match). Repeater pairs and HIGH are GMRS.
bool      SERVICE_IsFrs(uint16_t channel, uint8_t power);
// Highest power allowed on a service channel (GMRS 8-14: LOW4, 0.5 W).
uint8_t   SERVICE_PowerLimit(uint16_t channel);
// Persist power (all service channels) and tones (GMRS) of a channel
// (called from SETTINGS_SaveChannel).
void      SERVICE_SaveChannelTones(uint16_t channel, const VFO_Info_t *pVfo);
// Record the per-VFO service/channel state; written to flash only when changed.
void      SERVICE_SaveState(void);

void      SERVICE_SetSubCodeProfile(uint8_t profile);
// True exactly once (first boot of this edition): apply its side-key layout.
uint8_t      SERVICE_TakeKeyDefaultsOnce(void);   // bit 0 side keys, bit 1 long-press MENU

// T Live: show CTCSS / DCS codes heard on channels without one set
enum { CODE_DETECT_AUTO, CODE_DETECT_ALL, CODE_DETECT_OFF };   // AUTO: FRS / GMRS only
uint8_t   SERVICE_GetCodeDetect(void);
// HamRgn: the built-in ham channels and the ham TX bands (HamRegion_t)
// Licences / bands (CHIRP only): which services show and which may transmit. A service
// unticked is hidden (its channels skipped; a VFO / memory on its frequencies only
// listens). Ham and GMRS transmit only with their callsign set too. Erased (never set
// in CHIRP): the region's licence-free ones. TX Any lifts all of it.
#define LIC_MARINE_TX  0x0001u   // marine TX / RX (with NOAA weather)
#define LIC_MARINE_RX  0x0002u   // marine RX
#define LIC_HAM        0x0004u   // ham channels, ham-band TX (+ callsign)
#define LIC_AIR        0x0008u   // airband (receive)
#define LIC_FRS        0x0010u   // US / Canada: FRS 1-22
#define LIC_GMRS       0x0020u   // US: GMRS - 15R-22R, more than 2 W (+ callsign)
#define LIC_MURS       0x0040u   // US: MURS M1-M5
#define LIC_UK_SIMPLE  0x0080u   // UK: Simple UK U1-U19
#define LIC_PMR446     0x0100u   // INTL / UK: PMR446 1-16
uint16_t  SERVICE_Licences(void);
bool      SERVICE_Callsign(Service_t service, char *out);   // HAM, else the public licence ID; out[11]
bool      SERVICE_ChannelVisible(uint16_t channel);          // a plan channel licensed to show
bool      SERVICE_GmrsLicensed(void);                        // GMRS ticked, its callsign set
bool      SERVICE_OutOfSpec(uint32_t frequency);             // outside the radio's TX design (136-174, 400-470)
bool      SERVICE_Enabled(uint8_t mode);                     // a mode (F+1) with anything to show
bool      SERVICE_TxCallsign(char *out);   // the TX VFO transmits under a callsign: it (out[11])
void      SERVICE_LicencesKeep(bool restore);   // a reset keeping them: save, then restore
void      SERVICE_SetLicence(uint16_t lic, bool on);                  // (SysInf) as CHIRP sets them
void      SERVICE_SetCallsign(Service_t service, const char *text);   // HAM: 8 characters, else 10
void      SERVICE_Init(void);

// First run of this firmware (another one's settings): the reset screen asks until it is
// answered; SERVICE_SetInstalled() marks the radio ours (after a reset).
bool      SERVICE_IsFirstRun(void);
void      SERVICE_SetInstalled(void);
uint8_t   SERVICE_GetRegion(void);      // the Region menu: HAM_REGION_US, _CA, _R1 (INTL), PLAN_REGION_UK
uint8_t   SERVICE_GetHamRegion(void);   // its ham (and marine) region: UK -> IARU R1
bool      SERVICE_IsPmr(void);   // the INTL region: the FRS / GMRS mode is PMR446
void      SERVICE_SetHamRegion(uint8_t region);
void      SERVICE_SetCodeDetect(uint8_t mode);

uint8_t   SERVICE_GetMarinePlan(void);
// Switch US / Canada / International; VFOs on marine move to the matching channel.
void      SERVICE_SetMarinePlan(uint8_t plan);

#else

#define IS_SVC_CHANNEL(x)         false
#define SQUELCH_OF(vfo)           gEeprom.SQUELCH_LEVEL
#define SERVICE_IsTxInhibited(p)  false

#endif
#endif
