#include "../src/EternalPresentationFrame.h"
#include "../src/PauseMenuSession.h"
#include "../src/LauncherSettings.h"
#include "../src/EternalCrouchCommand.h"
#include "../src/hud/FlatMenuPolicy.h"
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <iostream>
using namespace argent::presentation;
void check(bool b){if(!b)throw std::runtime_error("presentation / settings / native command");}
int main(){try{
 Options o;Sample s{1000,true,true,false,false,false,false};
 check(!stereoCinematic(Mode::Cinematic,o,true));o.cinematics3d=true;
 for(auto mode:{Mode::Unknown,Mode::Menu,Mode::Pause,Mode::UpgradeMenu,Mode::Gameplay,Mode::Sync,Mode::Traversal,Mode::Interaction})check(!stereoCinematic(mode,o,true));
 check(stereoCinematic(Mode::Cinematic,o,true)&&!stereoCinematic(Mode::Cinematic,o,false));
 Sample flat=s;flat.cutscene=true;flat.flatMenu=true;check(classify(flat,1100,o)==Mode::Menu);
 auto modalOwners=std::vector<uintptr_t>(std::begin(argent::hud::modalOverlayVtables),std::end(argent::hud::modalOverlayVtables));
 for(auto tutorial:{0x2d19f98,0x2d07f90,0x2d08128})modalOwners.push_back(tutorial);
 modalOwners.push_back(0x2d19b00); // Rune menu observed open over gameplay.
 for(auto screen:argent::hud::mainMenuScreenVtables){modalOwners.push_back(screen);check(argent::hud::mainMenuScreenVtable(screen));}
 check(!argent::hud::mainMenuScreenVtable(0x2d004d8));
 for(auto overlay:modalOwners)for(bool cinema:{false,true})for(bool cutscene:{false,true}){
  Options opts=o;opts.cinematics3d=cinema;opts.cinematicsQuad=false;
  Sample modal=s;modal.cutscene=cutscene;modal.flatMenu=argent::hud::gameplayMenuVtable(overlay);
  Policy policy;policy.quad=false;
  check(classify(modal,1100,opts)==Mode::Menu);
  check(policy.update(classify(modal,1100,opts),opts,1100));
  check(!stereoCinematic(classify(modal,1100,opts),opts,true));
  modal.flatMenu=false;modal.cutscene=false;
  check(policy.update(classify(modal,1101,opts),opts,1101));
  check(!policy.update(classify(modal,1251,opts),opts,1251));
 }
 for(auto ordinary:{0x2b20068,0x2d57c80,0x2d03420,0x2d004d8})check(!argent::hud::gameplayMenuVtable(ordinary));
 for(bool cinema:{false,true}){Options summaryOptions=o;summaryOptions.cinematics3d=cinema;
  Sample summary=s;summary.flatMenu=true;summary.cutscene=false;summary.sync=true;
  Policy summaryPolicy;summaryPolicy.quad=false;
  check(summaryPolicy.update(classify(summary,1100,summaryOptions),summaryOptions,1100));
  summary.flatMenu=false;summary.sync=false;
  check(summaryPolicy.update(classify(summary,1101,summaryOptions),summaryOptions,1101));
  check(!summaryPolicy.update(classify(summary,1251,summaryOptions),summaryOptions,1251));
 }
 Sample dossier=s;dossier.dossierMenu=true;dossier.sync=true;dossier.ledge=true;
 check(classify(dossier,1100,o)==Mode::Pause);
 Policy dossierPolicy;check(dossierPolicy.update(classify(dossier,1100,o),o,1100));
 dossier.dossierMenu=false;dossier.sync=false;dossier.ledge=false;
 check(classify(dossier,1101,o)==Mode::Gameplay);
 Sample death=s;death.deathMenu=true;death.sync=true;death.interaction=true;death.cutscene=true;
 check(classify(death,1100,o)==Mode::Menu);
 Policy deathPolicy;check(deathPolicy.update(classify(death,1100,o),o,1100));
 death.deathMenu=false;death.sync=false;death.interaction=false;death.cutscene=false;
 check(classify(death,1101,o)==Mode::Gameplay);
 check(classify(s,1100,o)==Mode::Gameplay);s.paused=true;check(classify(s,1100,o)==Mode::Pause);
 s.cutscene=true;check(classify(s,1100,o)==Mode::Pause);s.paused=false;check(classify(s,1100,o)==Mode::Cinematic);
 s.sync=true;check(classify(s,1100,o)==Mode::Sync);s.sync=false;s.ledge=true;check(classify(s,1100,o)==Mode::Traversal);
 s.valid=false;check(classify(s,1100,o)==Mode::Menu);check(classify(s,1501,o)==Mode::Unknown);check(classify(s,999,o)==Mode::Unknown);
 Policy p;check(p.update(Mode::Gameplay,o,1000));check(p.update(Mode::Gameplay,o,1149));check(!p.update(Mode::Gameplay,o,1150));
 check(!p.update(Mode::Sync,o,1151));check(!p.update(Mode::Traversal,o,1152));
 Sample upgrade{1152,true,true,false,true,true,false,true};
 check(classify(upgrade,1152,o)==Mode::UpgradeMenu); // Modal menu wins over the interaction animation.
 check(p.update(Mode::UpgradeMenu,o,1152));
 upgrade.upgradeMenu=false;upgrade.cutscene=false;upgrade.sync=false;
 check(classify(upgrade,1153,o)==Mode::Gameplay);
 upgrade.interaction=true;upgrade.cutscene=true;
 check(classify(upgrade,1153,o)==Mode::Interaction);
 Policy interaction;check(interaction.update(Mode::Interaction,o,1200));check(!interaction.update(Mode::Interaction,o,1350));
 upgrade.interaction=false;upgrade.monkeyBar=true;check(classify(upgrade,1153,o)==Mode::Traversal);
 upgrade.monkeyBar=false;upgrade.meatHook=true;check(classify(upgrade,1153,o)==Mode::Cinematic);
 upgrade.cutscene=false;check(classify(upgrade,1153,o)==Mode::Gameplay);
 upgrade.sync=true;check(classify(upgrade,1153,o)==Mode::Sync);
 upgrade.sync=false;check(classify(upgrade,1153,o)==Mode::Gameplay);
 upgrade.meatHook=false;upgrade.cutscene=true;check(classify(upgrade,1153,o)==Mode::Cinematic);
 check(p.update(Mode::Pause,o,1153));check(p.update(Mode::Gameplay,o,1160));check(p.update(Mode::Menu,o,1200));
 check(p.update(Mode::Gameplay,o,1250));check(!p.update(Mode::Gameplay,o,1400));check(p.update(Mode::Unknown,o,1401));
 o.cinematicsQuad=false;check(p.update(Mode::Cinematic,o,1500));check(!p.update(Mode::Cinematic,o,1650));check(p.update(Mode::Pause,o,1651));
 o.syncImmersive=false;o.movementImmersive=false;check(p.update(Mode::Sync,o,1652));check(p.update(Mode::Traversal,o,1653));
 std::array<unsigned char,0x8220> frame{};Sample decoded{};
 auto read=[&](size_t offset,void* out,size_t size){if(offset+size>frame.size())return false;std::memcpy(out,frame.data()+offset,size);return true;};
 frame[0x65]=255;frame[0x8219]=255;check(decodeFrame(read,2000,decoded)&&classify(decoded,2000,o)==Mode::Menu);
 frame[0x40]=frame[0x41]=1;check(!decodeFrame(read,2001,decoded)&&decoded.tick==2000);
 frame[0x65]=0;frame[0x8219]=1;check(decodeFrame(read,2002,decoded)&&classify(decoded,2002,o)==Mode::Pause);
 frame[0x8219]=0;check(decodeFrame(read,2003,decoded)&&classify(decoded,2003,o)==Mode::Gameplay);
 check(!activeLedge(-1)&&activeLedge(0)&&activeLedge(15)&&!activeLedge(16));
 PauseMenuSession pause;pause.show(123);check(pause.active()&&pause.rootVisible);
 pause.hide(456,0);check(pause.rootVisible); // Unrelated screen cannot hide bindings.
 pause.hide(123,0);check(pause.active()&&!pause.rootVisible); // Settings: pause retained, image hidden.
 pause.show(123);check(pause.rootVisible); // Back to pause root.
 pause.hide(123,0);check(pause.active()); // Settings replaces the root.
 pause.show(123);pause.hide(123,1);check(pause.active());
 pause.hide(456,2);check(pause.active()); // Another screen cannot end it.
 pause.hide(123,2);check(!pause.active());
 pause.show(123);pause.reset();check(!pause.active()); // Level exit/reload.
 std::istringstream original("# calibration\nprofile shotgun\nhand right .1 0 0 0 5 0\nweapon shotgun .1 0 0 0 0 0 0 0 -.3 .14 1 1\nturn smooth\nturn snap 90\n");
 auto saved=argent::launcher::updateSettings(original,{{"turn","snap 45"},{"automatic_presentation","1"}});
 check(saved.find("hand right .1 0 0 0 5 0")!=std::string::npos&&saved.find("weapon shotgun .1")!=std::string::npos&&saved.find("profile shotgun")!=std::string::npos);
 check(saved.find("turn snap 45")!=std::string::npos&&saved.find("turn snap 90")==std::string::npos&&saved.find("automatic_presentation 1")!=std::string::npos);
 std::array<unsigned char,0x4180> physics{};physics[0x3dfa]=127;physics[0x3e92]=5;auto before=physics;
 {argent::player::CrouchCommand command(physics.data(),true,false);check(physics[0x4170]==1&&static_cast<signed char>(physics[0x3dfa])==-127&&physics[0x3e92]==5);physics[0x3fbc]=1;}
 before[0x3fbc]=1;check(physics==before);
 {argent::player::CrouchCommand command(physics.data(),false,true);check(physics[0x3dfa]==127&&static_cast<signed char>(physics[0x3e92])==-127);}
 check(physics==before);
 // The separate HUD camera is hidden for the animation, irrespective of the
 // world-camera presentation, and restored immediately when the mode ends.
 for(bool quad:{false,true}){
  check(hideHudDuringAnimation(Mode::Sync,quad));
  for(auto mode:{Mode::Gameplay,Mode::Pause,Mode::Menu,Mode::UpgradeMenu,Mode::Cinematic,Mode::Traversal,Mode::Interaction,Mode::Unknown})check(!hideHudDuringAnimation(mode,quad));
 }
 // A stereo movie retains authored-camera effects; only its shared-grid
 // sampling follows the per-eye projection. Flat movies and gameplay match
 // their existing policies, including after transitions in either direction.
 for(bool stereo:{false,true}){
  const auto world=shaderPolicy(false,stereo);
  check(world.centerGrid&&world.worldWorkarounds);
 }
 for(bool stereo:{false,true,false,true,false}){
  const auto cinema=shaderPolicy(true,stereo);
  check(cinema.centerGrid==stereo&&!cinema.worldWorkarounds);
 }
 std::cout<<"Native frame decode, modes, cinematic shader isolation, settings preservation and scoped crouch command passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
