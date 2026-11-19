// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "graphics/font.h"

#include <algorithm>
#include <cstdio>

namespace reluva::botf {

namespace {

constexpr int kTop = 24;
constexpr int kListW = 240;
constexpr int kPanelX = 256;

// Splits text into lines of at most `chars` characters at spaces, for the
// built-in font.
std::vector<std::string> wrapChars(const std::string &text, size_t chars) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t space = text.find(' ', i);
        if (space == std::string::npos)
            space = text.size();
        const std::string word = text.substr(i, space - i);
        if (!line.empty() && font::length(line) + 1 + font::length(word) > chars) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        i = space + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

} // namespace

DataViewer::DataViewer(System &sys, Surface &screen, const Archive &archive)
    : _sys(sys), _screen(screen), _archive(archive) {
    for (const TableLayout &layout : tableLayouts())
        if (archive.has(layout.file))
            _files.push_back(&layout);
}

void DataViewer::activate() {
    if (_table.count() == 0 && _error.empty() && !_files.empty())
        load();
    show();
}

void DataViewer::load() {
    _error.clear();
    _record = 0;
    const TableLayout &layout = *_files[_current];
    std::vector<uint8_t> data;
    std::vector<uint8_t> descriptions;
    if (Error err = _archive.read(layout.file, data)) {
        _error = err.message;
        _table = DataTable{};
        return;
    }
    if (layout.descriptions && _archive.has(layout.descriptions))
        _archive.read(layout.descriptions, descriptions);
    if (Error err = _table.load(layout, std::move(data), std::move(descriptions)))
        _error = "can't read: " + err.message;
}

void DataViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const size_t n = _table.count();
    if (key == key::kRight || key == key::kLeft) {
        _current = key == key::kRight ? (_current + 1) % _files.size() : (_current + _files.size() - 1) % _files.size();
        load();
    } else if (n == 0)
        return;
    else if (key == key::kDown)
        _record = (_record + 1) % n;
    else if (key == key::kUp)
        _record = (_record + n - 1) % n;
    else if (key == key::kPageDown)
        _record = std::min(_record + 20, n - 1);
    else if (key == key::kPageUp)
        _record = _record >= 20 ? _record - 20 : 0;
    else if (key == key::kHome)
        _record = 0;
    else if (key == key::kEnd)
        _record = n - 1;
    else
        return;
    show();
}

void DataViewer::show() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {16, 22, 30});
    if (_files.empty())
        return;
    const Color text{230, 230, 230};
    const Color dim{140, 140, 160};
    const Color hilite{255, 220, 120};
    const Color label{150, 200, 255};
    const TableLayout &layout = *_files[_current];
    std::string head = std::to_string(_current + 1) + "/" + std::to_string(_files.size()) + "  " + layout.file;
    head += _error.empty()
                ? "  " + std::to_string(_table.count()) + " records of " + std::to_string(layout.recordSize) + " bytes"
                : "  " + _error;
    font::drawText(_screen, 8, 6, head, text);
    const int bottom = _screen.h - 18;
    font::drawText(_screen, 8, bottom + 4, "Left/Right: table  Up/Down, PageUp/PageDown: record  Tab: next page", dim);
    const size_t n = _table.count();
    if (n == 0)
        return;

    // The list of records around the current one.
    const int rows = (bottom - kTop) / font::kLineHeight;
    size_t first = _record > size_t(rows / 2) ? _record - size_t(rows / 2) : 0;
    first = std::min(first, n > size_t(rows) ? n - size_t(rows) : 0);
    for (int r = 0; r < rows && first + size_t(r) < n; ++r) {
        const size_t i = first + size_t(r);
        const int y = kTop + r * font::kLineHeight;
        if (i == _record)
            fillRect(_screen, 4, y - 1, kListW, font::kLineHeight, {50, 60, 90});
        char num[24];
        std::snprintf(num, sizeof num, "%3zu ", i);
        font::drawText(_screen, 8, y, num + _table.name(i), i == _record ? hilite : text);
    }

    // The current record: its fields, its unexplained bytes, its description.
    int y = kTop;
    const size_t chars = size_t(_screen.w - kPanelX - 8) / font::kAdvance;
    for (const Field &f : layout.fields) {
        char off[16];
        std::snprintf(off, sizeof off, "%3u ", f.offset);
        int x = font::drawText(_screen, kPanelX, y, off, dim);
        x = font::drawText(_screen, x, y, std::string(f.name) + ": ", label);
        font::drawText(_screen, x, y, _table.value(_record, f), text);
        y += font::kLineHeight;
    }
    y += 4;
    font::drawText(_screen, kPanelX, y, "Bytes not worked out yet:", label);
    y += font::kLineHeight;
    const uint8_t *rec = _table.record(_record);
    for (const auto &[begin, end] : _table.unknownRanges())
        for (uint32_t o = begin; o < end && y < bottom - 60; o += 16) {
            char line[96];
            int p = std::snprintf(line, sizeof line, "%3u ", o);
            for (uint32_t k = o; k < std::min(end, o + 16); ++k)
                p += std::snprintf(line + p, sizeof line - size_t(p), "%02x ", rec[k]);
            font::drawText(_screen, kPanelX, y, line, text);
            y += font::kLineHeight;
        }
    const std::string desc = _table.description(_record);
    if (!desc.empty()) {
        y += 4;
        for (const std::string &line : wrapChars(desc, chars)) {
            if (y > bottom - font::kLineHeight)
                break;
            font::drawText(_screen, kPanelX, y, line, {200, 220, 200});
            y += font::kLineHeight;
        }
    }
    _sys.setWindowTitle("Reluva - Birth of the Federation - data " + std::string(layout.file) + " " +
                        std::to_string(_record + 1) + "/" + std::to_string(n) + ": " + _table.name(_record));
}

} // namespace reluva::botf
