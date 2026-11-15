// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// And Then There Were None's puzzle screens (a scene's PUZZ record) that
// Orient Express hasn't, run by ScenePage's puzzle hooks: the library's
// bookcase (ac.exe 0x4081a0) and the safe room's deposit boxes (0x408de0).
// See docs/engines/awe/games/attwn/systems/puzzles.md.

#include "engines/awe/attwn/puzzles.h"

#include "engines/awe/game/scene_page.h"
#include "engines/awe/game/script.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace reluva::awe {

using attwn::DepositBox;

namespace {

// The books by their number in the tags bookshelf1..3, and so the shelf
// each goes on (0x408470).
const char *const kBooks[4] = {nullptr, "booknavpan", "booksafepass", "booksailflo"};
// A shelf's book while its tag is unset (0x408270): the bookcase as found.
const int kFirstBooks[4] = {0, 2, 3, 1};

// The deposit boxes' grid (0x4096c0, 0x4091f0): 4 by 3, a drawer 133 by
// 139.
constexpr int kBoxLeft = 102;
constexpr int kBoxTop = 65;
constexpr int kBoxStepX = 154;
constexpr int kBoxStepY = 159;
// The drawers with something in them, by slot (0x4091c8).
struct Drawer {
    int slot;
    const char *item;
    const char *line;
};
constexpr Drawer kDrawers[4] = {{1, "ravenearring", "talk narracottgen 124"},
                                {4, "legaldocument", "talk narracottgen 125"},
                                {7, "microphone", "talk narracottgen 127"},
                                {10, "cardrowyourboat", "talk narracottgen 126"}};
// Solved (0x4092d0): RULE THE WAVES, any of the three Es in an E's place.
const char kAnswer[] = "rulethewaves";

int tagValue(const GameState &state, const std::string &name, int unset) {
    const auto it = state.tags.find(name);
    return it == state.tags.end() ? unset : it->second;
}

} // namespace

// --- The bookcase ------------------------------------------------------------------------

void ScenePage::bookcaseAction(const std::string &word, const std::string &arg) {
    // Its shelves' hotspots take a book down ("action takebook<n>") or put
    // one up ("action placebook<n> <book>"); the tags are the bookcase.
    if (word.size() < 9)
        return;
    const int shelf = word.back() - '0';
    if (shelf < 1 || shelf > 3)
        return;
    const std::string tag = "bookshelf" + std::to_string(shelf);
    if (word.find("takebook") != std::string::npos) {
        // 0x4081f0
        playSound("interface\\inventory");
        const int book = tagValue(_state, tag, kFirstBooks[shelf]);
        if (book >= 1 && book <= 3)
            addItem(_state, kBooks[book], 1);
        _state.tags[tag] = 0;
        retestObjects();
        return;
    }
    if (word.find("placebook") == std::string::npos)
        return;
    // 0x4082fe: the book's number on the shelf; then each book on its own
    // shelf opens the secret door, else one put on its own says so.
    int book = 0;
    for (int b = 1; b <= 3; b++)
        if (arg == kBooks[b])
            book = b;
    _state.tags[tag] = book;
    retestObjects();
    if (!_state.tags.count("bookcasesolved") && tagValue(_state, "bookshelf1", -1) == 1 &&
        tagValue(_state, "bookshelf2", -1) == 2 && tagValue(_state, "bookshelf3", -1) == 3) {
        _state.tags["bookcasesolved"] = 0;
        retestObjects();
        playSound("sfx\\bookcase");
        return;
    }
    if (book == shelf)
        _runner->run("talk narracottgen 122");
}

// --- The deposit boxes -------------------------------------------------------------------

void ScenePage::openDepositBox() {
    // 0x408de0: the letters' order and which drawers are shut, as text
    // tags; at first in alphabetical order, all shut.
    _deposit = std::make_unique<DepositBox>();
    DepositBox &d = *_deposit;
    const auto order = _state.texts.find("depositbox_order");
    std::istringstream in(order != _state.texts.end() && !order->second.empty()
                              ? order->second
                              : "a.jpg e.jpg e01.jpg e02.jpg h.jpg l.jpg r.jpg s.jpg t.jpg u.jpg v.jpg w.jpg");
    for (std::string &letter : d.letters)
        in >> letter;
    const auto shut = _state.texts.find("depositbox_visible");
    std::istringstream flags(shut != _state.texts.end() ? shut->second : "1 1 1 1 1 1 1 1 1 1 1 1");
    for (bool &c : d.closed) {
        int f = 1;
        flags >> f;
        c = f != 0;
    }
    placeDepositLetters();
}

void ScenePage::placeDepositLetters() {
    // Each letter's picture in its slot (0x4096c0), shown while its drawer
    // is shut (0x409750).
    const DepositBox &d = *_deposit;
    for (size_t i = 0; i < d.letters.size(); i++) {
        const Ac2Record *o = objectWithFile(d.letters[i]);
        if (!o)
            continue;
        PictureOverride p;
        p.x = float(kBoxLeft + kBoxStepX * int(i % 4));
        p.y = float(kBoxTop + kBoxStepY * int(i / 4));
        p.depth = o->f("depth") != 0 ? o->f("depth") : o->f("z");
        _overrides[o] = p;
        _shownNow[o] = d.closed[i];
    }
}

