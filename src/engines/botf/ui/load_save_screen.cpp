// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/load_save_screen.h"

#include "common/text.h"

#include <string>
#include <string_view>

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

// The slots' text, measured from a screenshot of the game: not their
// fonts' colours.
constexpr Color kTextColor{0, 166, 156};

int32_t slotButton(int slot) {
    return kFirstSlot + 2 * slot;
}

size_t characters(std::string_view utf8) {
    size_t n = 0;
    for (size_t i = 0; i < utf8.size(); ++n)
        text::nextUtf8(utf8, i);
    return n;
}

} // namespace

Error LoadSaveScreen::load() {
    return loadLayouts("loadsave.wdf",
                       {kAccept, kCancel, slotButton(0), slotButton(kSlots - 1), kFirstName,
                        kFirstName + 2 * (kSlots - 1), kFirstDescription, kFirstDescription + 2 * (kSlots - 1)});
}

void LoadSaveScreen::open(const Slots &slots, Mode mode, const std::string &description) {
    _slots = slots;
    _mode = mode;
    _description = description;
    _chosen = -1;
    _pressed = 0;
    _disabled.clear();
    // Accept is dimmed until a slot is chosen, as in the game; empty slots
    // aren't dimmed.
    _disabled.insert(kAccept);
}

void LoadSaveScreen::type(const char *utf8) {
    const Placed *box = find(kFirstDescription);
    const Font *font = box && !box->widget->fonts.empty() ? _assets.font(box->widget->fonts[0].file) : nullptr;
    if (!font)
        return;
    const std::string_view typed(utf8);
    for (size_t i = 0; i < typed.size();) {
        const size_t start = i;
        const uint32_t c = text::nextUtf8(typed, i);
        // Only what the font can draw (and so what gameInfo's Windows-1252
        // can hold), as long as it fits the field.
        if (c != ' ' && !font->glyph(char32_t(c)))
            continue;
        const std::string longer = _description + std::string(typed.substr(start, i - start));
        if (characters(longer) <= kMaxDescription && font->measure(longer) <= box->widget->textW)
            _description = longer;
    }
}

void LoadSaveScreen::erase() {
    size_t last = 0;
    for (size_t i = 0; i < _description.size();) {
        last = i;
        text::nextUtf8(_description, i);
    }
    _description.resize(last);
}

LoadSaveScreen::Result LoadSaveScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return _chosen >= 0 ? Result::kAccepted : Result::kOpen;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (_mode == Mode::kSave && _chosen >= 0) {
        if (e.type == EventType::kTextInput) {
            type(e.text);
            return Result::kOpen;
        }
        if (e.type == EventType::kKeyDown && e.key == key::kBackspace) {
            erase();
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
        // The name field shows the slot's file, as the game names it.
        drawText(screen, name, "game" + std::to_string(i) + ".sav", kTextColor);
        drawText(screen, description, saving ? _description : save->description, kTextColor);
    }
}

} // namespace reluva::botf
