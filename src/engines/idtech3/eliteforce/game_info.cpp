// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/game_info.h"

#include "engines/idtech3/eliteforce/ref_entity.h"

namespace reluva::idtech3::eliteforce {

GameInfo gameInfo() {
    GameInfo g;
    g.gameId = "stvef";
    g.name = "Star Trek: Voyager - Elite Force";
    g.baseDir = "baseEF";
    g.code = GameCode::EliteForce;
    g.refEntity = kRefEntityLayout;
    g.buttonAttack = 1;
    g.buttonAltAttack = 32;
    g.playerModel = "munro";
    g.spPlayerName = "Munro";
    g.spGameDll = "efgamex86.dll";
    g.spUiDll = "efuix86.dll";
    g.firstMap = "borg1";
    g.multiplayerMapPrefixes = {"hm_", "ctf_"}; // Holomatch's
    g.malePlayer = {"munro/default", "hazard/default", "hazard/default"};
    g.femalePlayer = {"alexandria/default", "hazardfemale/default", "hazardfemale/default"};
    g.cinematicExtension = ".bik";
    return g;
}

std::vector<std::string> masterServers() {
    return {"master.stef1.ravensoft.com", "efmaster.tjps.eu", "master.stvef.org", "master.stef1.daggolin.de"};
}

} // namespace reluva::idtech3::eliteforce
