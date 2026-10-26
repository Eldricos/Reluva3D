// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/x86/win32.h"

#include <cstdint>
#include <string>

namespace reluva::x86 {

// Stands in for MSVCRT.dll, the C runtime Windows shares between programs
// built with Visual C++ (programs that link it dynamically rather than
// carrying their own): memory, strings, characters, numbers, formatted
// text, files (through the process's), time, Visual C++'s rand, maths, and
// the start-up and exception-frame helpers compiled code calls. Written
// from Microsoft's documentation of each function; where the documentation
// leaves a result open, Visual C++'s behaviour of the time is followed and
// said so.
void addMsvcrt(Win32Process& process);

// scanf's conversions applied to `input`, storing through the pointers on
// the emulated stack from `args` on: d i u o x X f e E g G (with h, l, L,
// I64), s c [set] n % and *, with widths. Returns the number of items
// stored, or -1 if the input ended before the first.
int scanFormat(Cpu& cpu, const std::string& input, const std::string& format, uint32_t args);

} // namespace reluva::x86
