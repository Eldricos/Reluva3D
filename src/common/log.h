// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

namespace reluva::log {

#if defined(__GNUC__) || defined(__clang__)
#define RELUVA_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define RELUVA_PRINTF(fmt, args)
#endif

// How much an engine's debug() messages print: 0 (the default) none, and
// more the higher it is. An engine's developer settings set it.
void setDebugLevel(int level);
int debugLevel();

// A line on the console (stderr): debug() only up to the debug level,
// warning() always, marked as one.
void debug(int level, const char* fmt, ...) RELUVA_PRINTF(2, 3);
void warning(const char* fmt, ...) RELUVA_PRINTF(1, 2);

} // namespace reluva::log
