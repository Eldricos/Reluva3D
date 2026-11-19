// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <memory>

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
void debug(int level, const char *fmt, ...) RELUVA_PRINTF(2, 3);
void warning(const char *fmt, ...) RELUVA_PRINTF(1, 2);

} // namespace reluva::log

namespace reluva {

// While it lives, everything written to stderr (the engines' messages, and
// SDL's log through it) also goes to a file: `reluva.log` in Reluva's data
// folder, the run before's kept as `reluva-previous.log`. A console that
// closes with the program, or a build without one, then loses nothing.
// The stream goes through a pipe that a thread copies to both the file and
// wherever stderr went before.
class StderrLog {
public:
    explicit StderrLog(const std::filesystem::path &file);
    ~StderrLog();
    StderrLog(const StderrLog &) = delete;
    StderrLog &operator=(const StderrLog &) = delete;

private:
    struct State;
    std::unique_ptr<State> _s;
};

} // namespace reluva
