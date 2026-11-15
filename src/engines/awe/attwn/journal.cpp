// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/journal.h"

#include "common/fs.h"
#include "engines/awe/formats/tables.h"
#include "engines/awe/game/conditions.h"
#include "engines/awe/ui/game_text.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

// The panel slides at 500 pixels a second between these (0x41bf3e).
constexpr float kClosedX = 754;
constexpr float kOpenX = 420;
constexpr float kSpeed = 500;
// Where the pages are on the panel, and where they end (0x4747f4,
// 0x474818, 0x474810, 0x47480c; 0x13b).
constexpr int kContentsLeft = 115;
constexpr int kEntriesLeft = 110;
constexpr int kTop = 65;
constexpr int kTitle = 40;
constexpr int kBottom = 315;
constexpr int kWidth = 230;   // a line wraps past it
constexpr int kCentred = 200; // titles are centred in this from the page's left
// Each tab's box on the panel (0x41b544): x from, x to; y 364 to 394.
constexpr int kTabs[4][2] = {{81, 157}, {158, 224}, {225, 289}, {290, 351}};
constexpr int kTabTop = 364;
constexpr int kTabBottom = 394;
// 0xd0461e00: a dark brown, a little see-through.
constexpr Color kInk{0x46, 0x1e, 0x00};
constexpr uint8_t kInkAlpha = 0xd0;
const char *const kTables[4] = {"text/journal/general.txt", "text/journal/characters.txt", "text/journal/docs.txt",
                                "text/journal/books.txt"};

std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

const TextStyle *styleOf(const GameFonts *fonts, int n) {
    const TextStyle *s = fonts ? fonts->style("font " + std::to_string(n)) : nullptr;
    return s && s->font ? s : nullptr;
}

int widthOf(const TextStyle *s, const std::string &text) {
    return s ? s->font->width(text, s->pixels) : int(text.size()) * 7;
}

int heightOf(const TextStyle *s) {
    return s ? s->font->lineHeight(s->pixels) : 16;
}

} // namespace

Journal::Journal(OverlayHost &host) : _host(host) {
    // Its tables (0x41c360): Sub-Heading, Condition, Entry; lines shorter
    // than 10 characters skipped.
    for (size_t t = 0; t < 4; t++) {
        std::vector<uint8_t> data;
        if (_host.files.read(kTables[t], data))
            continue;
        const std::string text = decodeText(data);
        size_t at = text.find('\n');
        while (at != std::string::npos && at < text.size()) {
            const size_t end = std::min(text.find('\n', at + 1), text.size());
            std::string line = text.substr(at + 1, end - at - 1);
            at = end;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\t'))
                line.pop_back();
            if (line.size() < 10)
                continue;
            std::vector<std::string> cells;
            size_t from = 0;
            for (size_t tab = line.find('\t'); from <= line.size(); tab = line.find('\t', from)) {
                cells.push_back(unquote(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from)));
                if (tab == std::string::npos)
                    break;
                from = tab + 1;
            }
            cells.resize(3);
            _tabs[t].entries.push_back({cells[0], cells[1] == "0" ? std::string() : cells[1], cells[2], false});
        }
    }
}

void Journal::retest() {
    for (Tab &t : _tabs)
        for (Entry &e : t.entries)
            e.shown = testCondition(e.condition, _host.state);
}

std::vector<std::string> Journal::headings() const {
    // Each shown entry's sub-heading, unless the last one listed has it in
    // it (0x41c84e, a "contains").
    std::vector<std::string> out;
    for (const Entry &e : _tabs[size_t(_tab)].entries)
        if (e.shown && (out.empty() || fs::toLower(out.back()).find(fs::toLower(e.heading)) == std::string::npos))
            out.push_back(e.heading);
    return out;
}

