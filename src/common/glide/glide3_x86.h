// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// glide3x.dll stood in for in Reluva's x86 interpreter
// (docs/engines/glide.md, The x86 bindings): each of Glide 3's exports
// (the functions of the Glide 3.x SDK's glide.h and glideutl.h), under its
// stdcall name ("_grDrawTriangle@12"), a native function that reads its
// arguments and what they point at from guest memory (vertices by the
// grVertexLayout given, GrTexInfo, GrLfbInfo_t, GrResolution...) and calls
// the Glide 3 front end. Functions not carried out log once and return 0.

#include "common/glide/glide3.h"
#include "common/glide/glide_x86.h"

#include <cstdint>

namespace reluva::glide {

class Glide3Dll : public GlideDll {
public:
    // `memory` is where guest-visible buffers (linear frame buffer locks,
    // grGetString's strings) are allocated.
    Glide3Dll(x86::Cpu& cpu, x86::Arena& memory, Board board = voodoo3Board());

    Glide3& glide() { return glide_; }

private:
    void addAll();
    // A vertex's bytes as the layout reads them (null if not all there).
    const uint8_t* vertex(uint32_t address) const;
    Glide3TexInfo texInfo(uint32_t address) const;
    uint32_t guestString(const std::string& s);

    Glide3 glide_;
    std::map<std::string, uint32_t> strings_; // grGetString's, in guest memory
    std::vector<Glide3::Layout> layouts_;     // grGlideGetVertexLayout's, by number
};

} // namespace reluva::glide
