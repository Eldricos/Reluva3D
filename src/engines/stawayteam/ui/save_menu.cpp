// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/save_menu.h"

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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
// The toolkit's colour for a list's chosen row (0x40857b).
constexpr uint8_t kChosenRow[3] = {77, 62, 53};

// Seconds as hours, minutes and seconds of two figures each, a day and
// more wrapped back into the last day (0x554780, 0x5c4700).
std::array<int, 3> clockOf(float seconds) {
    int s = std::max(0, int(seconds));
    if (s > 86400)
        s -= 86400 * ((s - 1) / 86400);
    return {s / 3600, s / 60 % 60, s % 60};
}

std::string clockText(float seconds) {
    const auto [h, m, s] = clockOf(seconds);
    char text[16];
    std::snprintf(text, sizeof text, "%02d.%02d.%02d", h, m, s);
    return text;
}

} // namespace

SaveGameScreen::SaveGameScreen(const Archive &data, const TextObject &rules, Mode mode, std::filesystem::path folder,
                               SaveGame toSave)
    : _data(data), _rules(rules),
      _def(menuDefinition(rules, mode == Mode::kSave ? "Save Game Screen" : "Load Game Screen")), _mode(mode),
      _folder(std::move(folder)), _toSave(std::move(toSave)), _name(_toSave.name) {
    // The map's view shrunk to Image Width, as tall as keeps its shape:
    // the picture shown, and saved with it (0x5551ab).
    const image::Image &view = _toSave.picture;
    const int width = std::max(1, int(_def ? _def->integer("Image Width").value_or(129) : 129));
    if (view.w > 0 && view.h > 0) {
        image::Image small;
        small.w = width;
        small.h = std::max(1, width * view.h / view.w);
        small.rgba.resize(size_t(small.w) * size_t(small.h) * 4);
        for (int y = 0; y < small.h; ++y)
            for (int x = 0; x < small.w; ++x)
                std::memcpy(small.pixel(x, y), view.pixel(x * view.w / small.w, y * view.h / small.h), 4);
        _toSave.picture = std::move(small);
    }
    const std::string screen = mode == Mode::kSave ? "Save Game" : "Load Game";
    _background = screenPicture(data, screen + "/Background");
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
            removeSaveGame(_saves[size_t(_selected)].first);
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
        // Its state read now, being large.
        if (readSaveGame(_saves[size_t(_selected)].first, _chosen, true))
            return Choice::kNone;
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
    // The list (0x402d90): each save's name, the one chosen on a bar of
    // the toolkit's colour a frame's width and a pixel in from each side
    // (0x4029a0), and its mission time at the row's right in columns of
    // two figures as wide as the widest of "55", "22" and "00", with ':'
    // between, 3 widths of "22", 2 of ':' and 4 in from its right
    // (0x554530, 0x5c4700); with none, NO SAVE GAMES (inferred for where).
    int lx = 0;
    int ly = 0;
    int lw = 0;
    int lh = 0;
    readPoint(_def, "List Pos", lx, ly);
    readPoint(_def, "List Size", lw, lh);
    const int rowH = _font.height() + 2;
    const int border = _font.baseline() / 5;
    if (_saves.empty())
        _font.draw(screen, lx, ly, kNoSaves);
    const int figures = std::max({_font.width("55"), _font.width("22"), _font.width("00")});
    const int colon = _font.width(":");
    for (size_t i = 0; i < _saves.size(); ++i) {
        const int y = ly + int(i) * rowH;
        if (int(i) == _selected)
            for (int py = std::max(0, y); py < std::min(screen.h, y + rowH); ++py)
                for (int px = std::max(0, lx + border + 1); px < std::min(screen.w, lx + lw - border - 1); ++px)
                    std::copy(kChosenRow, kChosenRow + 3, screen.row(py) + px * 4);
        const SaveGame &save = _saves[i].second;
        _font.draw(screen, lx + border + 1, y, save.name);
        int x = lx + lw - 3 * _font.width("22") - 2 * colon - 4;
        const std::array<int, 3> clock = clockOf(save.missionTime);
        for (size_t k = 0; k < 3; ++k) {
            char two[8];
            std::snprintf(two, sizeof two, "%02d", clock[k]);
            _font.draw(screen, x, y, two);
            x += figures;
            if (k < 2) {
                _font.draw(screen, x, y, ":");
                x += colon;
            }
        }
    }
    // The name being typed, saving; the one chosen's, loading (its Static).
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
    // The map as it was saved, at Image Pos (0x5021f3).
    if (shown && shown->picture.w > 0) {
        int ix = 0;
        int iy = 0;
        readPoint(_def, "Image Pos", ix, iy);
        image::blit(screen, shown->picture, ix, iy);
    }
    if (shown) {
        // At Time Pos its mission time, hours, minutes and seconds: the
        // game's as it stands, saving (0x554fee), the chosen one's,
        // loading (0x502620).
        int tx = 0;
        int ty = 0;
        readPoint(_def, "Time Pos", tx, ty);
        _font.draw(screen, tx, ty, clockText(shown->missionTime));
        // Loading, at GameInfo Pos its mission, mission time and the
        // game's whole time, each line the font's height and 1 under the
        // last (0x5026ac, 0x5022fc).
        if (_mode == Mode::kLoad) {
            int gx = 0;
            int gy = 0;
            readPoint(_def, "GameInfo Pos", gx, gy);
            const std::string lines[] = {"Mission: " + shown->missionName, "Time: " + clockText(shown->missionTime),
                                         "Total Game Time: " + clockText(shown->campaign.time + shown->missionTime)};
            for (size_t k = 0; k < 3; ++k)
                _font.draw(screen, gx, gy + int(k) * (_font.height() + 1), lines[k]);
        }
    }
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
