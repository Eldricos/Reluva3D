// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "engines/tpw/script/machine.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace reluva::tpw {

/**
 * A stand-in for the park, for running a script on its own: a clock the
 * caller moves on, a fixed sequence of chance, children read from the
 * script's own folder, and every instruction for the game written to a
 * trace. Animations last as long as the model's own of their number and
 * variant, or kAnimationMs. Guests can be sent to the ride's queue; the
 * ride keeps them as the scripts' instructions say: in its limbo (LIMBO,
 * INLIMBO, UNLIMBO, FORCEUNLIMBO, LIMBOSPACE), on it (HUSH, HOP),
 * bouncing (BOUNCE, UNBOUNCE, FORCEUNBOUNCE, BOUNCING), walking off
 * (WALKOFF, then WALKGET once they're clear). Each script has a timer
 * (SETTIMER, GETTIMER) and animation channels (TRIGANIM_CH, GETANIM_CH).
 * A ride's checks of itself (its track valid, a car in its station)
 * answer yes. Everything else answers 0. See
 * docs/engines/tpw/games/simcoaster/systems/script-instructions.md.
 */
class SimulatedHost : public ScriptHost {
public:
    /** How long an animation lasts here, in milliseconds, without its own length: a stand-in. */
    static constexpr uint32_t kAnimationMs = 1000;
    /** How long a guest takes to walk off a ride here, in milliseconds: a stand-in. */
    static constexpr uint32_t kWalkOffMs = 1000;

    /**
     * Reads the lengths of the animations of the model named as `script`
     * (`levels/arabian/rides/wateride/Wateride.RSE`: `wateride.MD2`), by
     * the numbers and variants scripts play them by, as far as they are
     * known: 0 its `c` animation, 2 `i`, 3 `l`, 4 `s`, 5 `m`, 6 `e`, 7 `u`,
     * 9 `b`, 10 `r`, 11 `o`, variant v the one numbered v + 1 (`atmm15`
     * for 5 14), or the unnumbered one for 0 (inferred;
     * docs/engines/tpw/games/simcoaster/formats/md2-animation.md).
     */
    void readAnimationLengths(const std::string &script);
    /**
     * Reads the ride's capacity and duration from its settings, as the
     * game gives them to its script in VAR_CAPACITY and VAR_DURATION: the
     * script's own `.sam` beside it, or the general one of its folder's
     * kind (`levels/arabian/rides/Rides.sam`), `Upgrades[0].InitCapacity`
     * and `InitDuration` (inferred).
     */
    void readSettings(const std::string &script);

    /** The file readAnimationLengths found for an animation, as `levels/.../waterideb.MD2`; empty for none. */
    std::string animationFile(int number, int variant = 0) const;

    /** The animation the script last started on its model, and since when. */
    struct Playing {
        int number = -1; // -1 for none, or after FLUSHANIM
        int variant = 0;
        uint32_t startMs = 0;  // on the host's clock
        uint32_t lengthMs = 0; // as long as it takes, where known
        bool loop = false;     // LOOPANIM's go round and round
    };
    const Playing &playing() const { return _playing; }

    /** A host reading children from `folder`, as `levels/arabian/rides/wateride`. */
    SimulatedHost(const SearchSet &files, std::string folder) : _files(files), _folder(std::move(folder)) {}

    uint32_t now() const override { return _now; }
    int random(int below) override;
    Error loadScript(const ScriptMachine &parent, const std::string &name, Script &out) override;
    HostResult perform(ScriptMachine &machine, const Script::Instruction &instruction,
                       const std::vector<int> &values) override;

    /** Moves the clock on. */
    void advance(uint32_t ms) { _now += ms; }
    /**
     * Whether guests come: each time the script takes one (clears
     * VAR_LETMEON), the next is put there, numbered from 1; each one it
     * lets off (VAR_LETMEOFF) walks away.
     */
    void setGuests(bool on) { _guests = on; }
    /** Does the guests' part, between the script's turns, if guests come and it isn't locked. */
    void queueGuest(ScriptMachine &machine);

    /**
     * A line for each instruction for the game: its time, script, name and
     * operands' values (before it), and the stand-in's answer where not 0.
     */
    const std::vector<std::string> &trace() const { return _trace; }
    void clearTrace() { _trace.clear(); }
    /** Keeps only about the latest `lines` of the trace; 0, the default, keeps all. */
    void setTraceLimit(size_t lines) { _traceLimit = lines; }

private:
    const SearchSet &_files;
    std::string _folder;
    uint32_t _now = 0;
    uint32_t _seed = 1;
    bool _guests = false;
    int _nextGuest = 1;
    uint32_t animationMs(int number, int variant) const;
    uint32_t remainingMs() const; // of the animation playing, once

    /** Each guest in limbo and when its time there ends, in the order they went in. */
    std::vector<std::pair<int, uint32_t>> _limbo;
    std::vector<std::pair<int, uint32_t>> _bouncing;   // as the limbo
    std::vector<int> _onRide;                          // HUSHed, in order
    std::vector<std::pair<int, uint32_t>> _walkingOff; // and when each is clear
    std::map<const ScriptMachine *, uint32_t> _timerEnds;
    int _capacity = 0; // from readSettings; 0 for none
    int _duration = 0;
    std::vector<const ScriptMachine *> _given; // the machines given them
    std::map<std::pair<const ScriptMachine *, int>, uint32_t> _channelEnds;
    std::vector<std::string> _trace;
    // By the number and variant scripts play them by.
    std::map<std::pair<int, int>, uint32_t> _animationMs;
    std::map<std::pair<int, int>, std::string> _animationFiles;
    Playing _playing;
    size_t _traceLimit = 0;
};

/**
 * Runs `machine` on `host` for `ms` milliseconds, a turn every `turnMs`,
 * guests queued before each turn if the host sends them.
 */
void runFor(ScriptMachine &machine, SimulatedHost &host, uint32_t ms, uint32_t turnMs = 20);

} // namespace reluva::tpw
