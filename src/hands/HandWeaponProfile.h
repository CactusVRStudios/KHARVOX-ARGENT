#pragma once
#include <array>
#include <string_view>
namespace kharvox::hands {
// Explicit Eternal names. No DOOM-2016 entity offsets or weapon IDs are used.
enum class HandWeaponKind {Unknown,CombatShotgun,HeavyCannon,PlasmaRifle,RocketLauncher,SuperShotgun,Ballista,Chaingun,Bfg,Chainsaw,Fists,Crucible,SentinelHammer,Count};
inline constexpr std::array<const char*,13> handProfileKeys{"default","combat_shotgun","heavy_cannon","plasma_rifle","rocket_launcher","super_shotgun","ballista","chaingun","bfg","chainsaw","fists","crucible","sentinel_hammer"};
inline const char* HandWeaponKindKey(HandWeaponKind kind){const auto i=size_t(kind);return i<handProfileKeys.size()?handProfileKeys[i]:"default";}
inline HandWeaponKind handProfile(std::string_view key){for(size_t i=0;i<handProfileKeys.size();++i)if(key==handProfileKeys[i])return HandWeaponKind(i);return HandWeaponKind::Unknown;}
}