std::vector<Journal::Row> Journal::layout(const GameFonts *fonts) const {
    std::vector<Row> rows;
    const Tab &t = _tabs[size_t(_tab)];
    int page = 0;
    if (t.heading.empty()) {
        // The contents (0x41c720): the sub-headings 25 apart, a star before
        // each, on to another page from y 315.
        int y = kTop;
        for (const std::string &h : headings()) {
            if (y >= kBottom)
                y = kTop, page++;
            rows.push_back({page, kContentsLeft, y, h, true});
            y += 25;
        }
        return rows;
    }
    // A sub-heading's entries (0x41ccb0, 0x41cff0): each wrapped at 230
    // pixels, its rows the text's height less 5 apart and half that again
    // after it; a bullet before each in General and Guests.
    const TextStyle *s = styleOf(fonts, 4);
    const int step = heightOf(s) - 5;
    const int start = kTop + (widthOf(styleOf(fonts, 5), t.heading) > kWidth ? 10 : 0);
    int y = start;
    for (const Entry &e : t.entries) {
        if (!e.shown || e.heading != t.heading)
            continue;
        // One that fits is drawn as it is, its spaces and all (0x41d022).
        const std::vector<std::string> lines = s && widthOf(s, e.text) > kWidth
                                                   ? gameRows(*s->font, s->pixels, e.text, kWidth)
                                                   : std::vector<std::string>{e.text};
        for (size_t i = 0; i < lines.size(); i++) {
            if (y >= kBottom)
                y = start, page++;
            rows.push_back({page, kEntriesLeft, y, lines[i], i == 0 && _tab < 2});
            y += step;
        }
        y += step / 2;
    }
    return rows;
}

int Journal::pages() const {
    int n = 1;
    for (const Row &r : layout(_fonts))
        n = std::max(n, r.page + 1);
    return n;
}

void Journal::openClose() {
    // 0x41ba90: the book clicked; it opens with its entries looked at again.
    _blink = 0;
    _moving = _open ? 2 : 1;
    if (!_open)
        retest();
    _host.sound("sound\\interface\\openclose.wav");
}

void Journal::update(float seconds) {
    // The book's blink (0x41bf07), flipping every third of a second
    // (0x415c00).
    if (_blink > 0)
        _blink = std::max(0.0f, _blink - seconds);
    _flip -= seconds;
    if (_flip <= 0)
        _flip = 0.33f, _blinkOff = !_blinkOff;
    if (_moving == 0)
        return;
    // Whole pixels at a time, the rest kept (0x41bf3e).
    _step += seconds * kSpeed;
    if (_step < 1)
        return;
    const float move = float(int(_step));
    _step -= move;
    _x += _moving == 2 ? move : -move;
    if (_moving == 1 && _x <= kOpenX)
        _x = kOpenX, _moving = 0, _open = true;
    else if (_moving == 2 && _x >= kClosedX)
        _x = kClosedX, _moving = 0, _open = false;
}

void Journal::draw(Surface &dst, const GameFonts &fonts) {
    _fonts = &fonts;
    const int bx = int(_x);
    const image::Image &panel = _host.picture("interface/journalborder.tga");
    if (panel.w > 0)
        image::blitOver(dst, panel, bx, 0);
    // The book, blinking while it has something new to show (0x41bc96).
    if (_open || _moving != 0 || _blink <= 0 || !_blinkOff)
        if (const image::Image &book = _host.picture("interface/journalbook.tga"); book.w > 0)
            image::blitOver(dst, book, bx + 8, 13);
    if (!_open && _moving == 0)
        return;
    const TextStyle *small = styleOf(&fonts, 4);
    const TextStyle *large = styleOf(&fonts, 5);
    auto text = [&](const TextStyle *s, int x, int y, const std::string &t) {
        if (s)
            fonts.draw(dst, *s, x, y, t, kInk, kInkAlpha);
    };
    // The page's title: the tab's name, or the sub-heading (two lines if
    // it's wider than 230).
    const Tab &t = _tabs[size_t(_tab)];
    const int left = bx + (t.heading.empty() ? kContentsLeft : kEntriesLeft);
    std::string title = t.heading.empty() ? _host.text("tab" + std::to_string(_tab + 1)) : t.heading;
    std::string second;
    if (widthOf(large, title) > kWidth) {
        for (size_t cut = title.rfind(' '); cut != std::string::npos && cut > 0; cut = title.rfind(' ', cut - 1))
            if (widthOf(large, title.substr(0, cut)) <= kWidth) {
                second = title.substr(cut + 1);
                title.resize(cut);
                break;
            }
    }
    text(large, left + (kCentred - widthOf(large, title)) / 2, kTitle, title);
    if (!second.empty())
        text(large, left + (kCentred - widthOf(large, second)) / 2, kTitle + 15, second);
    const image::Image &star = _host.picture("interface/journalstar.tga");
    const image::Image &bullet = _host.picture("interface/journalbullet.tga");
    int last = 0;
    for (const Row &r : layout(&fonts)) {
        last = std::max(last, r.page);
        if (r.page != _page)
            continue;
        const int x = bx + r.x;
        if (r.bullet && t.heading.empty() && star.w > 0)
            image::blitOver(dst, star, x - 15, r.y + 5);
        else if (r.bullet && bullet.w > 0)
            image::blitOver(dst, bullet, x - 12, r.y + 8);
        text(small, x, r.y, r.text);
    }
    // The arrows to the next page and the last (0x41bd59).
    if (_page < last)
        if (const image::Image &next = _host.picture("interface/arrownext.tga"); next.w > 0)
            image::blitOver(dst, next, bx + kContentsLeft + 175, 335);
    if (_page > 0)
        if (const image::Image &back = _host.picture("interface/arrowlast.tga"); back.w > 0)
            image::blitOver(dst, back, bx + kContentsLeft - 15, 335);
    // The tabs' names, centred in their boxes, the tab shown in the bold
    // font (0x41bdc6).
    for (int i = 0; i < 4; i++) {
        const TextStyle *s = i == _tab ? large : small;
        const std::string name = _host.text("tab" + std::to_string(i + 1));
        text(s, bx + kTabs[i][0] + (kTabs[i][1] - kTabs[i][0] - widthOf(s, name)) / 2,
             kTabTop + (kTabBottom - kTabTop - heightOf(s)) / 2, name);
    }
}

