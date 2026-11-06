// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::awe {

// The game's own binkw32.dll, run in the x86 interpreter, to check Reluva's
// Bink decoder against: a movie opened from memory and its frames copied
// out as the game copies them (BinkCopyToBufferRect, 32-bit, 0x406058).
// Only the exports the game itself calls are used, with no sound and no
// background reading.
class BinkDll {
public:
    Error load(const std::vector<uint8_t>& dll);
    bool loaded() const { return module_ != nullptr; }

    // Opens a movie (the file's bytes); its size and frame count.
    Error open(const std::vector<uint8_t>& bik, int& width, int& height, uint32_t& frames);
    // Decodes the next frame and copies it out as BGRX pixels (the game's
    // surface type 3, or 5 with alpha), rows top to bottom; then moves on.
    Error frame(std::vector<uint8_t>& bgra, bool alpha = false);
    // The same as YUY2 (surface type 13): Y U Y V for each pair of pixels,
    // to tell the decoding from the colour conversion.
    Error frameYuy2(std::vector<uint8_t>& yuy2);
    void close();

private:
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    const x86::Win32Process::Module* module_ = nullptr;
    std::vector<uint8_t> file_; // the movie being read
    uint32_t filePos_ = 0;
    uint32_t bink_ = 0, buffer_ = 0;
    int width_ = 0, height_ = 0;
};

} // namespace reluva::awe
