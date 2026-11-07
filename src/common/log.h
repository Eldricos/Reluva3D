// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <memory>

namespace reluva {

// While it lives, everything written to stderr (the engines' messages, and
// SDL's log through it) also goes to a file: `reluva.log` in Reluva's data
// folder, the run before's kept as `reluva-previous.log`. A console that
// closes with the program, or a build without one, then loses nothing.
// The stream goes through a pipe that a thread copies to both the file and
// wherever stderr went before.
class StderrLog {
public:
    explicit StderrLog(const std::filesystem::path& file);
    ~StderrLog();
    StderrLog(const StderrLog&) = delete;
    StderrLog& operator=(const StderrLog&) = delete;

private:
    struct State;
    std::unique_ptr<State> s_;
};

} // namespace reluva
