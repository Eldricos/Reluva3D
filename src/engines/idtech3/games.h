// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"

#include "common/detection.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// What the shared id Tech 3 code needs to know about one game. Everything
// game-specific the engine does is keyed off this, so another id Tech 3
// game (Quake III Arena, Jedi Outcast...) is added with a new entry here
// and its own detection entries, not a new engine.
//
// Whose game code it is: which of Reluva's hosts can run the game's own
// programs (its QVMs and DLLs, whose engine calls are numbered and laid
// out per game). None: its files can be looked at, but not played. Jedi
// Outcast: its multiplayer menus.
enum class GameCode { kNone, kEliteForce, kJediOutcast };

// A player model's three parts ("model/skin").
struct PlayerModels {
    std::string head;
    std::string torso;
    std::string legs;
};

struct GameInfo {
    std::string gameId;  // stable id stored in the config, e.g. "stvef"
    std::string name;    // shown to the user
    std::string baseDir; // the folder holding the game's own .pk3 files
    // Where its shader scripts are (<shaderDir>/*.shader): Quake III's
    // scripts/, or Raven's shaders/ from Jedi Outcast on.
    std::string shaderDir = "scripts";
    // Folders under the one picked for an import that may hold the game's
    // install (baseDir and the single-player code beside it), as its CD
    // lays them out ("SETUP", "GAMEDATA/GameData").
    std::vector<std::string> installFolders;
    // Its recorded matches' extensions, in baseDir/demos (lower case).
    std::vector<std::string> demoExtensions;
    GameCode code = GameCode::kNone;
    // Its servers' first port, and its master servers' (Elite Force's
    // unless set).
    uint16_t serverPort = 27960;
    uint16_t masterPort = 27953;
    // How its client game lays out what it adds to the scene; single
    // player's, where it differs.
    RefEntityLayout refEntity;
    RefEntityLayout spRefEntity;
    // The command's button bits for attacking (and the second attack,
    // where the game has one; 0 if not).
    int32_t buttonAttack = 1;
    int32_t buttonAltAttack = 0;
    // Its other held commands (+use, +force_grip...): the name without the
    // '+', and the button number it holds (its bit, 1 << n).
    std::map<std::string, int> namedButtons;
    // What its multiplayer is called, on the Start page.
    std::string multiplayerName = "Multiplayer";
    // The console's version line, for a game whose console Reluva draws
    // as its original does (client/console_drawer.h); empty: Reluva's own.
    std::string consoleVersion;
    // The same for its single-player engine's console.
    std::string spConsoleVersion;
    // Those of later releases, by patch level (Release::patch, and
    // Release::singlePlayerPatch for single player's), where they differ.
    std::map<std::string, std::string> patchConsoleVersions;
    std::map<std::string, std::string> spPatchConsoleVersions;
    // The level's light brightened only in fullscreen with r_overBrightBits
    // on, by the screen's gamma ramp (Jedi Outcast); otherwise always.
    bool overbrightFullscreenOnly = false;
    // The config of default keys and settings run before the player's own.
    std::string defaultConfig = "default.cfg";
    // The server's settings its engine registers as it starts, before any
    // game: name, default value, CvarStore flags (Quake III's). The menus
    // show them (Create Server); the read-only ones a server sets itself
    // when it starts are left out.
    struct StartCvar {
        std::string name;
        std::string value;
        int32_t flags = 0;
    };
    std::vector<StartCvar> serverCvars;
    // The client's settings its engine registers at start that the game
    // module reads from a player's userinfo (the UserInfo ones), and their
    // defaults: Jedi Outcast's force powers, models, colours...
    std::vector<StartCvar> clientCvars;
    // Multiplayer's model for a player who hasn't chosen one.
    std::string playerModel;
    // Single player's: the player's name until one is set; its game code
    // and menus (native DLLs beside the
    // game's folder; empty if it has none), the first level, levels that
    // aren't single player's (by name prefix), and the player as each sex.
    std::string spPlayerName;
    std::string spGameDll;
    std::string spUiDll;
    // Single player's default config, when its settings and keys are kept
    // apart from multiplayer's (Jedi Outcast's two programs each have
    // their own: the same cvar names mean different things in them).
    // Empty: one set of settings for both.
    std::string spDefaultConfig;
    std::string firstMap;
    std::vector<std::string> multiplayerMapPrefixes;
    PlayerModels malePlayer;
    PlayerModels femalePlayer;
    // Its movies: video/<name> and their file type.
    std::string cinematicExtension;
    // Single player's opening text crawl, drawn by the engine over a movie
    // (Jedi Outcast's): that movie, the crawl's shader without its
    // language's number (sp_language's) and the music played instead of the
    // movie's sound. An empty movie: none.
    struct TextCrawl {
        std::string movie;
        std::string picture;
        std::string music;
    };
    TextCrawl textCrawl;
    // Movies held at their last frame whatever the cinematic command
    // asks (without their file type).
    std::vector<std::string> heldMovies;
    // Movies whose speech is a sound of its own outside English
    // (s_language other than "english"), the movie's own then silent:
    // movie -> sound.
    std::map<std::string, std::string> separateSpeech;
};

