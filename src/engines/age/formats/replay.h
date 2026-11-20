// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Replays: a race as it was driven, kept as what it was started with and
// each frame's time and keys. Played back, the race is run again from
// them: everything in it (the opponents, the traffic, the pedestrians,
// the police) is the same race, as it all follows from the race's seeds
// and the player's driving. Saved in Reluva's own file, <name>.rpl in the
// save folder's replays/. See docs/engines/age/games/mm2/systems/replays.md.

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::age {

/** A frame of a replay: how long it stepped, and the player's controls. */
struct ReplayFrame {
    float dt = 0;      // seconds the frame stepped
    uint16_t keys = 0; // the driving keys held (kDrive* bits)
    int8_t shift = 0;  // a manual gear change asked
    uint8_t flags = 0; // kReplayManual
    // A game controller's: the stick's steering (-127..127) and the
    // triggers (0..255), 0 when there's none.
    int8_t padSteer = 0;
    uint8_t padThrottle = 0;
    uint8_t padBrake = 0;
};
/** ReplayFrame::flags: the gears changed by hand. */
enum : uint8_t { kReplayManual = 1 };

/** A race as it was driven, as a replay file holds it. */
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
    // The conditions it was run in, when changed from the race's own
    // (laps, opponents, time, weather, police; traffic, pedestrians).
    bool changed = false;
    int laps = 1;
    int opponents = 0;
    int timeOfDay = 0;
    int weather = 0;
    int cops = 0;
    float traffic = 0;
    float peds = 0;
    // Its name, as saved.
    std::string name;
    std::vector<ReplayFrame> frames;
    // Where the player's car was every kMarkEvery frames: a played-back
    // race that isn't there has gone its own way.
    static constexpr size_t kMarkEvery = 60;
    std::vector<Vec3> marks;

    /** The frames' times added up, in seconds. */
    float duration() const;
    /** Whether it has no frames. */
    bool empty() const { return frames.empty(); }
};

/** The replay as a file's bytes. */
std::vector<uint8_t> encodeReplay(const Replay &replay);
/**
 * A replay from a file's bytes, of this version or an earlier one (those
 * without the conditions or a game controller's sticks have their
 * defaults). Fails on another file or version, or one cut short, leaving
 * `out` as it was.
 */
Error decodeReplay(const std::vector<uint8_t> &data, Replay &out);

/** The save folder's replays/. */
std::filesystem::path replayFolder(const std::filesystem::path &saveFolder);
/**
 * A name as a file's, with ".rpl": letters, digits, spaces, - and _ (else
 * _), without spaces at the end; "replay" for an empty name.
 */
std::string replayFileName(const std::string &name);
/**
 * Writes the replay to the replays folder under its name's file, over one
 * of the same name. Fails without a save folder.
 */
Error saveReplay(const std::filesystem::path &saveFolder, const Replay &replay);
/** Reads a replay file (see decodeReplay). */
Error loadReplay(const std::filesystem::path &file, Replay &out);
/** A saved replay, as the replays menu lists it. */
struct ReplayEntry {
    std::filesystem::path file;
    std::string name;
    std::string city;
    int kind = 0;
    int race = 0;
    float duration = 0;
};
/** Every replay saved, sorted by name; files that don't read are left out. */
std::vector<ReplayEntry> listReplays(const std::filesystem::path &saveFolder);
/** Saves the replay under a new name, then removes its old file if the name's file differs. */
Error renameReplay(const std::filesystem::path &saveFolder, const ReplayEntry &entry, const std::string &name);
/** Removes the replay's file. */
Error deleteReplay(const ReplayEntry &entry);

} // namespace reluva::age
