// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/objectives_window.h"

#include "engines/stawayteam/ui/layout.h"

#include <algorithm>

namespace reluva::stawayteam {

namespace {

// The lists' fonts (0x47f9a0, 0x47fac3).
const char *const kHeadingFont = "Trek Title Blue";
const char *const kTextFont = "Trek Small";
// A disabled objective is drawn at half its brightness (0x47fa7e).
constexpr float kDisabledBrightness = 0.5f;
// The icon and its words this far apart (0x47faf9).
constexpr int kIconGap = 2;
// RESUME's place on the objectives window: this far in, and this far
// above the window's bottom under its own height (0x51aefb).
constexpr int kResumeX = 69;
constexpr int kResumeAbove = 27;
// The end game's objectives this far under its message (0x50cfad).
constexpr int kListBelow = 5;

const char *const kStatusPictures[] = {"Disabled", "Incomplete", "Completed", "Failed"};

// A menu's Menu Font, kept beside its definition.
std::string menuFont(const TextObject &rules, const std::string &name) {
    const TextObject *custom = rules.object("Custom Rules");
    const TextObject *menus = custom ? custom->object("Menu") : nullptr;
    if (menus)
        for (const TextField *item : menus->items())
            if (item->object && readText(item->object.get(), "Menu Name") == name)
                return readText(item->object.get(), "Menu Font");
    return {};
}

} // namespace

ObjectiveList::ObjectiveList(const Archive &data) {
    (void)_heading.load(data, kHeadingFont);
    (void)_text.load(data, kTextFont);
    for (size_t i = 0; i < _icons.size(); ++i)
        _icons[i] = screenPicture(data, std::string("Objectives Menu/") + kStatusPictures[i]);
}

void ObjectiveList::draw(Surface &screen, int x, int y, int width, int spacing, const std::vector<ObjectiveLine> &lines,
                         int opacity) const {
    for (const bool primary : {true, false}) {
        bool headed = false;
        for (const ObjectiveLine &line : lines) {
            if (line.primary != primary)
                continue;
            if (!headed && _heading.loaded()) {
                _heading.draw(screen, x, y, primary ? "Primary Objectives" : "Secondary Objectives", opacity);
                y += _heading.height() + kIconGap;
            }
            headed = true;
            // Its state's icon, its words wrapped beside it, the two
            // centred on each other; the next under the taller.
            const image::Image &icon = _icons[size_t(line.status)];
            const int textW = width - icon.w - kIconGap;
            const auto wrapped = _text.loaded() ? _text.wrap(line.text, textW) : std::vector<std::string>{};
            const int textH = int(wrapped.size()) * _text.height();
            const float brightness = line.status == ObjectiveStatus::kDisabled ? kDisabledBrightness : 1.0f;
            image::blit(screen, icon, x, y + (textH - icon.h) / 2, opacity);
            int ly = y;
            for (const std::string &l : wrapped) {
                _text.draw(screen, x + icon.w + kIconGap, ly, l, opacity, brightness);
                ly += _text.height();
            }
            y += std::max(textH, icon.h) + spacing;
        }
    }
}

ObjectivesWindow::ObjectivesWindow(const Archive &data, const TextObject &rules, int screenW, int screenH)
    : _background(screenPicture(data, "Objectives Menu/Background")), _list(data), _x((screenW - _background.w) / 2),
      _y((screenH - _background.h) / 2) {
    const TextObject *def = menuDefinition(rules, "Objectives Menu");
    int h = 0;
    readPoint(def, "ObjectiveDisplay Pos", _listX, _listY);
    readPoint(def, "ObjectiveDisplay Size", _listW, h);
    _spacing = def ? int(def->integer("Objective Display Spacing").value_or(0)) : 0;
    (void)_font.load(data, menuFont(rules, "Objectives Menu"));
    image::Image picture = screenPicture(data, "Escape Menu/Button Background");
    const int pictureH = picture.h;
    Button resume(std::move(picture), kResumeX, _background.h - pictureH - kResumeAbove);
    resume.setLabel("&RESUME", &_font, 0, (pictureH - _font.height()) / 2);
    _buttons.add(std::move(resume));
    _buttons.setOrigin(_x, _y);
}

bool ObjectivesWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
        return true;
    return _buttons.handleEvent(ev, mixer) == 0;
}

void ObjectivesWindow::draw(Surface &screen, const std::vector<ObjectiveLine> &lines) const {
    image::blit(screen, _background, _x, _y);
    _list.draw(screen, _x + _listX, _y + _listY, _listW, _spacing, lines);
    _buttons.draw(screen);
}

EndGameWindow::EndGameWindow(const Archive &data, const TextObject &rules, bool completed, std::string message,
                             int screenW, int screenH)
    : _background(screenPicture(data, "End Game Menu/Background")), _list(data), _message(std::move(message)),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    const TextObject *def = menuDefinition(rules, "End Game Menu");
    int h = 0;
    readPoint(def, "EndGameDisplay Pos", _textX, _textY);
    readPoint(def, "EndGameDisplay Size", _textW, h);
    _spacing = def ? int(def->integer("Objective Display Spacing").value_or(0)) : 0;
    (void)_font.load(data, readText(def, "End Game Display Font"));
    // Completed, CONTINUE alone; failed, LOAD GAME, RESTART and END GAME
    // (0x50d08d).
    struct Kind {
        const char *key;
        Choice choice;
    };
    const std::vector<Kind> kinds = completed ? std::vector<Kind>{{"ContinueButton", Choice::kContinue}}
                                              : std::vector<Kind>{{"LoadSaveGameButton", Choice::kLoadGame},
                                                                  {"RestartMissionButton", Choice::kRestart},
                                                                  {"EndGameButton", Choice::kEndGame}};
    for (const Kind &k : kinds) {
        const std::string key = k.key;
        const std::string fontName = readText(def, key + " Font");
        if (!_buttonFonts.count(fontName))
            (void)_buttonFonts[fontName].load(data, fontName);
    }
    for (const Kind &k : kinds) {
        const std::string key = k.key;
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        readPoint(def, key + " Pos", x, y);
        readPoint(def, key + " Text Pos", textX, textY);
        Button button(data, "End Game Menu", "Button", x, y);
        button.setLabel(readText(def, key + " Text"), &_buttonFonts[readText(def, key + " Font")], textX, textY);
        button.setSound(gameSound(data, readText(def, key + " Press Sound")));
        // Saved games can't be loaded yet.
        if (k.choice == Choice::kLoadGame)
            button.setEnabled(false);
        _buttons.add(std::move(button));
        _choices.push_back(k.choice);
    }
    _buttons.setOrigin(_x, _y);
}

EndGameWindow::Choice EndGameWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    const int chosen = _buttons.handleEvent(ev, mixer);
    return chosen >= 0 ? _choices[size_t(chosen)] : Choice::kNone;
}

void EndGameWindow::draw(Surface &screen, const std::vector<ObjectiveLine> &lines) const {
    // The message wrapped and centred across the top of its place, the
    // objectives under it (0x50cef0).
    image::blit(screen, _background, _x, _y);
    int below = 0;
    if (_font.loaded()) {
        _font.drawWrapped(screen, _x + _textX + _textW / 2, _y + _textY, _textW, _message, Font::Align::kCenter);
        below = int(_font.wrap(_message, _textW).size()) * _font.height();
    }
    _list.draw(screen, _x + _textX, _y + _textY + below + kListBelow, _textW, _spacing, lines);
    _buttons.draw(screen);
}

} // namespace reluva::stawayteam
