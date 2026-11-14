// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The id Tech 3 engine's pages (a header each in this folder), running the
// game's own code; a Session (session.h) shows them. A game starts on the
// Start page, which chooses single player or Holomatch (the Menu page, and
// Play from there); Esc a page doesn't take (or the game's own Quit) goes
// back to it, and Esc there to the launcher.
//
// Menu:     the game's own menus (Elite Force: vm/ui.qvm) running; keys,
//           typing and the mouse go to them (their own pointer follows
//           the mouse, the system's is hidden), Ctrl+Esc leaves, Ctrl+R
//           starts them again.
// Play:     a Holomatch level run by the game's own code (Elite Force:
//           vm/qagame.qvm and vm/cgame.qvm) with you as the player:
//           keys do what they're bound to (the game's default.cfg, then
//           the menus' Controls, kept in reluva.cfg in the save folder),
//           the mouse looks (it's held by the window); Ctrl+PageUp and
//           Ctrl+PageDown pick a map, Ctrl+I shows the info, Ctrl+R
//           restarts, Esc goes back to the menus (Ctrl+Esc to the
//           launcher). ~ opens the console (lines starting / are
//           commands, others are said); messagemode (T) types a chat
//           line. Starting a level from the menus (Engage) comes here.

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/render/shader_cache.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/render/world_renderer.h"

#include "common/archive.h"
#include "audio/mixer.h"
#include "common/events.h"
#include "image/image.h"
#include "common/system.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/** What every page of a game shares: the system, the game's files, its settings. */
struct PageContext {
    System &sys;
    Surface &screen; // RGBA32
    const SearchSet &files;
    const ShaderLibrary &shaders;
    const GameInfo &game;
    const std::vector<std::string> &warnings; // problems loading the game's files
    std::filesystem::path savePath;           // where this game's own files go (GameTarget::savePath)
    audio::Mixer *mixer = nullptr;            // sound output; none if the system has no audio
    // The console variables, one set for every page that runs game code,
    // as the original has one: what's chosen in the menus applies to the
    // level started from them.
    CvarStore cvars;
    KeyBindings bindings;
    // A page asking for another to be shown (by name), as when the menus
    // start a level on the Play page; `requestMap` is the level.
    std::string requestPage;
    std::string requestMap;
    // A server to join on the Play page (connect <address>).
    std::string requestConnect;
    // Adds a pak (one just downloaded) to the game's files.
    std::function<Error(const std::filesystem::path &)> addPak;
    // Commands for the level's server after it starts ("addbot munro 3"...).
    std::vector<std::string> requestCommands;
    // The game's install folder (the one holding BaseEF), where native
    // game code (efgamex86.dll) sits.
    std::filesystem::path installPath;
    // A match ended (the Play page left): the menus go back to their main
    // menu, as the original's do on disconnecting.
    bool matchEnded = false;
    // A recorded match to play on the Play page (demo <name>: the file
    // under demos/).
    std::string requestDemo;
    // Where single player's code is (installPath, or its expansion/
    // folder for the Expansion Pack's); empty if there's none to run.
    std::filesystem::path singlePlayerPath;
    // The mod being played (its folder's name, the fs_game cvar), or empty
    // for the game itself. With one, savePath is the mod's own save folder
    // (saves, settings, screenshots), and homePath stays the game's.
    std::string gameDir;
    // The game's save folder whatever the mod (GameTarget::savePath): the
    // CD key, and paks downloaded into its <game folder>/ folders.
    std::filesystem::path homePath;
    // vid_restart run: the engine starts the game's files again if fs_game
    // changed (the menus' Mods screens), as Elite Force's engines do.
    bool requestRestart = false;
};

/**
 * Reads the settings: the game's default.cfg, then the player's own
 * (Reluva's config file in the save folder).
 */
void loadConfig(PageContext &ctx);
/**
 * Writes the player's config file back with the bindings and the settings
 * to keep (Archive cvars), if any changed.
 */
void saveConfig(PageContext &ctx);
/**
 * The brightness setting (r_gamma, the menus' brightness slider; 1 unless
 * set), which 3D pages show their frames with.
 */
float brightness(const PageContext &ctx);

/**
 * The video settings the original's Video and Graphics menus set (Quake
 * III's cvars), as Reluva applies them.
 *
 * A setting not made counts as the original's default, but the size, which
 * is Reluva's 960x720 until a mode is chosen.
 */
struct VideoSettings {
    // r_mode (r_customwidth/height for -1)
    int width = 960;
    int height = 720;
    bool fullscreen = false; // r_fullscreen
    int picmip = 0;          // r_picmip: textures halved this many times (texture detail)
    // r_texturemode: GL_{NEAREST,LINEAR}[_MIPMAP_{NEAREST,LINEAR}]
    bool nearest = false;
    bool mipmaps = true;
    bool mipNearest = true;
    bool anisotropic = false; // r_ext_texture_filter_anisotropic
    bool vertexLight = false; // r_vertexLight: the level lit by its vertices, not its lightmaps
    float subdivisions = 4;   // r_subdivisions: how far curves may stray from round, in units (geometric detail)
    int lodBias = 0;          // r_lodBias: models' lower details used (geometric detail)
    bool fastSky = false;     // r_fastsky: the sky left plain
};
/** The video settings the cvars hold. */
VideoSettings videoSettings(const CvarStore &cvars);
/**
 * Gives the caches the settings that change how things load (call before
 * loading); a null cache is skipped.
 */
void applyVideoSettings(const VideoSettings &v, ShaderCache &shaders, WorldRenderer *world, ModelCache *models);

/** A screen of a game, shown by a Session one at a time. */
class Page {
public:
    explicit Page(PageContext &ctx) : _ctx(ctx) {}
    virtual ~Page() = default;

    /** The page's name, which PageContext::requestPage asks for. */
    virtual const char *name() const = 0;
    /** How many things it shows; pages with nothing to show are skipped. */
    virtual size_t count() const = 0;
    /** Redraws and retitles after switching here. */
    virtual void activate() = 0;
    /** Called before switching away. */
    virtual void deactivate() {}
    virtual void handleEvent(const Event &ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    /**
     * Whether the page draws with System::init3D; the session puts the 2D
     * framebuffer back when leaving one.
     */
    virtual bool uses3D() const { return false; }
    /** Whether Esc (which otherwise leaves) goes to the page instead. */
    virtual bool takesKey(const Event & /*ev*/) const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string &s, Color c = {230, 230, 230});
    void setTitle(const std::string &detail);

    PageContext &_ctx;
};

/** The code points in UTF-8 text (typed characters). */
std::vector<uint32_t> decodeUtf8(const char *s);

} // namespace reluva::idtech3
