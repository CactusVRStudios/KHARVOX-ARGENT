#include <iostream>

#include "../src/psvr2/Psvr2IpcProtocol.h"
#include "../src/psvr2/Psvr2TriggerPolicy.h"
#include "../src/weapon/EternalHapticsWeapon.h"

namespace {

int failures{};

void require(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAILED: " << name << '\n';
        ++failures;
    }
}

kharvox::psvr2::TriggerCommand select(KharvoxWeaponKind weapon,
    bool leftHanded = false, bool fireDown = false,
    KharvoxWeaponAmmoState ammoState = KharvoxWeaponAmmoState::Unknown,
    std::uint64_t fireHeldMilliseconds = 0) {
    return kharvox::psvr2::selectTriggerCommand({
        true, true, true, true, weapon, fireDown, fireHeldMilliseconds,
        ammoState, leftHanded,
        kharvox::psvr2::TriggerOffReason::Loading});
}

void expectProfile(KharvoxWeaponKind weapon, int start, int end,
    int strength, const char* name) {
    const auto command = select(weapon);
    require(command.effect == kharvox::psvr2::TriggerEffect::Weapon, name);
    require(command.startPosition == start && command.endPosition == end
        && command.strength == strength, name);
}

void expectVibration(KharvoxWeaponKind weapon, int position, int amplitude,
    int frequency, const char* name) {
    const auto command = select(weapon, false, true);
    require(command.effect == kharvox::psvr2::TriggerEffect::Vibration, name);
    require(command.position == position && command.amplitude == amplitude
        && command.frequency == frequency, name);
}

} // namespace

