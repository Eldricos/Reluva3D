// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/key_config.h"

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::stawayteam {

namespace {

const char *const kResetQuestion = "Are you sure you want to reset your controls?"; // 0x63f160
const char *const kDeleted = " action deleted.";                                    // 0x63f130
constexpr uint64_t kChangeMs = 10000;                                               // 0x4ea9f3
// A category's title is 24 above its first action (0x4ea66b); each action
// row is the font's baseline and 6 high (0x4e97ee), 2 apart (0x4ea7b3), as
// wide as 33 X's (0x4e9817), its name 3 in from its right and down
// (0x5efef0); what it's bound to just after it, 2 in.
constexpr int kTitleGap = 0x18;
constexpr std::array<int, 14> kReserved = {0x15b, 0x15c, 0x3b, 0x42, 0x43, 0x44, 0x57,
                                           0x58,  0x2,   0x3,  0x4,  0x5,  0x6,  0x7};
constexpr int kRowGap = 2;
const char *const kRowWidth = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
constexpr int kRowTextInset = 3;
constexpr int kLabelBorder = 2;

int whole(const TextObject *def, const std::string &key) {
    return def ? int(def->integer(key).value_or(0)) : 0;
}

} // namespace

KeyConfigScreen::KeyConfigScreen(const Archive &data, const TextObject &rules, GameOptions &options)
    : _data(data), _rules(rules), _options(options), _background(screenPicture(data, "Key Config/Background")),
      _mouse(screenPicture(data, "Key Config/Mouse")), _reverse("Reverse Mouse Buttons", &_panelFont) {
    // The window's font and its panels' (0x4ea0ee, 0x4ea2c1).
    (void)_windowFont.load(data, "Placar Condensed");
    (void)_panelFont.load(data, "Trek Small");
    const TextObject *def = menuDefinition(rules, "Config Control Screen");

    // Rules.txt's texts on it.
    _texts = readStaticTexts(data, def);

    // EXIT and RESET, their labels on their right (0x4ea165).
    int x = 0;
    int y = 0;
    for (const char *name : {"Exit", "Reset"}) {
        const std::string key = std::string(name) + "Button";
        readPoint(def, key + " Pos", x, y);
        Button button(data, "Key Config", name, x, y);
        int textX = 0;
        int textY = 0;
        readPoint(def, key + " Text Pos", textX, textY);
        (void)_buttonFont.load(data, readText(def, key + " Font"));
        button.setLabel(readText(def, key + " Text"), &_buttonFont, textX, textY);
        button.alignLabelRight();
        button.setSound(gameSound(data, readText(def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }

    // A tab and a panel for each category, in the order the actions bring
    // them (0x4ea34f); the tabs down from ButtonStart Pos, the first's
    // height and Button Spacing apart, the panels all at ConfigStart Pos
    // (0x4eaaf0).
    int tabX = 0;
    int tabY = 0;
    readPoint(def, "ButtonStart Pos", tabX, tabY);
    const int spacing = whole(def, "Button Spacing");
    readPoint(def, "ConfigStart Pos", _panelX, _panelY);
    int panelW = 0;
    int panelH = 0;
    readPoint(def, "ConfigStart Size", panelW, panelH);
    // A tab is its label's width and 2 by the font's baseline and 2, with
    // no margin (0x4ea4c7, 0x4e4730).
    const int titleH = (_windowFont.loaded() ? _windowFont.baseline() : 0) + 2;
    _rowsTop = titleH + kTitleGap;
    const int rowW = _panelFont.loaded() ? _panelFont.width(kRowWidth) : 0;
    const int rowH = (_panelFont.loaded() ? _panelFont.baseline() : 0) + 6;
    const auto &actions = gameActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        auto panel = std::find_if(_panels.begin(), _panels.end(),
                                  [&](const Panel &p) { return p.title == actions[i].category; });
        if (panel == _panels.end()) {
            _panels.push_back({actions[i].category, {}});
            panel = _panels.end() - 1;
            const int w = (_windowFont.loaded() ? _windowFont.width(actions[i].category) : 0) + 2;
            TextButton tab(actions[i].category, &_windowFont, w, titleH);
            const int step = _tabs.empty() ? 0 : _tabs.front().height() + spacing;
            tab.setPosition(tabX, _tabs.empty() ? tabY : _tabs.back().y() + step);
            _tabs.push_back(std::move(tab));
        }
        TextButton name(actions[i].name, &_panelFont, rowW, rowH);
        name.setPosition(0, _rowsTop + int(panel->rows.size()) * (rowH + kRowGap));
        name.setText(kRowTextInset, kRowTextInset, true);
        panel->rows.push_back({i, std::move(name)});
    }
    if (!_tabs.empty())
        _tabs.front().setChecked(true);

    readPoint(def, "KeyChangeList Pos", _changesX, _changesY);
    readPoint(def, "KeyChangeList Size", _changesW, _changesH);

    // The mouse's panel (0x4e90c0): its picture, what each of its buttons
    // does, help for the one under the mouse, and reversing them.
    readPoint(def, "Mouse Pos", _mouseX, _mouseY);
    for (size_t i = 0; i < 3; ++i) {
        const std::string key = "Button" + std::to_string(i + 1);
        readPoint(def, key + " Pos", _mouseButtons[i][0], _mouseButtons[i][1]);
        readPoint(def, key + " Size", _mouseButtons[i][2], _mouseButtons[i][3]);
    }
    readPoint(def, "HelpText Pos", _help[0], _help[1]);
    readPoint(def, "HelpText Size", _help[2], _help[3]);
    _mouseTexts = {readText(def, "Action Text"),    readText(def, "Move Text"),
                   readText(def, "Inventory Text"), readText(def, "Action Help Text"),
                   readText(def, "Move Help Text"), readText(def, "Inventory Help Text")};
    readPoint(def, "ReverseMouse Pos", x, y);
    _reverse.setPosition(x, y);
    _reverse.setChecked(_options.controls.scheme == 1);
    _switch = gameSound(data, "UI/Button Switch.wav");
}

KeyConfigScreen::~KeyConfigScreen() = default;

void KeyConfigScreen::save() {
    _options.save();
}

void KeyConfigScreen::bind(int key, int modifier, uint64_t now) {
    Panel &panel = _panels[_shown];
    Row &row = panel.rows[size_t(_waiting)];
    for (const std::string &name : _options.controls.bindKey(row.action, key, modifier))
        _changes.push_back({name + kDeleted, now + kChangeMs});
    row.name.setChecked(false);
    _waiting = -1;
    save();
}

bool KeyConfigScreen::handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now) {
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, mixer);
        if (answer && *answer) {
            _options.controls.reset();
            _changes.clear();
            save();
        }
        if (answer)
            _confirm.reset();
        return false;
    }
    Panel &panel = _panels[_shown];
    // A joystick's button pressed while waiting binds it as the action's
    // Joystick Button (inferred: as a key's bound).
    if (_waiting >= 0 && ev.type == EventType::kJoyButtonDown) {
        Row &row = panel.rows[size_t(_waiting)];
        for (const std::string &name : _options.controls.bindJoystick(row.action, ev.joyButton + 1))
            _changes.push_back({name + kDeleted, now + kChangeMs});
        row.name.setChecked(false);
        _waiting = -1;
        save();
        return false;
    }
    if (_waiting >= 0 && (ev.type == EventType::kKeyDown || ev.type == EventType::kKeyUp)) {
        // Waiting for a key (0x484060): Esc gives up, the binding as it
        // was (0x48417e); Delete takes its key away (0x484149).
        const int code = scanCode(ev.key);
        if (ev.type == EventType::kKeyDown && (ev.key == key::kEscape || ev.key == key::kDelete)) {
            if (ev.key == key::kDelete) {
                _options.controls.clearKey(panel.rows[size_t(_waiting)].action);
                save();
            }
            panel.rows[size_t(_waiting)].name.setChecked(false);
            _waiting = -1;
            return false;
        }
        if (ev.type == EventType::kKeyDown && code && std::find(_down.begin(), _down.end(), code) == _down.end()) {
            _down.push_back(code);
            // Keys kept for the program can't be bound (0x63b6e8): the
            // Windows keys, F1 and F8-F12, and 1-6 (the game plays an
            // Error.wav it hasn't got).
            if (std::find(kReserved.begin(), kReserved.end(), code) != kReserved.end())
                return false;
            // Any key down before the last is held with it (0x4841c6).
            if (_key && !_held)
                _held = _key;
            _key = code;
        } else if (ev.type == EventType::kKeyUp) {
            std::erase(_down, code);
            // Bound once every key is let go (0x4841d6).
            if (_down.empty() && _key) {
                bind(_key, _held, now);
                _key = _held = 0;
            }
        }
        return false;
    }
    if (ev.type == EventType::kKeyDown) {
        // EXIT is the screen's default and cancel button, and has the
        // focus (0x4ea923).
        if ((isEnter(ev) || isEscape(ev)) && _buttons.press(0, mixer) == 0)
            return true;
        // Delete or Backspace takes the chosen action's key away (0x4e990f).
        if ((ev.key == key::kDelete || ev.key == key::kBackspace) && _selected >= 0) {
            _options.controls.clearKey(panel.rows[size_t(_selected)].action);
            save();
            return false;
        }
    }
    switch (_buttons.handleEvent(ev, mixer)) {
    case 0:
        return true;
    case 1:
        _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kResetQuestion, _background.w, _background.h);
        return false;
    default:
        break;
    }
    for (size_t i = 0; i < _tabs.size(); ++i)
        if (_tabs[i].handleEvent(ev, 0, 0) && i != _shown) {
            if (_waiting >= 0)
                panel.rows[size_t(_waiting)].name.setChecked(false);
            _waiting = _selected = -1;
            _shown = i;
            for (size_t j = 0; j < _tabs.size(); ++j)
                _tabs[j].setChecked(j == i);
            // Button Switch as the tabs change to another (0x40786f).
            if (mixer && _switch)
                mixer->play(_switch, audio::SoundType::kEffects);
            return false;
        }
    Panel &current = _panels[_shown];
    for (size_t i = 0; i < current.rows.size(); ++i)
        if (current.rows[i].name.handleEvent(ev, _panelX, _panelY)) {
            // A click waits for a key (0x4e9c30).
            if (_waiting >= 0)
                current.rows[size_t(_waiting)].name.setChecked(false);
            _waiting = _selected = int(i);
            _down.clear();
            _key = _held = 0;
            current.rows[i].name.setChecked(true);
        }
    if (_reverse.handleEvent(ev, _mouseX, _mouseY)) {
        // Button Switch as a check box changes (0x4e64b0).
        if (mixer && _switch)
            mixer->play(_switch, audio::SoundType::kEffects);
        _options.controls.scheme = _reverse.checked() ? 1 : 0;
        save();
    }
    if (ev.type == EventType::kMouseMove) {
        _hoverButton = -1;
        for (size_t i = 0; i < 3; ++i) {
            const auto &b = _mouseButtons[i];
            const int mx = ev.mouseX - _mouseX - b[0];
            const int my = ev.mouseY - _mouseY - b[1];
            if (mx >= 0 && my >= 0 && mx < b[2] && my < b[3])
                _hoverButton = int(i);
        }
    }
    return false;
}

