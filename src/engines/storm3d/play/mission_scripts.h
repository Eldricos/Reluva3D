// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
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

private:
    void provideImports(const std::vector<uint8_t> &file);
    uint32_t callGame(const std::string &decorated, const Signature &sig, x86::Cpu &cpu) const;

    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::map<std::string, uint32_t> _data; // the exe's variables they use, by decorated name
    std::vector<std::string> _unknown;
    std::vector<uint32_t> _tls; // thread-local storage's slots
};

} // namespace reluva::storm3d
