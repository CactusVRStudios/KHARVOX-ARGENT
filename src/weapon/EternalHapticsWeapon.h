#pragma once
#include "LegacyHapticsWeaponKind.h"
#include <string>
#include <string_view>
namespace argent {
inline const char* eternalCalibrationProfile(KharvoxWeaponKind kind,bool crucible=false,bool sentinelHammer=false){
 if(sentinelHammer)return "sentinel_hammer";
 if(crucible)return "crucible";
 switch(kind){
 case KharvoxWeaponKind::Shotgun:return "combat_shotgun";
 case KharvoxWeaponKind::HeavyAssaultRifle:return "heavy_cannon";
 case KharvoxWeaponKind::PlasmaRifle:return "plasma_rifle";
 case KharvoxWeaponKind::RocketLauncher:return "rocket_launcher";
 case KharvoxWeaponKind::SuperShotgun:return "super_shotgun";
 case KharvoxWeaponKind::GaussCannon:return "ballista";
 case KharvoxWeaponKind::Chaingun:return "chaingun";
 case KharvoxWeaponKind::Bfg:return "bfg";
 case KharvoxWeaponKind::Chainsaw:return "chainsaw";
 case KharvoxWeaponKind::Fists:return "fists";
 default:return "default";
 }
}
inline KharvoxWeaponKind eternalHapticsWeapon(std::string_view name){
 std::string key;for(unsigned char c:name)if(c!='_'&&c!='-'&&c!=' ')key+=char(c>='A'&&c<='Z'?c+32:c);
 const auto has=[&](const char* part){return key.find(part)!=std::string::npos;};
 if(has("supershotgun")||has("doublebarrel"))return KharvoxWeaponKind::SuperShotgun;
 if(has("shotgun"))return KharvoxWeaponKind::Shotgun;
 if(has("heavycannon")||has("heavyassaultrifle"))return KharvoxWeaponKind::HeavyAssaultRifle;
 if(has("plasmarifle"))return KharvoxWeaponKind::PlasmaRifle;
 if(has("rocketlauncher"))return KharvoxWeaponKind::RocketLauncher;
 if(has("ballista")||has("gauss"))return KharvoxWeaponKind::GaussCannon;
 if(has("chaingun"))return KharvoxWeaponKind::Chaingun;
 if(has("bfg"))return KharvoxWeaponKind::Bfg;
 if(has("chainsaw"))return KharvoxWeaponKind::Chainsaw;
 if(has("fists"))return KharvoxWeaponKind::Fists;
 return KharvoxWeaponKind::Unknown;
}
}
