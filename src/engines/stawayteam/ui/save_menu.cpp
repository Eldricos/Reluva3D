// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/save_menu.h"

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace reluva::stawayteam {

namespace {

// The words the screens show (0x5017a1, 0x5017b5).
const char *const kNoSaves = "NO SAVE GAMES";
const char *const kDeleteQuestion = "Are you sure you want to delete this game?";
// Its buttons, in order.
enum Which { kAction, kDelete, kExit };
// The longest name typed (inferred).
constexpr size_t kMostName = 40;

} // namespace

SaveGameScreen::SaveGameScreen(const Archive &data, const TextObject &rules, Mode mode, std::filesystem::path folder,
                               SaveGame toSave)
    : _data(data), _rules(rules),
      _def(menuDefinition(rules, mode == Mode::kSave ? "Save Game Screen" : "Load Game Screen")), _mode(mode),
      _folder(std::move(folder)), _toSave(std::move(toSave)), _name(_toSave.name) {
    const std::string screen = mode == Mode::kSave ? "Save Game" : "Load Game";
    _background = screenPicture(data, screen + "/Background");
    _overlay = screenPicture(data, "Load Game/Select Overlay");
    _texts = readStaticTexts(data, _def);
    _numbers = readRandomNumbers(data, _def, 1);
    // Its Menu Font, the list's and the name's (inferred).
    (void)_font.load(data, mode == Mode::kSave ? "Bach White" : "Trek Small");
    const char *const keys[] = {mode == Mode::kSave ? "SaveButton" : "LoadButton", "DeleteButton", "ExitButton"};
    const char *const pictures[] = {mode == Mode::kSave ? "Save" : "Load", "Delete", "Exit"};
    _buttonFonts.resize(3);
    for (size_t i = 0; i < 3; ++i) {
        const std::string key = keys[i];
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        readPoint(_def, key + " Pos", x, y);
        readPoint(_def, key + " Text Pos", textX, textY);
        Button button(data, screen, pictures[i], x, y);
        (void)_buttonFonts[i].load(data, readText(_def, key + " Font"));
        button.setLabel(readText(_def, key + " Text"), &_buttonFonts[i], textX, textY);
        button.setSound(gameSound(data, readText(_def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }
    refresh();
}

SaveGameScreen::~SaveGameScreen() = default;

void SaveGameScreen::refresh() {
    _saves = listSaveGames(_folder);
    _selected = std::min(_selected, int(_saves.size()) - 1);
    if (_mode == Mode::kLoad && _selected < 0 && !_saves.empty())
        _selected = 0;
    // To load, one chosen; to delete, one chosen; to save, a name.
    _buttons.button(kAction).setEnabled(_mode == Mode::kSave ? !_name.empty() : _selected >= 0);
    _buttons.button(kDelete).setEnabled(_selected >= 0);
}

void SaveGameScreen::select(int row) {
    _selected = row;
    if (_mode == Mode::kSave && row >= 0)
        _name = _saves[size_t(row)].second.name;
    refresh();
}

int SaveGameScreen::rowAt(int x, int y) const {
    int lx = 0;
    int ly = 0;
    int lw = 0;
    int lh = 0;
    readPoint(_def, "List Pos", lx, ly);
    readPoint(_def, "List Size", lw, lh);
    const int rowH = std::max(1, _font.height() + 2);
    if (x < lx || y < ly || x >= lx + lw || y >= ly + lh)
        return -1;
    const int row = (y - ly) / rowH;
    return row < int(_saves.size()) ? row : -1;
}

SaveGameScreen::Choice SaveGameScreen::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, mixer);
        if (!answer)
            return Choice::kNone;
        _confirm.reset();
        if (*answer && _selected >= 0) {
            std::error_code ec;
            std::filesystem::remove(_saves[size_t(_selected)].first, ec);
            _selected = -1;
            refresh();
        }
        return Choice::kNone;
    }
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
        return Choice::kCancel;
    // Enter saves or loads. Saving, the other keys type its name, not
    // the buttons' shortcuts.
    const bool enter = ev.type == EventType::kKeyDown && (ev.key == key::kReturn || ev.key == key::kKpEnter);
    if (_mode == Mode::kSave && !enter) {
        if (ev.type == EventType::kTextInput) {
            if (_name.size() < kMostName)
                _name += ev.text;
            refresh();
            return Choice::kNone;
        }
        if (ev.type == EventType::kKeyDown) {
            if (ev.key == key::kBackspace && !_name.empty())
                _name.pop_back();
            refresh();
            return Choice::kNone;
        }
    }
    const int chosen = enter ? kAction : _buttons.handleEvent(ev, mixer);
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && chosen < 0) {
        const int row = rowAt(ev.mouseX, ev.mouseY);
        if (row >= 0)
            select(row);
    }
    if (chosen < 0 || !_buttons.buttons()[size_t(chosen)].enabled())
        return Choice::kNone;
    if (chosen == kExit)
        return Choice::kCancel;
    if (chosen == kDelete) {
        _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kDeleteQuestion, 640, 480);
        return Choice::kNone;
    }
    if (_mode == Mode::kLoad) {
        _chosen = _saves[size_t(_selected)].second;
        return Choice::kLoad;
    }
    // Saved over the one chosen if its name's kept, else a new one.
    SaveGame save = _toSave;
    save.name = _name;
    save.savedAt = int64_t(std::time(nullptr));
    std::filesystem::path path;
    if (_selected >= 0 && _saves[size_t(_selected)].second.name == _name) {
        path = _saves[size_t(_selected)].first;
    } else {
        for (int n = 1; path.empty() || std::filesystem::exists(path); ++n) {
            char file[32];
            std::snprintf(file, sizeof file, "save%03d.sav", n);
            path = _folder / file;
        }
    }
    if (writeSaveGame(path, save))
        return Choice::kNone;
    _chosen = save;
    return Choice::kSaved;
}

