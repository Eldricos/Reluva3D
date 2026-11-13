// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// dialogInfo.res: the fonts, and the layout of every menu and dialog. See
// docs/engines/clue/formats/resi.md.

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::clue {

struct ResiFont {
    uint32_t id = 0;
    std::string animation; // "largeu:Alargeu": frame n is character n
    int32_t textSpacing = 0;
    int32_t lineSpacing = 0;
};

struct ResiControl {
    // The kinds, by what each kind of control is in the game's dialogs.
    enum Type : int32_t {
        kImage = 0,
        kText = 1,
        kImageButton = 2,
        kTextButton = 3,
        kEdit = 5,
        kSlider = 7,
        kScrollKnob = 8,
    };

    int32_t type = 0;
    int32_t id = 0; // what the game knows it by; 9999 for decoration, 10000 Return/Cancel, 10001 OK/Yes
    // Its rectangle: right and bottom 0 when its image gives its size;
    // right -1 for the whole width of the screen.
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    // An Image's or ImageButton's animation is in text; a TextButton's
    // label is text and its animation animation. '|' breaks lines.
    std::string text;
    std::string animation;
    std::vector<std::string> items;
    int32_t font = 0;
    int32_t otherFont = 0;
    int32_t group = 0; // 1: an alternative the game shows when needed; hidden at first
    // How its text sits in its rectangle (as in recordings: the
    // preferences' labels are left-aligned, the titles centred).
    int32_t centreX = 0;           // 1: centred across; 0: from the left
    int32_t centreY = 0;           // 1: centred down; 2: the suggestion window's choices (their name at their foot)
    int32_t maxLength = 0;         // 32 in many controls, not understood; Reluva takes it as a text box's length limit
    int32_t style = 0;             // 2 for two-line buttons ("Locally|on this machine")
    std::array<int32_t, 28> raw{}; // the 112 bytes as stored (pointers among them)
};

struct ResiDialog {
    int32_t id = 0;
    std::string background; // its animation, "menu:AmainBgnd"
    std::vector<ResiControl> controls;
    std::array<int32_t, 9> raw{};

    const ResiControl *control(int32_t id) const;
};

struct Resi {
    std::vector<ResiFont> fonts;
    std::vector<ResiDialog> dialogs;

    const ResiDialog *dialog(int32_t id) const;
    const ResiFont *font(uint32_t id) const;
};

Error parseResi(const std::vector<uint8_t> &data, Resi &out);

} // namespace reluva::clue
