// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <set>
#include <vector>

#include "engines/stgenerations/formats/script.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

class ScriptVM;

// Events the interpreter itself sends.
enum ScriptEvent {
    kEventEnterState = 2,
    kEventLeaveState = 3
};

// A running script object: its definition plus mutable state.
struct ScriptInstance {
    const ScriptObject* object = nullptr;
    int16_t state = -1;
    // Cleared by DISABLE_OBJECT; a disabled object ignores events.
    bool enabled = true;
    // Set by GOTO_STATE and CASE; applied when the current block ends.
    int16_t pendingState = -1;
    std::vector<int16_t> locals;
    // Handlers missing here are looked up in the parent (this+0x48 in the original).
    ScriptInstance* parent = nullptr;
    // (event << 8 | state) -> word index of the block's code.
    std::map<uint16_t, const ScriptBlock *> handlers;
};

// The game side of the interpreter: everything the scripts do to the world
// goes through here. Opcodes at or above OP_FIRST_NATIVE, plus
// SEND_SIGNAL and GAME_INIT, are forwarded to callNative().
class ScriptHost {
public:
    virtual ~ScriptHost() {}

    // Execute a game opcode. Pop arguments with vm.pop() and push results
    // with vm.push(), following the opcode's stack effect.
    virtual void callNative(ScriptVM& vm, ScriptInstance& self, uint16_t op) = 0;

    // Called when no script handles an event; the original falls back to built-in behaviour.
    virtual void unhandledEvent(ScriptVM& /*vm*/, ScriptInstance& /*self*/, uint16_t /*event*/,
                                const std::vector<int16_t>& /*args*/) {}
};

// Interpreter for the SGEN bytecode, reimplementing the one at 0x44F9D4
// in sgens.exe: a 16-bit stack machine with per-object locals, shared
// globals, structured if/else and event driven state machines.
class ScriptVM {
public:
    static const int kGlobalCount = 32768;
    static const int kMaxStack = 256;

    // host may be null, in which case game opcodes only balance the stack.
    ScriptVM(const ScriptFile& script, ScriptHost* host);

    // Create an instance for every object, all in state -1, globals cleared.
    void reset();

    int getInstanceCount() const { return (int)_instances.size(); }
    ScriptInstance& getInstance(int index) { return _instances[index]; }
    // Index of the instance with the given object id, or -1.
    int findInstance(uint16_t id) const;

    // Deliver an event to an object, running the matching block. Returns the
    // block's result (SET_RESULT), or 0 if nothing handled the event.
    int16_t sendEvent(ScriptInstance& self, uint16_t event, const std::vector<int16_t>& args = {});

    // Change state, sending leave and enter events as the original does.
    void setState(ScriptInstance& self, int16_t state);

    int16_t getGlobal(int index) const;
    void setGlobal(int index, int16_t value);

    // Stack access for ScriptHost::callNative().
    void push(int16_t value);
    int16_t pop();

    // Save or load the objects' states, locals and parents, and the globals.
    void sync(Serializer& s);

    // Opcodes that reached the stub host, for tracking what's left to implement.
    const std::set<uint16_t>& getStubbedOpcodes() const { return _stubbed; }
    uint64_t getInstructionCount() const { return _instructionCount; }

private:
    int16_t execute(ScriptInstance& self, uint32_t ip, const std::vector<int16_t>& args);
    // Skip to the matching else/endif, as the original's method 0x22C does.
    uint32_t skipConditional(uint32_t ip) const;
    void callNative(ScriptInstance& self, uint16_t op);
    uint16_t word(uint32_t ip) const;

    const ScriptFile& _script;
    ScriptHost* _host;
    std::vector<ScriptInstance> _instances;
    std::vector<int16_t> _globals;
    std::vector<int16_t> _stack;
    std::set<uint16_t> _stubbed;
    uint64_t _instructionCount = 0;
    int _depth = 0;
};

} // namespace reluva::stgenerations

