// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/inventory.h"

#include "common/fs.h"
#include "engines/awe/formats/tables.h"
#include "engines/awe/game/conditions.h"
#include "engines/awe/game/script.h"
#include "engines/awe/ui/game_text.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

// The panel slides at 500 pixels a second between these (0x417fe0).
constexpr float kClosedX = -341;
constexpr float kOpenX = -8;
constexpr float kSpeed = 500;
// The grid (0x418860): 4 by 3, 71 apart, from (x + 34, 47); its boxes hit
// 64 square, an item's picture 5 in; the tray a row at y 260.
constexpr int kGridLeft = 34;
constexpr int kGridTop = 47;
constexpr int kStep = 71;
constexpr int kBox = 64;
constexpr int kTrayTop = kGridTop + 213;
constexpr int kPerPage = 12;
constexpr int kMaxPages = 12;
// Rectangles on the panel as the code sets them, with it open at x -8
// (0x4179db): the arrows, Examine, Combine.
struct Rect {
    int l, t, r, b;
};
constexpr Rect kBack{42, 28, 70, 43};
constexpr Rect kNext{275, 28, 303, 43};
constexpr Rect kExamine{129, 328, 171, 370};
constexpr Rect kCombine{177, 328, 217, 370};
constexpr Color kText{0xd2, 0xaa, 0x6e}; // 0xffd2aa6e (0x4187a3)

// State: Orient Express's numbers (GameState): 1 just taken, 2 in the
// bag, 3 in hand, 4 used.
std::vector<std::string> names(std::string list) {
    if (list.size() >= 2 && list.front() == '"' && list.back() == '"')
        list = list.substr(1, list.size() - 2);
    std::vector<std::string> out;
    for (const Command &c : parseCommands(list))
        for (const std::string &w : c.words)
            out.push_back(fs::toLower(w));
    return out;
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

} // namespace

Inventory::Inventory(OverlayHost &host) : _host(host) {
    // itemlist.txt (0x41b220): by Item Name, its description and Examine.
    std::vector<uint8_t> data;
    if (!_host.files.read("text/itemlist.txt", data)) {
        const Table t = parseTable(decodeText(data));
        for (size_t r = 0; r < t.rows.size(); r++)
            _rows[fs::toLower(t.cell(r, 1))] = {unquote(t.cell(r, 6)), fs::toLower(unquote(t.cell(r, 7)))};
    }
}

const Inventory::Row *Inventory::row(const std::string &item) const {
    const auto it = _rows.find(item);
    return it == _rows.end() ? nullptr : &it->second;
}

std::string Inventory::art(const std::string &item, const char *kind) const {
    return "inventory/" + item + "/" + kind + item + ".tga";
}

void Inventory::toggle() {
    _moving = _open ? 2 : 1;
    if (_open) {
        // Closing ends the new item's blink (0x417fe0).
        _blink = 0;
        for (GameState::Item &i : _host.state.items)
            if (i.state == 1)
                i.state = 2;
        _host.state.fresh.clear();
    }
    _host.sound("sound\\interface\\openclose.wav");
}

void Inventory::updatePages() {
    // The highest page holding an item, and one more; at most 12; it never
    // shrinks (0x418660).
    int last = 0;
    for (const GameState::Item &i : _host.state.items)
        if ((i.state == 1 || i.state == 2) && i.slot < kTray)
            last = std::max(last, i.slot / kPerPage);
    _pages = std::max(_pages, std::min(kMaxPages, last + 2));
}

