// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// reluva-tool's Need for Speed commands.

// Runs the command named by argv[1] and returns its exit code, or -1 if
// it isn't one of these.
int runNfsCommand(int argc, char** argv);

// Their lines of reluva-tool's usage text.
extern const char* const kNfsUsage;
