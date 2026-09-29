#pragma once
// KHARVOX protocol enum only. No Eternal weapon mapping or AER code.
enum class KharvoxWeaponKind {
    Unknown = 0,
    Pistol = 1,
    Shotgun = 2,
    HeavyAssaultRifle = 3,
    PlasmaRifle = 4,
    RocketLauncher = 5,
    SuperShotgun = 6,
    GaussCannon = 7,
    Chaingun = 8,
    Bfg = 9,
    Chainsaw = 10,
    Fists = 11,
    AssaultRifle = 12,
    ArcCannon = 13,
    MancubusGland = 14,
    Count = 15
};
enum class KharvoxWeaponAmmoState {
    Unknown = 0,
    Unavailable,
    Empty,
    Usable
};