void Inventory::update(float seconds) {
    // A new item: its page, and the sack blinking for 5 seconds (0x418cfd).
    if (!_host.state.fresh.empty() && _host.state.fresh != _seen) {
        _seen = _host.state.fresh;
        for (GameState::Item &i : _host.state.items)
            if (i.state == 1 && i.name == _host.state.fresh) {
                _blink = 5;
                _page = i.slot < kTray ? i.slot / kPerPage : _page;
            }
    }
    if (_blink > 0) {
        _blink -= seconds;
        if (_blink <= 0) {
            // Over: in the bag (0x417f50).
            _blink = 0;
            for (GameState::Item &i : _host.state.items)
                if (i.state == 1)
                    i.state = 2;
            _host.state.fresh.clear();
            _seen.clear();
        }
    }
    _flip -= seconds;
    if (_flip <= 0)
        _flip = 0.33f, _blinkOff = !_blinkOff;
    updatePages();
    if (_showing && _movie) {
        // The close-up's movie, at its own rate, round again.
        _movieTime += seconds;
        const double fps =
            _movie->rateDenominator() ? double(_movie->rateNumerator()) / _movie->rateDenominator() : 15.0;
        const size_t count = std::max<size_t>(1, _movie->frameCount());
        const size_t want = size_t(_movieTime * fps) % count;
        if (want + 1 < _next || _next == 0) {
            _movie->rewind();
            _next = 0;
        }
        for (; _next <= want; _next++) {
            bool more = false;
            if (_movie->nextFrame(more) || !more)
                break;
        }
        _frame.w = _movie->width();
        _frame.h = _movie->height();
        _movie->toRgba(_frame.rgba, bink::Movie::Rgb::kBinkw32);
    }
    if (_moving == 0)
        return;
    _step += seconds * kSpeed;
    if (_step < 1)
        return;
    const float move = float(int(_step));
    _step -= move;
    _x += _moving == 1 ? move : -move;
    if (_moving == 1 && _x >= kOpenX)
        _x = kOpenX, _moving = 0, _open = true;
    else if (_moving == 2 && _x <= kClosedX)
        _x = kClosedX, _moving = 0, _open = false;
}

int Inventory::slotAt(int x, int y) const {
    const int bx = int(_x);
    const int col = (x - bx - kGridLeft) / kStep;
    if (x < bx + kGridLeft || col > 3 || (x - bx - kGridLeft) % kStep >= kBox)
        return -1;
    if (y >= kTrayTop && y < kTrayTop + kBox)
        return kTray + col;
    const int r = (y - kGridTop) / kStep;
    if (y < kGridTop || r > 2 || (y - kGridTop) % kStep >= kBox)
        return -1;
    return _page * kPerPage + r * 4 + col;
}

void Inventory::hover(int x, int y) {
    // Only with nothing in hand (0x418290).
    _hover.clear();
    if (!_open || !_host.state.held.empty())
        return;
    const int slot = slotAt(x, y);
    for (const GameState::Item &i : _host.state.items)
        if ((i.state == 1 || i.state == 2) && i.slot == slot && slot >= 0)
            _hover = i.name;
}

std::vector<std::string> Inventory::tray() const {
    std::vector<std::pair<int, std::string>> in;
    for (const GameState::Item &i : _host.state.items)
        if ((i.state == 1 || i.state == 2) && i.slot >= kTray && i.slot < kTray + 4)
            in.emplace_back(i.slot, i.name);
    std::sort(in.begin(), in.end());
    std::vector<std::string> out;
    for (auto &[slot, name] : in)
        out.push_back(name);
    return out;
}

void Inventory::putInTray() {
    // The item in hand to the tray's first free box (0x41aa10).
    GameState &s = _host.state;
    if (s.held.empty())
        return;
    for (int slot = kTray; slot < kTray + 4; slot++) {
        const bool taken = std::any_of(s.items.begin(), s.items.end(), [&](const GameState::Item &i) {
            return (i.state == 1 || i.state == 2) && i.slot == slot;
        });
        if (taken)
            continue;
        if (GameState::Item *i = s.findItem(s.held, {3})) {
            i->state = 2;
            i->slot = slot;
        }
        s.held.clear();
        return;
    }
}

bool Inventory::toTray(const std::vector<std::string> &items) {
    GameState &s = _host.state;
    for (const std::string &name : items)
        if (!s.findItem(name, {1, 2, 3}))
            return false;
    if (GameState::Item *i = s.held.empty() ? nullptr : s.findItem(s.held, {3}))
        i->state = 2;
    s.held.clear();
    for (GameState::Item &i : s.items)
        if ((i.state == 1 || i.state == 2) && i.slot >= kTray && i.slot < kTray + 4) {
            i.slot = -1;
            i.slot = s.freeSlot();
        }
    for (size_t k = 0; k < items.size() && k < 4; k++) {
        GameState::Item *found = nullptr;
        for (GameState::Item &i : s.items)
            if (i.name == fs::toLower(items[k]) && (i.state == 1 || i.state == 2) && i.slot < kTray)
                found = &i;
        if (!found)
            return false;
        found->slot = kTray + int(k);
    }
    return true;
}

