// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/campaign.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/static_text.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * Rules.txt's Save Game Screen and Load Game Screen
 * (`CStarTrekSaveGameMenu`, `CStarTrekLoadGameMenu`): the saves in a list,
 * the one chosen's mission and time, and SAVE GAME (with the name typed
 * for it) or LOAD GAME, DELETE GAME and CANCEL
 * (docs/engines/stawayteam/systems/menus.md#saved-games).
 */
class SaveGameScreen {
public:
    enum class Mode { kSave, kLoad };
    enum class Choice { kNone, kCancel, kSaved, kLoad };

    /**
     * For saves in `folder`; to save, `toSave` is what's saved, its name
     * the one offered. `data` and `rules` must outlive it.
     */
    SaveGameScreen(const Archive &data, const TextObject &rules, Mode mode, std::filesystem::path folder,
                   SaveGame toSave = {});
    ~SaveGameScreen();

    Choice handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

    /** The save chosen to load, once Choice::kLoad's given. */
    const SaveGame &chosen() const { return _chosen; }
    /** The saves listed, newest first. */
    const std::vector<std::pair<std::filesystem::path, SaveGame>> &saves() const { return _saves; }
    /** Whether typed text is wanted (System::setTextInput): saving, the name. */
    bool wantsText() const { return _mode == Mode::kSave; }

private:
    void refresh();
    void select(int row);
    int rowAt(int x, int y) const;

    const Archive &_data;
    const TextObject &_rules;
    const TextObject *_def;
    Mode _mode;
    std::filesystem::path _folder;
    SaveGame _toSave;
    SaveGame _chosen;
    image::Image _background;
    image::Image _overlay; // the chosen row's
    std::vector<StaticText> _texts;
    std::vector<RandomNumbers> _numbers; // its random-number nernies
    uint64_t _last = 0;
    Font _font;
    std::vector<Font> _buttonFonts;
    ButtonGroup _buttons;
    std::vector<std::pair<std::filesystem::path, SaveGame>> _saves;
    int _selected = -1;
    std::string _name; // being typed, saving
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
