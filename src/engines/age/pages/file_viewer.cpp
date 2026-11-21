// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Files page, and the helpers its pages share.

#include "engines/age/pages/viewers.h"

#include "graphics/font.h"
#include "engines/age/formats/ar_archive.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace reluva::age {

namespace {

constexpr size_t kMaxDumpBytes = 1 << 20; // hex dumps stop after this much

std::string topFolder(const std::string &name) {
    const size_t slash = name.find('/');
    return slash == std::string::npos ? std::string() : normalizeArchiveName(name.substr(0, slash));
}

std::string formatSize(uint64_t size) {
    char buf[32];
    if (size < 10 * 1024)
        std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(size));
    else if (size < 10 * 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%lluK", static_cast<unsigned long long>(size / 1024));
    else
        std::snprintf(buf, sizeof buf, "%lluM", static_cast<unsigned long long>(size / (1024 * 1024)));
    return buf;
}

} // namespace

std::string kindOf(const std::string &name) {
    std::string file = name.substr(name.rfind('/') + 1);
    file = file.substr(0, file.find('.'));
    return normalizeArchiveName(file.substr(0, file.find('_')));
}

std::vector<std::string> fileLines(const std::vector<uint8_t> &data, bool &isText) {
    // Text: no zero bytes, and control characters only as line ends and
    // tabs. The game's text files are code page 1252, shown as Latin-1.
    isText = std::none_of(data.begin(), data.end(), [](uint8_t c) {
        return c == 0 || (c < 0x20 && c != '\t' && c != '\r' && c != '\n' && c != 0x1a);
    });
    std::vector<std::string> lines;
    if (isText) {
        std::string line;
        for (uint8_t c : data) {
            if (c == '\n') {
                lines.push_back(std::move(line));
                line.clear();
            } else if (c == '\t') {
                line.append(4 - line.size() % 4, ' ');
            } else if (c >= 0x80) {
                line += static_cast<char>(0xc0 | c >> 6); // Latin-1 as UTF-8
                line += static_cast<char>(0x80 | (c & 0x3f));
            } else if (c != '\r' && c != 0x1a) {
                line += static_cast<char>(c);
            }
        }
        if (!line.empty())
            lines.push_back(std::move(line));
        return lines;
    }
    const size_t end = std::min(data.size(), kMaxDumpBytes);
    for (size_t at = 0; at < end; at += 16) {
        char buf[100];
        int n = std::snprintf(buf, sizeof buf, "%08zx ", at);
        std::string ascii;
        for (size_t i = 0; i < 16; ++i) {
            if (at + i < end) {
                const uint8_t c = data[at + i];
                n += std::snprintf(buf + n, sizeof buf - size_t(n), "%s%02x", i == 8 ? "  " : " ", c);
                ascii += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.';
            } else {
                n += std::snprintf(buf + n, sizeof buf - size_t(n), "%s  ", i == 8 ? "   " : " ");
            }
        }
        lines.push_back(std::string(buf) + "  " + ascii);
    }
    if (end < data.size())
        lines.push_back("... (the first " + formatSize(end) + " of " + formatSize(data.size()) + ")");
    return lines;
}

FileViewer::FileViewer(ScreenContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        Row r{n, {}, 0};
        if (const Archive *a = ctx.files.find(n)) {
            r.source = std::filesystem::path(a->describe()).filename().string();
            if (auto *ar = dynamic_cast<const ArArchive *>(a)) {
                r.size = ar->entry(n)->size;
            } else {
                std::error_code ec;
                const auto size = std::filesystem::file_size(std::filesystem::path(a->describe()) / n, ec);
                r.size = ec ? 0 : size;
            }
        }
        const std::string top = topFolder(n);
        if (!top.empty() && std::find(_folders.begin(), _folders.end(), top) == _folders.end())
            _folders.push_back(top);
        _files.push_back(std::move(r));
    }
    std::sort(_folders.begin(), _folders.end());
    applyFolder();
}

void FileViewer::applyFolder() {
    _shown.clear();
    for (size_t i = 0; i < _files.size(); ++i)
        if (_folder < 0 || topFolder(_files[i].name) == _folders[size_t(_folder)])
            _shown.push_back(i);
    _selected = _top = 0;
}

int FileViewer::rows() const {
    return (_ctx.screen.h - 4 * font::kLineHeight) / font::kLineHeight;
}

void FileViewer::activate() {
    setTitle(std::to_string(_files.size()) + " files in " + std::to_string(_ctx.files.size()) + " archives");
    draw();
}

