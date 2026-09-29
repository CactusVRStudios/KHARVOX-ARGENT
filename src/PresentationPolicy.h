#pragma once
#include <cstdint>
namespace argent::presentation {
struct Options {bool automatic{true},cinematicsQuad{true},syncImmersive{true},movementImmersive{true},cinematics3d{};};
struct Sample {uint64_t tick{};bool valid{},inGame{},paused{},cutscene{},sync{},ledge{},upgradeMenu{},dossierMenu{},interaction{},monkeyBar{},meatHook{},flatMenu{},deathMenu{};};
enum class Mode {Unknown,Menu,Pause,Cinematic,Sync,Traversal,Gameplay,UpgradeMenu,Interaction};
inline bool stereoCinematic(Mode mode,const Options& options,bool quad){return options.cinematics3d&&quad&&mode==Mode::Cinematic;}
struct ShaderPolicy {bool centerGrid,worldWorkarounds;};
inline ShaderPolicy shaderPolicy(bool quad,bool stereoQuad){
 // Stereo coordinates do not imply the gameplay camera. Authored cinematics
 // retain native effect clamps. Shared center-grid HiZ safety follows
 // centerGrid, including stereo cinematics; geometric rejection stays native.
 return {!quad||stereoQuad,!quad};
}
inline bool hideHudDuringAnimation(Mode mode,bool quad){
 // HUD has its own camera; the world/quad presentation must not gate it.
 (void)quad;return mode==Mode::Sync;
}
inline const char* name(Mode mode){switch(mode){case Mode::Menu:return "menu";case Mode::UpgradeMenu:return "weapon-upgrade-menu";case Mode::Pause:return "pause";case Mode::Cinematic:return "cinematic";case Mode::Gameplay:return "gameplay";case Mode::Sync:return "glory-or-chainsaw";case Mode::Interaction:return "interaction-animation";case Mode::Traversal:return "traversal";default:return "loading-or-unknown";}}
inline Mode classify(const Sample& s,uint64_t now,const Options& o){
 if(!s.tick||now<s.tick||now-s.tick>500)return Mode::Unknown;
 if(!s.valid||!s.inGame)return Mode::Menu;
 if(s.dossierMenu)return Mode::Pause;
 if(s.paused)return Mode::Pause;
 if(s.deathMenu)return Mode::Menu;
 if(s.upgradeMenu)return Mode::UpgradeMenu;
 if(s.flatMenu)return Mode::Menu;
 if(s.sync)return Mode::Sync;
 if(s.interaction)return Mode::Interaction;
 // Meathook keeps gameplay input and the physics-anchored VR camera. A real
 // sync attack still takes precedence above and returns here on completion.
 if(s.ledge||s.monkeyBar)return Mode::Traversal;
 if(s.cutscene)return Mode::Cinematic;
 return Mode::Gameplay;
}
struct Policy {
 bool quad{true};uint64_t worldSince{};
 bool update(Mode mode,const Options& options,uint64_t now){
  const bool wanted=mode!=Mode::Gameplay&&mode!=Mode::Interaction&&!(mode==Mode::Cinematic&&!options.cinematicsQuad)&&!(mode==Mode::Sync&&options.syncImmersive)&&!(mode==Mode::Traversal&&options.movementImmersive);
  if(wanted){quad=true;worldSince=0;}
  else if(quad){if(!worldSince)worldSince=now;if(now>=worldSince&&now-worldSince>=150)quad=false;}
  return quad;
 }
};
}