std::vector<std::vector<std::string>> Inventory::examinable() const {
    std::vector<std::vector<std::string>> out;
    const GameState &s = _host.state;
    std::vector<std::string> bag;
    for (const GameState::Item &i : s.items)
        if ((i.state == 1 || i.state == 2) && std::find(bag.begin(), bag.end(), i.name) == bag.end())
            bag.push_back(i.name);
    for (const std::string &item : bag) {
        const Row *r = row(item);
        const std::vector<std::string> words = r ? names(r->examine) : std::vector<std::string>{};
        const bool reads =
            !words.empty() && (words[0] == "docs" || (words[0] == "books" && !s.tags.count("read_" + item)));
        if (reads || _host.files.has("inventory/" + item + "/exm" + item + ".bik") || _host.files.has(art(item, "exm")))
            out.push_back({item});
    }
    for (const char *who : {"brent", "marston", "rogers"}) {
        const std::string p = std::string("print") + who;
        std::vector<std::string> prints;
        for (const std::string &item : bag)
            if (item.find(p) != std::string::npos && _host.files.has(art(item, "exm")))
                prints.push_back(item);
        if (prints.size() >= 2 && !s.tags.count(std::string("compare_") + who))
            out.push_back({prints[0], prints[1]});
    }
    return out;
}

OverlayHand Inventory::hand(OverlayWant want, const std::vector<std::string> &items) const {
    using K = OverlayHand::Kind;
    const GameState &s = _host.state;
    const int bx = int(kOpenX);
    auto at = [](int x, int y, K k = K::kClick) { return OverlayHand{k, x, y}; };
    auto centre = [&](int slot) {
        const int box = slot >= kTray ? slot - kTray : slot % kPerPage;
        const int y = slot >= kTray ? kTrayTop : kGridTop + kStep * (box / 4);
        return at(bx + kGridLeft + kStep * (box % 4) + kBox / 2, y + kBox / 2);
    };
    const OverlayHand sack = at(int(_x) + 360, 30);
    if (_moving != 0)
        return {};
    if (_showing)
        return at(400, 300); // the close-up put away
    auto slotOf = [&](const std::string &item, bool tray) {
        for (const GameState::Item &i : s.items)
            if (i.name == item && (i.state == 1 || i.state == 2) && (i.slot >= kTray) == tray)
                return i.slot;
        return -1;
    };
    // Something in hand put down: in an empty box on this page, else the
    // next page looked at.
    auto putDown = [&]() {
        for (int box = 0; box < kPerPage; box++) {
            const int slot = _page * kPerPage + box;
            if (std::none_of(s.items.begin(), s.items.end(), [&](const GameState::Item &i) {
                    return (i.state == 1 || i.state == 2) && i.slot == slot;
                }))
                return centre(slot);
        }
        return at(bx + 8 + (kNext.l + kNext.r) / 2, (kNext.t + kNext.b) / 2);
    };
    auto pickUp = [&](int slot) {
        if (slot < kTray && slot / kPerPage != _page)
            return at(bx + 8 + (kNext.l + kNext.r) / 2, (kNext.t + kNext.b) / 2);
        return centre(slot);
    };
    switch (want) {
    case OverlayWant::kNothing:
        if (!s.held.empty())
            return _open ? putDown() : sack;
        return _open ? sack : OverlayHand{K::kReady};
    case OverlayWant::kHold: {
        const std::string &item = items.at(0);
        if (s.held == item)
            return _open ? sack : OverlayHand{K::kReady};
        if (!_open)
            return sack;
        if (!s.held.empty())
            return putDown();
        int slot = slotOf(item, false);
        if (slot < 0)
            slot = slotOf(item, true);
        return slot < 0 ? OverlayHand{K::kReady} : pickUp(slot);
    }
    case OverlayWant::kCombine:
    case OverlayWant::kExamine: {
        if (!_open)
            return sack;
        // The tray holding the items and nothing else; then the button.
        std::vector<std::string> missing = items;
        int stray = -1;
        for (const GameState::Item &i : s.items) {
            if ((i.state != 1 && i.state != 2) || i.slot < kTray || i.slot >= kTray + 4)
                continue;
            const auto it = std::find(missing.begin(), missing.end(), i.name);
            if (it != missing.end())
                missing.erase(it);
            else if (stray < 0)
                stray = i.slot;
        }
        if (!s.held.empty()) {
            const auto it = std::find(missing.begin(), missing.end(), s.held);
            if (it == missing.end())
                return putDown();
            for (int slot = kTray; slot < kTray + 4; slot++)
                if (std::none_of(s.items.begin(), s.items.end(), [&](const GameState::Item &i) {
                        return (i.state == 1 || i.state == 2) && i.slot == slot;
                    }))
                    return centre(slot);
            return {K::kReady}; // a full tray: can't be done
        }
        if (stray >= 0)
            return centre(stray);
        if (!missing.empty()) {
            const int slot = slotOf(missing.front(), false);
            return slot < 0 ? OverlayHand{K::kReady} : pickUp(slot);
        }
        const Rect &b = want == OverlayWant::kCombine ? kCombine : kExamine;
        return at(bx + 8 + (b.l + b.r) / 2, (b.t + b.b) / 2, K::kDone);
    }
    }
    return {K::kReady};
}

