#include "../src/EternalBuildProfile.h"
#include "../src/EternalRenderControls.h"
#include "../src/hud/HudLayoutPolicy.h"
#include "../src/hud/FlatMenuPolicy.h"
#include "../src/hud/TutorialPolicy.h"
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool b){if(!b)throw std::runtime_error("Build profile contract failed");}
int main(){try{
 using namespace argent;
 for(bool store:{false,true}){
  build::microsoftStore=store;
  require(build::rva(0x13807ea)==(store?0x1384dca:0x13807ea));
  require(build::rva(0x53a9f0)==(store?0x54a3b0:0x53a9f0));
  require(build::rva(0x6685ce0)==(store?0x673d6e0:0x6685ce0));
  for(auto a:build::addresses){
   if(a.steam<0x2a00000&&!build::separatelyValidatedCode(a.steam)){
    bool protectedCode=false;for(auto c:build::storeCode)if(c.rva==a.store)protectedCode=true;
    require(protectedCode); // New gameplay/camera hooks must enter the Store preflight.
   }
   require(build::semanticRva(build::rva(a.steam))==a.steam);
   for(auto b:build::addresses)if(a.steam!=b.steam)require(a.store!=b.store);
  }
  for(size_t i=0;i<hud::roles.size();++i)require(hud::roleIndex(build::semanticRva(build::rva(hud::roles[i].vtable)))==int(i));
  for(auto a:hud::flatMenuVtables)require(hud::flatMenuVtable(build::semanticRva(build::rva(a))));
  for(auto a:hud::mainMenuScreenVtables){require(build::rva(a)!=0);require(hud::mainMenuScreenVtable(build::semanticRva(build::rva(a))));require(hud::gameplayMenuVtable(build::semanticRva(build::rva(a))));}
  require(hud::summaryMenuVtable(build::semanticRva(build::rva(0x2d11dd8))));
  require(hud::gameplayMenuVtable(build::semanticRva(build::rva(0x2d19b00))));
  for(auto r:hud::modalOverlayVtables){
   require(build::rva(r)!=0);
   require(hud::gameplayMenuVtable(build::semanticRva(build::rva(r))));
   require(hud::flatMenuVtable(build::semanticRva(build::rva(r))));
  }
  require(build::rva(0x1389020)==(store?0x138d600:0x1389020));
  require(build::rva(0x1389910)==(store?0x138def0:0x1389910));
  require(hud::tutorialKind(build::semanticRva(build::rva(0x2d19f98)))==0);
  for(auto r:{0x2d19f98,0x2d07f90,0x2d08128})require(hud::gameplayMenuVtable(build::semanticRva(build::rva(r))));
  for(auto c:camera::renderControls)require(build::rva(c.rva)!=0);
 }
 require(build::rva(0x123456)==0);require(build::semanticRva(0x123456)==0);
 size_t size=0;for(auto c:build::storeCode)if(c.rva+24>size)size=c.rva+24;
 std::vector<unsigned char> image(size);
 require(build::validateStoreCode(image.data())!=0);
 for(auto c:build::storeCode)std::memcpy(image.data()+c.rva,c.bytes,24);
 require(build::validateStoreCode(image.data())==0);
 for(auto contract:build::storeCode){
  image[contract.rva]^=1;require(build::validateStoreCode(image.data())!=0);image[contract.rva]^=1;
 }
 std::cout<<"Steam/Store mapping, HUD identities, CVar coverage and unknown-code rejection passed\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

