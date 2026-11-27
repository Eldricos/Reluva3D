// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/ui/model_view.h"

#include <cstdint>
#include <map>
#include <string>

namespace reluva::tpw {

/**
 * A tooltip, as the park's HUD shows them: a pale yellow box (`!tooltip`)
 * fading in by the pointer once it has rested a while, its lines in
 * `GAME8AA`, dark, from `UIHELPTEXT`. See
 * docs/engines/tpw/games/simcoaster/systems/park.md#menus-and-tooltips.
 */
class Tooltip {
public:
    /** How long the pointer rests before one shows, in milliseconds (guessed). */
    static constexpr uint32_t kDelay = 500;

    /** Reads the box's model, the font, the character set and UIHELPTEXT. */
    Error load(const SearchSet &files);
    /** UIHELPTEXT's string, its parameters filled. */
    std::string help(uint32_t index, const std::map<int, std::string> &values = {}) const;
    /**
     * Draws `text`, its lines broken at its own line breaks, by the pointer,
     * once it has rested `rested` milliseconds on what it's over; nothing
     * before, or for empty text.
     */
    void draw(Surface &screen, int mouseX, int mouseY, const std::string &text, uint32_t rested) const;

private:
    LoadedModel _box;
    CharacterSet _characters;
    Font _font;
    StringTable _help;
};

} // namespace reluva::tpw
