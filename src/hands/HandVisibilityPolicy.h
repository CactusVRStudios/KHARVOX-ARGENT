#pragma once

namespace kharvox::hands {
inline bool rendererRequired(bool showHands,bool handsCalibration,bool hudCalibration){
    return showHands||handsCalibration||hudCalibration;
}

enum class HandModelKind { None, Fist, GunHolding };

struct HandAssetAvailability {
    bool leftFist{};
    bool rightFist{};
    bool leftGun{};
    bool rightGun{};
};

struct HandVisibilityInput {
    bool leftHanded{};
    bool twoHandGrab{};
    bool berserk{};
    HandAssetAvailability assets{};
    bool showHands{};
    // False for every authored camera sequence, including Glory Kills, ledge
    // transitions and ordinary cinematics. Presentation context has priority
    // over Berserk and all handedness rules.
    bool presentationAllowsHands{true};
};

struct HandVisibilityOutput {
    HandModelKind left{HandModelKind::None};
    HandModelKind right{HandModelKind::None};
    // True means KHARVOX deliberately supplies no replacement for the support
    // hand and must leave DOOM's existing native representation untouched.
    bool preserveNativeOffHand{};
};

inline HandVisibilityOutput selectHandVisibility(
    const HandVisibilityInput& input) {
    HandVisibilityOutput output{};
    if (!input.showHands || !input.presentationAllowsHands)
        return output;
    if (input.berserk) {
        output.left = input.assets.leftFist
            ? HandModelKind::Fist : HandModelKind::None;
        output.right = input.assets.rightFist
            ? HandModelKind::Fist : HandModelKind::None;
        return output;
    }

    const bool leftIsWeaponHand = input.leftHanded;
    // Once the support grip is latched, DOOM's weapon already supplies the
    // visual two-hand contact. Keep only KHARVOX's weapon-hand pose so the
    // free/off-hand model cannot overlap the grabbed weapon.
    const auto desiredLeft = leftIsWeaponHand
        ? HandModelKind::GunHolding
        : (input.twoHandGrab ? HandModelKind::None : HandModelKind::Fist);
    const auto desiredRight = !leftIsWeaponHand
        ? HandModelKind::GunHolding
        : (input.twoHandGrab ? HandModelKind::None : HandModelKind::Fist);
    const auto available = [&](bool left, HandModelKind kind) {
        if (kind == HandModelKind::Fist)
            return left ? input.assets.leftFist : input.assets.rightFist;
        if (kind == HandModelKind::GunHolding)
            return left ? input.assets.leftGun : input.assets.rightGun;
        return kind == HandModelKind::None;
    };
    if (available(true, desiredLeft)) output.left = desiredLeft;
    if (available(false, desiredRight)) output.right = desiredRight;

    const bool offHandIsRight = input.leftHanded;
    const auto offHandKind = offHandIsRight ? desiredRight : desiredLeft;
    if (!input.twoHandGrab && !available(!offHandIsRight, offHandKind))
        output.preserveNativeOffHand = true;
    return output;
}

} // namespace kharvox::hands
