// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/load_save_screen.h"

#include "common/text.h"

#include <algorithm>
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

// The slots' text, and the chosen slot's, measured from screenshots of the
// game: not their fonts' colours.
constexpr Color kTextColor{0, 166, 156};
constexpr Color kChosenColor{239, 182, 198};
// Saving, the cursor after the description: a white block 8 pixels wide,
// as high as the font's glyphs (a screenshot).
constexpr int kCursorWidth = 8;

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
    _newDescription = description;
    _description.clear();
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
                // Saving over a game keeps its description, to be typed
                // over (the game does); an empty slot takes the new one's.
                const std::optional<SaveSummary> &save = _slots[size_t(i)];
                _description = save ? save->description : _newDescription;
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
        // The name field shows the slot's file, as the game names it; the
        // chosen slot's text is pink.
        const Color color = i == _chosen ? kChosenColor : kTextColor;
        drawText(screen, name, "game" + std::to_string(i) + ".sav", color);
        drawText(screen, description, saving ? _description : save->description, color);
        if (saving && _cursorShown)
            drawCursor(screen, description);
    }
}

void LoadSaveScreen::drawCursor(Surface &screen, int32_t box) const {
    const Placed *p = find(box);
    const Font *font = p && !p->widget->fonts.empty() ? _assets.font(p->widget->fonts[0].file) : nullptr;
    if (!font)
        return;
    const Widget &w = *p->widget;
    // Where the text's glyphs are: after its last one, from their top.
    const int x0 = p->x + w.textX + font->measure(_description);
    const int y0 = p->y + w.textY + (w.textH - font->lineHeight()) / 2 + 1;
    for (int y = std::max(y0, 0); y < std::min(y0 + font->glyphHeight(), screen.h); ++y)
        for (int x = std::max(x0, 0); x < std::min(x0 + kCursorWidth, screen.w); ++x) {
            uint8_t *px = screen.row(y) + size_t(x) * 4;
            px[0] = px[1] = px[2] = px[3] = 255;
        }
}

} // namespace reluva::botf
