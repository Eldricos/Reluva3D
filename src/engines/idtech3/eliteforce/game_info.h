// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/games.h"

#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Star Trek: Voyager - Elite Force, as the shared id Tech 3 code sees it.
GameInfo gameInfo();

// The executable its DLLs are told loaded them.
inline constexpr const char* kExeName = "stvoy.exe";
// Its master servers: Raven's first, as the original; then the community's
// (those the GPL engine and community guides list, October 2026).
std::vector<std::string> masterServers();

} // namespace reluva::idtech3::eliteforce
