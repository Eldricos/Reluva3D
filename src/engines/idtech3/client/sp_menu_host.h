// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/vm/vm_services.h"

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * A single-player game's menus, as the single-player Play page drives
 * them: Elite Force's (efuix86.dll, run in the x86 interpreter) or Jedi
 * Outcast's (menu scripts its engine runs itself). Drawing is recorded,
 * not done: each refresh's pictures and scenes, for the page to show.
 */
class SpMenuHost {
public:
    // The key catcher's bit for the menus (the engine's KEYCATCH_UI): while
    // it's set, keys go to them rather than to the game.
    static constexpr int32_t kCatchUi = 2;
    // A saved game's picture: 256 x 256 RGBA.
    static constexpr uint32_t kShotSize = 256 * 256 * 4;

    virtual ~SpMenuHost() = default;

    /** A key's press (key | q3key::kCharFlag: a typed character). */
    virtual Error keyEvent(int32_t key) = 0;
    /** The mouse moved, in the 640 x 480 screen's units. */
    virtual Error mouseEvent(int32_t dx, int32_t dy) = 0;
    /** Draws them (pictures(), scene()) at `time`, the real time in ms. */
    virtual Error refresh(int32_t time) = 0;
    /** Whether they cover the whole screen (the game isn't drawn under). */
    virtual bool fullscreen() = 0;
    /**
     * The loading screen as a level loads, into pictures(), with `server`
     * and `info` as the original's engine gives them; nothing for menus
     * that leave it to the client game.
     */
    virtual Error drawLoadingScreen(const std::string &server, const std::string &info) = 0;
    /** The connect text alone, over the client game's loading screen. */
    virtual Error drawConnectText(const std::string &server, const std::string &info) = 0;
    /** Opens a menu ("main", "ingame"...), or closes them (""). */
    virtual Error setActiveMenu(const std::string &name) = 0;
    /**
     * A console command for them.
     * @param handled gets whether they knew it.
     */
    virtual Error consoleCommand(const std::string &text, bool *handled) = 0;

    /**
     * The picture of the game for the next save, or of the saved game
     * loading (kShotSize bytes, rows bottom first as the original's screen
     * captures); empty: none.
     */
    virtual void setCapture(const std::vector<uint8_t> &rgba) = 0;
    /** The capture, or a black picture if there's none. */
    virtual std::vector<uint8_t> capture() const = 0;

    /** Who has the keys (kCatchUi while a menu is open), as they set it. */
    int32_t catcher() const { return _catcher; }
    void setCatcher(int32_t c) { _catcher = c; }

    /** The last refresh's drawing, and what it was drawn with. */
    const std::vector<Picture> &pictures() const { return _pictures; }
    const Scene &scene() const { return _scene; }
    /** A raw image drawn (a saved game's picture): where, its size and RGBA pixels. */
    struct RawImage {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        int cols = 0;
        int rows = 0;
        std::vector<uint8_t> rgba;
    };
    const std::vector<RawImage> &rawImages() const { return _raw; }
    const HandleTable &shaders() const { return _shaders; }
    const HandleTable &models() const { return _models; }
    const HandleTable &skins() const { return _skins; }
    const HandleTable &sounds() const { return _sounds; }

    /** Commands they ran ("map kejim_post", "quit"...) in order; taken by the caller. */
    std::vector<std::string> takeCommands() { return std::move(_commands); }

    // --- What the engine provides ------------------------------------------
    // What they print.
    std::function<void(const std::string &)> print;
    // A sound to play now, not placed in the world.
    std::function<void(const std::string &sound, int channel)> startLocalSound;
    // Configstrings of the game running: false if none.
    std::function<bool(int32_t index, std::string &out)> configString;
    // The running game's GameAllowedToSaveHere with that flag; unset while
    // there's no game (the flag comes back).
    std::function<uint32_t(uint32_t flag)> askGame;
    // What keys do (the Controls menu); none: no bindings.
    KeyBindings *bindings = nullptr;
    // Whether a shader or picture can be found; unset: all can.
    std::function<bool(const std::string &name)> shaderExists;
    // Keys held now.
    std::set<int32_t> keysDown;
    // The connection state they're told (1: no game, 7: in one).
    int32_t connectionState = 1;

    // --- Saved games -----------------------------------------------------------
    // A saved game's file by name (no folder or extension); false if
    // there's none.
    std::function<bool(const std::string &name, std::vector<uint8_t> &file)> readSave;
    // Where saved games are written, listed with the game's own.
    std::filesystem::path saveDir;
    // Whether a saved game (by name) is listed: those the game running
    // can't read are left out. All when unset.
    std::function<bool(const std::string &name)> listSave;
    // The comment the save menu set for the next save (its description).
    std::string saveComment;

protected:
    int32_t _catcher = 0;
    std::vector<Picture> _pictures;
    std::vector<RawImage> _raw;
    Scene _scene;
    HandleTable _shaders;
    HandleTable _models;
    HandleTable _skins;
    HandleTable _sounds;
    std::vector<std::string> _commands;
};

} // namespace reluva::idtech3
