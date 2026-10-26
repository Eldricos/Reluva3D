// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// reluva-tool's AWE engine commands: the command's exit code, or -1 if
// argv[1] isn't one of them.
int aweToolCommand(int argc, char** argv);
// reluva-tool awe-check: every Granny file, scene and text file of an
// install read, failures reported (awe_check.cpp).
int aweCheckCommand(int argc, char** argv);
// Their usage lines.
const char* aweToolUsage();

namespace reluva::awe {
// reluva-tool awe-path: the game's path between two cells beside Reluva's
// (awe_path.cpp).
int aweCommandPath(int argc, char** argv);
// reluva-tool awe-scripts: every script command of an install, by verb
// (awe_scripts.cpp).
int aweCommandScripts(int argc, char** argv);
} // namespace reluva::awe
