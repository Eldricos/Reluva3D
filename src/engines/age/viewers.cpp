// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's shared drawing and its Files page.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "engines/age/ar_archive.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace reluva::age {

namespace {

constexpr size_t kMaxDumpBytes = 1 << 20; // hex dumps stop after this much

std::string topFolder(const std::string& name) {
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

// --- Page ------------------------------------------------------------------------------

void Page::clear(uint8_t r, uint8_t g, uint8_t b) { fill(0, 0, ctx_.screen.w, ctx_.screen.h, r, g, b); }

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface& s = ctx_.screen;
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w), y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t* p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string& s, Color c) { font::drawText(ctx_.screen, x, y, s, c); }

void Page::setTitle(const std::string& detail) {
    ctx_.sys.setWindowTitle(std::string("Reluva - ") + ctx_.game.name + " - " + name() + ": " + detail);
}

// --- fileLines --------------------------------------------------------------------------

std::vector<std::string> fileLines(const std::vector<uint8_t>& data, bool& isText) {
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

// --- FileViewer -------------------------------------------------------------------------

FileViewer::FileViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        Row r{n, {}, 0};
        if (const Archive* a = ctx.files.find(n)) {
            r.source = std::filesystem::path(a->describe()).filename().string();
            if (auto* ar = dynamic_cast<const ArArchive*>(a)) {
                r.size = ar->entry(n)->size;
            } else {
                std::error_code ec;
                const auto size = std::filesystem::file_size(std::filesystem::path(a->describe()) / n, ec);
                r.size = ec ? 0 : size;
            }
        }
        const std::string top = topFolder(n);
        if (!top.empty() && std::find(folders_.begin(), folders_.end(), top) == folders_.end())
            folders_.push_back(top);
        files_.push_back(std::move(r));
    }
    std::sort(folders_.begin(), folders_.end());
    applyFolder();
}

void FileViewer::applyFolder() {
    shown_.clear();
    for (size_t i = 0; i < files_.size(); ++i)
        if (folder_ < 0 || topFolder(files_[i].name) == folders_[size_t(folder_)])
            shown_.push_back(i);
    selected_ = top_ = 0;
}

int FileViewer::rows() const { return (ctx_.screen.h - 4 * font::kLineHeight) / font::kLineHeight; }

void FileViewer::activate() {
    setTitle(std::to_string(files_.size()) + " files in " + std::to_string(ctx_.files.size()) + " archives");
    draw();
}

bool FileViewer::takesKey(const Event& ev) const {
    return viewing_ && ev.type == EventType::KeyDown && ev.key == key::Escape;
}

void FileViewer::open() {
    if (shown_.empty())
        return;
    const Row& r = files_[shown_[size_t(selected_)]];
    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(r.name, data)) {
        lines_ = {err.message};
        isText_ = true;
    } else {
        lines_ = fileLines(data, isText_);
    }
    viewing_ = true;
    viewName_ = r.name;
    lineTop_ = 0;
    setTitle(viewName_);
}

void FileViewer::handleEvent(const Event& ev) {
    const int page = rows();
    int step = 0;
    if (ev.type == EventType::MouseWheel)
        step = -ev.wheelY * 3;
    else if (ev.type != EventType::KeyDown)
        return;
    else if (ev.key == key::Up)
        step = -1;
    else if (ev.key == key::Down)
        step = 1;
    else if (ev.key == key::PageUp)
        step = -page;
    else if (ev.key == key::PageDown)
        step = page;
    else if (ev.key == key::Home)
        step = -1 << 30;
    else if (ev.key == key::End)
        step = 1 << 30;

    if (viewing_) {
        if (ev.type == EventType::KeyDown && (ev.key == key::Escape || ev.key == key::Backspace)) {
            viewing_ = false;
            lines_.clear();
            activate();
            return;
        }
        const int last = std::max(0, static_cast<int>(lines_.size()) - (page - 1));
        lineTop_ = std::clamp(lineTop_ + step, 0, last);
        draw();
        return;
    }

    if (ev.type == EventType::KeyDown && (ev.key == key::Return || ev.key == key::KpEnter)) {
        open();
        draw();
        return;
    }
    if (ev.type == EventType::KeyDown && (ev.key == key::Left || ev.key == key::Right) && !folders_.empty()) {
        const int n = static_cast<int>(folders_.size()) + 1; // with "all" as -1
        folder_ = (folder_ + 1 + (ev.key == key::Right ? 1 : n - 1)) % n - 1;
        applyFolder();
        draw();
        return;
    }
    if (shown_.empty())
        return;
    // The wheel scrolls the list; keys move the selection, the list following.
    const int count = static_cast<int>(shown_.size());
    if (ev.type == EventType::MouseWheel) {
        top_ = std::clamp(top_ + step, 0, std::max(0, count - page));
        selected_ = std::clamp(selected_, top_, std::min(count, top_ + page) - 1);
    } else {
        selected_ = static_cast<int>(std::clamp<int64_t>(int64_t(selected_) + step, 0, count - 1));
        if (selected_ < top_)
            top_ = selected_;
        else if (selected_ >= top_ + page)
            top_ = selected_ - page + 1;
    }
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    const int page = rows();
    if (viewing_) {
        text(4, 4, viewName_ + (isText_ ? "" : " (hex)"), {150, 200, 255});
        text(4, 4 + lh,
             "line " + std::to_string(lineTop_ + 1) + " of " + std::to_string(lines_.size()) +
                 "; Up/Down, PageUp/PageDown, Home/End scroll; Esc or Backspace goes back",
             {150, 150, 150});
        for (int i = 0; i < page && lineTop_ + i < static_cast<int>(lines_.size()); ++i)
            text(4, 3 * lh + i * lh, lines_[size_t(lineTop_ + i)]);
        return;
    }

    std::string order;
    for (const Archive* a : ctx_.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4, "Search path, first match wins: " + order, {150, 200, 255});
    text(4, 4 + lh,
         "Folder (Left/Right): " + (folder_ < 0 ? std::string("all") : folders_[size_t(folder_)]) + ", " +
             std::to_string(shown_.size()) + " files; Enter opens",
         {150, 200, 255});
    if (!ctx_.warnings.empty())
        text(4, 4 + 2 * lh,
             std::to_string(ctx_.warnings.size()) + " problems, the first: " + ctx_.warnings.front(),
             {255, 120, 120});
    const int sourceX = ctx_.screen.w - 18 * font::kAdvance;
    for (int i = 0; i < page && top_ + i < static_cast<int>(shown_.size()); ++i) {
        const Row& r = files_[shown_[size_t(top_ + i)]];
        const int y = 4 * lh + i * lh;
        if (top_ + i == selected_)
            fill(0, y - 1, ctx_.screen.w, lh, 50, 60, 90);
        text(4, y, r.name);
        const std::string size = formatSize(r.size);
        text(sourceX - 2 * font::kAdvance - static_cast<int>(size.size()) * font::kAdvance, y, size,
             {180, 180, 180});
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

} // namespace reluva::age
