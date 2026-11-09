// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What the stand-ins for glide2x.dll and glide3x.dll in Reluva's x86
// interpreter share (docs/engines/glide.md, The x86 bindings): the table of
// exports, each a native function named as stdcall decorates it
// ("_grDrawTriangle@12") that pops its arguments; stubs that log once; and
// the guest copies of the frame buffer that linear frame buffer locks hand
// out. Glide2Dll (glide2_x86.h) and Glide3Dll (glide3_x86.h) fill the table.

#include "common/glide/glide2.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

class GlideDll {
public:
    virtual ~GlideDll() = default;
    GlideDll(const GlideDll&) = delete;
    GlideDll& operator=(const GlideDll&) = delete;

    // The library's file name ("glide2x.dll").
    const std::string& dllName() const { return dllName_; }
    // Each export's decorated name -> its native function's address.
    const std::map<std::string, uint32_t>& exports() const { return exports_; }
    // The same, as imports of dllName().
    void provideTo(x86::Win32Process& process) const;
    // How many times each export was called.
    const std::map<std::string, uint64_t>& calls() const { return calls_; }
    // For the mode-setting functions (grFogMode, grTexFilterMode,
    // grDitherMode, grDepthBufferMode, grCullMode, grEnable...): how many
    // times each mode was asked for (the TMU functions' second argument).
    const std::map<std::string, std::map<uint32_t, uint64_t>>& modes() const { return modes_; }
    // The names this stands in for only as stubs.
    std::vector<std::string> stubs() const { return stubs_; }
    // The Glide 2 front end the calls end in (Glide 3's core()).
    Glide2& core() { return *core_; }

protected:
    // `memory` is where guest-visible buffers (linear frame buffer locks)
    // are allocated. The derived class gives its Glide 2 front end with
    // setCore() before any call.
    GlideDll(x86::Cpu& cpu, x86::Arena& memory, std::string dllName);
    void setCore(Glide2& core) { core_ = &core; }

    using Fn = std::function<uint32_t(x86::Cpu&)>;
    // An export: `name` undecorated ("grDrawTriangle"), `argBytes` its
    // arguments' size.
    void add(const std::string& name, int argBytes, Fn fn);
    void stub(const std::string& name, int argBytes, uint32_t result = 0);
    const uint8_t* guest(uint32_t address, uint32_t size) const; // null if not all there
    static float argFloat(const x86::Cpu& c, int i);
    // guFogTableIndexToW and the guFogGenerate functions.
    void addFogUtilities();

    // grLfbLock and grLfbUnlock, with the guest copies below. `info` is
    // the guest's GrLfbInfo_t (the same in Glide 2 and 3).
    uint32_t lfbLock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pipeline,
                     uint32_t info);
    uint32_t lfbUnlock(uint32_t type, uint32_t buffer);

    x86::Cpu& cpu_;
    x86::Arena& memory_;

private:
    Glide2* core_ = nullptr;
    std::string dllName_;
    std::map<std::string, uint32_t> exports_;
    std::map<std::string, x86::Cpu::Native> natives_;
    std::map<std::string, uint64_t> calls_;
    std::map<std::string, std::map<uint32_t, uint64_t>> modes_;
    std::vector<std::string> stubs_;

    // A linear frame buffer lock's guest copy: filled from the buffer when
    // first locked, and on the last unlock the pixels the program changed
    // written back (through the pixel pipeline if the lock said so).
    struct Mirror {
        Glide2LfbLock lock;
        uint32_t address = 0, size = 0;
        int locks = 0;
        bool written = false;
        std::vector<uint8_t> snapshot;
    };
    std::map<uint32_t, Mirror> mirrors_; // by (buffer, write mode, origin)
    void fillMirror(Mirror& m);
    void flushMirror(Mirror& m);
};

} // namespace reluva::glide
