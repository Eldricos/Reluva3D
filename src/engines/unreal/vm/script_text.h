// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Compiled UnrealScript written out as text, close to the source it was
// compiled from: a class's declarations, then each function's and state's
// statements with their code offsets (jumps are shown as offsets, not
// rebuilt into if/while). For reading what the game's scripts do.

#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/vm/script.h"

#include <map>
#include <string>

namespace reluva::unreal {

/** A native function as its declaration in the packages has it. */
struct NativeFunction {
    std::string name;   // the function's own name ("Not_PreBool")
    std::string symbol; // what scripts write: its friendly name ("!")
    uint32_t flags = 0; // funcflag::
    uint8_t precedence = 0;
    int params = 0;
};

/**
 * The native functions the packages declare, by number (each declared
 * once), for showing native calls by name and operators as operators.
 */
class NativeTable {
public:
    /** Reads every native function of every package in the set. */
    explicit NativeTable(const PackageSet &packages);
    /** The function with that native number, or nullptr. */
    const NativeFunction *find(int native) const;
    /** How many there are. */
    size_t size() const { return _natives.size(); }

private:
    std::map<int, NativeFunction> _natives;
};

/**
 * A class (its declarations, functions, states and defaults), a struct, a
 * function or a state, as text.
 */
std::string scriptText(const Package &package, int exportIndex, const NativeTable &natives);

} // namespace reluva::unreal
