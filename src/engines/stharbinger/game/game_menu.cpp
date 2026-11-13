// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game menu (DEEP.EXE Game_Setup 0x20D54): new game, save, load, quit,
// resume, delete, difficulty, autosave and credits over art\s\e\setup.cmp.

#include "engines/stharbinger/game/game.h"

#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace reluva::stharbinger {

namespace {

enum MenuButton {
    kButtonNew = 0,
    kButtonSave = 1,
    kButtonLoad = 2,
    kButtonQuit = 3,
    kButtonResume = 4,
    kButtonDelete = 5,
    kButtonEasy = 6, // 6..8: difficulty 0..2
    kButtonAutosave = 9,
    kButtonCredits = 10,
    kButtonPrev = 11,
    kButtonNext = 12,
    kButtonPrev10 = 13,
    kButtonNext10 = 14,
    kNumButtons = 15
};

// Hit rectangles (0x55898.. tables).
const Rect kButtons[kNumButtons] = {
    {112, 39, 253, 62},   {112, 64, 253, 87},   {112, 89, 253, 112},  {112, 114, 253, 137}, {112, 139, 253, 162},
    {112, 164, 253, 187}, {91, 283, 230, 306},  {91, 308, 230, 331},  {91, 333, 230, 356},  {42, 447, 329, 469},
    {494, 447, 597, 469}, {332, 328, 431, 351}, {494, 328, 593, 351}, {332, 353, 431, 376}, {494, 353, 593, 376},
};

const int kThumbX = 301, kThumbY = 79;
const int kTextX = 301, kTextY = 52;
const Rect kTextBar(296, 49, 626, 67);
const Rect kConfirmBox(120, 162, 520, 318);
const size_t kMaxNameLength = 67;
const int kMaxNameWidth = 294;

bool isSaveFile(const std::string &name) {
    return name.size() > 4 && text::equalsIgnoreCase(name.substr(name.size() - 4), ".ds9");
}

uint8_t nearest(const Palette &pal, int r, int g, int b) {
    return uint8_t(pal.findBestColor(uint8_t(r * 255 / 63), uint8_t(g * 255 / 63), uint8_t(b * 255 / 63)));
}

} // namespace

void Game::requestMenu() {
    // Right click (or Esc) while the map is allowed and the script is idle.
    if (_mapEnabled && !_thread.active && _pendingScene < 0)
        openMenu(false);
}

void Game::openMenu(bool first) {
    draw::create(_menuGameScreen, kScreenWidth, kScreenHeight);
    if (!draw::empty(_cleanScreen))
        draw::blit(_menuGameScreen, _cleanScreen, 0, 0);
    _menuGamePalette = _palette;
    _menu.active = true;
    _menu.first = first;
    loadFont(); // for the confirmations and the save name, before any conversation
    _menu.mode = GameMenu::kMain;
    _system->setTextInput(_menu.mode == GameMenu::kNameEntry); // typed names arrive as text
    if (draw::empty(_menu.art)) {
        bool loaded = false;
        if (!_resources.loadBackground("SETUP", _menu.art, _menu.artPalette, loaded)) {
            log::warning("art\\s\\e\\setup.cmp not found: the menu has no picture");
            draw::create(_menu.art, kScreenWidth, kScreenHeight, 0);
            for (int i = 0; i < 256; i++)
                _menu.artPalette.setColor(i, uint8_t(i), uint8_t(i), uint8_t(i));
        }
        if (auto s = _resources.openFile("ART/BUTTONS.RLE")) {
            uint8_t header[8];
            _menu.buttons.loadWithHeader(*s, header);
        }
    }
    if (_system->getMixer() && _conv.speechHandle != audio::kNoSound)
        _system->getMixer()->stop(_conv.speechHandle);
    scanSaves();
    selectSave(_lastSave);
    drawMenuScreen();
    log::debug(1, "Game menu%s", first ? " (start)" : "");
}

void Game::closeMenu() {
    _menu.active = false;
    _system->setTextInput(false);
    _lastSave = _menu.selected;
    saveSettings();
}

void Game::scanSaves() {
    _menu.saves.clear();
    std::string dir = getSaveDirectory();
    if (dir.empty())
        return;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = it->path().filename().string();
        if (isSaveFile(f))
            _menu.saves.push_back(f);
    }
    std::sort(_menu.saves.begin(), _menu.saves.end());
}

