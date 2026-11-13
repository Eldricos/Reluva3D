// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/font.h"
#include "common/text.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace reluva::newworlds {

namespace {

constexpr Color kBackground{22, 26, 38};
constexpr Color kIdColor{110, 118, 140};
constexpr Color kTextColor{220, 224, 232};
constexpr Color kHeadingColor{255, 200, 80}; // ~HEADING~
constexpr Color kFormatColor{120, 200, 255}; // printf codes: %d, %%
constexpr int kMargin = 4;

bool endsWith(const std::string &s, const char *suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// One word (or run of spaces) of a string, in its colour; a break forces a
// new line.
struct Piece {
    std::string text;
    Color color;
    bool space = false, lineBreak = false;
};

// Splits a string into pieces: "~HEADING~" on a line of its own, printf
// codes coloured, '\n' as breaks.
std::vector<Piece> piecesOf(const std::string &text) {
    std::vector<Piece> out;
    auto add = [&](const std::string &word, Color c) {
        // Mark printf codes within the word.
        size_t at = 0;
        for (size_t i = 0; i < word.size(); ++i) {
            if (word[i] != '%' || i + 1 >= word.size())
                continue;
            size_t j = i + 1;
            while (j < word.size() && std::isdigit(static_cast<unsigned char>(word[j])))
                ++j;
            if (j < word.size() && (word[j] == 'd' || word[j] == 's' || word[j] == '%' || word[j] == 'i')) {
                if (i > at)
                    out.push_back({word.substr(at, i - at), c});
                out.push_back({word.substr(i, j + 1 - i), kFormatColor});
                at = j + 1;
                i = j;
            }
        }
        if (at < word.size())
            out.push_back({word.substr(at), c});
    };
    std::string word;
    Color color = kTextColor;
    bool heading = false;
    auto flush = [&] {
        if (!word.empty())
            add(word, color);
        word.clear();
    };
    for (char c : text) {
        if (c == '~') {
            flush();
            heading = !heading;
            color = heading ? kHeadingColor : kTextColor;
            if (!heading)
                out.push_back({"", kTextColor, false, true});
        } else if (c == '\n') {
            flush();
            out.push_back({"", kTextColor, false, true});
        } else if (c == ' ' || c == '\t' || c == '\r') {
            flush();
            out.push_back({" ", color, true});
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

} // namespace

StringViewer::StringViewer(System &sys, Surface &screen, const Archives &archives)
    : _sys(sys), _screen(screen), _archives(archives) {
    // misc.cat stores one table (str/eng/frend.stb) with the texture tag, so
    // go by name as well as tag.
    for (size_t a = 0; a < _archives.size(); ++a) {
        const auto &entries = _archives[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].container && (entries[i].tag == cattag::kStrings || endsWith(entries[i].name, ".stb")))
                _tables.push_back({a, i});
    }
    _fonts = loadGameFonts(_archives);
}

int StringViewer::measure(std::string_view text) const {
    if (_font < 0)
        return int(font::length(text)) * font::kAdvance * _scale;
    return _fonts[size_t(_font)].textWidth(text) * _scale;
}

int StringViewer::lineHeight() const {
    return (_font < 0 ? font::kLineHeight : _fonts[size_t(_font)].lineHeight) * _scale;
}

int StringViewer::drawSpan(int x, int y, std::string_view text, Color color) {
    if (_font < 0)
        return font::drawText(_screen, x, y, text, color, _scale);
    return _fonts[size_t(_font)].drawText(_screen, x, y, text, color, _scale);
}

void StringViewer::activate() {
    if (!_shown) {
        _shown = true;
        show(0);
        return;
    }
    draw();
    updateTitle();
}

int StringViewer::rows() const {
    return std::max(1, (_screen.h - 2 * kMargin) / lineHeight());
}

void StringViewer::handleKey(uint32_t k) {
    const int n = int(_tables.size());
    if (n == 0)
        return;
    const int last = std::max(0, int(_lines.size()) - rows());
    switch (k) {
    case key::kRight:
        show((_current + 1) % n);
        return;
    case key::kLeft:
        show((_current + n - 1) % n);
        return;
    case key::kDown:
        _top = std::min(_top + 1, last);
        break;
    case key::kUp:
        _top = std::max(_top - 1, 0);
        break;
    case key::kPageDown:
        _top = std::min(_top + rows() - 1, last);
        break;
    case key::kPageUp:
        _top = std::max(_top - (rows() - 1), 0);
        break;
    case key::kHome:
        _top = 0;
        break;
    case key::kEnd:
        _top = last;
        break;
    case '+':
    case '=':
        _scale = std::min(_scale + 1, 3);
        layout();
        break;
    case '-':
        _scale = std::max(_scale - 1, 1);
        layout();
        break;
    case 'f':
        // The next font: the built-in one, then each of the game's.
        _font = _font + 1 < int(_fonts.size()) ? _font + 1 : -1;
        layout();
        break;
    default:
        return;
    }
    draw();
    updateTitle();
}

void StringViewer::show(int index) {
    _current = index;
    _top = 0;
    _status.clear();
    const EntryRef &ref = _tables[size_t(index)];
    CatArchive &cat = *_archives[ref.archive];
    std::vector<uint8_t> data;
    if (Error err = cat.read(cat.entries()[ref.entry], data))
        _status = err.message;
    else if (Error err2 = _table.parse(data))
        _status = err2.message;
    if (!_status.empty())
        _table = StringTable{};
    layout();
    draw();
    updateTitle();
}

void StringViewer::layout() {
    _lines.clear();
    const int idWidth = measure("9999 ");
    const int width = std::max(measure("MMMMMMMMMM"), _screen.w - 2 * kMargin - idWidth);
    for (const StringTable::String &s : _table.strings()) {
        Line line{std::to_string(s.id), {}};
        int used = 0; // pixels on this line after the id column
        auto newLine = [&] {
            _lines.push_back(std::move(line));
            line = Line{};
            used = 0;
        };
        for (const Piece &p : piecesOf(s.text)) {
            if (p.lineBreak) {
                if (used > 0)
                    newLine();
                continue;
            }
            const int w = measure(p.text);
            if (p.space) {
                if (used > 0 && used + w <= width)
                    line.spans.push_back({p.text, p.color}), used += w;
                continue;
            }
            if (used > 0 && used + w > width)
                newLine();
            // Words longer than a line are cut, a character at a time.
            std::string part;
            for (size_t i = 0; i < p.text.size();) {
                const size_t from = i;
                text::nextUtf8(p.text, i);
                const std::string ch = p.text.substr(from, i - from);
                if (!part.empty() && used + measure(part + ch) > width) {
                    line.spans.push_back({part, p.color});
                    newLine();
                    part.clear();
                }
                part += ch;
            }
            used += measure(part);
            line.spans.push_back({std::move(part), p.color});
        }
        _lines.push_back(std::move(line));
    }
    _top = std::min(_top, std::max(0, int(_lines.size()) - rows()));
}

void StringViewer::draw() {
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *row = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, row += 4)
            row[0] = kBackground.r, row[1] = kBackground.g, row[2] = kBackground.b, row[3] = 255;
    }
    if (!_status.empty()) {
        font::drawText(_screen, kMargin, kMargin, _status, kHeadingColor, _scale);
        return;
    }
    const int idWidth = measure("9999 ");
    for (int r = 0; r < rows() && _top + r < int(_lines.size()); ++r) {
        const Line &line = _lines[size_t(_top + r)];
        const int y = kMargin + r * lineHeight();
        // Ids right-aligned in their column.
        drawSpan(kMargin + idWidth - measure(line.id + " "), y, line.id, kIdColor);
        int x = kMargin + idWidth;
        for (const Span &span : line.spans)
            x = drawSpan(x, y, span.text, span.color);
    }
}

void StringViewer::updateTitle() {
    if (_tables.empty())
        return;
    const EntryRef &ref = _tables[size_t(_current)];
    std::string title = "New Worlds strings " + std::to_string(_current + 1) + "/" + std::to_string(_tables.size()) +
                        ": " + _archives[ref.archive]->entries()[ref.entry].name + " in " +
                        _archives[ref.archive]->path().filename().string() + " (";
    if (!_status.empty()) {
        title += _status + ")";
    } else {
        title += std::to_string(_table.strings().size()) + " strings)";
        title += _font < 0 ? " [built-in font]" : " [font " + _fonts[size_t(_font)].name + "]";
        if (int(_lines.size()) > rows())
            title += " lines " + std::to_string(_top + 1) + "-" +
                     std::to_string(std::min(_top + rows(), int(_lines.size()))) + " of " +
                     std::to_string(_lines.size());
    }
    _sys.setWindowTitle(title);
}

} // namespace reluva::newworlds
