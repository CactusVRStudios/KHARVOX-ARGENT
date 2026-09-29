#pragma once
#include <cstdint>
namespace argent::presentation {
// Native menuTransition_t: ADVANCE=0, BACK=1, FORCE=2. Keep the pause
// session while its root is hidden by Settings or another child screen.
struct PauseMenuSession {
 uintptr_t root{};
 bool rootVisible{};
 void show(uintptr_t screen){root=screen;rootVisible=screen!=0;}
 void hide(uintptr_t screen,int transition){if(root==screen){rootVisible=false;if(transition==2)root=0;}}
 void reset(){root=0;rootVisible=false;}
 bool active() const{return root!=0;}
};
}
