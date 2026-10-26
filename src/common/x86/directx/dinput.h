// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/win32.h"

#include <memory>

namespace reluva::x86 {

// DirectInput (DINPUT.dll's DirectInputCreateA and DirectInputCreateEx, up
// to DirectX 7's interfaces) for the programs that use it, written from
// Microsoft's documentation: the system mouse and keyboard, read with the
// data formats programs give (c_dfDIMouse, c_dfDIKeyboard or their own),
// as an immediate state or a buffer of changes. They hear the input the
// engine passes to the process (Win32Process::rawInput).
class DirectInput {
public:
    explicit DirectInput(Win32Process& process);
    ~DirectInput();

    struct State;

private:
    std::unique_ptr<State> s_;
};

} // namespace reluva::x86