void Game::selectSave(int index) {
    int n = int(_menu.saves.size());
    _menu.selected = n ? std::clamp(index, 0, n - 1) : 0;
    _menu.infoValid = false;
    if (!n)
        return;
    SaveInfo info;
    if (readSaveInfo((std::filesystem::path(getSaveDirectory()) / _menu.saves[_menu.selected]).string(), info)) {
        _menu.infoValid = true;
        _menu.description = info.description;
        _menu.thumbnail.assign(320 * 240, 0);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++)
                _menu.thumbnail[y * 320 + x] = info.screenshot[(y * 2) * 640 + x * 2];
        for (int i = 0; i < 256; i++)
            _menu.savePalette.setColor(i, info.palette[i * 3], info.palette[i * 3 + 1], info.palette[i * 3 + 2]);
        // The thumbnail's colours in the menu's own palette (when it keeps it).
        for (int i = 0; i < 256; i++)
            _menu.thumbnailMap[i] = uint8_t(
                _menu.artPalette.findBestColor(info.palette[i * 3], info.palette[i * 3 + 1], info.palette[i * 3 + 2]));
    }
}

int Game::menuButtonAt(int x, int y) const {
    for (int i = 0; i < kNumButtons; i++)
        if (kButtons[i].contains(x, y))
            return i;
    return -1;
}

void Game::drawMenuScreen() {
    // The original shows the menu in the selected save's palette (setup.pal
    // without saves), so the menu art changes colour from save to save. As a
    // convenience the menu keeps setup.pal and the thumbnail is recoloured.
    bool savePalette = _menu.infoValid && _vm->getOptions().original;
    const Palette &pal = savePalette ? _menu.savePalette : _menu.artPalette;
    _palette = pal;
    _fadeLevel = 256;
    applyPalette();

    // Drawn into its own surface; each step copies it to the screen under the cursor.
    Surface &dst = _menu.frame;
    draw::create(dst, kScreenWidth, kScreenHeight);

    draw::blit(dst, _menu.art, 0, 0);
    auto lit = [&](int b) {
        if (b >= 0 && b < _menu.buttons.frameCount())
            _menu.buttons.drawFrame(b, dst);
    };
    lit(kButtonEasy + _arcadeDifficulty);
    if (_autosave)
        lit(kButtonAutosave);
    if (_menu.hover >= 0)
        lit(_menu.hover);

    uint8_t grey = nearest(pal, 40, 40, 40);
    uint8_t white = nearest(pal, 63, 63, 63);
    uint8_t black = nearest(pal, 0, 0, 0);
    if (_menu.infoValid) {
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++) {
                uint8_t c = _menu.thumbnail[y * 320 + x];
                *draw::at(dst, kThumbX + x, kThumbY + y) = savePalette ? c : _menu.thumbnailMap[c];
            }
    }
    std::string text = _menu.infoValid ? _menu.description : std::string();
    if (_menu.mode == GameMenu::kNameEntry) {
        bool blinkOn = (_millis / 200) % 2 == 0;
        text = _menu.typed ? _menu.name + (blinkOn ? "_" : "") : (blinkOn ? "Type a name for your saved game!!!" : "");
    }
    if (!text.empty() || _menu.mode == GameMenu::kNameEntry) {
        draw::fillRect(dst, kTextBar.left, kTextBar.top, kTextBar.width(), kTextBar.height(), black);
        if (!_font.empty())
            _font.drawString(dst, kTextX, kTextY, text, grey, black);
    }

    if (_menu.mode == GameMenu::kConfirm) {
        draw::fillRect(dst, kConfirmBox.left, kConfirmBox.top, kConfirmBox.width(), kConfirmBox.height(), black);
        draw::frameRect(dst, kConfirmBox.left, kConfirmBox.top, kConfirmBox.width(), kConfirmBox.height(), grey);
        if (!_font.empty()) {
            _font.drawString(dst, 162, 220, _menu.confirmText, white, black);
            if (!_vm->getOptions().original)
                _font.drawString(dst, 162, 256, "Right button: yes     Left button: no", grey, black);
        }
    }
}

void Game::handleMenuEvent(const InputEvent &event) {
    switch (event.type) {
    case InputEvent::kQuit:
        _quit = true;
        break;
    case InputEvent::kMouseMove: {
        _mouse = event.mouse;
        Point p = cursorHitPoint(false);
        int hover = _menu.mode == GameMenu::kMain ? menuButtonAt(p.x, p.y) : -1;
        if (hover != _menu.hover) {
            _menu.hover = hover;
            drawMenuScreen();
        }
        break;
    }
    case InputEvent::kLeftDown:
    case InputEvent::kRightDown: {
        _mouse = event.mouse;
        bool right = event.type == InputEvent::kRightDown;
        if (_menu.mode == GameMenu::kConfirm) {
            menuConfirm(right); // right button: yes, left: no
        } else if (_menu.mode == GameMenu::kMain && !right) {
            Point p = cursorHitPoint(false);
            int b = menuButtonAt(p.x, p.y);
            if (b >= 0)
                menuButton(b);
        }
        break;
    }
    case InputEvent::kKeyDown: {
        uint32_t k = event.key;
        if (_menu.mode == GameMenu::kConfirm) {
            // Keys are an added convenience: the original only takes the mouse.
            if (_vm->getOptions().original)
                break;
            if (k == 'y' || k == key::kReturn)
                menuConfirm(true);
            else if (k == 'n' || k == key::kEscape)
                menuConfirm(false);
        } else if (_menu.mode == GameMenu::kNameEntry) {
            if (k == key::kReturn || k == key::kEscape) {
                finishNameEntry();
            } else if (k == key::kBackspace) {
                if (!_menu.name.empty())
                    _menu.name.pop_back();
                _menu.typed = true;
            } else if (event.ascii >= 32 && event.ascii < 127) {
                std::string next = _menu.name + char(event.ascii);
                if (next.size() <= kMaxNameLength && (_font.empty() || _font.getStringWidth(next) < kMaxNameWidth))
                    _menu.name = next;
                _menu.typed = true;
            }
            drawMenuScreen();
        } else if (k == key::kEscape && !_menu.first && !_dead && !_vm->getOptions().original) {
            menuButton(kButtonResume);
        }
        break;
    }
    default:
        break;
    }
}

