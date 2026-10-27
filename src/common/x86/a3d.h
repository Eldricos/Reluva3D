// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/win32.h"

#include <memory>

namespace reluva::audio {
class Mixer;
}

namespace reluva::x86 {

// Aureal's A3D 3 (the A3dApi COM class, through CoCreateInstance) for the
// programs of 1998 to 2000 that use it, written from Aureal's public SDK
// documentation and checked against how the programs call it: the
// interfaces' methods and their arguments are those the programs push.
//
// A source's wave data lives in the process's memory, where the program
// writes it (Lock, Unlock). Its play position follows the process's clock,
// so its position, time and status move as they would, a source played once
// stops at its end, and its wave events are passed as it goes. What it
// plays is handed, as it's passed, to the mixer as a stream; positions in
// 3D become a gain and a pan.
class A3d {
public:
    // Stands in for the class in `process` (its createComObject).
    explicit A3d(Win32Process& process);
    ~A3d();

    // Where sound goes; none, and sources play in silence (their positions
    // still move).
    audio::Mixer* mixer = nullptr;
    // Sources made, and those playing.
    size_t sourcesMade() const;
    size_t sourcesPlaying() const;

    struct State;

private:
    std::unique_ptr<State> s_;
};

} // namespace reluva::x86
