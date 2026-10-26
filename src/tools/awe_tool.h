// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// reluva-tool's AWE engine commands: the command's exit code, or -1 if
// argv[1] isn't one of them.
int aweToolCommand(int argc, char** argv);
// Their usage lines.
const char* aweToolUsage();
