#include "../src/hud/TutorialBindingText.h"
#include <iostream>
#include <stdexcept>
using namespace argent::hud;
void check(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
int main(){try {
 const TutorialBindingLayout right{},left{true,false},full{true,true},index{false,false,true,true};
 check(tutorialBindingLabel("_attack1",right)=="R Trigger","right primary fire");
 check(tutorialBindingLabel("_attack2",right)=="R Stick click","right Glory Kill");
 check(tutorialBindingLabel("_bfg",right)=="L X","Flame Belch is not the BFG weapon");
 check(tutorialBindingLabel("_reload",right)=="L Y","weapon mod swap");
 check(tutorialBindingLabel("_inventory",right)=="L Stick click (hold)","Dossier hold");
 check(tutorialBindingLabel("_objectives",right)=="L Stick click (tap)","objectives tap");
 check(tutorialBindingLabel("_quickuse",right)=="L Trigger","localized grenade token");
 check(tutorialBindingLabel("_quick0",right)=="L Grip","equipment cycle");
 check(tutorialBindingLabel("_crucible",right)=="R Stick up","Crucible shortcut");
 check(tutorialBindingLabel("_changeWeapon",right)=="R Stick down","weapon wheel");
 for(auto l:{left,full}) {
  check(tutorialBindingLabel("_attack1",l)=="L Trigger","left fire");
  check(tutorialBindingLabel("_altfire",l)=="L Grip","left mod fire");
  check(tutorialBindingLabel("_quickuse",l)=="R Trigger","left equipment");
  check(tutorialBindingLabel("_quick0",l)=="R Grip","left equipment cycle");
  check(tutorialBindingLabel("_inventory",l)=="R Stick click (hold)","left Dossier");
  check(tutorialBindingLabel("_attack2",l)=="L Stick click","left melee");
 }
 check(tutorialBindingLabel("_jump",left)=="R B","buttons layout jump");
 check(tutorialBindingLabel("_bfg",left)=="L X","buttons layout Flame Belch");
 check(tutorialBindingLabel("_crucible",left)=="R Stick up","buttons layout turn");
 check(tutorialBindingLabel("_jump",full)=="L Y","full layout jump");
 check(tutorialBindingLabel("_dash",full)=="L X","full layout dash");
 check(tutorialBindingLabel("_bfg",full)=="R A","full layout Flame Belch");
 check(tutorialBindingLabel("_reload",full)=="R B","full layout mod swap");
 check(tutorialBindingLabel("_crucible",full)=="L Stick up","full layout Crucible");
 check(tutorialBindingLabel("_moveforward",full)=="R Stick forward","full layout movement");
 check(tutorialBindingLabel("_bfg",index)=="L A","Index left lower face");
 check(tutorialBindingLabel("_reload",index)=="L B","Index left upper face");
 check(tutorialBindingLabel("_jump",{true,true,true,true})=="L B","Index full jump");
 // Exact live tutorial excerpt, including adjacent color codes and line breaks.
 check(replaceTutorialBindings("When close, ^apress _attack2^7 to ^5Glory Kill^7 it.\n",right)==
  "When close, ^apress [R Stick click]^7 to ^5Glory Kill^7 it.\n","live Glory Kill text");
 check(replaceTutorialBindings("^a_quick0^7 / _quickuse. _inventory!",left)==
  "^a[R Grip]^7 / [R Trigger]. [R Stick click (hold)]!","color-adjacent token");
 const std::string unknown="id_attack1 _attack10 _attack1_suffix <JOY_STICK1_UP> #STR_HELP %s <var1>";
 check(replaceTutorialBindings(unknown,right)==unknown,"unknown identifiers, glyphs and format arguments retained");
 check(replaceTutorialBindings("Drücke _jump – öffnen\n",right)=="Drücke [R B] – öffnen\n","UTF-8 retained");
 auto once=replaceTutorialBindings("_attack1 _inventory",right);
 check(replaceTutorialBindings(once,right)==once,"replacement idempotent");
 std::cout<<"Tutorial binding tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
