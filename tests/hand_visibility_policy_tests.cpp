#include "../src/hands/HandVisibilityPolicy.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace kharvox::hands;
    const HandAssetAvailability all{true, true, true, true};

    auto output = selectHandVisibility(
        {false, false, false, all, true, true});
    require(output.left == HandModelKind::Fist
        && output.right == HandModelKind::GunHolding,
        "right-handed mode uses free left fist and right gun pose");

    output = selectHandVisibility({true, false, false, all, true, true});
    require(output.left == HandModelKind::GunHolding
        && output.right == HandModelKind::Fist,
        "left-handed mode mirrors weapon and free-hand poses");

    output = selectHandVisibility({false, true, false, all, true, true});
    require(output.left == HandModelKind::None
        && output.right == HandModelKind::GunHolding,
        "right-handed two-hand grab hides the left off-hand pose");

    output = selectHandVisibility({true, true, false, all, true, true});
    require(output.left == HandModelKind::GunHolding
        && output.right == HandModelKind::None,
        "left-handed two-hand grab hides the right off-hand pose");

    output = selectHandVisibility({false, true, true, all, true, true});
    require(output.left == HandModelKind::Fist
        && output.right == HandModelKind::Fist,
        "berserk overrides handedness and two-hand grab");

    output = selectHandVisibility({false, false, false, all, false, true});
    require(output.left == HandModelKind::None
        && output.right == HandModelKind::None,
        "Show Hands defaults to an inactive renderer policy");

    output = selectHandVisibility({false, false, false, all, true, false});
    require(output.left == HandModelKind::None
        && output.right == HandModelKind::None,
        "cinematics suppress both custom hands");
    output = selectHandVisibility({false, true, true, all, true, false});
    require(output.left == HandModelKind::None
        && output.right == HandModelKind::None,
        "cinematic suppression has priority over berserk and two-hand grab");

    output = selectHandVisibility({false, false, false,
        {true, false, false, true}, true, true});
    require(output.left == HandModelKind::Fist
        && output.right == HandModelKind::GunHolding,
        "missing unrelated pose assets do not hide available models");

    output = selectHandVisibility({false, false, false,
        {false, true, true, true}, true, true});
    require(output.left == HandModelKind::None
        && output.preserveNativeOffHand,
        "missing free-hand fist retains the native off-hand fallback");
    return 0;
}