void Inventory::draw(Surface &dst, const GameFonts &fonts) {
    const int bx = int(_x);
    const image::Image &panel = _host.picture("interface/inventoryborder.tga");
    if (panel.w > 0)
        image::blitOver(dst, panel, bx, 0);
    // The sack, hidden on the blink's off half while a new item blinks
    // and the panel's shut (0x41871f).
    if (!(!_open && _moving == 0 && _blink > 0 && _blinkOff))
        if (const image::Image &sack = _host.picture("interface/inventorysack.tga"); sack.w > 0)
            image::blitOver(dst, sack, bx + 342, 12);
    if (_open || _moving != 0) {
        // The page's marker on its line (0x418750).
        if (const image::Image &blip = _host.picture("interface/blip.tga"); blip.w > 0)
            image::blitOver(dst, blip, bx + 69 + (_pages > 1 ? _page * (192 / (_pages - 1)) : 0), 29);
        // The page's items and the tray's: one just taken on the blink's
        // on half (0x418860).
        for (const GameState::Item &i : _host.state.items) {
            if (i.state != 2 && !(i.state == 1 && !_blinkOff))
                continue;
            int x;
            int y;
            if (i.slot >= kTray && i.slot < kTray + 4) {
                x = bx + kGridLeft + (i.slot - kTray) * kStep, y = kTrayTop;
            } else if (i.slot / kPerPage == _page) {
                const int k = i.slot % kPerPage;
                x = bx + kGridLeft + (k % 4) * kStep, y = kGridTop + (k / 4) * kStep;
            } else {
                continue;
            }
            if (const image::Image &pic = _host.picture(art(i.name, "inv")); pic.w > 0)
                image::blitOver(dst, pic, x + 5, y + 5);
        }
        // The item under the mouse's description, centred over the foot
        // (0x4187a3).
        if (const Row *r = _hover.empty() ? nullptr : row(_hover)) {
            const TextStyle *s = fonts.style("font 6");
            if (s && s->font && !r->description.empty() && r->description != "0") {
                const int w = s->font->width(r->description, s->pixels);
                fonts.draw(dst, *s, bx + 41 + (265 - w) / 2, 437 - 59, r->description, kText);
            }
        }
    }
    // The close-up, centred (0x419bd0): two prints side by side on their
    // paper.
    if (_showing) {
        auto centred = [&](const image::Image &img, int shift) {
            if (img.w > 0)
                image::blitOver(dst, img, (dst.w - img.w) / 2 + shift, (dst.h - img.h) / 2);
        };
        if (!_second.empty()) {
            centred(_host.picture("interface/fingerprintpaper.tga"), 0);
            centred(_host.picture(art(_examined, "exm")), -50);
            centred(_host.picture(art(_second, "exm")), 50);
        } else if (_movie) {
            centred(_frame, 0);
        } else {
            centred(_host.picture(art(_examined, "exm")), 0);
        }
    }
}

bool Inventory::covers(int x, int y) const {
    if (_moving != 0 || _showing)
        return true;
    const int bx = int(_x);
    if (x >= bx + 339 && x < bx + 382 && y >= 8 && y < 54)
        return true;
    const image::Image &panel = _host.picture("interface/inventoryborder.tga");
    return _open && x < bx + (panel.w > 0 ? panel.w : 386) && y < (panel.h > 0 ? panel.h : 437);
}

