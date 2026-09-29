#pragma once
#include <cstdint>
#include "TutorialPolicy.h"
namespace argent::hud {
// Supported-EXE RTTI: idHUDMenu families, never ordinary HUD/subtitles.
inline constexpr uintptr_t flatMenuVtables[]={
 0x2d056e8,0x2d057c8,0x2d058a8,0x2d05988,0x2d07ae0,0x2d07c70,
 0x2d07e00,0x2d07f90,0x2d19f98,0x2d08128,0x2d09070,0x2d09730,
 0x2d09b10,0x2d09df8,0x2d09ed8,0x2d0a430,0x2d0a8c0,0x2d0f2a0,
 0x2d0f7f0,0x2d13d68,0x2d16270,0x2d144a0,0x2d14790,0x2d167e8,
 0x2d16a30,0x2d16e38,0x2d18060,0x2d182d0,0x2d18520,0x2d189e0,
 0x2d193b0,0x2d19490,0x2d19b00,0x2d19d70,0x308bf08,0x30fb670
};
inline bool summaryMenuVtable(uintptr_t rva){return rva==0x2d11dd8;} // idEndOfLevel_Summary, live verified
// Concrete main-menu screens also opened over gameplay. Their owner visibility
// must be checked: the shared movie draw does not carry the screen's identity.
inline constexpr uintptr_t mainMenuScreenVtables[]={
 0x2d3dba8, // idMainMenu_Screen_MasterLevels, live verified
 0x2d3df10, // idMainMenu_Screen_MasterLevelsMilestones
 0x2d3bce8  // idMainMenu_Screen_Difficulty
};
inline bool mainMenuScreenVtable(uintptr_t rva){for(auto value:mainMenuScreenVtables)if(value==rva)return true;return false;}
// Concrete modal owners, also while a playable/cinematic camera remains behind
// the UI. Like KHARVOX's native screen ownership, these outrank camera state.
// Generic idDialogMenu_Overlay/idMenuElement_Overlay are not menu lifecycle
// evidence: the former is visible in the live HUD even without its own modal.
inline constexpr uintptr_t modalOverlayVtables[]={
 0x2d10238, // idEndOfLevel_Credits, live visible during campaign credits
 0x2d08550, // idArgentSelection_Overlay (Sentinel crystal selection)
 0x2d088b0, // idBattleArena_Overlay
 0x2d0ab88, // idDossier_Overlay
 0x2d16ef0, // idInvasionMenu_Overlay
 0x2d18f20, // idPauseMenu_Overlay
 0x2d19858, // idRespec_Overlay
 0x2d32868  // idMainMenu_Overlay
};
inline bool gameplayMenuVtable(uintptr_t rva){if(mainMenuScreenVtable(rva)||tutorialKind(rva)>=0||summaryMenuVtable(rva)||rva==0x2d19b00)return true;for(auto value:modalOverlayVtables)if(value==rva)return true;return false;} // Rune selection also runs over gameplay.
inline bool flatMenuVtable(uintptr_t rva){if(gameplayMenuVtable(rva))return true;for(auto value:flatMenuVtables)if(value==rva)return true;return false;}
}
