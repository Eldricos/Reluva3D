// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>

namespace reluva {

/**
 * When the program crashes (a bad memory access, an abort...), where it
 * was is written to `report` (a text file: the signal or exception and the
 * call stack, as addresses within each module), and on Windows a minidump
 * beside it (`report` with .dmp), which Visual Studio opens with the
 * program's .pdb. Only calls safe in a crash are made then; nothing is
 * written without one. Call once, early. Builds with AddressSanitizer
 * leave crashes to it.
 */
void installCrashReport(const std::filesystem::path &report);

} // namespace reluva
