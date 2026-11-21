// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "graphics/draw.h"
#include "common/events.h"
#include "common/system.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace reluva::stharbinger {

/**
 * What the game sees of the player's input: the events DEEP.EXE acts on.
 * Platform::pollEvent makes them from Reluva's events.
 */
struct InputEvent {
    /** What happened; every kind has the mouse's position. */
    enum Type {
        kNone,
        kQuit,      // the window closed, or back to the launcher (Ctrl+F5)
        kKeyDown,   // `key` (common/events.h codes) with `mods`; or a character typed (`ascii`, key 0)
        kMouseMove, // to `mouse`
        kLeftDown,  // a click at `mouse`
        kRightDown,
    } type = kNone;
    uint32_t key = 0;
    uint16_t mods = 0; // keymod bits
    char ascii = 0;    // printable ASCII typed, while text input is on (setTextInput)
    Point mouse;
};

/**
 * The engine's side of Reluva's System, as the game uses it: one 640x480
 * 8-bit screen, its palette, the player's input, the time, the mixer and
 * the folder for saved games. Tests make one over a System of their own.
 */
class Platform {
public:
    /**
     * `system` and `mixer` must outlive it. `mixer` may be null (no sound);
     * `saveDir` is the game's own folder for its files (GameTarget::savePath).
     */
    Platform(System &system, audio::Mixer *mixer, std::filesystem::path saveDir);

    /** Opens an 8-bit screen of that size; again only if the size changes. */
    void initGraphics(int width, int height);
    /** Sets `count` palette entries from `start`; `rgb` has 8 bits per channel. */
    void setPalette(const uint8_t *rgb, int start, int count);
    /** The picture shown by the next updateScreen(); one of another size is ignored. */
    void copyRectToScreen(const Surface &screen);
    /** Shows the picture, once initGraphics() has opened the screen. */
    void updateScreen();
    /** Passed on to the System. */
    ///@{
    void setWindowTitle(const std::string &title);
    void setMouseCursorVisible(bool visible);
    ///@}
    /** Typed characters arrive as kKeyDown events with `ascii` (for names). */
    void setTextInput(bool on);

    /** The next event the game acts on; false when no more are pending. */
    bool pollEvent(InputEvent &event);
    /** Whether the player closed the window or went back to the launcher. */
    bool quitRequested() const { return _quit; }

    /** The System's clock, in milliseconds. */
    uint32_t getMillis() const;
    /** Waits, as the System does. */
    void delayMillis(uint32_t ms);

    /** The mixer, or nullptr without sound. */
    audio::Mixer *getMixer() { return _mixer; }

    /**
     * The folder for saved games and settings, made if needed, with a
     * separator at the end; the game adds its own subfolders.
     */
    std::string getSavePath();

private:
    System &_system;
    audio::Mixer *_mixer;
    std::filesystem::path _saveDir;
    Surface _screen;
    bool _graphics = false;
    bool _quit = false;
};

} // namespace reluva::stharbinger
