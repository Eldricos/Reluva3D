// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/qvm.h"

#include <cstdint>

namespace reluva::idtech3::eliteforce {

// The start of every entity, the part the engine reads and writes, and
// the player state's size, as Elite Force lays them out. Two versions:
//
// - The CD's game code (1.0), found from the code itself: what it passes
//   to traces and SetBrushModel, and the size of what it copies whole
//   (see eliteforce-syscalls.md). An entity state of 204 bytes (Quake
//   III's is 208), then entityShared_t at 204 with one field fewer than
//   1.2's before the box (which of linkcount, svFlags and singleClient
//   isn't known, so svFlags is found from the running game); a player
//   state of 460 bytes.
// - Patch 1.2's, as the GPL engine that runs it has them (see
//   eliteforce-network.md): linked, linkcount, svFlags, singleClient,
//   then the box; a player state of 468 bytes (an intro timer at 56, the
//   rest after it 4 bytes on).
//
// The entity state's fields are the same in both.
struct GameLayout {
    int32_t version = 10; // 10 for 1.0, 12 for 1.2
    int32_t playerStateSize = 460;
    int32_t number = 0;       // s.number
    int32_t modelIndex = 160; // s.modelindex
    // s.solid: what the client game draws a brush entity by, and predicts
    // collisions with.
    int32_t solid = 176;
    int32_t linked = 204;
    int32_t svFlags = -1; // -1: not known (found from the running game)
    int32_t bmodel = 216;
    int32_t mins = 220, maxs = 232;
    int32_t contents = 244;
    int32_t absMin = 248, absMax = 260;
    int32_t currentOrigin = 272, currentAngles = 284;
    int32_t ownerNum = 296;
    int32_t sharedEnd = 300;
};
constexpr GameLayout kLayout10{};
constexpr GameLayout kLayout12{12, 468, 0, 160, 176, 204, 212, 220, 224, 236, 248, 252, 264, 276, 288, 300, 304};
constexpr int32_t kSolidBrushModel = 0xffffff;

// Which layout game code was built with: 1.2's copies 468-byte player
// states whole, 1.0's 460-byte ones.
inline const GameLayout& detectLayout(const Qvm& vm) {
    for (const auto& i : vm.code())
        if (i.op == qvmop::BlockCopy && i.operand == kLayout12.playerStateSize)
            return kLayout12;
    return kLayout10;
}

constexpr int32_t kEntityNone = 1023, kEntityWorld = 1022;

} // namespace reluva::idtech3::eliteforce
