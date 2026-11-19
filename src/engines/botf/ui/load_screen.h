// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>
#include <optional>

// The Load Game screen (loadsave.wdf), opened from the main menu: the
// twelve saved-game slots, each with its name and description; a click
// lights one and Accept loads it. What it does is the manual's (chapter 4);
// the slots' order and fields, the layout's
// (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The Load Game screen. */
class LoadScreen : LayoutScreen {
public:
    enum class Result { kOpen, kAccepted, kCancelled };
    static constexpr int kSlots = 12;
    /** Each slot's saved game, or nothing for an empty slot. */
    using Slots = std::array<std::optional<SaveSummary>, kSlots>;

    LoadScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // the two layouts
    /** Shows `slots`, none of them chosen; empty slots can't be chosen. */
    void open(const Slots &slots);
    void draw(Surface &screen) const;
    /**
     * Mouse clicks: a slot chooses it, Accept loads the chosen one (and does
     * nothing until one is), Cancel goes back; and Return/Escape for
     * Accept/Cancel.
     */
    Result handle(const Event &event);

    /** The slot chosen (0-11), or -1. */
    int chosen() const { return _chosen; }

private:
    Slots _slots;
    int _chosen = -1;
};

} // namespace reluva::botf