bool Inventory::click(int x, int y) {
    // 0x418fe0: everything while it slides; a click on a close-up puts it
    // away.
    if (_moving != 0)
        return true;
    if (_showing) {
        closeExamine(0, true);
        return true;
    }
    const int bx = int(_x);
    if (x >= bx + 339 && x < bx + 382 && y >= 8 && y < 54) {
        toggle();
        return true;
    }
    const image::Image &panel = _host.picture("interface/inventoryborder.tga");
    if (!_open || x >= bx + (panel.w > 0 ? panel.w : 386) || y >= (panel.h > 0 ? panel.h : 437))
        return false;
    auto in = [&](const Rect &r) { return x >= r.l + bx + 8 && x < r.r + bx + 8 && y >= r.t && y < r.b; };
    if (in(kBack) || in(kNext)) {
        // Round from either end (0x419600, 0x419630).
        _page = in(kNext) ? (_page + 1) % _pages : (_page + _pages - 1) % _pages;
        _host.sound("sound\\interface\\click1.wav");
        return true;
    }
    if (in(kExamine)) {
        examineButton();
        return true;
    }
    if (in(kCombine)) {
        combineButton();
        return true;
    }
    const int slot = slotAt(x, y);
    if (slot < 0)
        return true;
    GameState &s = _host.state;
    GameState::Item *there = nullptr;
    for (GameState::Item &i : s.items)
        if ((i.state == 1 || i.state == 2) && i.slot == slot)
            there = &i;
    if (s.held.empty() && there) {
        // Into the hand; the blink is over (0x419175).
        _host.sound("sound\\interface\\click4.wav");
        _blink = 0;
        for (GameState::Item &i : s.items)
            if (i.state == 1)
                i.state = 2;
        s.fresh.clear();
        there->state = 3;
        s.held = there->name;
    } else if (!s.held.empty() && !there) {
        // Put down in the empty box (0x419217).
        _host.sound("sound\\interface\\click4.wav");
        if (GameState::Item *i = s.findItem(s.held, {3})) {
            i->state = 2;
            i->slot = slot;
        }
        s.held.clear();
    }
    return true;
}

void Inventory::examineButton() {
    // 0x419790: what's in hand to the tray first; one item there, or two
    // prints.
    putInTray();
    const std::vector<std::string> items = tray();
    const bool prints =
        items.size() == 2 && items[0].find("print") != std::string::npos && items[1].find("print") != std::string::npos;
    if (items.size() != 1 && !prints)
        return;
    const std::string &item = items[0];
    if (_host.files.has("inventory/" + item + "/exm" + item + ".bik")) {
        _host.run("talk narracottgen 41, examine");
        return;
    }
    if (_host.files.has(art(item, "exm"))) {
        // Two prints of one person: the comparison (0x419919).
        for (const char *who : {"brent", "marston", "rogers"}) {
            const std::string p = std::string("print") + who;
            if (prints && items[0].find(p) != std::string::npos && items[1].find(p) != std::string::npos) {
                const char *line = std::string(who) == "brent" ? "59" : std::string(who) == "marston" ? "153" : "56";
                _host.run(std::string("examine, talk narracottgen ") + line + ", tag compare_" + who);
                return;
            }
        }
        _host.run("talk narracottgen 41, examine");
        return;
    }
    // A document or a book: read into the journal (0x419a11); "journal 2"
    // then uses a document up.
    _examined = item;
    const Row *r = row(item);
    const std::vector<std::string> words = r ? names(r->examine) : std::vector<std::string>{};
    const std::string n = words.size() > 1 ? words[1] : std::string("0");
    if (!words.empty() && words[0] == "docs") {
        _host.state.tags["read_" + item] = 0;
        _host.run("sounda interface\\notebook, wait .5, talk narracottgen " + n + ", journal 2");
    } else if (!words.empty() && words[0] == "books") {
        if (_host.state.tags.count("read_" + item)) {
            _host.run("talk narracottgen 183");
        } else {
            _host.state.tags["read_" + item] = 0;
            _host.run("sounda interface\\notebook, wait .5, talk narracottgen " + n + ", journal 3");
        }
    } else {
        _host.run("talk narracottgen 38");
    }
}

