// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Tables page: the tab-separated tables and the string
// tables, a row to a line.

#include "common/fs.h"
#include "common/stream.h"
#include "engines/sfc/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <string>

namespace reluva::sfc {

namespace {

constexpr int kTop = 2 * font::kLineHeight + 8;
constexpr int kCellChars = 14; // a tab table's column, in the built-in font's characters

// The tables, as each game's folder has them; those not there are left out.
const char *const kTables[] = {
    "Assets/specs/shiplist.txt",    "Assets/specs/ftrlist.txt",     "Assets/Sound/sounds.txt",
    "Assets/Sound/SFCMusic.txt",    "Assets/Strings/strings.txt",   "Assets/Strings/aaStrings.txt",
    "Assets/Strings/quicktips.txt", "Assets/Strings/shipnames.txt",
};

bool isStringTable(const std::filesystem::path &p) {
    return fs::toLower(p.parent_path().filename().string()) == "strings";
}

size_t rowsShown() {
    return size_t((kScreenH - kTop - font::kLineHeight) / font::kLineHeight);
}

} // namespace

TablePage::TablePage(ViewerContext &ctx) : Page(ctx) {
    for (const char *name : kTables)
        if (const auto path = fs::findFile(ctx.root, name))
            _files.push_back(path->lexically_relative(ctx.root));
}

void TablePage::activate() {
    load();
    show();
}

void TablePage::load() {
    _lines.clear();
    _error.clear();
    _first = 0;
    _column = 0;
    std::vector<uint8_t> data;
    if (!readFile(_ctx.root / _files[_current], data)) {
        _error = "Can't read it";
        return;
    }
    if (isStringTable(_files[_current])) {
        StringTable table;
        if (Error err = table.read(data)) {
            _error = err.message;
            return;
        }
        for (const auto &e : table.entries)
            for (size_t i = 0; i < e.text.size(); ++i)
                _lines.push_back({i ? std::string() : e.key, e.text[i]});
        return;
    }
    TabTable table;
    if (Error err = table.read(data)) {
        _error = err.message;
        return;
    }
    for (auto &row : table.rows)
        if (_comments || TabTable::isData(row))
            _lines.push_back(std::move(row));
}

void TablePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _files.empty())
        return;
    const size_t n = _files.size();
    const size_t page = rowsShown();
    const size_t last = _lines.empty() ? 0 : _lines.size() - 1;
    switch (ev.key) {
    case key::kLeft:
        _current = (_current + n - 1) % n;
        load();
        break;
    case key::kRight:
        _current = (_current + 1) % n;
        load();
        break;
    case key::kUp:
        _first = _first ? _first - 1 : 0;
        break;
    case key::kDown:
        _first = std::min(last, _first + 1);
        break;
    case key::kPageUp:
        _first = _first > page ? _first - page : 0;
        break;
    case key::kPageDown:
        _first = std::min(last, _first + page);
        break;
    case key::kHome:
        _first = 0;
        break;
    case key::kEnd:
        _first = last;
        break;
    case '[':
        _column = _column ? _column - 1 : 0;
        break;
    case ']':
        ++_column;
        break;
    case 'c':
        _comments = !_comments;
        load();
        break;
    default:
        return;
    }
    show();
}

void TablePage::show() {
    clear({20, 22, 28});
    if (_files.empty())
        return;
    const auto &rel = _files[_current];
    const bool strings = isStringTable(rel);
    text(4, 2,
         rel.generic_string() + "  (" + std::to_string(_current + 1) + " of " + std::to_string(_files.size()) +
             "): " + std::to_string(_lines.size()) + " rows" +
             (strings     ? ""
              : _comments ? ", comments shown"
                          : ", without comments") +
             "; from column " + std::to_string(_column + 1),
         {150, 200, 255});
    text(4, 2 + font::kLineHeight,
         "Left/Right: tables  Up/Down, PgUp/PgDn, Home/End: rows  [ ]: columns  C: comment rows", {170, 170, 170});
    setTitle(rel.filename().string());
    if (!_error.empty()) {
        text(4, kTop, _error, {255, 120, 120});
        return;
    }
    const int charW = font::kAdvance;
    int y = kTop;
    for (size_t i = _first; i < _lines.size() && i < _first + rowsShown(); ++i, y += font::kLineHeight) {
        const auto &row = _lines[i];
        if (strings) {
            text(4, y, row[0], {150, 220, 150});
            text(4 + 30 * charW, y, row.size() > 1 ? row[1] : std::string());
            continue;
        }
        int x = 4;
        for (size_t c = _column; c < row.size() && x < kScreenW; ++c, x += kCellChars * charW) {
            std::string cell = row[c];
            if (cell.size() > size_t(kCellChars - 1))
                cell = cell.substr(0, size_t(kCellChars - 2)) + "~";
            text(x, y, cell, c == 0 ? Color{150, 220, 150} : Color{230, 230, 230});
        }
    }
}

} // namespace reluva::sfc
