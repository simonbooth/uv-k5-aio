
#ifndef APP_COMMON_H
#define APP_COMMON_H

#include "functions.h"
#include "settings.h"
#include "ui/ui.h"

void COMMON_KeypadLockToggle();
void COMMON_SwitchVFOs();
#ifdef ENABLE_FEAT_SERVICE_MODES
#include "service.h"
// Put both VFOs on `service` (last-used channels): the F+5 marine watch.
void COMMON_SetService(Service_t service);
// F+1 in channel modes: the selected VFO's next mode (HAM -> GMRS -> MARINE -> AIR -> FM).
void COMMON_CycleService(void);
// F+0: the selected VFO to broadcast FM (last station) and back to HAM.
void COMMON_ToggleFm(void);
// Up / Down on a VFO (its frequency, or the next channel of its list).
bool COMMON_StepChannel(uint8_t vfo, int8_t dir);

#endif
void COMMON_SwitchVFOMode();

#endif