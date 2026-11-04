#ifndef APP_PARAM_EDIT_H
#define APP_PARAM_EDIT_H

#include <stdint.h>

#ifdef ENABLE_FEAT_SERVICE_MODES

// Parameter selection on the selected VFO: F steps through the parameters the
// channel has, the frequency / channel first (GMRS: + privacy code, power; marine:
// + power; VFO mode: + mode, power, tones, shift,
// offset, step; broadcast FM: mode), Up / Down change the selected one, F after
// the last one or EXIT leaves. Shown in place where the parameter is displayed.
typedef enum {
    PE_NONE = 0,
    PE_TUNE,     // the frequency (VFO) / channel: Up / Down tune as usual
    PE_BAND,     // a VFO's ham band (HamRgn; shown in its mode icon)
    PE_CODE,     // GMRS privacy code (RX and TX)
    PE_MODE,     // NFM / FM / AM / USB (+ WFM, broadcast FM, in the broadcast band; CHIRP's names)
    PE_POWER,
    PE_RXTONE,
    PE_TXTONE,
    PE_SHIFT,    // off / + / -
    PE_OFFSET,
    PE_STEP,
    PE_SQUELCH,  // this VFO's level
    PE_VOLUME,   // broadcast FM: the FM chip's volume
    PE_STEREO,   // broadcast FM: stereo / mono
    PE_SUBCODE,  // walkie channels (US / Canada), full screen: the code numbering (SubCod)
    PE_NAME,     // a memory's name: F skips it; Up / Down (or a digit) edits it a character at a time
    PE_GROUPS,   // a memory's groups 1-3 and Call (full screen), a flag at a time as the name
    PE_COMPAND,  // ham VFOs / memories, FM, full screen: the compander (off / TX / RX / both)
    PE_SLOT,     // the memory slot (the M spot): VFO M+ / free ones; a memory: free ones (move), M- (delete)
} ParamItem_t;

extern uint8_t gEditItem;   // ParamItem_t being selected, PE_NONE when not
extern uint8_t gEditVfo;    // the VFO it belongs to (A / B view: F walks both VFOs' items)

void PARAM_Next(void);              // F: the first / next parameter, PE_NONE after the last
void PARAM_Prev(void);              // F held: the previous one (the first: stays)
void PARAM_Step(int8_t direction);  // Up / Down
void PARAM_Cancel(void);            // EXIT, F + digit, frequency entry
// The selected item if it still applies to the selected VFO (else selection ends).
uint8_t PARAM_Current(void);
const char *PARAM_Name(uint8_t item);   // "TX POWER", ... (full screen, under the row)
void    PARAM_Digit(uint8_t digit);     // OFFSET selected: digits type it (kHz); NAME: the character
// PE_NAME / PE_GROUPS: the character / flag being changed (PARAM_CURSOR_NONE: the field).
#define PARAM_CURSOR_NONE 0xFFu
uint8_t PARAM_Cursor(void);
char    PARAM_StepChar(char c, int8_t direction);   // a name / callsign character: the next one
const char *PARAM_NameText(void);     // the name being edited (10 characters)
// PE_SLOT: the slot it shows - 0: none picked (a VFO: M+, a memory: its own), 1-200 a
// free memory, PARAM_SLOT_DELETE: M-; taken when F leaves the item.
#define PARAM_SLOT_DELETE 201u
uint8_t PARAM_SlotPick(void);

#endif
#endif
