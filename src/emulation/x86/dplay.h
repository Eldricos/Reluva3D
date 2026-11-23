// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <memory>

namespace reluva::x86 {

/**
 * DirectPlay 3 (the DirectPlay COM class, through CoCreateInstance), with
 * no service providers installed: a program finds no connection to play
 * over, as on a machine without a network. Written from the DirectX 5
 * documentation and SDK headers.
 */
class DirectPlay {
public:
    /**
     * Stands in for the class in `process`, wrapping its createComObject:
     * other classes still go to the one set before.
     */
    explicit DirectPlay(Win32Process &process);
    ~DirectPlay();

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
