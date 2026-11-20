// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/ui/layout_screen.h"

#include <array>
#include <optional>
#include <string>

// The Load Game and Save Game screens (loadsave.wdf, both), opened from the
// main menu: the twelve saved-game slots, each with its file's name and its
// description. Loading, a click lights a saved game and Accept loads it;
// saving, a click chooses any slot, its description can be typed, and
// Accept saves there. What they do is the manual's (chapter 4); how Load
// Game looks, a screenshot of the game's
// (docs/engines/botf/systems/screens.md).

namespace reluva::botf {

/** The Load Game and Save Game screens. */
class LoadSaveScreen : LayoutScreen {
public:
    using LayoutScreen::setSoundHandler;
    enum class Result { kOpen, kAccepted, kCancelled };
    enum class Mode { kLoad, kSave };
    static constexpr int kSlots = 12;
    static constexpr size_t kMaxDescription = 50; // characters: gameInfo's 51 bytes with the zero
    /** Each slot's saved game, or nothing for an empty slot. */
    using Slots = std::array<std::optional<SaveSummary>, kSlots>;

    LoadSaveScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    Error load(); // the two layouts
    /**
     * Shows `slots`, none of them chosen. Loading, empty slots can't be
     * chosen; saving, the slot chosen keeps its saved game's description,
     * or an empty one shows `description` (UTF-8), the game's to save,
     * either to be typed over.
     */
    void open(const Slots &slots, Mode mode, const std::string &description = {});
    void draw(Surface &screen) const;
    /**
     * Mouse clicks: a slot chooses it, Accept takes the chosen one (and is
     * dimmed until one is), Cancel goes back; Return/Escape for
     * Accept/Cancel. Saving, typed text (kTextInput) and Backspace edit the
     * description, as far as it fits its field.
     */
    Result handle(const Event &event);

    /** The slot chosen (0-11), or -1. */
    int chosen() const { return _chosen; }
    /** Saving: the description as typed, UTF-8. */
    const std::string &description() const { return _description; }
    /** Saving: whether the flashing cursor is shown just now. */
    void setCursorShown(bool shown) { _cursorShown = shown; }

private:
    Slots _slots;
    Mode _mode = Mode::kLoad;
    std::string _description;
    std::string _newDescription; // saving: an empty slot's description at first
    int _chosen = -1;
    bool _cursorShown = true;
};

} // namespace reluva::botf
