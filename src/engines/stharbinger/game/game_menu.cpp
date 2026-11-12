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

bool isSaveFile(const std::string& name) {
    return name.size() > 4 && text::equalsIgnoreCase(name.substr(name.size() - 4), ".ds9");
}

uint8_t nearest(const Palette& pal, int r, int g, int b) {
    return uint8_t(pal.findBestColor(uint8_t(r * 255 / 63), uint8_t(g * 255 / 63), uint8_t(b * 255 / 63)));
}

} // namespace

void Game::requestMenu() {
    // Right click (or Esc) while the map is allowed and the script is idle.
    if (mapEnabled_ && !thread_.active && pendingScene_ < 0)
        openMenu(false);
}

void Game::openMenu(bool first) {
    draw::create(menuGameScreen_, kScreenWidth, kScreenHeight);
    if (!draw::empty(cleanScreen_))
        draw::blit(menuGameScreen_, cleanScreen_, 0, 0);
    menuGamePalette_ = palette_;
    menu_.active = true;
    menu_.first = first;
    loadFont(); // for the confirmations and the save name, before any conversation
    menu_.mode = GameMenu::kMain;
    system_->setTextInput(menu_.mode == GameMenu::kNameEntry); // typed names arrive as text
    if (draw::empty(menu_.art)) {
        bool loaded = false;
        if (!resources_.loadBackground("SETUP", menu_.art, menu_.artPalette, loaded)) {
            log::warning("art\\s\\e\\setup.cmp not found: the menu has no picture");
            draw::create(menu_.art, kScreenWidth, kScreenHeight, 0);
            for (int i = 0; i < 256; i++)
                menu_.artPalette.setColor(i, uint8_t(i), uint8_t(i), uint8_t(i));
        }
        if (auto s = resources_.openFile("ART/BUTTONS.RLE")) {
            uint8_t header[8];
            menu_.buttons.loadWithHeader(*s, header);
        }
    }
    if (system_->getMixer() && conv_.speechHandle != audio::kNoSound)
        system_->getMixer()->stop(conv_.speechHandle);
    scanSaves();
    selectSave(lastSave_);
    drawMenuScreen();
    log::debug(1, "Game menu%s", first ? " (start)" : "");
}

void Game::closeMenu() {
    menu_.active = false;
    system_->setTextInput(false);
    lastSave_ = menu_.selected;
    saveSettings();
}

void Game::scanSaves() {
    menu_.saves.clear();
    std::string dir = getSaveDirectory();
    if (dir.empty())
        return;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = it->path().filename().string();
        if (isSaveFile(f))
            menu_.saves.push_back(f);
    }
    std::sort(menu_.saves.begin(), menu_.saves.end());
}

