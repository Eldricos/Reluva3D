// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3::raven {

/**
 * Raven's effect files (effects/<name>.efx, Jedi Outcast's and later games'):
 * a list of primitives (particles, lines, sounds, lights...), each a
 * template the effects system picks its values from whenever it plays the
 * effect. See docs/engines/idtech3/formats/efx.md.
 */
struct EffectRange {
    float min = 0;
    float max = 0;
};

struct EffectPrimitive {
    enum Type {
        kParticle,
        kLine,
        kTail,
        kSound,
        kCylinder,
        kElectricity,
        kEmitter,
        kDecal,
        kOrientedParticle,
        kFxRunner,
        kLight,
        kCameraShake,
    };
    // How a value changes over a primitive's life (a group's "flags"), in
    // the 4 bits of each of `flags`' channels below.
    static constexpr int32_t kLinear = 0x1;
    static constexpr int32_t kRandom = 0x2;
    static constexpr int32_t kNonLinear = 0x4;
    static constexpr int32_t kWave = 0x8;
    static constexpr int32_t kClamp = 0xc; // kNonLinear | kWave
    static constexpr int32_t kParmMask = 0xc;
    // Where each channel's 4 bits are in `flags`.
    static constexpr int kAlphaShift = 0;
    static constexpr int kRgbShift = 4;
    static constexpr int kSizeShift = 8;
    static constexpr int kLengthShift = 12;
    static constexpr int kSize2Shift = 16;
    // The rest of `flags` (the "flags" key).
    static constexpr int32_t kDepthHack = 0x100000;
    static constexpr int32_t kRelative = 0x200000;
    static constexpr int32_t kSetShaderTime = 0x400000;
    static constexpr int32_t kExpensivePhysics = 0x800000;
    static constexpr int32_t kAttachedModel = 0x1000000;
    static constexpr int32_t kApplyPhysics = 0x2000000;
    static constexpr int32_t kUseBBox = 0x4000000;
    static constexpr int32_t kUseAlpha = 0x8000000;
    static constexpr int32_t kEmitFx = 0x10000000;
    static constexpr int32_t kDeathRunsFx = 0x20000000;
    static constexpr int32_t kKillOnImpact = 0x40000000;
    static constexpr uint32_t kImpactRunsFx = 0x80000000;
    // Electricity's, sharing bits with the model's and physics'.
    static constexpr int32_t kTaper = 0x1000000;
    static constexpr int32_t kBranch = 0x2000000;
    static constexpr int32_t kGrow = 0x4000000;
    // `spawnFlags` ("spawnFlags"): how it's placed when created.
    static constexpr int32_t kOrgOnSphere = 0x1;
    static constexpr int32_t kAxisFromSphere = 0x2;
    static constexpr int32_t kOrgOnCylinder = 0x4;
    static constexpr int32_t kOrg2FromTrace = 0x10;
    static constexpr int32_t kTraceImpactFx = 0x20;
    static constexpr int32_t kOrg2IsOffset = 0x40;
    static constexpr int32_t kCheapOrgCalc = 0x100;
    static constexpr int32_t kCheapOrg2Calc = 0x200;
    static constexpr int32_t kAbsoluteVelocity = 0x400;
    static constexpr int32_t kAbsoluteAcceleration = 0x800;
    static constexpr int32_t kRandomRotationAroundForward = 0x1000;
    static constexpr int32_t kEvenDistribution = 0x2000;
    static constexpr int32_t kRgbComponentInterpolation = 0x4000;
    static constexpr int32_t kAffectedByWind = 0x10000;

    // A value with its start, end and parameter (a time in hundredths of
    // the life, or a wave's speed), each a range.
    struct Channel {
        EffectRange start{1, 1};
        EffectRange end{1, 1};
        EffectRange parm;
    };
    enum MediaKind { kShader, kModel, kSoundFile };

    Type type = kParticle;
    std::string name;
    int32_t flags = 0;
    int32_t spawnFlags = 0;
    EffectRange life{1, 1};              // ms
    EffectRange delay;                   // ms
    EffectRange count{1, 1};             // how many are made
    float cullRange = 0;                 // units from the eye past which none are; 0: no limit
    std::array<EffectRange, 3> origin{}; // forward, right, up of the effect (or x, y, z: kCheapOrgCalc)
    std::array<EffectRange, 3> origin2{};
    EffectRange radius{1, 1};
    EffectRange height{1, 1};
    EffectRange wind;
    EffectRange rotation;      // degrees
    EffectRange rotationDelta; // degrees per 100 ms
    std::array<EffectRange, 3> angle{};
    std::array<EffectRange, 3> angleDelta{};
    std::array<EffectRange, 3> velocity{};
    std::array<EffectRange, 3> acceleration{};
    EffectRange gravity;
    EffectRange density{10, 10};
    EffectRange variance{1, 1};
    EffectRange elasticity; // "bounce", or a camera shake's "intensity"
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    std::array<Channel, 3> rgb{}; // red, green, blue
    EffectRange rgbParm;
    Channel alpha;
    Channel size;
    Channel size2;
    Channel length;
    std::vector<std::pair<MediaKind, std::string>> media;
    std::vector<std::string> impactFx;
    std::vector<std::string> deathFx;
    std::vector<std::string> emitFx;
    std::vector<std::string> playFx;
};

struct EffectFile {
    std::vector<EffectPrimitive> primitives;
};

// Reads an effect file. Unknown primitives, keys and flags are skipped (as
// the original does) and named in `warnings`, if given; false only if the
// text isn't made of well-formed groups.
bool parseEffectFile(std::string_view text, EffectFile &out, std::vector<std::string> *warnings = nullptr);

} // namespace reluva::idtech3::raven
