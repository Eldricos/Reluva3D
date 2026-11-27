// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/static_text.h"
#include "engines/stawayteam/ui/widgets.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * The keyboard screen, which the settings window's CONFIGURE KEYBOARD
 * opens (0x4ea060): the game's actions by category, each with what it's
 * bound to, a click and then a key binding it anew; the mouse's buttons
 * and their scheme; EXIT and RESET. Laid out by Rules.txt's `Config
 * Control Screen` (docs/engines/stawayteam/systems/menus.md#the-keyboard-screen).
 */
class KeyConfigScreen {
public:
    /** The screen, changing `options`' controls; `data`, `rules` and `options` must outlive it. */
    KeyConfigScreen(const Archive &data, const TextObject &rules, GameOptions &options);
    ~KeyConfigScreen();

    /** What `ev` does to it, at `now` (milliseconds): true once it's to close. */
    bool handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    void draw(Surface &screen, uint64_t now);

private:
    /** An action's row: its name, which a click sets waiting for a key, and what it's bound to. */
    struct Row {
        size_t action;
        TextButton name;
    };
    /** A category's panel: its title and its actions. */
    struct Panel {
        std::string title;
        std::vector<Row> rows;
    };
    /** A line of the list of changes, shown 10 seconds. */
    struct Change {
        std::string text;
        uint64_t until;
    };

    // Binds the waiting row's action to `key`, noting the actions that lose it.
    void bind(int key, int modifier, uint64_t now);
    void save();

    const Archive &_data;
    const TextObject &_rules;
    GameOptions &_options;
    image::Image _background;
    image::Image _mouse;
    Font _windowFont; // Placar Condensed
    Font _panelFont;  // Trek Small
    Font _buttonFont; // the EXIT and RESET buttons'
    std::vector<StaticText> _texts;
    ButtonGroup _buttons; // EXIT, RESET
    std::vector<TextButton> _tabs;
    std::shared_ptr<audio::Sound> _switch; // UI/Button Switch.wav
    std::vector<Panel> _panels;
    size_t _shown = 0;
    int _panelX = 0;
    int _panelY = 0;
    int _rowsTop = 0;
    // The row waiting for a key, and the one the keys go to (Delete takes
    // its key away).
    int _waiting = -1;
    int _selected = -1;
    // While waiting: the keys down, and the key and the one held with it
    // so far (scan codes; 0: none).
    std::vector<int> _down;
    int _key = 0;
    int _held = 0;
    std::vector<Change> _changes;
    int _changesX = 0;
    int _changesY = 0;
    int _changesW = 0;
    int _changesH = 0;
    // The mouse's panel.
    int _mouseX = 0;
    int _mouseY = 0;
    std::array<std::array<int, 4>, 3> _mouseButtons{}; // x, y, w, h
    std::array<int, 4> _help{};
    std::array<std::string, 6> _mouseTexts; // Action, Move, Inventory, then their help
    int _hoverButton = -1;
    CheckBox _reverse;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
