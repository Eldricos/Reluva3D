// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/games.h"

namespace reluva::idtech3::jk2 {

// Star Wars Jedi Knight II: Jedi Outcast, as the shared id Tech 3 code sees
// it. Of its game code, only its multiplayer menus run yet.
GameInfo gameInfo();
// The master servers its server list asks: Raven's, as the original, then
// the community's (sv_master1 to sv_master3).
std::vector<std::string> masterServers();

} // namespace reluva::idtech3::jk2