void ScenePage::saveDepositBox() {
    // As text tags, for the next visit and saved games (0x409600).
    const DepositBox &d = *_deposit;
    std::string order;
    std::string shut;
    for (size_t i = 0; i < d.letters.size(); i++) {
        order += (i ? " " : "") + d.letters[i];
        shut += std::string(i ? " " : "") + (d.closed[i] ? "1" : "0");
    }
    _state.texts["depositbox_order"] = order;
    _state.texts["depositbox_visible"] = shut;
}

int ScenePage::depositSlotAt(int x, int y) const {
    for (int i = 0; i < 12; i++) {
        const int l = kBoxLeft + kBoxStepX * (i % 4);
        const int t = kBoxTop + kBoxStepY * (i / 4);
        if (x >= l && x < l + 133 && y >= t && y < t + 139)
            return i;
    }
    return -1;
}

void ScenePage::depositClick(int x, int y) {
    // 0x408f90: a drawer opened or shut; one with something in it gives it
    // once; two open, they swap a moment later.
    DepositBox &d = *_deposit;
    if (_state.tags.count("depositboxsolved") || d.timer > 0)
        return;
    const int slot = depositSlotAt(x, y);
    if (slot < 0)
        return;
    if (!d.closed[size_t(slot)]) {
        playSound("sfx\\boxclose2");
        d.closed[size_t(slot)] = true;
        saveDepositBox();
        placeDepositLetters();
        return;
    }
    playSound("sfx\\boxopen1");
    d.closed[size_t(slot)] = false;
    saveDepositBox();
    placeDepositLetters();
    for (const Drawer &drawer : kDrawers)
        if (drawer.slot == slot && !_state.itemState(drawer.item)) {
            addItem(_state, drawer.item, 1);
            _runner->run(drawer.line);
        }
    if (std::count(d.closed.begin(), d.closed.end(), false) == 2)
        d.timer = 0.75f;
}

void ScenePage::depositUpdate(float seconds) {
    // 0x408f30: the two open drawers swap once nothing's being said.
    DepositBox &d = *_deposit;
    if (d.timer <= 0 || _dialogue->active())
        return;
    d.timer -= seconds;
    if (d.timer > 0)
        return;
    d.timer = 0;
    // 0x4097b0
    playSound("sfx\\boxclose2");
    int first = -1;
    for (int i = 0; i < 12; i++)
        if (!d.closed[size_t(i)]) {
            if (first < 0)
                first = i;
            else
                std::swap(d.letters[size_t(first)], d.letters[size_t(i)]);
        }
    d.closed.fill(true);
    saveDepositBox();
    placeDepositLetters();
    if (depositSolved()) {
        _state.tags["depositboxsolved"] = 0;
        _runner->run("talk narracottgen 92, fade out, scene saferoom02 4 0, fade in, talk saferoom 11");
    } else if (!_state.tags.count("saidswapcomment")) {
        _runner->run("talk narracottgen 123");
        _state.tags["saidswapcomment"] = 0;
    }
}

bool ScenePage::depositSolved() const {
    // The letters, an E any of the three (0x4092d0).
    for (size_t i = 0; i < 12; i++)
        if (_deposit->letters[i].empty() || _deposit->letters[i][0] != kAnswer[i])
            return false;
    return true;
}

bool ScenePage::depositStep() {
    // For the tools: a click towards the answer. The drawers with
    // something in them opened first (and shut), then each slot in turn
    // given its letter by a swap.
    DepositBox &d = *_deposit;
    if (!_runner->idle() || _dialogue->active() || d.timer > 0 || _state.tags.count("depositboxsolved"))
        return true; // waiting
    auto click = [&](int slot) {
        const int x = kBoxLeft + kBoxStepX * (slot % 4) + 66;
        const int y = kBoxTop + kBoxStepY * (slot / 4) + 70;
        Event ev;
        ev.mouseX = x, ev.mouseY = y;
        ev.type = EventType::kMouseMove;
        handleEvent(ev);
        ev.type = EventType::kMouseDown, ev.button = MouseButton::kLeft, ev.clicks = 1;
        handleEvent(ev);
        ev.type = EventType::kMouseUp;
        handleEvent(ev);
        return true;
    };
    for (int i = 0; i < 12; i++)
        if (!d.closed[size_t(i)] && std::count(d.closed.begin(), d.closed.end(), false) == 1) {
            // One open: the drawer to swap it with, else shut again.
            if (d.letters[size_t(i)][0] != kAnswer[i])
                for (int j = 0; j < 12; j++)
                    if (j != i && d.letters[size_t(j)][0] == kAnswer[i] && d.letters[size_t(j)][0] != kAnswer[j])
                        return click(j);
            return click(i);
        }
    for (const Drawer &drawer : kDrawers)
        if (!_state.itemState(drawer.item))
            return click(drawer.slot);
    for (int i = 0; i < 12; i++)
        if (d.letters[size_t(i)][0] != kAnswer[i])
            return click(i);
    return false;
}

} // namespace reluva::awe