void Game::selectSave(int index) {
    int n = int(menu_.saves.size());
    menu_.selected = n ? std::clamp(index, 0, n - 1) : 0;
    menu_.infoValid = false;
    if (!n)
        return;
    SaveInfo info;
    if (readSaveInfo((std::filesystem::path(getSaveDirectory()) / menu_.saves[menu_.selected]).string(), info)) {
        menu_.infoValid = true;
        menu_.description = info.description;
        menu_.thumbnail.assign(320 * 240, 0);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++)
                menu_.thumbnail[y * 320 + x] = info.screenshot[(y * 2) * 640 + x * 2];
        for (int i = 0; i < 256; i++)
            menu_.savePalette.setColor(i, info.palette[i * 3], info.palette[i * 3 + 1], info.palette[i * 3 + 2]);
        // The thumbnail's colours in the menu's own palette (when it keeps it).
        for (int i = 0; i < 256; i++)
            menu_.thumbnailMap[i] =
                uint8_t(menu_.artPalette.findBestColor(info.palette[i * 3], info.palette[i * 3 + 1], info.palette[i * 3 + 2]));
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
    bool savePalette = menu_.infoValid && vm_->getOptions().original;
    const Palette& pal = savePalette ? menu_.savePalette : menu_.artPalette;
    palette_ = pal;
    fadeLevel_ = 256;
    applyPalette();

    // Drawn into its own surface; each step copies it to the screen under the cursor.
    Surface& dst = menu_.frame;
    draw::create(dst, kScreenWidth, kScreenHeight);

    draw::blit(dst, menu_.art, 0, 0);
    auto lit = [&](int b) {
        if (b >= 0 && b < menu_.buttons.frameCount())
            menu_.buttons.drawFrame(b, dst);
    };
    lit(kButtonEasy + arcadeDifficulty_);
    if (autosave_)
        lit(kButtonAutosave);
    if (menu_.hover >= 0)
        lit(menu_.hover);

    uint8_t grey = nearest(pal, 40, 40, 40), white = nearest(pal, 63, 63, 63), black = nearest(pal, 0, 0, 0);
    if (menu_.infoValid) {
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++) {
                uint8_t c = menu_.thumbnail[y * 320 + x];
                *draw::at(dst, kThumbX + x, kThumbY + y) = savePalette ? c : menu_.thumbnailMap[c];
            }
    }
    std::string text = menu_.infoValid ? menu_.description : std::string();
    if (menu_.mode == GameMenu::kNameEntry) {
        bool blinkOn = (millis_ / 200) % 2 == 0;
        text = menu_.typed ? menu_.name + (blinkOn ? "_" : "") : (blinkOn ? "Type a name for your saved game!!!" : "");
    }
    if (!text.empty() || menu_.mode == GameMenu::kNameEntry) {
        draw::fillRect(dst, kTextBar.left, kTextBar.top, kTextBar.width(), kTextBar.height(), black);
        if (!font_.empty())
            font_.drawString(dst, kTextX, kTextY, text, grey, black);
    }

    if (menu_.mode == GameMenu::kConfirm) {
        draw::fillRect(dst, kConfirmBox.left, kConfirmBox.top, kConfirmBox.width(), kConfirmBox.height(), black);
        draw::frameRect(dst, kConfirmBox.left, kConfirmBox.top, kConfirmBox.width(), kConfirmBox.height(), grey);
        if (!font_.empty()) {
            font_.drawString(dst, 162, 220, menu_.confirmText, white, black);
            if (!vm_->getOptions().original)
                font_.drawString(dst, 162, 256, "Right button: yes     Left button: no", grey, black);
        }
    }
}

void Game::handleMenuEvent(const InputEvent& event) {
    switch (event.type) {
    case InputEvent::kQuit: quit_ = true; break;
    case InputEvent::kMouseMove: {
        mouse_ = event.mouse;
        Point p = cursorHitPoint(false);
        int hover = menu_.mode == GameMenu::kMain ? menuButtonAt(p.x, p.y) : -1;
        if (hover != menu_.hover) {
            menu_.hover = hover;
            drawMenuScreen();
        }
        break;
    }
    case InputEvent::kLeftDown:
    case InputEvent::kRightDown: {
        mouse_ = event.mouse;
        bool right = event.type == InputEvent::kRightDown;
        if (menu_.mode == GameMenu::kConfirm) {
            menuConfirm(right); // right button: yes, left: no
        } else if (menu_.mode == GameMenu::kMain && !right) {
            Point p = cursorHitPoint(false);
            int b = menuButtonAt(p.x, p.y);
            if (b >= 0)
                menuButton(b);
        }
        break;
    }
    case InputEvent::kKeyDown: {
        uint32_t k = event.key;
        if (menu_.mode == GameMenu::kConfirm) {
            // Keys are an added convenience: the original only takes the mouse.
            if (vm_->getOptions().original)
                break;
            if (k == 'y' || k == key::Return)
                menuConfirm(true);
            else if (k == 'n' || k == key::Escape)
                menuConfirm(false);
        } else if (menu_.mode == GameMenu::kNameEntry) {
            if (k == key::Return || k == key::Escape) {
                finishNameEntry();
            } else if (k == key::Backspace) {
                if (!menu_.name.empty())
                    menu_.name.pop_back();
                menu_.typed = true;
            } else if (event.ascii >= 32 && event.ascii < 127) {
                std::string next = menu_.name + char(event.ascii);
                if (next.size() <= kMaxNameLength && (font_.empty() || font_.getStringWidth(next) < kMaxNameWidth))
                    menu_.name = next;
                menu_.typed = true;
            }
            drawMenuScreen();
        } else if (k == key::Escape && !menu_.first && !dead_ && !vm_->getOptions().original) {
            menuButton(kButtonResume);
        }
        break;
    }
    default: break;
    }
}

