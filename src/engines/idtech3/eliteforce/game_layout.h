// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/vm/qvm.h"

#include <cstdint>

namespace reluva::idtech3::eliteforce {

// The start of every entity, the part the engine reads and writes, and
// the other structures the game module shares with the engine, as each
// game lays them out. Elite Force's in two versions:
//
// - The CD's game code (1.0), found from the code itself: what it passes
//   to traces and SetBrushModel, and the size of what it copies whole
//   (see docs/engines/idtech3/games/stvef/systems/syscalls.md). An entity state of 204 bytes (Quake
//   III's is 208), then entityShared_t at 204 with one field fewer than
//   1.2's before the box (which of linkcount, svFlags and singleClient
//   isn't known, so svFlags is found from the running game); a player
//   state of 460 bytes.
// - Patch 1.2's, as the GPL engine that runs it has them (see
//   docs/engines/idtech3/games/stvef/systems/network.md): linked, linkcount, svFlags, singleClient,
//   then the box; a player state of 468 bytes (an intro timer at 56, the
//   rest after it 4 bytes on).
//
// The entity state's fields are the same in both. Jedi Outcast's
// multiplayer (1.02a) is kLayoutJk2.
struct GameLayout {
    int32_t version = 10; // 10 for Elite Force 1.0, 12 for 1.2, 102 for Jedi Outcast 1.02
    int32_t playerStateSize = 460;
    int32_t number = 0;       // s.number
    int32_t modelIndex = 160; // s.modelindex
    // s.solid: what the client game draws a brush entity by, and predicts
    // collisions with.
    int32_t solid = 176;
    int32_t origin2 = 104; // s.origin2: where a portal's camera looks from
    int32_t entityStateSize = 204;
    int32_t linked = 204;
    int32_t svFlags = -1;      // -1: not known (found from the running game)
    int32_t singleClient = -1; // -1: not known
    int32_t bmodel = 216;
    int32_t mins = 220;
    int32_t maxs = 232;
    int32_t contents = 244;
    int32_t absMin = 248;
    int32_t absMax = 260;
    int32_t currentOrigin = 272;
    int32_t currentAngles = 284;
    int32_t ownerNum = 296;
    int32_t sharedEnd = 300;
    // Jedi Outcast's mIsRoffing: the entity is playing a ROFF (-1: none).
    int32_t isRoffing = -1;
    // The player's eye: the player state's origin raised by its view
    // height, when both offsets are known (-1: the entity's origin and the
    // standing view height).
    int32_t psOrigin = -1;
    int32_t psViewHeight = -1;
    // A player state word the original's network leaves out, cleared in
    // snapshots (-1: none).
    int32_t psServerOnly = 48;
    // Elite Force's SVF_CLIENTMASK (4): sent only to the clients whose bits
    // singleClient holds.
    bool clientMask = true;
    // trace_t: Quake III's 56 bytes, then (Jedi Outcast) its Ghoul2
    // collision records, 64 bytes each.
    int32_t traceSize = 56;
    int32_t g2Collisions = 0;
    // usercmd_t: Elite Force's (buttons and weapon before the angles,
    // 24 bytes) or Quake III's order with Raven's three selection bytes
    // (Jedi Outcast's, 28 bytes).
    bool ravenUserCommand = false;
    int32_t userCommandSize = 24;
    // The client game's: gameState_t's configstrings (an offset each into
    // 16,000 bytes of text), and glconfig_t (its strings, then Quake III's
    // 17 words from glconfigWords).
    int32_t maxConfigstrings = 1024;
    int32_t glconfigWords = 5124;
};
constexpr GameLayout kLayout10{};
constexpr GameLayout kLayout12{
    .version = 12,
    .playerStateSize = 468,
    .linked = 204,
    .svFlags = 212,
    .singleClient = 216,
    .bmodel = 220,
    .mins = 224,
    .maxs = 236,
    .contents = 248,
    .absMin = 252,
    .absMax = 264,
    .currentOrigin = 276,
    .currentAngles = 288,
    .ownerNum = 300,
    .sharedEnd = 304,
    .psOrigin = 20,
    .psViewHeight = 180,
};
// Jedi Outcast 1.02a's: an entity state of 296 bytes and a player state of
// 1,368 (the sizes its modules copy whole), entityShared_t after it as
// Quake III's with mIsRoffing before ownerNum and two broadcastClients
// words after it; a trace_t of 1,080 bytes (16 collision records); 1,400
// configstrings (the client game's bound on them); see
// docs/engines/idtech3/games/jk2/systems/game-calls.md.
constexpr GameLayout kLayoutJk2{
    .version = 102,
    .playerStateSize = 1368,
    .modelIndex = 212,
    .solid = 248,
    .entityStateSize = 296,
    .linked = 296,
    .svFlags = 304,
    .singleClient = 308,
    .bmodel = 312,
    .mins = 316,
    .maxs = 328,
    .contents = 340,
    .absMin = 344,
    .absMax = 356,
    .currentOrigin = 368,
    .currentAngles = 380,
    .ownerNum = 396,
    .sharedEnd = 408,
    .isRoffing = 392,
    .psOrigin = 20,
    .psViewHeight = 168,
    .psServerOnly = -1,
    .clientMask = false,
    .g2Collisions = 16,
    .ravenUserCommand = true,
    .userCommandSize = 28,
    .maxConfigstrings = 1400,
    .glconfigWords = 3 * 1024 + 8192,
};
constexpr int32_t kSolidBrushModel = 0xffffff;

// Which layout Elite Force's game code was built with: 1.2's copies
// 468-byte player states whole, 1.0's 460-byte ones.
inline const GameLayout &detectLayout(const Qvm &vm) {
    for (const auto &i : vm.code())
        if (i.op == qvmop::kBlockCopy && i.operand == kLayout12.playerStateSize)
            return kLayout12;
    return kLayout10;
}

constexpr int32_t kEntityNone = 1023;
constexpr int32_t kEntityWorld = 1022;

} // namespace reluva::idtech3::eliteforce
