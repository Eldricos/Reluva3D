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
 * trace. Animations last as long as the model's own of their number, or
 * kAnimationMs; guests can be sent to the ride's queue, and kept in its limbo
 * (LIMBO, INLIMBO, UNLIMBO, FORCEUNLIMBO), as the shipped Shop.rss uses
 * it; a ride's checks of itself (its track valid, a car in its station)
 * answer yes. Everything else answers 0.
 */
class SimulatedHost : public ScriptHost {
public:
    /** How long an animation lasts here, in milliseconds, without its own length: a stand-in. */
    static constexpr uint32_t kAnimationMs = 1000;

    /**
     * Reads the lengths of the animations of the model named as `script`
     * (`levels/arabian/rides/wateride/Wateride.RSE`: `wateride.MD2`), by
     * the numbers scripts play them by, as far as they are known: 0 its
     * `c` animation, 2 `i`, 5 `m` (or `m1`), 9 `b`, 10 `r`, 11 `o`
     * (inferred; docs/engines/tpw/games/simcoaster/formats/md2-animation.md).
     */
    void readAnimationLengths(const std::string &script);
    /** The file readAnimationLengths found for an animation's number, as `levels/.../waterideb.MD2`; empty for none. */
    std::string animationFile(int number) const;

    /** The animation the script last started on its model, and since when. */
    struct Playing {
        int number = -1;       // -1 for none, or after FLUSHANIM
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
    /** Each guest in limbo and when its time there ends, in the order they went in. */
    std::vector<std::pair<int, uint32_t>> _limbo;
    std::vector<std::string> _trace;
    std::map<int, uint32_t> _animationMs; // by the number scripts play them by
    std::map<int, std::string> _animationFiles;
    Playing _playing;
    size_t _traceLimit = 0;
};

/**
 * Runs `machine` on `host` for `ms` milliseconds, a turn every `turnMs`,
 * guests queued before each turn if the host sends them.
 */
void runFor(ScriptMachine &machine, SimulatedHost &host, uint32_t ms, uint32_t turnMs = 20);

} // namespace reluva::tpw
