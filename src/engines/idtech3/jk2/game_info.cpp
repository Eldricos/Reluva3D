// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/game_info.h"

#include "engines/idtech3/cvars.h"
#include "engines/idtech3/jk2/ref_entity.h"

namespace reluva::idtech3::jk2 {

// From the English 1.02a files (docs/engines/idtech3/games/jk2/README.md).
GameInfo gameInfo() {
    GameInfo g;
    g.gameId = "jk2";
    g.name = "Star Wars Jedi Knight II: Jedi Outcast";
    g.baseDir = "base";
    g.shaderDir = "shaders";
    // Its multiplayer menus run; the rest of its game code doesn't yet.
    g.code = GameCode::kJediOutcast;
    // jk2mp.exe's (0x40acb6, the local search from it up; 0x40d975).
    g.serverPort = 28070;
    g.masterPort = 28060;
    g.multiplayerName = "Multiplayer";
    // The multiplayer engine's (default.cfg is single player's).
    g.defaultConfig = "mpdefault.cfg";
    // Kyle Katarn, as its menus start a new player.
    g.playerModel = "kyle/default";
    // Its buttons, as jk2mp.exe names them: attack 0, alt attack 7, use 5,
    // force grip 6, the active force power 9, lightning 10, drain 11, the
    // taunt 3.
    g.buttonAttack = 1;
    g.buttonAltAttack = 1 << 7;
    g.namedButtons = {{"use", 5},          {"force_grip", 6}, {"useforce", 9}, {"force_lightning", 10},
                      {"force_drain", 11}, {"taunt", 3}};
    g.refEntity = kRefEntityLayout;
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
    // As jk2mp.exe's server registers them at start (0x44dcfc on).
    constexpr int32_t kS = CvarStore::kServerInfo;
    constexpr int32_t kSA = CvarStore::kServerInfo | CvarStore::kArchive;
    constexpr int32_t kT = CvarStore::kTemp;
    g.serverCvars = {
        {"dmflags", "0", kS},
        {"fraglimit", "20", kS},
        {"timelimit", "0", kS},
        {"g_maxHolocronCarry", "3", kS},
        {"g_privateDuel", "1", kS},
        {"g_saberLocking", "1", kS},
        {"g_maxForceRank", "6", kS},
        {"duel_fraglimit", "10", kS},
        {"g_forceBasedTeams", "0", kS},
        {"g_duelWeaponDisable", "1", kS},
        {"g_needpass", "0", kS},
        {"g_gametype", "0", kS | CvarStore::kLatch},
        {"sv_keywords", "", kS},
        {"sv_privateClients", "0", kS},
        {"sv_hostname", "noname", kSA},
        {"sv_maxclients", "8", kS | CvarStore::kLatch},
        {"sv_maxRate", "0", kSA},
        {"sv_minPing", "0", kSA},
        {"sv_maxPing", "0", kSA},
        {"sv_floodProtect", "1", kSA},
        {"sv_allowAnonymous", "0", kS},
        {"sv_pure", "1", CvarStore::kSystemInfo},
        {"rconPassword", "", kT},
        {"sv_fps", "20", kT},
        {"sv_timeout", "200", kT},
        {"sv_zombietime", "2", kT},
        {"nextmap", "", kT},
        {"sv_allowDownload", "0", kS},
        // Its network protocol, which the server list's search names.
        {"protocol", "15", kS | CvarStore::kReadOnly},
    };
    // As jk2mp.exe's client registers them (0x40c9a2 on).
    constexpr int32_t kUA = CvarStore::kUserInfo | CvarStore::kArchive;
    g.clientCvars = {
        {"name", "Padawan", kUA},
        {"rate", "4000", kUA},
        {"snaps", "20", kUA},
        {"model", "kyle/default", kUA},
        {"team_model", "kyle/default", kUA},
        {"forcepowers", "7-1-032330000000001333", kUA},
        {"g_redTeam", "Empire", kSA},
        {"g_blueTeam", "Rebellion", kSA},
        {"color1", "4", kUA},
        {"color2", "5", kUA},
        {"handicap", "100", kUA},
        {"teamtask", "0", CvarStore::kUserInfo},
        {"sex", "male", kUA},
        {"cl_anonymous", "0", kUA},
        {"password", "", CvarStore::kUserInfo},
        {"cg_predictItems", "1", kUA},
    };
    return g;
}

std::vector<std::string> masterServers() {
    return {"masterjk2.ravensoft.com", "master.ouned.de", "master.jkhub.org"};
}

} // namespace reluva::idtech3::jk2
