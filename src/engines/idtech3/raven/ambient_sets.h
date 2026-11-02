// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/collision.h"

#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3::raven {

// Ambient sound sets, as Elite Force's and Jedi Knight's engines keep them
// (sound/sound.txt; see docs/engines/idtech3/formats/eliteforce-sp-interface.md):
// named groups of sounds the client game asks the engine to play.
//
// - A general set is the level's background: a looping sound and, every
//   so often, one of its other sounds, heard everywhere. Changing to
//   another cross-fades over a second.
// - A local set belongs to an entity: the same, but heard within its
//   radius.
// - A brush model set lists the sounds a door or platform makes at each
//   stage of its move.
class AmbientSets {
public:
    enum class Kind { General, Local, BModel };

    struct Set {
        std::string name;
        Kind kind = Kind::General;
        int id = 0;
        // Seconds between the occasional sounds: a random whole number in
        // [timeStart, timeEnd], drawn afresh each time it's checked.
        int timeStart = 10, timeEnd = 25;
        // Their volume (0-255): a random one in [volumeMin, volumeMax].
        int volumeMin = 255, volumeMax = 255;
        std::vector<std::string> subWaves; // "sound/<dir>/<wave>.wav", at most 8
        std::string loopedWave;           // "sound/<name>.wav", or empty
        int radius = 250;                 // local sets
        // While cross-fading (0-255), and since when.
        int masterVolume = 255;
        int64_t fadeStart = 0;
    };

    // A sound to play: a loop (to keep only while asked for each frame),
    // or one played once. Volume 0 to 1.
    struct Sound {
        bool loop = false;
        int entity = 0;
        Vec3f origin{};
        float volume = 1;
        std::string name;
    };

    explicit AmbientSets(uint32_t seed = 0) : random_(seed) { clear(); }

    // The sets the level uses (AS_AddPrecacheEntry); "#clear" forgets them
    // and the general set playing. Only these are kept by parse().
    void addPrecacheEntry(std::string_view name);
    // Reads sound/sound.txt's text (AS_ParseSets): the sets asked for.
    // Problems (an unknown keyword, a set asked for but missing) go to
    // `warnings`. False if the file isn't an ambient set file.
    bool parse(std::string_view text, std::vector<std::string>& warnings);
    bool parsed() const { return parsed_; }
    void clear();

    const Set* find(std::string_view name) const;
    size_t size() const { return sets_.size(); }

    // The level's general set this frame, heard at `origin` (the view),
    // at `nowMs` (S_UpdateAmbientSet).
    std::vector<Sound> updateGeneral(std::string_view name, const Vec3f& origin, int64_t nowMs);
    // A local set on entity `entity` at `origin`, heard from `listener`;
    // `lastMs` is when it last played a sound, and the result's time is
    // the new one (S_AddLocalSet).
    std::vector<Sound> addLocal(std::string_view name, const Vec3f& listener, const Vec3f& origin, int entity,
                                int64_t& lastMs, int64_t nowMs);
    // A brush model set's sound for `stage`, or empty (AS_GetBModelSound).
    std::string bmodelSound(std::string_view name, int stage) const;

private:
    Set* findSet(std::string_view name);
    int pick(int low, int high);
    void fade(int64_t nowMs);
    void playGeneral(const Set& set, const Vec3f& origin, int64_t& lastMs, int64_t nowMs, std::vector<Sound>& out);

    std::vector<Set> sets_;
    std::map<std::string, size_t> byName_; // lower case
    std::set<std::string> precache_;       // lower case
    bool parsed_ = false;
    int current_ = -1, old_ = -1;
    int64_t currentLast_ = 0, oldLast_ = 0;
    std::mt19937 random_;
};

} // namespace reluva::idtech3::raven
