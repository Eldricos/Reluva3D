// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::idtech3::raven {

/**
 * Raven's recorded paths (`.rof`, "ROFF"): an object's move and turn for
 * each frame of an animation made in a modelling program, which the game
 * plays on a mover. See docs/engines/idtech3/formats/roff.md.
 */
struct RoffFile {
    struct Frame {
        std::array<float, 3> move{};    // units this frame
        std::array<float, 3> turn{};    // degrees this frame (pitch, yaw, roll), within -180..180
        std::vector<std::string> notes; // given to the game's code as the frame starts
    };
    std::vector<Frame> frames;
    int32_t frameMs = 100;
};

// False if the data isn't a ROFF of version 1 or 2 with at least one frame.
bool parseRoff(const std::vector<uint8_t> &data, RoffFile &out);

/**
 * The engine's ROFF system as Jedi Outcast's multiplayer modules use it:
 * files cached by name and numbered, entities set playing one, and each
 * frame those whose next step is due given their trajectories for it. The
 * game module and the client game share one in the original (the number
 * the server gives an event is the client's too), each playing its own
 * entities.
 */
class RoffSystem {
public:
    using Vec3 = std::array<float, 3>;
    explicit RoffSystem(const SearchSet &files) : _files(files) {}

    // What a side gives the system of its entities: where one is and which
    // way it faces now; its position's and angles' trajectories set (type 0
    // still, 2 linear, at `time`, moving by the deltas each second); that
    // its path has ended; a frame's note.
    struct Side {
        std::function<bool(int32_t entity, Vec3 &origin, Vec3 &angles)> current;
        std::function<void(int32_t entity, int32_t type, int32_t time, const Vec3 &origin, const Vec3 &originDelta,
                           const Vec3 &angles, const Vec3 &anglesDelta)>
            setTrajectories;
        std::function<void(int32_t entity)> finished;
        std::function<void(int32_t entity, const std::string &note)> note;
    };

    // Its number (the same for the same name), 0 if it can't be read.
    int32_t cache(const std::string &file, bool client);
    // Entity `entity` plays ROFF `id` from `time`, its moves turned to face
    // as it does now (`translate`) or along the world's axes.
    bool play(int32_t entity, int32_t id, bool translate, bool client, int32_t time, const Vec3 &startAngles);
    // Each of that side's playing entities whose next frame is due takes it.
    void update(bool client, int32_t time, const Side &side);
    // An entity stopped where it is.
    bool purge(int32_t entity, bool client, int32_t time, const Side &side);
    // The level's over: that side's entities dropped, and files neither
    // side uses any more forgotten.
    void clean(bool client);

    size_t cached() const { return _roffs.size(); }
    size_t playing() const { return _playing.size(); }

private:
    struct Cached {
        std::string name;
        RoffFile file;
        bool client = false;
        bool server = false;
    };
    struct Playing {
        int32_t entity = 0;
        int32_t id = 0;
        int32_t nextTime = 0;
        size_t frame = 0;
        bool translate = false;
        bool client = false;
        Vec3 startAngles{};
    };
    // A step: false once the path is over (the entity left still).
    bool step(Playing &p, const RoffFile &roff, int32_t time, const Side &side);

    const SearchSet &_files;
    std::map<int32_t, Cached> _roffs;
    int32_t _nextId = 0;
    std::vector<Playing> _playing;
};

} // namespace reluva::idtech3::raven
