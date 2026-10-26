// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
#include "common/types.h"
#include "engines/awe/game_text.h"

#include <string>
#include <vector>

// The dialogue box at the foot of the screen (Interface/dialoguewin01.png):
// the line being said, centred, in its speaker's colour; or the lines
// Antoinette can choose from, each by a bullet (dialoguewin04.png, and
// dialoguewin05.png for the one under the mouse).

namespace reluva::awe {

struct DialogueBoxPictures {
    const image::Image* window = nullptr;
    const image::Image* bullet = nullptr;
    const image::Image* bulletOn = nullptr;
};

struct DialogueChoiceArea {
    int x = 0, y = 0, w = 0, h = 0;
};

// Draws a line said (`choices` empty) or the choices (`text` ignored),
// the box's top at `top` (it slides in and out), over `dst` keeping its
// alpha (an overlay). Returns where each choice is, for clicks. As the
// game lays it out (0x43cf90): the line wrapped at 944 pixels and centred;
// the choices from 25 pixels down the box, at x = 40, a bullet before
// each, 4 pixels apart.
std::vector<DialogueChoiceArea> drawDialogueBox(Surface& dst, const GameFonts& fonts, const DialogueBoxPictures& pictures,
                                                int top, const std::string& text, Color colour,
                                                const std::vector<std::string>& choices, int highlighted);

} // namespace reluva::awe
