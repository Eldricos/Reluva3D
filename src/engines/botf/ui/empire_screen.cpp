// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/empire_screen.h"

#include "engines/botf/ui/setup_screen.h"

#include <string>

namespace reluva::botf {

namespace {

// emppick.wdf's widgets, per empire in race.rst's order (Cardassians,
// Federation, Ferengi, Klingons, Romulans), matched by their pictures (the
// race letter: c, h, f, k, r) and their places on the screen.
constexpr int32_t kAccept = 9110;
constexpr int32_t kCancel = 9111;
constexpr std::array<int32_t, 5> kNames = {9105, 9106, 9107, 9108, 9109};   // buttons, kind 2
constexpr std::array<int32_t, 5> kEmblems = {9100, 9101, 9102, 9103, 9104}; // pictures
constexpr std::array<int32_t, 5> kLevels = {9114, 9115, 9116, 9117, 9118};  // text boxes
constexpr int32_t kDescription = 9112;
// Not shown in a single-player game: a small button with a number beside
// each empire (multiplayer, presumably), and a 250x350 picture under the
// description.
constexpr std::array<int32_t, 11> kHidden = {9124, 9125, 9126, 9127, 9128, 9119, 9120, 9121, 9122, 9123, 9113};
const char *const kEmblemPictures[] = {"es_cicn0.tga", "es_hicn0.tga", "es_ficn0.tga", "es_kicn0.tga", "es_ricn0.tga"};

// The descriptions: lexicon.dic's entries 270-274, found by their text
// (the recording shows the Federation's), in the lexicon's order:
// Cardassians, Ferengi, Federation, Klingons, Romulans.
constexpr std::array<int, 5> kDescriptions = {270, 272, 271, 273, 274};

// Colours measured from the recording, as on the setup screen.
constexpr Color kLevelColor{120, 180, 150};
constexpr Color kDescriptionColor{133, 115, 75};
constexpr int kDescriptionLineGap = 2; // 23 pixels a line, human20's height 21

} // namespace

Error EmpireScreen::load() {
    if (Error err = loadLayouts(
            "emppick.wdf", {kAccept, kCancel, kNames[0], kNames[1], kNames[2], kNames[3], kNames[4], kDescription}))
        return err;
    _hidden = std::set<int32_t>(kHidden.begin(), kHidden.end());
    return Error::success();
}

int EmpireScreen::empireAt(int x, int y) const {
    for (int i = 0; i < 5; ++i)
        for (int32_t id : {kNames[size_t(i)], kEmblems[size_t(i)]})
            if (const Placed *p = find(id); p && p->contains(x, y))
                return i;
    return -1;
}

EmpireScreen::Result EmpireScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return _chosen >= 0 ? Result::kAccepted : Result::kOpen;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (e.type == EventType::kMouseMove || e.type == EventType::kMouseDown || e.type == EventType::kMouseUp)
        _pointed = empireAt(e.mouseX, e.mouseY);
    if (e.button != MouseButton::kLeft)
        return Result::kOpen;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
        // Choosing an empire happens on the press, like a radio button; its
        // emblem chooses it too.
        if (const int empire = empireAt(e.mouseX, e.mouseY); empire >= 0)
            _chosen = empire;
    } else if (e.type == EventType::kMouseUp) {
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kAccept && _chosen >= 0)
            return Result::kAccepted;
        if (id == kCancel)
            return Result::kCancelled;
    }
    return Result::kOpen;
}

void EmpireScreen::draw(Surface &screen) {
    std::set<int32_t> selected;
    if (_chosen >= 0)
        selected.insert(kNames[size_t(_chosen)]);
    drawLayouts(screen, selected);
    for (size_t i = 0; i < 5; ++i) {
        drawPicture(screen, kEmblems[i], kEmblemPictures[i]);
        drawText(screen, kLevels[i], civilizationLevel(_lexicon, _levels[i]), kLevelColor);
    }
    // The empire pointed at, or else the one chosen.
    const int shown = _pointed >= 0 ? _pointed : _chosen;
    if (shown >= 0)
        drawText(screen, kDescription, _lexicon[size_t(kDescriptions[size_t(shown)])], kDescriptionColor,
                 kDescriptionLineGap);
}

} // namespace reluva::botf
