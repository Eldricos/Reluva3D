// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/types.h"

#include <array>
#include <filesystem>

namespace reluva::tpw {

/**
 * The Options screen's settings that Reluva follows: the game's speed,
 * the sound's three volumes and the advisor, kept in `options.txt` in the
 * game's save folder. See docs/engines/tpw/games/simcoaster/systems/options.md.
 */
struct GameOptions {
    /** The speed the park's clock runs at Reluva's own pace: Config.sam's default config's DefaultGameSpeed. */
    static constexpr int kNormalSpeed = 70;
    int gameSpeed = kNormalSpeed; // percent, 10 to 100
    /** Each kind of sound's volume, percent: sound effects, speech, music. */
    std::array<int, 3> volume{100, 100, 100};
    bool advisor = true;
};

/**
 * A new player's options, as the game's settings give them: the default
 * config's speed (`Config.sam`) and the sound's levels (`sound.sam`).
 */
GameOptions defaultOptions(const SearchSet &files);

/** The options kept in the save folder, or `defaults` for those it hasn't. */
GameOptions readOptions(const std::filesystem::path &saveFolder, const GameOptions &defaults);

/** Sets the mixer's volumes for each kind of sound by them; nothing for nullptr. */
void applyOptions(const GameOptions &options, audio::Mixer *mixer);

/** Keeps them there; nothing without a folder. */
Error writeOptions(const std::filesystem::path &saveFolder, const GameOptions &options);

} // namespace reluva::tpw
