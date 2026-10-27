// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once


// The id Tech 3 engine's pages (a header each, in this folder), showing
// what a game's files hold and running its game code. A game starts on the
// Start page, which chooses single player, Holomatch (the Menu page, and
// Play from there) or the developer tools; Esc a page doesn't take (or the
// game's own Quit) goes back to it, and Esc there to the launcher. With the
// developer tools chosen, Ctrl+Tab steps through every page (Tab is the
// game's).
//
// World:    the map drawn by the GPU renderer. Left/Right pick a map; drag
//           to look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift
//           to go faster, R back to the start point, I to show or hide
//           the info.
// Models:   every .md3 and .mdr; players (lower, upper and head in one folder) put
//           together. Left/Right step, PageUp/PageDown jump 10; drag to
//           turn, wheel or +/- to zoom, A to animate, ,/. to step frames,
//           [ and ] to step through a player's animations (from its
//           animation.cfg, legs and torso paired as the game plays them),
//           T to place a player's torso on its legs' tag or as in its
//           file, R to reset the view, I to show or hide the info.
// Files:    Up/Down, PageUp/PageDown, Home/End scroll the search path's files.
// Textures: Left/Right step, PageUp/PageDown jump 10, Home/End.
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
//           line. Starting
//           a level from the menus (Engage) comes here.
// Maps:     Left/Right pick a map; drag (or arrow keys with Shift) to rotate,
//           wheel or +/- to zoom, W/A/S/D/Q/E to move, R to reset the view,
//           P to show or hide patches, I to show or hide the info.

#include "engines/idtech3/bindings.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/shader_cache.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/world_renderer.h"

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/image.h"
#include "common/system.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace reluva::idtech3 {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const ShaderLibrary& shaders;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
    std::filesystem::path savePath;           // where this game's own files go (GameTarget::savePath)
    audio::Mixer* mixer = nullptr;            // sound output; none if the system has no audio
    // The console variables, one set for every page that runs game code,
    // as the original has one: what's chosen in the menus applies to the
    // level started from them.
    CvarStore cvars;
    KeyBindings bindings;
    // A page asking for another to be shown (by name), as when the menus
    // start a level on the Play page; `requestMap` is the level.
    std::string requestPage, requestMap;
    // A server to join on the Play page (connect <address>).
    std::string requestConnect;
    // Adds a pak (one just downloaded) to the game's files.
    std::function<Error(const std::filesystem::path&)> addPak;
    // Commands for the level's server after it starts ("addbot munro 3"...).
    std::vector<std::string> requestCommands;
    // The game's install folder (the one holding BaseEF), where native
    // game code (efgamex86.dll) sits.
    std::filesystem::path installPath;
};

// The settings: the game's default.cfg, then the player's own (Reluva's
// config file in the save folder), and writing that file back with the
// bindings and the settings to keep (Archive cvars), if any changed.
void loadConfig(ViewerContext& ctx);
void saveConfig(ViewerContext& ctx);
// The brightness setting (r_gamma, the menus' brightness slider; 1 unless
// set), which 3D pages show their frames with.
float brightness(const ViewerContext& ctx);

// The video settings the original's Video and Graphics menus set (Quake
// III's cvars), as Reluva applies them; a setting not made counts as the
// original's default, but the size, which is Reluva's 960x720 until a
// mode is chosen.
struct VideoSettings {
    int width = 960, height = 720; // r_mode (r_customwidth/height for -1)
    bool fullscreen = false;       // r_fullscreen
    int picmip = 0;                // r_picmip: textures halved this many times (texture detail)
    // r_texturemode: GL_{NEAREST,LINEAR}[_MIPMAP_{NEAREST,LINEAR}]
    bool nearest = false, mipmaps = true, mipNearest = true;
    bool anisotropic = false;  // r_ext_texture_filter_anisotropic
    bool vertexLight = false;  // r_vertexLight: the level lit by its vertices, not its lightmaps
    float subdivisions = 4;    // r_subdivisions: how far curves may stray from round, in units (geometric detail)
    int lodBias = 0;           // r_lodBias: models' lower details used (geometric detail)
    bool fastSky = false;      // r_fastsky: the sky left plain
};
VideoSettings videoSettings(const CvarStore& cvars);
// Gives the caches the settings that change how things load (call before
// loading); a null cache is skipped.
void applyVideoSettings(const VideoSettings& v, ShaderCache& shaders, WorldRenderer* world, ModelCache* models);

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0;   // pages with nothing to show are skipped
    virtual void activate() = 0;        // redraw and retitle after switching here
    virtual void deactivate() {}        // before switching away
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    // Pages that draw with System::init3D; the engine puts the 2D
    // framebuffer back when leaving one.
    virtual bool uses3D() const { return false; }
    // Whether Esc (which otherwise leaves) goes to the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

// The code points in UTF-8 text (typed characters).
std::vector<uint32_t> decodeUtf8(const char* s);

} // namespace reluva::idtech3
