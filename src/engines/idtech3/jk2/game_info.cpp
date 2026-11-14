// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/game_info.h"

namespace reluva::idtech3::jk2 {

// From the English 1.02a files (docs/engines/idtech3/games/jk2/README.md).
GameInfo gameInfo() {
    GameInfo g;
    g.gameId = "jk2";
    g.name = "Star Wars Jedi Knight II: Jedi Outcast";
    g.baseDir = "base";
    g.shaderDir = "shaders";
    // The CD holds the game as an install has it, in GAMEDATA/GameData;
    // an install puts it in GameData under the folder it was given.
    g.installFolders = {"GameData", "GAMEDATA/GameData"};
    // As its own recording is named (demos/JK2FFA.dm_15 in assets0.pk3).
    g.demoExtensions = {".dm_15"};
    // Imported now so single player needs no second import once it runs;
    // its menus are in jk2sp.exe, not a DLL.
    g.spGameDll = "jk2gamex86.dll";
    g.firstMap = "kejim_post";
    g.multiplayerMapPrefixes = {"ffa_", "duel_", "ctf_"};
    g.cinematicExtension = ".roq";
    return g;
}

} // namespace reluva::idtech3::jk2
