/* GMRS/FRS privacy sub-code numbering, selectable by radio make/model.
 *
 * Sub-codes are always stored as the real tone (CodeType + Code option index);
 * a profile only decides how that tone is numbered and the order it is stepped
 * through. Numbering source: K0TFU "FRS/GMRS Privacy Codes Demystified".
 */

#ifndef SUBCODE_H
#define SUBCODE_H

#include <stdbool.h>
#include <stdint.h>

#include "dcs.h"

typedef enum {
    SUBCODE_MOTO = 0,   // Motorola Talkabout, Uniden, Backcountry Access, Rocky Talkie: 1-38 CT, 39-121 DCS
    SUBCODE_COBRA,      // 1-38 CT, d1-d83 DCS
    SUBCODE_MIDLAND,    // 1-38 CT, d1-d104 DCS
    SUBCODE_RUGGED,     // 1-51 CT (39 = 62.5 Hz unsupported), d1-d104 DCS
    SUBCODE_BAOFENG,    // Baofeng, Retevis, Radioddity GM-30: 1-50 CT, d1-d105 DCS (94 = D645 unsupported)
    // not picked (SubCod lists the brands above): the INTL channels' own numbering
    SUBCODE_PMR446,     // PMR446 1-16: as consumer radios, 1-38 CT only
    SUBCODE_OFCOM,      // Simple UK U1-U19 (Ofcom OfW164 tables 3, 4): C1-C42 CT (C32 = 198 Hz unsupported), 51 DCS
    SUBCODE_PROFILE_COUNT
} SubCodeProfile_t;

#define SUBCODE_BRAND_COUNT SUBCODE_PMR446   // the ones SubCod offers
extern const char * const gSubMenu_SUBCODE[SUBCODE_BRAND_COUNT];

// The brand picked in SubCod (persisted by the service-mode state), and the
// numbering a channel's codes use: that brand, or the INTL channels' own.
uint8_t   SERVICE_GetSubCodeProfile(void);
uint8_t   SERVICE_SubCodeProfileFor(uint16_t channel);

// Sub-code number of a tone on a channel. 0 = off, 0xFF = the tone has no number
// there. *isSeparateDcs is set when it is shown as "dN".
uint8_t   SUBCODE_FromTone(uint16_t channel, DCS_CodeType_t type, uint8_t code, bool *isSeparateDcs);
// "12", "d12", "C13" / "D023" (Ofcom), or the raw tone ("67.0", "D023N").
void      SUBCODE_Format(uint16_t channel, DCS_CodeType_t type, uint8_t code, char *out);
// Step to the next/previous numbered sub-code (wraps through off).
void      SUBCODE_Step(uint16_t channel, int8_t direction, DCS_CodeType_t *type, uint8_t *code);

#endif
