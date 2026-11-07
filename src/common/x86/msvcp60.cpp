// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/msvcp60.h"

#include "common/x86/runtime/msvcp60_dll.h"

#include <vector>

namespace reluva::x86 {

Error loadMsvcp60(Win32Process& process, const Win32Process::Module*& out) {
    const std::vector<uint8_t> file(kMsvcp60Dll, kMsvcp60Dll + sizeof kMsvcp60Dll);
    return process.load("MSVCP60.dll", file, out);
}

} // namespace reluva::x86
