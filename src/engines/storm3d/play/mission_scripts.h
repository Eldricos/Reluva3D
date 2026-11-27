// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A campaign mission's scripts: the two DLLs the game loads for it (its
 * `missions/<name>S.dsl`, the script's variables and steps, and
 * `<name>R.drl`, its rules), run in the x86 emulator, with what they
 * import from Armada.exe stood in for: the rule engine here, the game's
 * functions by the `game` callback.
 */
class MissionScripts {
public:
    /** An Armada.exe import, read from its decorated name. */
    struct Signature {
        std::string name;      // "GetHandle", "hot_int::set"
        bool data = false;     // a variable, not a function
        bool member = false;   // called with `this` in ECX, popping its arguments
        uint32_t argBytes = 0; // its arguments on the stack
        bool returnsFloat = false;
        std::string kinds; // each argument's: 'i' a word, 'f' a float, 'b' a bool, 's' a string
    };
    /** What Visual C++'s decoration says of a name; empty if it can't be read. */
    static std::optional<Signature> signatureOf(const std::string &decorated);

    /** A game function's arguments, as the script passed them. */
    struct Arguments {
        x86::Cpu &cpu;
        uint32_t self = 0; // ECX, for a member
        uint32_t word(int n) const { return cpu.arg(n); }
        int32_t integer(int n) const { return int32_t(word(n)); }
        float real(int n) const;
        bool flag(int n) const { return (word(n) & 0xff) != 0; }
        std::string text(int n) const;
    };
    /**
     * A game function called: its name, as signatureOf() gives it, and its
     * arguments; it returns the result (a float's bits for one returning a
     * float), or nothing if it isn't known.
     */
    std::function<std::optional<uint32_t>(const std::string &name, const Arguments &args)> game;
    /** Each call the scripts make of the game, for a log. */
    std::function<void(const std::string &line)> trace;

    MissionScripts();
    ~MissionScripts();

    /**
     * Loads the two DLLs (the script's first: the rules import from it),
     * and runs their start-up, which builds the script's variables and
     * makes the rules.
     */
    Error load(const std::string &mission, const std::vector<uint8_t> &script, const std::vector<uint8_t> &rules);

    /** The names the scripts import that nothing stands in for yet. */
    const std::vector<std::string> &unknown() const { return _unknown; }

    /**
     * A pass of the rule engine, `seconds` of game time on, as the program
     * runs one each frame (0x4032b0): the timers count down; the rules
     * depending on a variable changed since are tested; every rule whose
     * test holds fires, newest first, again each pass until it fails; each
     * script runs its next step. Fails if the scripts' code faults.
     */
    Error tick(float seconds);
    /**
     * The program's cineractive flags, which rules wait on: whether the
     * shot's moves are done, and whether it was skipped. The program sets
     * them after each pass, for the next.
     */
    void setCineractive(bool done, bool cancel);
    /** A variable the script exports, by its plain name: a hot_int's or timer's value, or an int's. */
    std::optional<int32_t> variable(const std::string &name) const;
    /** The game seconds the passes have counted, which GetTime gives if the game doesn't. */
    float clock() const { return _clock; }
    /** The rules made, and the scripts running. */
    size_t ruleCount() const { return _rules.size(); }
    size_t scriptCount() const { return _scripts.size(); }

private:
    // A variable rules depend on: a hot_int, a timer_int (true for one pass
    // as it runs out), or ALWAYS_EVALUATE, a dirty_int never clean.
    enum class HotKind : uint8_t { kInt, kTimer, kAlways };
    struct Hot {
        HotKind kind = HotKind::kInt;
        int32_t value = -1;
        float remaining = 0; // a timer's seconds
        bool dirty = false;
        std::vector<uint32_t> rules; // those depending on it, newest first
    };
    struct Rule {
        uint32_t condition = 0;
        uint32_t action = 0;
        bool withCharacter = false; // both called with the world's character
        bool queued = false;        // on the list to test this pass
        int32_t last = 0;           // its test's last result
        bool satisfied = false;
        std::list<uint32_t>::iterator cell; // its latest place on the satisfied list
        std::string name;
    };
    struct Script {
        uint32_t address = 0;
        std::vector<uint32_t> steps; // in the order added
        size_t next = 0;
        std::string name;
    };

    void provideImports(const std::vector<uint8_t> &file);
    uint32_t callGame(const std::string &decorated, const Signature &sig, x86::Cpu &cpu);
    std::optional<uint32_t> engine(const std::string &decorated, const Signature &sig, const Arguments &args);
    void set(uint32_t hot, int32_t value);
    void mirror(uint32_t hot);
    Error call(uint32_t function, const std::vector<uint32_t> &args, uint32_t &result);

    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::map<std::string, uint32_t> _data; // the exe's variables they use, by decorated name
    std::vector<std::string> _unknown;
    std::vector<uint32_t> _tls; // thread-local storage's slots
    const x86::Win32Process::Module *_script = nullptr;
    std::map<uint32_t, Hot> _hots; // by address
    std::list<uint32_t> _dirty;    // changed since the last pass, newest first
    std::list<uint32_t> _timers;   // running
    std::map<uint32_t, Rule> _rules;
    std::list<uint32_t> _affected;                    // to test this pass, newest first
    std::list<uint32_t> _satisfied;                   // to fire, newest first
    std::vector<Script> _scripts;                     // oldest first
    std::map<uint32_t, std::vector<uint32_t>> _lists; // the save and load functions, by list
    uint32_t _world = 0;
    float _clock = 0;
};

} // namespace reluva::storm3d
