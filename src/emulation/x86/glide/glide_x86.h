// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What the stand-ins for glide2x.dll and glide3x.dll in Reluva's x86
// interpreter share (docs/common/graphics/glide.md, The x86 bindings): the table of
// exports, each a native function named as stdcall decorates it
// ("_grDrawTriangle@12") that pops its arguments; stubs that log once; and
// the guest copies of the frame buffer that linear frame buffer locks hand
// out. Glide2Dll (glide2_x86.h) and Glide3Dll (glide3_x86.h) fill the table.

#include "graphics/glide/glide2.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

/**
 * The part of a Glide library's stand-in that Glide 2's and Glide 3's
 * share: its exports, their call counts, and linear frame buffer locks.
 */
class GlideDll {
public:
    virtual ~GlideDll() = default;
    GlideDll(const GlideDll &) = delete;
    GlideDll &operator=(const GlideDll &) = delete;

    /** The library's file name ("glide2x.dll"). */
    const std::string &dllName() const { return _dllName; }
    /** Each export's decorated name -> its native function's address. */
    const std::map<std::string, uint32_t> &exports() const { return _exports; }
    /** Provides the exports to `process`, as imports of dllName(). */
    void provideTo(x86::Win32Process &process) const;
    /** How many times each export was called. */
    const std::map<std::string, uint64_t> &calls() const { return _calls; }
    /**
     * For the mode-setting functions (grFogMode, grTexFilterMode,
     * grDitherMode, grDepthBufferMode, grCullMode, grEnable...): how many
     * times each mode was asked for (the TMU functions' second argument).
     */
    const std::map<std::string, std::map<uint32_t, uint64_t>> &modes() const { return _modes; }
    /** The names this stands in for only as stubs. */
    std::vector<std::string> stubs() const { return _stubs; }
    /** The Glide 2 front end the calls end in (Glide 3's core()). */
    Glide2 &core() { return *_core; }
    // How gu3dfGetInfo and gu3dfLoad read a file the program names (its
    // own path): the whole file into `bytes`, false if there's none. Unset,
    // they fail as for a missing file.
    std::function<bool(const std::string &path, std::vector<uint8_t> &bytes)> readFile;

protected:
    /**
     * `memory` is where guest-visible buffers (linear frame buffer locks)
     * are allocated. The derived class gives its Glide 2 front end with
     * setCore() before any call.
     */
    GlideDll(x86::Cpu &cpu, x86::Arena &memory, std::string dllName);
    /** Sets the front end the exports call; it has to outlive this. */
    void setCore(Glide2 &core) { _core = &core; }

    /** An export's native function: its result goes in EAX. */
    using Fn = std::function<uint32_t(x86::Cpu &)>;
    /**
     * An export, counted in calls() (and in modes() for the mode-setting
     * functions), which pops its arguments as stdcall does.
     * @param name Undecorated ("grDrawTriangle").
     * @param argBytes The arguments' size, which the decorated name ends in.
     */
    void add(const std::string &name, int argBytes, Fn fn);
    /**
     * An export not carried out: it logs once through the front end's
     * notImplemented() and returns `result`, and is listed in stubs().
     */
    void stub(const std::string &name, int argBytes, uint32_t result = 0);
    /**
     * A procedure that isn't exported, only handed out (grGetProcAddress's
     * extension procedures): its native function's address.
     */
    uint32_t addProc(const std::string &name, int argBytes, Fn fn);
    /** The guest's bytes at `address`, or null if it is 0 or not all `size` bytes are there. */
    const uint8_t *guest(uint32_t address, uint32_t size) const;
    /** Argument `i` (from 0) of the call, read as a float. */
    static float argFloat(const x86::Cpu &c, int i);
    /** Adds guFogTableIndexToW and the guFogGenerate functions. */
    void addFogUtilities();
    /**
     * Adds gu3dfGetInfo and gu3dfLoad, which read files through readFile;
     * Gu3dfHeader's LODs and aspect in Glide
     * 3's log2 form if `glide3`, else Glide 2's constants.
     */
    void add3dfLoaders(bool glide3);

    /**
     * grLfbLock and grLfbUnlock, with the guest copies below. `info` is
     * the guest's GrLfbInfo_t (the same in Glide 2 and 3).
     * @return 1 if done, 0 if the lock failed.
     */
    ///@{
    uint32_t lfbLock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pipeline, uint32_t info);
    uint32_t lfbUnlock(uint32_t type, uint32_t buffer);
    ///@}

    x86::Cpu &_cpu;
    x86::Arena &_memory;

private:
    Glide2 *_core = nullptr;
    std::string _dllName;
    std::map<std::string, uint32_t> _exports;
    std::map<std::string, x86::Cpu::Native> _natives;
    std::map<std::string, uint64_t> _calls;
    std::map<std::string, std::map<uint32_t, uint64_t>> _modes;
    std::vector<std::string> _stubs;

    // A linear frame buffer lock's guest copy: filled from the buffer when
    // first locked, and on the last unlock the pixels the program changed
    // written back (through the pixel pipeline if the lock said so).
    struct Mirror {
        Glide2LfbLock lock;
        uint32_t address = 0;
        uint32_t size = 0;
        int locks = 0;
        bool written = false;
        std::vector<uint8_t> snapshot;
    };
    std::map<uint32_t, Mirror> _mirrors; // by (buffer, write mode, origin)
    void fillMirror(Mirror &m);
    void flushMirror(Mirror &m);
};

} // namespace reluva::glide