// Every game the engine knows; nullptr if `gameId` isn't one.
const std::vector<GameInfo> &games();
const GameInfo *findGame(std::string_view gameId);
// The game whose code Reluva runs as `code` (Elite Force's for kNone).
const GameInfo &gameWithCode(GameCode code);
// The game in an install folder (the one holding baseDir), as detection
// finds it; the first game's if it finds none.
const GameInfo &gameInFolder(const std::filesystem::path &install);

// The detection table for every game, for MetaEngine::detect.
const std::vector<GameVariant> &detectionTable();
// The same games by file names only, for releases not in detectionTable()
// yet; consulted only when nothing there matches.
const std::vector<GameVariant> &fallbackTable();

// What a folder holding a game (its install folder: the game folder, and
// the single-player code beside it) has of it, from the files' signatures:
// the patch level ("1.0", "1.1", "1.2"), whether single player can run and
// which release its code is from (empty if unknown), and a description
// to show ("1.2", "1.2, no single player").
struct Release {
    std::string patch;
    // What the import says of that patch level ("as on the CD; online
    // play needs 1.2"); empty if nothing.
    std::string patchNote;
    bool singlePlayer = false;
    std::string singlePlayerPatch;
    std::string summary;
    // The Expansion Pack: its pak (pak3.pk3), and its single-player code
    // (beside the game folder, as its own setup leaves it, or in
    // expansion/, as an import does, the 1.x code staying beside it).
    bool expansion = false;
    bool expansionSinglePlayer = false;
};
Release describeRelease(const std::filesystem::path &dir, const GameInfo &game);

// The Expansion Pack's pak, as the game folder holds it.
constexpr const char *kExpansionPak = "pak3.pk3";
// Whether `file` is the Expansion Pack's game DLL or menus DLL.
bool isExpansionDll(const std::filesystem::path &file);
// Whether `file` is the Expansion Pack's pak.
bool isExpansionPak(const std::filesystem::path &file);
// The folder whose single-player DLLs to run: with the Expansion Pack,
// its own (expansion/, or beside the game folder when those are its);
// without, the 1.x ones beside it (none when those are the expansion's).
// Empty if there are none.
std::filesystem::path singlePlayerDir(const std::filesystem::path &dir, const GameInfo &game, bool expansion);

// A file a patch brings, by its signature (as detection's): where it goes
// in the game's folder, and the patch level it gives (empty for one that
// says nothing about it, as the DLLs).
struct PatchFile {
    const char *to;
    const char *md5;
    int64_t size;
    const char *patch;
};
// The files of the game's patches that Reluva uses (Elite Force's 1.2
// patch, eliteforcepatch1_2.exe, holds them all: pak1.pk3 from 1.1,
// pak2.pk3, and single player's DLLs as 1.1 left them).
const std::vector<PatchFile> &patchFiles(const GameInfo &game);

} // namespace reluva::idtech3
