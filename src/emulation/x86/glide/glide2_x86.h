// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// glide2x.dll stood in for in Reluva's x86 interpreter
// (docs/common/graphics/glide.md, The x86 binding): each of Glide 2's exports,
// under its stdcall name ("_grDrawTriangle@12"), a native function that
// reads its arguments and the structures they point at (GrVertex,
// GrTexInfo, GrHwConfiguration, GrLfbInfo_t...) from guest memory and
// calls the Glide 2 front end. Functions not carried out are there too:
// they log once and return 0, so a program fails softly and the gaps show.
//
// A host serves exports() when the program loads glide2x.dll by name
// (LoadLibraryA and GetProcAddress), or provideTo() a Win32Process so
// that programs importing it directly are bound to it.

#include "graphics/glide/glide2.h"
#include "emulation/x86/glide/glide_x86.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

class Glide2Dll : public GlideDll {
public:
    // `memory` is where guest-visible buffers (linear frame buffer locks)
    // are allocated.
    Glide2Dll(x86::Cpu &cpu, x86::Arena &memory, Board board = {});

    Glide2 &glide() { return _glide; }

private:
    void addAll();
    Glide2Vertex vertex(uint32_t address) const;
    Glide2TexInfo texInfo(uint32_t address) const;
    // guTexGetMipMapInfo's GrMipMapInfo in guest memory, filled in now.
    uint32_t mipMapInfo(const Glide2 &g, uint32_t id);

    Glide2 _glide;
    std::map<uint32_t, uint32_t> _mipMapInfos; // by handle
};

} // namespace reluva::glide