void Inventory::examine() {
    // "examine" (0x419bd0): the tray's item close up, its movie or its
    // picture; two prints side by side.
    const std::vector<std::string> items = tray();
    if (items.empty())
        return;
    _examined = items[0];
    _second = items.size() == 2 && items[0].find("print") != std::string::npos ? items[1] : std::string();
    _movie.reset();
    const std::string movie = "inventory/" + _examined + "/exm" + _examined + ".bik";
    std::vector<uint8_t> data;
    if (_second.empty() && !_host.files.read(movie, data)) {
        _movie = std::make_unique<bink::Movie>();
        if (_movie->open(std::move(data)))
            _movie.reset();
        _movieTime = 0;
        _next = 0;
    }
    _showing = true;
}

void Inventory::closeExamine(int tab, bool said) {
    // 0x41a010: a movie's close-up ends with Narracott's word on it; the
    // item in hand goes back; the journal's Docs tab uses the item up.
    if (said && _showing && _movie)
        _host.run("talk narracottgen 184");
    _showing = false;
    _movie.reset();
    GameState &s = _host.state;
    if (GameState::Item *i = s.held.empty() ? nullptr : s.findItem(s.held, {3})) {
        i->state = 2;
        s.held.clear();
    }
    if (tab == 2 && !_examined.empty())
        if (GameState::Item *i = s.findItem(_examined, {1, 2}))
            i->state = 4;
    _examined.clear();
}

void Inventory::combineButton() {
    // 0x41a740: what's in hand to the tray; prints are compared; one item
    // taken apart, more put together, by uselist.txt.
    putInTray();
    const std::vector<std::string> items = tray();
    if (items.empty())
        return;
    if (std::any_of(items.begin(), items.end(),
                    [](const std::string &i) { return i.find("print") != std::string::npos; })) {
        examineButton();
        return;
    }
    std::vector<uint8_t> data;
    Table t;
    if (!_host.files.read("text/uselist.txt", data))
        t = parseTable(decodeText(data));
    GameState &s = _host.state;
    auto addToTray = [&](const std::vector<std::string> &made) {
        for (size_t k = 0; k < made.size(); k++) {
            int slot = kTray;
            while (slot < kTray + 4 && std::any_of(s.items.begin(), s.items.end(), [&](const GameState::Item &i) {
                       return (i.state == 1 || i.state == 2) && i.slot == slot;
                   }))
                slot++;
            addItem(s, made[k], 2, slot);
        }
    };
    if (items.size() == 1) {
        // Taken apart (0x41a8f0): a row whose Result it is.
        bool found = false;
        for (size_t r = 0; r < t.rows.size(); r++) {
            if (names(t.cell(r, 1)) != items)
                continue;
            found = true;
            if (std::atoi(unquote(t.cell(r, 2)).c_str()) == 0 || !testCondition(unquote(t.cell(r, 4)), s))
                continue;
            if (GameState::Item *i = s.findItem(items[0], {1, 2}))
                i->state = 4;
            addToTray(names(t.cell(r, 0)));
            const std::string event = unquote(t.cell(r, 5));
            _host.run("sound interface\\decombine, " + (event == "0" ? std::string() : event));
            return;
        }
        _host.run(found ? "talk narracottgen 39" : "talk narracottgen 40");
        return;
    }
    // Put together (0x41ab30): a row whose parts are all in the tray and
    // no more, that can be, its condition holding.
    std::vector<std::string> have = items;
    std::sort(have.begin(), have.end());
    for (size_t r = 0; r < t.rows.size(); r++) {
        std::vector<std::string> parts = names(t.cell(r, 0));
        std::sort(parts.begin(), parts.end());
        if (parts != have || std::atoi(unquote(t.cell(r, 3)).c_str()) == 0 || !testCondition(unquote(t.cell(r, 4)), s))
            continue;
        for (const std::string &p : items)
            if (GameState::Item *i = s.findItem(p, {1, 2}))
                i->state = 4;
        addToTray(names(t.cell(r, 1)));
        const std::string event = unquote(t.cell(r, 5));
        _host.run("sound interface\\combine, " + (event == "0" ? std::string("talk narracottgen 4") : event));
        return;
    }
    _host.run("talk narracottgen 3");
}

} // namespace reluva::awe::attwn
