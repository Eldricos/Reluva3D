// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/load_save_screen.h"

#include <algorithm>
#include <string>
#include <utility>

namespace reluva::botf {

namespace {

// loadsave.wdf's widgets. The slots are top down; each is a button the
// width of the list, with a name field and a description field on it.
constexpr int32_t kAccept = 9503;
constexpr int32_t kCancel = 9504;
constexpr int32_t kFirstSlot = 9507;        // 9507, 9509 ... 9529
constexpr int32_t kFirstName = 9506;        // 9506, 9508 ... 9528
constexpr int32_t kFirstDescription = 9530; // 9530, 9532 ... 9552
// The title along the top: the layout names ls_svlb.tga ("SAVE GAME"); the
// game shows ls_ldlb.tga ("LOAD GAME") there when loading.
constexpr int32_t kTitle = 9505;
constexpr const char *kLoadTitle = "ls_ldlb.tga";

// The slots' text in each empire's colour, Cardassians to Romulans, not
// their fonts': measured from a screenshot of Load Game with a save of each.
constexpr Color kEmpireColors[] = {{0, 134, 165}, {99, 77, 181}, {214, 32, 66}, {255, 24, 33}, {0, 166, 156}};
// The chosen slot's, the empire's lighter colour: the Romulans' from
// screenshots, the others' from a recording, to within about ten.
constexpr Color kChosenColors[] = {{255, 219, 156}, {255, 154, 41}, {189, 199, 90}, {214, 178, 255}, {239, 182, 198}};
constexpr int kRomulans = 4; // the colours for a save whose empire isn't known

Color empireColor(int empire, bool chosen) {
    const int e = empire >= 0 && empire < 5 ? empire : kRomulans;
    return chosen ? kChosenColors[e] : kEmpireColors[e];
}

int32_t slotButton(int slot) {
    return kFirstSlot + 2 * slot;
}

} // namespace

std::string saveDescription(const Lexicon &lexicon, int empire, int turn, int galaxySize, int galaxyShape) {
    static const std::pair<int, const char *> kEmpires[] = {{lex::kCardassians, "Cardassians"},
                                                            {lex::kFederation, "Federation"},
                                                            {lex::kFerengi, "Ferengi"},
                                                            {lex::kKlingons, "Klingons"},
                                                            {lex::kRomulans, "Romulans"}};
    static const std::pair<int, const char *> kSizes[] = {
        {lex::kSmall, "Small"}, {lex::kMedium, "Medium"}, {lex::kLarge, "Large"}};
    static const std::pair<int, const char *> kShapes[] = {{lex::kIrregular, "Irregular"},
                                                           {lex::kElliptical, "Elliptical"},
                                                           {lex::kRing, "Ring"},
                                                           {lex::kSpiral, "Spiral"}};
    auto word = [&](const std::pair<int, const char *> &e) {
        const std::string &w = lexicon[size_t(e.first)];
        return w.empty() ? std::string(e.second) : w;
    };
    return word(kEmpires[std::clamp(empire, 0, 4)]) + " " + std::to_string(turn) + " " +
           word(kSizes[std::clamp(galaxySize, 0, 2)]) + " " + word(kShapes[std::clamp(galaxyShape, 0, 3)]) + " " +
           word({lex::kGalaxy, "Galaxy"});
}

Error LoadSaveScreen::load() {
    return loadLayouts("loadsave.wdf",
                       {kAccept, kCancel, slotButton(0), slotButton(kSlots - 1), kFirstName,
                        kFirstName + 2 * (kSlots - 1), kFirstDescription, kFirstDescription + 2 * (kSlots - 1)});
}

void LoadSaveScreen::open(const Slots &slots, Mode mode, const std::string &description, int empire) {
    _slots = slots;
    _mode = mode;
    _newDescription = description;
    _newEmpire = empire;
    _description.clear();
    _chosen = -1;
    _pressed = 0;
    _disabled.clear();
    // Accept is dimmed until a slot is chosen, as in the game; empty slots
    // aren't dimmed.
    _disabled.insert(kAccept);
}

LoadSaveScreen::Result LoadSaveScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return _chosen >= 0 ? Result::kAccepted : Result::kOpen;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (_mode == Mode::kSave && _chosen >= 0) {
        if (e.type == EventType::kTextInput) {
            typeInto(_description, kFirstDescription, e.text, kMaxDescription);
            return Result::kOpen;
        }
        if (e.type == EventType::kKeyDown && e.key == key::kBackspace) {
            eraseLast(_description);
            return Result::kOpen;
        }
    }
    if (e.button != MouseButton::kLeft)
        return Result::kOpen;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
    } else if (e.type == EventType::kMouseUp) {
        // A button acts when the mouse comes up on it, as Windows buttons do.
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kAccept && _chosen >= 0)
            return Result::kAccepted;
        if (id == kCancel)
            return Result::kCancelled;
        for (int i = 0; i < kSlots; ++i)
            if (id == slotButton(i) && (_mode == Mode::kSave || _slots[size_t(i)])) {
                _chosen = i;
                _disabled.erase(kAccept);
                // Saving over a game keeps its description, to be typed
                // over; an empty slot takes the new one's (the game does
                // both).
                const std::optional<SaveSummary> &save = _slots[size_t(i)];
                _description = save ? save->description : _newDescription;
                // A click and a spoken line, each mode its own.
                sound(UiSound::kClick);
                sound(_mode == Mode::kLoad ? UiSound::kGameChosen : UiSound::kSlotChosen);
            }
    }
    return Result::kOpen;
}

void LoadSaveScreen::draw(Surface &screen) const {
    // The manual: "click on its slot and the selected game will light up".
    std::set<int32_t> lit;
    if (_chosen >= 0)
        lit.insert(slotButton(_chosen));
    drawLayouts(screen, lit);
    if (_mode == Mode::kLoad)
        drawPicture(screen, kTitle, kLoadTitle);
    for (int i = 0; i < kSlots; ++i) {
        const std::optional<SaveSummary> &save = _slots[size_t(i)];
        // Saving, the chosen slot shows the game to save there.
        const bool saving = _mode == Mode::kSave && i == _chosen;
        if (!save && !saving)
            continue;
        const int32_t name = kFirstName + 2 * i;
        const int32_t description = kFirstDescription + 2 * i;
        // The name field shows the slot's file, as the game names it. The
        // text is in the colours of the saved game's empire, an empty slot's
        // the game to save's, even when saving over another empire's game.
        const Color color = empireColor(save ? save->empire : _newEmpire, i == _chosen);
        drawText(screen, name, "game" + std::to_string(i) + ".sav", color);
        drawText(screen, description, saving ? _description : save->description, color);
        if (saving && _cursorShown)
            drawCursor(screen, description, _description);
    }
}

} // namespace reluva::botf
