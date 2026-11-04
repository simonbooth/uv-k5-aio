// The services build without the legacy menu (no ENABLE_DTMF_UI): SysInf is the radio's
// settings screen (M), the rest is CHIRP's. What the rest of the firmware still refers to.

#include <stdbool.h>
#include <stdint.h>

#include "app/menu.h"
#include "ui/menu.h"

const t_menu_item MenuList[]             = { { "", 0xFF } };   // (empty)
const uint8_t     FIRST_HIDDEN_MENU_ITEM = 0xFF;
const char        gSubMenu_BATTYP[3][9]  = { "1600mAh", "2200mAh", "3500mAh" };   // (SysInf)
bool              gIsInSubMenu;
int32_t           gSubMenuSelection;

int  UI_MENU_GetCurrentMenuId(void) { return 0xFF; }   // (no item: matches none)
void UI_DisplayMenu(void) {}
void MENU_AcceptSetting(void) {}
void MENU_ShowCurrentSetting(void) {}
void MENU_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) { (void)Key; (void)bKeyPressed; (void)bKeyHeld; }