void Game::menuButton(int button) {
    bool canResume = !_menu.first && !_dead;
    auto confirm = [&](const char *text) {
        _menu.mode = GameMenu::kConfirm;
        _menu.confirmButton = button;
        _menu.confirmText = text;
        drawMenuScreen();
    };
    int n = int(_menu.saves.size());
    switch (button) {
    case kButtonNew:
        confirm("Start a new game?");
        return;
    case kButtonSave:
        if (!canResume)
            return;
        _menu.mode = GameMenu::kNameEntry;
        _system->setTextInput(_menu.mode == GameMenu::kNameEntry); // typed names arrive as text
        _menu.name.clear();
        _menu.typed = false;
        break;
    case kButtonLoad:
        if (n)
            confirm("Load this saved game?");
        return;
    case kButtonQuit:
        confirm("Quit the game?");
        return;
    case kButtonResume:
        if (!canResume)
            return;
        closeMenu();
        _palette = _menuGamePalette;
        applyPalette();
        draw::blit(_screen, _menuGameScreen, 0, 0);
        draw::blit(_cleanScreen, _menuGameScreen, 0, 0);
        return;
    case kButtonDelete:
        if (n)
            confirm("Delete saved game?");
        return;
    case kButtonEasy:
    case kButtonEasy + 1:
    case kButtonEasy + 2:
        _arcadeDifficulty = button - kButtonEasy;
        break;
    case kButtonAutosave:
        _autosave = !_autosave;
        break;
    case kButtonCredits:
        startCredits(false, true);
        return;
    case kButtonPrev:
        selectSave(_menu.selected - 1);
        break;
    case kButtonNext:
        selectSave(_menu.selected + 1);
        break;
    case kButtonPrev10:
        selectSave(_menu.selected - 10);
        break;
    case kButtonNext10:
        selectSave(_menu.selected + 10);
        break;
    default:
        break;
    }
    drawMenuScreen();
}

void Game::menuConfirm(bool yes) {
    _menu.mode = GameMenu::kMain;
    _system->setTextInput(_menu.mode == GameMenu::kNameEntry); // typed names arrive as text
    int button = _menu.confirmButton;
    _menu.confirmButton = -1;
    if (!yes) {
        drawMenuScreen();
        return;
    }
    switch (button) {
    case kButtonNew:
        closeMenu();
        startNewGame();
        return;
    case kButtonLoad: {
        std::string file = _menu.saves.empty() ? std::string() : _menu.saves[_menu.selected];
        closeMenu();
        if (!loadGame(file))
            openMenu(_menu.first);
        return;
    }
    case kButtonQuit:
        closeMenu();
        _quit = true;
        return;
    case kButtonDelete:
        if (!_menu.saves.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(getSaveDirectory()) / _menu.saves[_menu.selected], ec);
            scanSaves();
            selectSave(_menu.selected);
        }
        break;
    default:
        break;
    }
    drawMenuScreen();
}

void Game::finishNameEntry() {
    // The file gets the first free number; Esc does not cancel (as in the game).
    _menu.mode = GameMenu::kMain;
    _system->setTextInput(_menu.mode == GameMenu::kNameEntry); // typed names arrive as text
    std::string dir = getSaveDirectory();
    std::string file;
    for (int i = 0; i < 100000000; i++) {
        file = text::format("%08d.ds9", i);
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(dir) / file, ec))
            break;
    }
    Palette menuPalette = _palette;
    _palette = _menuGamePalette; // saved with the game's palette
    bool ok = saveGame(file, _menu.name, getResumePc());
    _palette = menuPalette;
    scanSaves();
    if (ok) {
        auto it = std::find(_menu.saves.begin(), _menu.saves.end(), file);
        selectSave(int(it - _menu.saves.begin()));
    }
    drawMenuScreen();
}

void Game::typeSaveName(const std::string &name) {
    _menu.name = name;
    _menu.typed = true;
    finishNameEntry();
}

} // namespace reluva::stharbinger