bool Journal::covers(int x, int y) const {
    if (_moving != 0)
        return true;
    const int bx = int(_x);
    if (x >= bx + 3 && x < bx + 46 && y >= 8 && y < 54)
        return true;
    const image::Image &panel = _host.picture("interface/journalborder.tga");
    return _open && x >= bx && y < (panel.h > 0 ? panel.h : 437);
}

bool Journal::click(int x, int y) {
    // 0x41ba90: nothing while it slides; the book opens or closes it.
    if (_moving != 0)
        return true;
    const int bx = int(_x);
    if (x >= bx + 3 && x < bx + 46 && y >= 8 && y < 54) {
        openClose();
        return true;
    }
    if (!_open)
        return false;
    const image::Image &panel = _host.picture("interface/journalborder.tga");
    if (x < bx || y >= (panel.h > 0 ? panel.h : 437))
        return false;
    // A tab (0x41c0f0): its contents.
    for (int i = 0; i < 4; i++)
        if (x >= bx + kTabs[i][0] && x < bx + kTabs[i][1] && y >= kTabTop && y < kTabBottom) {
            // (pagesflip.wav only with an animation at +0x14, never made.)
            _host.sound("sound\\interface\\pageflip.wav");
            _tab = i;
            _tabs[size_t(i)].heading.clear();
            _page = 0;
            return true;
        }
    // The arrows.
    if (y >= 335 && y < 355) {
        const int left = bx + kContentsLeft;
        if (_page + 1 < pages() && x >= left + 175 && x < left + 195) {
            _page++;
            _host.sound("sound\\interface\\pageflip.wav");
            return true;
        }
        if (_page > 0 && x >= left - 15 && x < left + 5) {
            _page--;
            _host.sound("sound\\interface\\pageflip.wav");
            return true;
        }
    }
    // A sub-heading on the contents (0x41cab0): its entries.
    if (_tabs[size_t(_tab)].heading.empty())
        for (const Row &r : layout(_fonts))
            if (r.page == _page && x >= bx + r.x - 15 && x < bx + r.x + kCentred && y >= r.y && y < r.y + 25) {
                _tabs[size_t(_tab)].heading = r.text;
                _page = 0;
                _host.sound("sound\\interface\\pageflip.wav");
                return true;
            }
    return true;
}

bool Journal::verb(const std::vector<std::string> &words) {
    if (words.empty() || words[0] != "journal")
        return false;
    const int n = words.size() > 1 ? std::atoi(words[1].c_str()) : 0;
    if (n < 0 || n > 3)
        return true;
    _tab = n;
    _tabs[size_t(n)].heading.clear();
    _page = 0;
    retest();
    if (_open)
        _host.sound("sound\\interface\\pageflip.wav");
    else
        _blink = 5;
    return true;
}

} // namespace reluva::awe::attwn
