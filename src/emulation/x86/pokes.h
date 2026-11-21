// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/cpu.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reluva::x86 {

/**
 * Memory written, or saved, at a chosen moment of a run: once the clock
 * reaches a millisecond, or as an instruction is reached (before it runs).
 * For starting a program in a state its menus would take long to reach.
 */
struct Poke {
    // When: at `ms` (atAddress 0), or as the instruction at `atAddress` is
    // reached the `nth` time (1: the first).
    uint64_t ms = 0;
    uint32_t atAddress = 0;
    uint32_t nth = 1;
    // Each time from then on (an instruction each time it's reached, a time
    // each time the run checks it), rather than once.
    bool every = false;
    uint32_t address = 0;
    std::vector<uint8_t> bytes; // written there (little-endian for a number)
    // A dump instead: `dumpSize` bytes from `address` written to this file.
    std::string dumpFile;
    uint32_t dumpSize = 0;

    // State.
    uint32_t reached = 0; // times the instruction was reached
    uint32_t applied = 0; // times written or saved
};

/**
 * Reads a poke written "WHEN:ADDR:VALUE[:SIZE]": WHEN is ms=N or
 * at=ADDR[#N]; VALUE a number (negative too) of SIZE bytes (1, 2 or 4; 4
 * if not given) or bytes=hex ("bytes=0a0b0c").
 * @param every Sets Poke::every.
 * @return False, with `error` set, if it isn't one.
 */
bool parsePoke(const std::string &text, bool every, Poke &poke, std::string &error);
/** Reads a dump written "WHEN:ADDR:SIZE:file", as parsePoke() does. */
bool parseDump(const std::string &text, Poke &poke, std::string &error);

/** The pokes of a run, applied as the run tells it where it is. */
class Pokes {
public:
    std::vector<Poke> list;
    // A line for each poke applied (only the first three times of an
    // `every`, but each time it misses memory).
    std::function<void(const std::string &line)> log;
    // Writes `bytes` to the file (for dumps).
    std::function<void(const std::string &file, const std::vector<uint8_t> &bytes)> save;

    /** Whether there are none. */
    bool empty() const { return list.empty(); }
    /** Whether any waits on an instruction (and so needs a trace). */
    bool needsTrace() const;
    /** Applies the pokes due before the instruction at cpu.eip runs. */
    void instruction(Cpu &cpu, uint64_t nowMs);
    /** Applies the timed pokes due by `nowMs`; called now and then (each thread slice). */
    void time(Cpu &cpu, uint64_t nowMs);

private:
    void apply(Poke &p, Cpu &cpu, uint64_t nowMs);
};

} // namespace reluva::x86