void KeyConfigScreen::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, 0, 0);
    for (const StaticText &t : _texts)
        t.draw(screen, 0, 0);
    for (TextButton &tab : _tabs)
        tab.draw(screen, 0, 0, now);

    // The category's panel: its title in the middle across, a pixel past
    // its border (0x4ea623), then its actions.
    Panel &panel = _panels[_shown];
    if (_windowFont.loaded()) {
        int panelW = 0;
        int panelH = 0;
        readPoint(menuDefinition(_rules, "Config Control Screen"), "ConfigStart Size", panelW, panelH);
        const int w = _windowFont.width(panel.title);
        _windowFont.draw(screen, _panelX + (panelW - w) / 2 + kLabelBorder, _panelY + kLabelBorder + 1, panel.title);
    }
    for (Row &row : panel.rows) {
        row.name.draw(screen, _panelX, _panelY, now);
        if (_panelFont.loaded())
            _panelFont.draw(screen, _panelX + row.name.x() + row.name.width() + kLabelBorder,
                            _panelY + row.name.y() + kLabelBorder + 1, _options.controls.text(row.action));
    }

    // The changes, the oldest first, each for 10 seconds (0x4e9c70).
    _changes.erase(std::remove_if(_changes.begin(), _changes.end(), [&](const Change &c) { return now >= c.until; }),
                   _changes.end());
    if (_panelFont.loaded()) {
        int y = _changesY;
        for (const Change &c : _changes) {
            if (y + _panelFont.height() > _changesY + _changesH)
                break;
            _panelFont.draw(screen, _changesX, y, c.text);
            y += _panelFont.height();
        }
    }

    // The mouse's panel: its title in its own font (0x4e9238), its picture, each button's use by the
    // scheme (0x4e9461), the middle "Custom" unless both inventory actions
    // are on the wheel (0x4e9525), and help for the one under the mouse.
    const TextObject *def = menuDefinition(_rules, "Config Control Screen");
    int px = 0;
    int py = 0;
    readPoint(def, "MouseTitle Pos", px, py);
    if (_panelFont.loaded())
        _panelFont.draw(screen, _mouseX + px, _mouseY + py + 1, "MOUSE CONFIG");
    readPoint(def, "MouseImage Pos", px, py);
    image::blit(screen, _mouse, _mouseX + px, _mouseY + py);
    const bool reversed = _options.controls.scheme == 1;
    const auto &actions = gameActions();
    bool wheel = true;
    for (size_t i = 0; i < actions.size(); ++i)
        if (std::string(actions[i].name).starts_with("Inventory ") && !(_options.controls.binding(i).mouse & 0x18))
            wheel = false;
    const std::array<std::string, 3> labels = {reversed ? _mouseTexts[0] : _mouseTexts[1],
                                               reversed ? _mouseTexts[1] : _mouseTexts[0],
                                               wheel ? _mouseTexts[2] : std::string("Custom")};
    const std::array<std::string, 3> help = {reversed ? _mouseTexts[3] : _mouseTexts[4],
                                             reversed ? _mouseTexts[4] : _mouseTexts[3], _mouseTexts[5]};
    if (_panelFont.loaded()) {
        for (size_t i = 0; i < 3; ++i)
            _panelFont.drawWrapped(screen, _mouseX + _mouseButtons[i][0], _mouseY + _mouseButtons[i][1],
                                   _mouseButtons[i][2], labels[i], Font::Align::kLeft, 2);
        if (_hoverButton >= 0)
            _panelFont.drawWrapped(screen, _mouseX + _help[0], _mouseY + _help[1], _help[2], help[size_t(_hoverButton)],
                                   Font::Align::kLeft, 2);
    }
    _reverse.draw(screen, _mouseX, _mouseY);
    _buttons.draw(screen);
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