int main() {
    using argent::eternalHapticsWeapon;
    require(eternalHapticsWeapon("weapon/player/combat_shotgun")==KharvoxWeaponKind::Shotgun,"Eternal combat shotgun");
    require(eternalHapticsWeapon("weapon/player/super_shotgun")==KharvoxWeaponKind::SuperShotgun,"Eternal super shotgun precedes shotgun");
    require(eternalHapticsWeapon("weapon/player/heavy_cannon")==KharvoxWeaponKind::HeavyAssaultRifle,"Eternal heavy cannon");
    require(eternalHapticsWeapon("weapon/player/plasma_rifle")==KharvoxWeaponKind::PlasmaRifle,"Eternal plasma rifle");
    require(eternalHapticsWeapon("weapon/player/rocket_launcher")==KharvoxWeaponKind::RocketLauncher,"Eternal rocket launcher");
    require(eternalHapticsWeapon("weapon/player/Ballista")==KharvoxWeaponKind::GaussCannon,"Eternal ballista uses KHARVOX Gauss");
    require(eternalHapticsWeapon("weapon/player/chaingun")==KharvoxWeaponKind::Chaingun,"Eternal chaingun");
    require(eternalHapticsWeapon("weapon/player/bfg")==KharvoxWeaponKind::Bfg,"Eternal BFG");
    require(eternalHapticsWeapon("weapon/player/chainsaw")==KharvoxWeaponKind::Chainsaw,"Eternal chainsaw");
    require(eternalHapticsWeapon("default")==KharvoxWeaponKind::Unknown,"Calibration default is not an equipped weapon");
    using namespace kharvox::psvr2;

    expectProfile(KharvoxWeaponKind::Pistol, 4, 6, 3, "Pistol light");
    const auto plasma=select(KharvoxWeaponKind::PlasmaRifle);
    require(plasma.effect==TriggerEffect::Feedback&&plasma.position==4
        &&plasma.strength==3,"Plasma soft feedback");

    expectProfile(KharvoxWeaponKind::HeavyAssaultRifle, 3, 5, 5, "HAR medium");
    expectProfile(KharvoxWeaponKind::AssaultRifle, 3, 5, 5, "Assault medium");
    expectProfile(KharvoxWeaponKind::Chaingun, 3, 5, 5, "Chaingun medium");
    expectProfile(KharvoxWeaponKind::ArcCannon, 3, 5, 5, "Arc medium");
    const auto gauss=select(KharvoxWeaponKind::GaussCannon);
    require(gauss.effect==TriggerEffect::SlopeFeedback
        &&gauss.startPosition==2&&gauss.endPosition==8
        &&gauss.startStrength==2&&gauss.endStrength==7,
        "Gauss rising slope");

    expectProfile(KharvoxWeaponKind::Shotgun, 2, 4, 7, "Shotgun heavy");
    expectProfile(KharvoxWeaponKind::SuperShotgun, 2, 4, 7,
        "Super Shotgun heavy");
    expectProfile(KharvoxWeaponKind::RocketLauncher, 2, 4, 7,
        "Rocket Launcher heavy");
    expectProfile(KharvoxWeaponKind::MancubusGland, 2, 4, 7, "Gland heavy");
    const auto bfg=select(KharvoxWeaponKind::Bfg);
    require(bfg.effect==TriggerEffect::SlopeFeedback
        &&bfg.startPosition==2&&bfg.endPosition==8
        &&bfg.startStrength==3&&bfg.endStrength==8,
        "BFG rising slope");

    expectVibration(KharvoxWeaponKind::HeavyAssaultRifle, 3, 7, 40,
        "HAR firing vibration");
    expectVibration(KharvoxWeaponKind::AssaultRifle, 3, 7, 40,
        "Assault firing vibration");
    expectVibration(KharvoxWeaponKind::Chaingun, 2, 6, 25,
        "Chaingun firing vibration starts slowly");
    const auto chaingunSecond=select(KharvoxWeaponKind::Chaingun,false,true,
        KharvoxWeaponAmmoState::Usable,200);
    require(chaingunSecond.amplitude==7&&chaingunSecond.frequency==35,
        "Chaingun second warmup stage");
    const auto chaingunThird=select(KharvoxWeaponKind::Chaingun,false,true,
        KharvoxWeaponAmmoState::Usable,450);
    require(chaingunThird.amplitude==8&&chaingunThird.frequency==45,
        "Chaingun third warmup stage");
    const auto chaingunFull=select(KharvoxWeaponKind::Chaingun,false,true,
        KharvoxWeaponAmmoState::Usable,750);
    require(chaingunFull.amplitude==8&&chaingunFull.frequency==55,
        "Chaingun full-speed vibration");
    expectVibration(KharvoxWeaponKind::PlasmaRifle, 4, 3, 60,
        "Plasma firing vibration");
    require(select(KharvoxWeaponKind::Pistol, false, true).effect
        == TriggerEffect::Weapon, "semi-auto fire retains static profile");
    require(select(KharvoxWeaponKind::HeavyAssaultRifle, false, false).effect
        == TriggerEffect::Weapon, "fire release restores static profile");
    require(select(KharvoxWeaponKind::HeavyAssaultRifle, false, true,
        KharvoxWeaponAmmoState::Empty).effect == TriggerEffect::Weapon,
        "empty ammo blocks firing vibration");
    require(select(KharvoxWeaponKind::Chaingun, false, true,
        KharvoxWeaponAmmoState::Unavailable).effect == TriggerEffect::Weapon,
        "unavailable weapon blocks firing vibration");
    require(select(KharvoxWeaponKind::PlasmaRifle, false, true,
        KharvoxWeaponAmmoState::Unknown).effect == TriggerEffect::Vibration,
        "unknown ammo does not block firing vibration");
    const auto chainsaw=select(KharvoxWeaponKind::Chainsaw,false,true,
        KharvoxWeaponAmmoState::Unknown);
    require(chainsaw.effect==TriggerEffect::MultiplePositionVibration
        &&chainsaw.frequency==45
        &&chainsaw.controlPoints==chainsawVibrationPoints,
        "Chainsaw pull-dependent vibration");
    require(select(KharvoxWeaponKind::Chainsaw,false,true,
        KharvoxWeaponAmmoState::Empty).effect==TriggerEffect::Off,
        "empty Chainsaw stays off");
    require(select(KharvoxWeaponKind::HeavyAssaultRifle, true, true).hand
        == TriggerHand::Left, "left-handed firing vibration routing");

    for (const auto weapon : {KharvoxWeaponKind::Unknown,
            KharvoxWeaponKind::Fists, KharvoxWeaponKind::Chainsaw,
            KharvoxWeaponKind::Count})
        require(select(weapon).effect == TriggerEffect::Off,
            "non-firearm always off");

    require(select(KharvoxWeaponKind::Shotgun).hand == TriggerHand::Right,
        "right-handed routing");
    require(select(KharvoxWeaponKind::Shotgun, true).hand == TriggerHand::Left,
        "left-handed routing");

    auto input = TriggerPolicyInput{true, true, true, true,
        KharvoxWeaponKind::Shotgun, true, 0, KharvoxWeaponAmmoState::Usable,
        false, TriggerOffReason::Menu};
    input.launcherEnabled = false;
    require(selectTriggerCommand(input).offReason == TriggerOffReason::Disabled,
        "disabled option off");
    input.launcherEnabled = true;
    input.steamVrCompatibleRuntime = false;
    require(selectTriggerCommand(input).offReason
        == TriggerOffReason::UnsupportedRuntime, "unsupported runtime off");
    input.steamVrCompatibleRuntime = true;
    input.openXrFocused = false;
    require(selectTriggerCommand(input).offReason == TriggerOffReason::Unfocused,
        "focus loss off");
    input.openXrFocused = true;
    input.gameplayActive = false;
    require(selectTriggerCommand(input).offReason == TriggerOffReason::Menu,
        "menu off");

    TriggerDeliveryState delivery{};
    const auto heavy = select(KharvoxWeaponKind::Shotgun);
    require(shouldApplyTriggerCommand(delivery, heavy, 1), "first command sent");
    markTriggerCommandApplied(delivery, heavy, 1);
    require(!shouldApplyTriggerCommand(delivery, heavy, 1),
        "identical command suppressed");
    require(shouldApplyTriggerCommand(delivery, heavy, 2),
        "driver reconnect reapplies");
    const auto light = select(KharvoxWeaponKind::Pistol);
    require(shouldApplyTriggerCommand(delivery, light, 1),
        "material profile change sent");
    const auto automatic = select(KharvoxWeaponKind::HeavyAssaultRifle,
        false, true, KharvoxWeaponAmmoState::Usable);
    markTriggerCommandApplied(delivery,
        select(KharvoxWeaponKind::HeavyAssaultRifle), 1);
    require(shouldApplyTriggerCommand(delivery, automatic, 1),
        "fire transition sends vibration once");
    markTriggerCommandApplied(delivery, automatic, 1);
    require(!shouldApplyTriggerCommand(delivery, automatic, 1),
        "held fire vibration is not resent per frame");
    require(shouldApplyTriggerCommand(delivery,
        select(KharvoxWeaponKind::HeavyAssaultRifle), 1),
        "fire release restores weapon profile");

    return failures == 0 ? 0 : 1;
}