bool FileViewer::takesKey(const Event &ev) const {
    return _viewing && ev.type == EventType::kKeyDown && ev.key == key::kEscape;
}

void FileViewer::open() {
    if (_shown.empty())
        return;
    const Row &r = _files[_shown[size_t(_selected)]];
    std::vector<uint8_t> data;
    if (Error err = _ctx.files.read(r.name, data)) {
        _lines = {err.message};
        _isText = true;
    } else {
        _lines = fileLines(data, _isText);
    }
    _viewing = true;
    _viewName = r.name;
    _lineTop = 0;
    setTitle(_viewName);
}

void FileViewer::handleEvent(const Event &ev) {
    const int page = rows();
    int step = 0;
    if (ev.type == EventType::kMouseWheel)
        step = -ev.wheelY * 3;
    else if (ev.type != EventType::kKeyDown)
        return;
    else if (ev.key == key::kUp)
        step = -1;
    else if (ev.key == key::kDown)
        step = 1;
    else if (ev.key == key::kPageUp)
        step = -page;
    else if (ev.key == key::kPageDown)
        step = page;
    else if (ev.key == key::kHome)
        step = -1 << 30;
    else if (ev.key == key::kEnd)
        step = 1 << 30;

    if (_viewing) {
        if (ev.type == EventType::kKeyDown && (ev.key == key::kEscape || ev.key == key::kBackspace)) {
            _viewing = false;
            _lines.clear();
            activate();
            return;
        }
        const int last = std::max(0, static_cast<int>(_lines.size()) - (page - 1));
        _lineTop = std::clamp(_lineTop + step, 0, last);
        draw();
        return;
    }

    if (ev.type == EventType::kKeyDown && (ev.key == key::kReturn || ev.key == key::kKpEnter)) {
        open();
        draw();
        return;
    }
    if (ev.type == EventType::kKeyDown && (ev.key == key::kLeft || ev.key == key::kRight) && !_folders.empty()) {
        const int n = static_cast<int>(_folders.size()) + 1; // with "all" as -1
        _folder = (_folder + 1 + (ev.key == key::kRight ? 1 : n - 1)) % n - 1;
        applyFolder();
        draw();
        return;
    }
    if (_shown.empty())
        return;
    // The wheel scrolls the list; keys move the selection, the list following.
    const int count = static_cast<int>(_shown.size());
    if (ev.type == EventType::kMouseWheel) {
        _top = std::clamp(_top + step, 0, std::max(0, count - page));
        _selected = std::clamp(_selected, _top, std::min(count, _top + page) - 1);
    } else {
        _selected = static_cast<int>(std::clamp<int64_t>(int64_t(_selected) + step, 0, count - 1));
        if (_selected < _top)
            _top = _selected;
        else if (_selected >= _top + page)
            _top = _selected - page + 1;
    }
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    const int page = rows();
    if (_viewing) {
        text(4, 4, _viewName + (_isText ? "" : " (hex)"), {150, 200, 255});
        text(4, 4 + lh,
             "line " + std::to_string(_lineTop + 1) + " of " + std::to_string(_lines.size()) +
                 "; Up/Down, PageUp/PageDown, Home/End scroll; Esc or Backspace goes back",
             {150, 150, 150});
        for (int i = 0; i < page && _lineTop + i < static_cast<int>(_lines.size()); ++i)
            text(4, 3 * lh + i * lh, _lines[size_t(_lineTop + i)]);
        return;
    }

    std::string order;
    for (const Archive *a : _ctx.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4, "Search path, first match wins: " + order, {150, 200, 255});
    text(4, 4 + lh,
         "Folder (Left/Right): " + (_folder < 0 ? std::string("all") : _folders[size_t(_folder)]) + ", " +
             std::to_string(_shown.size()) + " files; Enter opens",
         {150, 200, 255});
    if (!_ctx.warnings.empty())
        text(4, 4 + 2 * lh, std::to_string(_ctx.warnings.size()) + " problems, the first: " + _ctx.warnings.front(),
             {255, 120, 120});
    const int sourceX = _ctx.screen.w - 18 * font::kAdvance;
    for (int i = 0; i < page && _top + i < static_cast<int>(_shown.size()); ++i) {
        const Row &r = _files[_shown[size_t(_top + i)]];
        const int y = 4 * lh + i * lh;
        if (_top + i == _selected)
            fill(0, y - 1, _ctx.screen.w, lh, 50, 60, 90);
        text(4, y, r.name);
        const std::string size = formatSize(r.size);
        text(sourceX - 2 * font::kAdvance - static_cast<int>(size.size()) * font::kAdvance, y, size, {180, 180, 180});
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

} // namespace reluva::age
