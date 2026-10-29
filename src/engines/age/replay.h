// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Replays: a race as it was driven, kept as what it was started with and
// each frame's time and keys. Played back, the race is run again from
// them: everything in it (the opponents, the traffic, the pedestrians,
// the police) is the same race, as it all follows from the race's seeds
// and the player's driving. Saved in Reluva's own file, <name>.rpl in the
// save folder's replays/. See docs/engines/age-racing.md.

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::age {

struct ReplayFrame {
    float dt = 0;      // seconds the frame stepped
    uint16_t keys = 0; // the driving keys held (kDrive* bits)
    int8_t shift = 0;  // a manual gear change asked
    uint8_t flags = 0; // kReplayManual
};
enum : uint8_t { kReplayManual = 1 };

struct Replay {
    // What the race was started with.
    std::string city; // its map ("sf")
    int kind = 0;     // RaceKind
    int race = 0;     // its index
    int level = 0;    // 0 amateur, 1 professional
    std::string car;  // the player's ("vpbug")
    int paint = 0;
    bool autoReverse = false;
    float steerRate = 1;
    float pedestrians = 1; // the share Show Pedestrians left
    // Its name, as saved.
    std::string name;
    std::vector<ReplayFrame> frames;
    // Where the player's car was every kMarkEvery frames: a played-back
    // race that isn't there has gone its own way.
    static constexpr size_t kMarkEvery = 60;
    std::vector<Vec3> marks;

    float duration() const;
    bool empty() const { return frames.empty(); }
};

std::vector<uint8_t> encodeReplay(const Replay& replay);
Error decodeReplay(const std::vector<uint8_t>& data, Replay& out);

// The save folder's replays/.
std::filesystem::path replayFolder(const std::filesystem::path& saveFolder);
// A name as a file's: letters, digits, spaces, - and _ (else _).
std::string replayFileName(const std::string& name);
Error saveReplay(const std::filesystem::path& saveFolder, const Replay& replay);
Error loadReplay(const std::filesystem::path& file, Replay& out);
struct ReplayEntry {
    std::filesystem::path file;
    std::string name, city;
    int kind = 0, race = 0;
    float duration = 0;
};
// Every replay saved, by name.
std::vector<ReplayEntry> listReplays(const std::filesystem::path& saveFolder);
Error renameReplay(const std::filesystem::path& saveFolder, const ReplayEntry& entry, const std::string& name);
Error deleteReplay(const ReplayEntry& entry);

} // namespace reluva::age
