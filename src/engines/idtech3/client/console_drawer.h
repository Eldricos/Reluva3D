// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/scene.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

/**
 * The engine's console as Jedi Outcast's multiplayer engine draws it
 * (docs/engines/idtech3/games/jk2/systems/console.md): the console
 * dropped over the top of the screen, or the notify lines and the chat
 * being typed when it's shut.
 */
struct ConsoleFrame {
    int screenW = 640; // the frame, in pixels
    int screenH = 480;
    float frac = 0; // how much of the screen it covers: 0 shut, 0.5 open
    std::string version;
    // The lines printed, oldest first, each with its colour codes;
    // wrapped to the console's width as they're drawn.
    const std::deque<std::string> *lines = nullptr;
    int scroll = 0;      // lines scrolled back from the newest
    bool typing = false; // the console has the keys: its input line shown
    std::string input;
    size_t cursor = 0;
    int fieldScroll = 0;     // the first character shown of what's typed (or the chat's)
    bool overstrike = false; // typing over: the cursor is character 11
    bool cursorOn = true;    // the cursor's blink
    // Shut: the lines still shown over the game, oldest first.
    std::vector<std::string> notify;
    enum class Chat { kNone, kSay, kSayTeam } chat = Chat::kNone;
    std::string chatInput;
    size_t chatCursor = 0;
};

/** The shaders it's drawn with, as the renderer's materials. */
struct ConsoleShaders {
    int32_t charSet = -1; // gfx/2d/charsgrid_med
    int32_t console = -1; // the background
    int32_t white = -1;
};

/**
 * The console's pictures, in the original's 640 x 480 screen, each
 * Picture::shader one of `shaders`.
 */
std::vector<Picture> consolePictures(const ConsoleFrame &frame, const ConsoleShaders &shaders);

/**
 * A printed line as the console wraps it, `width` characters across:
 * at a word that would reach the edge, or at the edge itself. Each piece
 * after the first starts with the colour in force.
 */
std::vector<std::string> wrapConsoleLine(std::string_view text, int width);

} // namespace reluva::idtech3
