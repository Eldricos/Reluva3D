// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// glide2x.dll stood in for in Reluva's x86 interpreter
// (docs/engines/glide.md, The x86 binding): each of Glide 2's exports,
// under its stdcall name ("_grDrawTriangle@12"), a native function that
// reads its arguments and the structures they point at (GrVertex,
// GrTexInfo, GrHwConfiguration, GrLfbInfo_t...) from guest memory and
// calls the Glide 2 front end. Functions not carried out are there too:
// they log once and return 0, so a program fails softly and the gaps show.
//
// A host serves exports() when the program loads glide2x.dll by name
// (LoadLibraryA and GetProcAddress), or provideTo() a Win32Process so
// that programs importing it directly are bound to it.

#include "common/glide/glide2.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

class Glide2Dll {
public:
    // `memory` is where guest-visible buffers (linear frame buffer locks)
    // are allocated.
    Glide2Dll(x86::Cpu& cpu, x86::Arena& memory, Board board = {});

    Glide2& glide() { return glide_; }
    // Each export's decorated name -> its native function's address.
    const std::map<std::string, uint32_t>& exports() const { return exports_; }
    // The same, as imports of "glide2x.dll".
    void provideTo(x86::Win32Process& process) const;
    // How many times each export was called.
    const std::map<std::string, uint64_t>& calls() const { return calls_; }

    // The names Glide 2 exports that this stands in for only as stubs.
    std::vector<std::string> stubs() const;

private:
    using Fn = std::function<uint32_t(x86::Cpu&)>;
    // An export: `name` undecorated ("grDrawTriangle"), `argBytes` its
    // arguments' size.
    void add(const std::string& name, int argBytes, Fn fn);
    void stub(const std::string& name, int argBytes, uint32_t result = 0);
    void addAll();

    Glide2Vertex vertex(uint32_t address) const;
    Glide2TexInfo texInfo(uint32_t address) const;
    const uint8_t* guest(uint32_t address, uint32_t size) const; // null if not all there
    uint32_t lfbLock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pipeline,
                     uint32_t info);
    uint32_t lfbUnlock(uint32_t type, uint32_t buffer);

    x86::Cpu& cpu_;
    x86::Arena& memory_;
    Glide2 glide_;
    std::map<std::string, uint32_t> exports_;
    std::map<std::string, x86::Cpu::Native> natives_;
    std::map<std::string, uint64_t> calls_;
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