void SaveGameScreen::draw(Surface &screen, uint64_t now) {
    const float dt = _last ? float(now - std::min(now, _last)) / 1000.0f : 0.0f;
    _last = now;
    image::blit(screen, _background, 0, 0);
    for (const StaticText &t : _texts)
        t.draw(screen, 0, 0);
    for (RandomNumbers &n : _numbers) {
        n.update(dt);
        n.draw(screen, 0, 0);
    }
    _buttons.draw(screen);
    if (!_font.loaded())
        return;
    // The list: each save's name, the one chosen over the select overlay;
    // with none, NO SAVE GAMES (inferred for where).
    int lx = 0;
    int ly = 0;
    readPoint(_def, "List Pos", lx, ly);
    const int rowH = _font.height() + 2;
    if (_saves.empty())
        _font.draw(screen, lx, ly, kNoSaves);
    for (size_t i = 0; i < _saves.size(); ++i) {
        const int y = ly + int(i) * rowH;
        if (int(i) == _selected)
            image::blit(screen, _overlay, lx - 2, y - 1);
        _font.draw(screen, lx, y, _saves[i].second.name);
    }
    // The name being typed, saving; the one chosen's, loading (its Static);
    // and its time, the missions' total (inferred, as the debriefing's).
    int sx = 0;
    int sy = 0;
    readPoint(_def, _mode == Mode::kSave ? "Edit Pos" : "Static Pos", sx, sy);
    const SaveGame *shown = _mode == Mode::kSave ? &_toSave
                            : _selected >= 0     ? &_saves[size_t(_selected)].second
                                                 : nullptr;
    if (_mode == Mode::kSave)
        _font.draw(screen, sx, sy, _name + ((now / 500) % 2 ? "_" : ""));
    else if (shown)
        _font.draw(screen, sx, sy, shown->name);
    if (shown) {
        int tx = 0;
        int ty = 0;
        readPoint(_def, "Time Pos", tx, ty);
        const int total = int(shown->campaign.time);
        char time[32];
        // Saving, its field fits hours and minutes alone (Time Size 33).
        if (_mode == Mode::kSave)
            std::snprintf(time, sizeof time, "%d:%02d", total / 3600, total / 60 % 60);
        else
            std::snprintf(time, sizeof time, "%02d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
        _font.draw(screen, tx, ty, time);
        int gx = 0;
        int gy = 0;
        readPoint(_def, "GameInfo Pos", gx, gy);
        if (_mode == Mode::kLoad) {
            char score[32];
            std::snprintf(score, sizeof score, "%d", shown->campaign.score);
            _font.draw(screen, gx, gy, shown->missionName);
            _font.draw(screen, gx, gy + rowH, score);
        }
    }
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
