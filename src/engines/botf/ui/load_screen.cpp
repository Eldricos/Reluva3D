// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/load_screen.h"

namespace reluva::botf {

namespace {

// loadsave.wdf's widgets. The slots are top down; each is a button the
// width of the list, with a name field and a description field on it.
constexpr int32_t kAccept = 9503;
constexpr int32_t kCancel = 9504;
constexpr int32_t kFirstSlot = 9507;        // 9507, 9509 ... 9529
constexpr int32_t kFirstName = 9506;        // 9506, 9508 ... 9528
constexpr int32_t kFirstDescription = 9530; // 9530, 9532 ... 9552

int32_t slotButton(int slot) {
    return kFirstSlot + 2 * slot;
}

} // namespace

Error LoadScreen::load() {
    return loadLayouts("loadsave.wdf",
                       {kAccept, kCancel, slotButton(0), slotButton(kSlots - 1), kFirstName,
                        kFirstName + 2 * (kSlots - 1), kFirstDescription, kFirstDescription + 2 * (kSlots - 1)});
}

void LoadScreen::open(const Slots &slots) {
    _slots = slots;
    _chosen = -1;
    _pressed = 0;
    _disabled.clear();
    for (int i = 0; i < kSlots; ++i)
        if (!_slots[size_t(i)])
            _disabled.insert(slotButton(i));
}

LoadScreen::Result LoadScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return _chosen >= 0 ? Result::kAccepted : Result::kOpen;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
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
            if (id == slotButton(i))
                _chosen = i;
    }
    return Result::kOpen;
}

void LoadScreen::draw(Surface &screen) const {
    // The manual: "click on its slot and the selected game will light up".
    std::set<int32_t> lit;
    if (_chosen >= 0)
        lit.insert(slotButton(_chosen));
    drawLayouts(screen, lit);
    for (int i = 0; i < kSlots; ++i) {
        const std::optional<SaveSummary> &save = _slots[size_t(i)];
        if (!save)
            continue;
        const int32_t name = kFirstName + 2 * i;
        const int32_t description = kFirstDescription + 2 * i;
        drawText(screen, name, save->name, fontColor(name));
        drawText(screen, description, save->description, fontColor(description));
    }
}

} // namespace reluva::botf
