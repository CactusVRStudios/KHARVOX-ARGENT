#pragma once
namespace argent::hud {
inline bool hideManagedHud(bool allHudHidden,bool sync,bool handBound,bool animation,bool poseAvailable){
 return allHudHidden||sync||(handBound&&(animation||!poseAvailable));
}
// Scope the decision to the SWF, never its possibly shared HUD-camera canvas.
class HudDrawScope {
 void*& slot;
 void* previous;
public:
 HudDrawScope(void*& target,void* swf,bool owned,bool hidden)
  :slot(target),previous(target){slot=owned&&hidden?swf:nullptr;}
 ~HudDrawScope(){slot=previous;}
 HudDrawScope(const HudDrawScope&)=delete;
 HudDrawScope& operator=(const HudDrawScope&)=delete;
};
}