void Game::menuButton(int button) {
    bool canResume = !menu_.first && !dead_;
    auto confirm = [&](const char* text) {
        menu_.mode = GameMenu::kConfirm;
        menu_.confirmButton = button;
        menu_.confirmText = text;
        drawMenuScreen();
    };
    int n = int(menu_.saves.size());
    switch (button) {
    case kButtonNew: confirm("Start a new game?"); return;
    case kButtonSave:
        if (!canResume)
            return;
        menu_.mode = GameMenu::kNameEntry;
        system_->setTextInput(menu_.mode == GameMenu::kNameEntry); // typed names arrive as text
        menu_.name.clear();
        menu_.typed = false;
        break;
    case kButtonLoad:
        if (n)
            confirm("Load this saved game?");
        return;
    case kButtonQuit: confirm("Quit the game?"); return;
    case kButtonResume:
        if (!canResume)
            return;
        closeMenu();
        palette_ = menuGamePalette_;
        applyPalette();
        draw::blit(screen_, menuGameScreen_, 0, 0);
        draw::blit(cleanScreen_, menuGameScreen_, 0, 0);
        return;
    case kButtonDelete:
        if (n)
            confirm("Delete saved game?");
        return;
    case kButtonEasy:
    case kButtonEasy + 1:
    case kButtonEasy + 2: arcadeDifficulty_ = button - kButtonEasy; break;
    case kButtonAutosave: autosave_ = !autosave_; break;
    case kButtonCredits: startCredits(false, true); return;
    case kButtonPrev: selectSave(menu_.selected - 1); break;
    case kButtonNext: selectSave(menu_.selected + 1); break;
    case kButtonPrev10: selectSave(menu_.selected - 10); break;
    case kButtonNext10: selectSave(menu_.selected + 10); break;
    default: break;
    }
    drawMenuScreen();
}

void Game::menuConfirm(bool yes) {
    menu_.mode = GameMenu::kMain;
    system_->setTextInput(menu_.mode == GameMenu::kNameEntry); // typed names arrive as text
    int button = menu_.confirmButton;
    menu_.confirmButton = -1;
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
        std::string file = menu_.saves.empty() ? std::string() : menu_.saves[menu_.selected];
        closeMenu();
        if (!loadGame(file))
            openMenu(menu_.first);
        return;
    }
    case kButtonQuit:
        closeMenu();
        quit_ = true;
        return;
    case kButtonDelete:
        if (!menu_.saves.empty()) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(getSaveDirectory()) / menu_.saves[menu_.selected], ec);
            scanSaves();
            selectSave(menu_.selected);
        }
        break;
    default: break;
    }
    drawMenuScreen();
}

void Game::finishNameEntry() {
    // The file gets the first free number; Esc does not cancel (as in the game).
    menu_.mode = GameMenu::kMain;
    system_->setTextInput(menu_.mode == GameMenu::kNameEntry); // typed names arrive as text
    std::string dir = getSaveDirectory();
    std::string file;
    for (int i = 0; i < 100000000; i++) {
        file = text::format("%08d.ds9", i);
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(dir) / file, ec))
            break;
    }
    Palette menuPalette = palette_;
    palette_ = menuGamePalette_; // saved with the game's palette
    bool ok = saveGame(file, menu_.name, getResumePc());
    palette_ = menuPalette;
    scanSaves();
    if (ok) {
        auto it = std::find(menu_.saves.begin(), menu_.saves.end(), file);
        selectSave(int(it - menu_.saves.begin()));
    }
    drawMenuScreen();
}

void Game::typeSaveName(const std::string& name) {
    menu_.name = name;
    menu_.typed = true;
    finishNameEntry();
}

} // namespace reluva::stharbinger
