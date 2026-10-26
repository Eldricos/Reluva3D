// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/cpu.h"

#include <cstdint>
#include <string>

namespace reluva::x86 {

// A C printf format applied to arguments on the emulated stack, as a
// 32-bit Windows C library reads them: `format`'s address, then the
// arguments from `args` on (4 bytes each; doubles for %f and its kind take
// 8, as C promotes floats). The conversions d i u x X o c s f F e E g G p %
// with flags, width and precision (and *), and the size prefixes h l ll
// I64. %n is ignored.
std::string formatPrintf(const Cpu& cpu, uint32_t format, uint32_t args);

} // namespace reluva::x86
