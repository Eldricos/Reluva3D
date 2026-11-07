// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/x86/win32.h"

namespace reluva::x86 {

// Loads Reluva's own MSVCP60.dll (runtime/msvcp60.cpp, built into the
// program) into the process: the C++ library of Visual C++ 6, for programs
// that import it. Its imports are MSVCRT's, so addMsvcrt() comes first;
// load it before the modules that import from it, and attach it before
// theirs too.
Error loadMsvcp60(Win32Process& process, const Win32Process::Module*& out);

} // namespace reluva::x86
