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

// What the game sees of the player's input: the events DEEP.EXE acts on.
// Platform::pollEvent makes them from Reluva's events.
struct InputEvent {
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

// The engine's side of Reluva's System, as the game uses it: one 640x480
// 8-bit screen, its palette, the player's input, the time, the mixer and
// the folder for saved games. Tests make one over a System of their own.
class Platform {
public:
    // `mixer` may be null (no sound); `saveDir` is the game's own folder
    // for its files (GameTarget::savePath).
    Platform(System& system, audio::Mixer* mixer, std::filesystem::path saveDir);

    void initGraphics(int width, int height);
    // `rgb`: 8 bits per channel, `count` entries from `start`.
    void setPalette(const uint8_t* rgb, int start, int count);
    // The picture shown by the next updateScreen().
    void copyRectToScreen(const Surface& screen);
    void updateScreen();
    void setWindowTitle(const std::string& title);
    void setMouseCursorVisible(bool visible);
    // Typed characters arrive as kKeyDown events with `ascii` (for names).
    void setTextInput(bool on);

    // False when no more events are pending.
    bool pollEvent(InputEvent& event);
    // The player closed the window or went back to the launcher.
    bool quitRequested() const { return quit_; }

    uint32_t getMillis() const;
    void delayMillis(uint32_t ms);

    audio::Mixer* getMixer() { return mixer_; }

    // The folder for saved games and settings, made if needed, with a
    // separator at the end; the game adds its own subfolders.
    std::string getSavePath();

private:
    System& system_;
    audio::Mixer* mixer_;
    std::filesystem::path saveDir_;
    Surface screen_;
    bool graphics_ = false;
    bool quit_ = false;
};

} // namespace reluva::stharbinger
