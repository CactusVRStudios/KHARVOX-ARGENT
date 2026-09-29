#include "../src/hands/HandCalibrationPolicy.h"

#include <cassert>

int main() {
    using namespace kharvox::hands;

    bool plusWasDown{};
    auto mode=updateHandCalibrationMode(CalibrationMode::Rotation,true,plusWasDown);
    assert(mode==CalibrationMode::Position);
    mode=updateHandCalibrationMode(mode,true,plusWasDown);
    assert(mode==CalibrationMode::Position); // held key / second eye
    mode=updateHandCalibrationMode(mode,false,plusWasDown);
    mode=updateHandCalibrationMode(mode,true,plusWasDown);
    assert(mode==CalibrationMode::Rotation);
    plusWasDown=false;
    assert(updateHandCalibrationMode(CalibrationMode::None,true,plusWasDown)
        ==CalibrationMode::None);

    auto profile = selectHandCalibrationProfile(false, false,
        HandWeaponKind::CombatShotgun, true);
    assert(profile.weaponSpecific && profile.handIndex == 1u);
    assert(profile.weaponIndex ==
        static_cast<std::size_t>(HandWeaponKind::CombatShotgun));

    profile = selectHandCalibrationProfile(true, true,
        HandWeaponKind::Ballista, true);
    assert(profile.weaponSpecific && profile.handIndex == 0u);

    profile = selectHandCalibrationProfile(true, false,
        HandWeaponKind::CombatShotgun, false);
    assert(!profile.weaponSpecific && profile.handIndex == 0u);

    profile = selectHandCalibrationProfile(false, false,
        HandWeaponKind::Unknown, true);
    assert(!profile.weaponSpecific);
    assert(handProfile("crucible")==HandWeaponKind::Crucible);
    for(bool left:{false,true}){
        profile=selectHandCalibrationProfile(left,left,HandWeaponKind::Crucible,true);
        assert(profile.weaponSpecific&&profile.handIndex==(left?0u:1u));
        assert(profile.weaponIndex==static_cast<std::size_t>(HandWeaponKind::Crucible));
    }
    assert(handProfile("sentinel_hammer")==HandWeaponKind::SentinelHammer);
    for(bool left:{false,true}){
        profile=selectHandCalibrationProfile(left,left,HandWeaponKind::SentinelHammer,true);
        assert(profile.weaponSpecific&&profile.weaponIndex==size_t(HandWeaponKind::SentinelHammer));
        assert(handPoseKey(HandWeaponKind::SentinelHammer,left,left)!=handPoseKey(HandWeaponKind::Crucible,left,left));
    }
    return 0;
}
