// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/tpw/formats/rse.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::tpw {

class ScriptMachine;

/** What the game makes of an instruction the machine leaves to it. */
struct HostResult {
    int value = 0;       // sets the flags, for the instructions whose result branches test
    uint32_t waitMs = 0; // the script waits this long before going on: an animation's length
};

/**
 * The game's side of the script machine: the time, chance, the scripts a
 * script starts, and every instruction that acts on the park (animations,
 * guests, objects, events, the ride's own commands).
 */
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    /** The time, in milliseconds, from any start that doesn't change while scripts run. */
    virtual uint32_t now() const = 0;
    /** A number from 0 to `below` - 1, or 0 for a `below` under 1. */
    virtual int random(int below) = 0;
    /** Reads the script a script starts by the name it gives (`EventMap.rse`). */
    virtual Error loadScript(const ScriptMachine &parent, const std::string &name, Script &out) = 0;
    /**
     * An instruction for the game; `values` are its operands' values
     * (variables read, strings as their offsets). It may set variables
     * through `machine`.
     */
    virtual HostResult perform(ScriptMachine &machine, const Script::Instruction &instruction,
                               const std::vector<int> &values) = 0;
};

/**
 * One running script: its variables, flags, stack and place in its code,
 * and the child script it may have started. See
 * docs/engines/tpw/games/simcoaster/systems/scripts.md.
 */
class ScriptMachine {
public:
    /** Where the script is. */
    enum class State : uint8_t {
        kRunning, // has more to do
        kWaiting, // until its wake time
        kEnded,   // ran END, or off the end of its code
        kFailed,  // can't go on: error() says why
    };

    /** A machine for `script`, which must outlive it, its variables all 0. */
    ScriptMachine(const Script &script, ScriptHost &host, ScriptMachine *parent = nullptr);
    ~ScriptMachine();

    /**
     * Runs the script for one turn of the game: on from where it is, until
     * it gives way (ENDSLICE, a wait) or has run its time slice of
     * instructions; then its child's turn.
     */
    void tick();

    State state() const { return _state; }
    const std::string &error() const { return _error; }
    const Script &script() const { return _script; }
    /** The name the script gave itself (NAME); empty before. */
    const std::string &name() const { return _name; }
    /** Where it is in its code, in words. */
    size_t address() const;
    /** When a waiting script wakes, in the host's milliseconds. */
    uint32_t wakeTime() const { return _wakeTime; }

    /** A variable's value; 0 for one the script hasn't. */
    int variable(size_t index) const { return index < _variables.size() ? _variables[index] : 0; }
    /** Sets a variable, as the game does (a guest wanting on in VAR_LETMEON); ignored for one it hasn't. */
    void setVariable(size_t index, int value);
    const std::vector<int> &variables() const { return _variables; }
    /** The flags the last arithmetic, TEST or CMP, or the game's answer, set. */
    ///@{
    bool zero() const { return _zero; }
    bool negative() const { return _negative; }
    ///@}
    /**
     * Whether it is between CRIT_LOCK and CRIT_UNLOCK: the game leaves its
     * variables alone then (inferred).
     */
    bool locked() const { return _locked; }
    /** The script it started (SPAWNCHILD), or nullptr. */
    ScriptMachine *child() const { return _child.get(); }
    ScriptMachine *parent() const { return _parent; }

private:
    void step();
    int value(const Script::Operand &o) const;
    void store(const Script::Operand &o, int v);
    void setFlags(int v);
    void fail(const std::string &why);
    void jump(int address);

    const Script &_script;
    ScriptHost &_host;
    ScriptMachine *_parent;
    std::vector<size_t> _byAddress; // the instruction at each word, or none
    size_t _next = 0;               // the next instruction to run
    State _state = State::kRunning;
    std::string _error;
    std::string _name;
    std::vector<int> _variables;
    std::vector<int> _stack;
    std::vector<size_t> _returns;
    bool _zero = false;
    bool _negative = false;
    bool _locked = false;
    bool _yield = false;
    uint32_t _start = 0;
    uint32_t _wakeTime = 0;
    std::unique_ptr<Script> _childScript;
    std::unique_ptr<ScriptMachine> _child;
};

} // namespace reluva::tpw
