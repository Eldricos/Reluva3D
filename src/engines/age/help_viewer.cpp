// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/help_viewer.h"

#include "common/font.h"

#include <algorithm>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr int kLeft = 24, kTop = 52, kBottom = kFrameH - 30, kRow = 14;
constexpr int kColumns = (kFrameW - 2 * kLeft) / font::kAdvance; // characters on a line
constexpr int kTab = 24;                                         // a table's second column

// Text in lines of at most `width` characters, broken between words; a
// tab moves on to the next column.
std::vector<std::string> wrap(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string para;
        for (char c : text.substr(at, end - at)) {
            if (c == '\t')
                para.append(para.size() < kTab ? kTab - para.size() : 2, ' ');
            else
                para += c;
        }
        // A table's row goes on under its second column.
        const size_t indent = text.substr(at, end - at).find('\t') != std::string::npos ? kTab : 0;
        if (para.empty())
            lines.emplace_back();
        while (!para.empty()) {
            if (para.size() <= width) {
                lines.push_back(para);
                break;
            }
            size_t cut = para.rfind(' ', width);
            if (cut == std::string::npos || cut <= indent)
                cut = width;
            lines.push_back(para.substr(0, cut));
            para = std::string(indent, ' ') + para.substr(para[cut] == ' ' ? cut + 1 : cut);
        }
        at = end + 1;
    }
    return lines;
}

void fill(Surface& o, int x, int y, int w, int h, Color c, uint8_t alpha) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t* p = o.row(yy) + xx * 4;
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = alpha;
        }
}

} // namespace

Error HelpViewer::load(const SearchSet& files) {
    std::vector<uint8_t> data;
    if (Error err = files.read("MM2HELP.HLP", data))
        return err;
    std::vector<winhelp::Topic> all;
    if (Error err = winhelp::readHelp(data, all))
        return Error::fail("MM2HELP.HLP: " + err.message);
    topics_.clear();
    for (winhelp::Topic& t : all)
        if (!t.title.empty() && !t.text.empty())
            topics_.push_back(std::move(t));
    return Error::success();
}

void HelpViewer::open() {
    open_ = loaded();
    reading_ = -1;
}

int HelpViewer::visibleRows() const { return (kBottom - kTop) / kRow; }

void HelpViewer::openTopic(int index) {
    if (index < 0 || index >= int(topics_.size()))
        return;
    reading_ = index;
    lines_ = wrap(topics_[size_t(index)].text, kColumns);
    line_ = 0;
}

void HelpViewer::scroll(int lines) {
    if (reading_ >= 0) {
        line_ = std::clamp(line_ + lines, 0, std::max(0, int(lines_.size()) - visibleRows()));
        return;
    }
    row_ = std::clamp(row_ + lines, 0, int(topics_.size()) - 1);
    if (row_ < top_)
        top_ = row_;
    if (row_ >= top_ + visibleRows())
        top_ = row_ - visibleRows() + 1;
}

void HelpViewer::handle(const Event& ev) {
    if (!open_)
        return;
    const int page = visibleRows() - 1;
    if (ev.type == EventType::MouseWheel) {
        scroll(-ev.wheelY * 3);
        return;
    }
    if (ev.type == EventType::MouseDown) {
        if (ev.button == MouseButton::Right) { // back
            if (reading_ >= 0)
                reading_ = -1;
            else
                open_ = false;
        } else if (ev.button == MouseButton::Left && reading_ < 0 && ev.mouseY >= kTop && ev.mouseY < kBottom) {
            const int row = top_ + (ev.mouseY - kTop) / kRow;
            if (row < int(topics_.size())) {
                row_ = row;
                openTopic(row);
            }
        }
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    switch (ev.key) {
    case key::Up:
        scroll(-1);
        break;
    case key::Down:
        scroll(1);
        break;
    case key::PageUp:
        scroll(-page);
        break;
    case key::PageDown:
    case key::Space:
        scroll(page);
        break;
    case key::Home:
        scroll(-100000);
        break;
    case key::End:
        scroll(100000);
        break;
    case key::Return:
    case key::KpEnter:
    case key::Right:
        if (reading_ < 0)
            openTopic(row_);
        break;
    case key::Escape:
    case key::Backspace:
    case key::Left:
        if (reading_ >= 0)
            reading_ = -1;
        else if (ev.key == key::Escape)
            open_ = false;
        break;
    default:
        break;
    }
}

void HelpViewer::draw(Surface& o) const {
    if (!open_)
        return;
    fill(o, 0, 0, kFrameW, kFrameH, {10, 12, 40}, 235);
    fill(o, 0, 0, kFrameW, 40, {30, 30, 90}, 255);
    const std::string heading = reading_ >= 0 ? topics_[size_t(reading_)].title : std::string("Midtown Madness 2 Help");
    font::drawText(o, kLeft, 12, heading, {255, 230, 140}, 2);
    const int rows = visibleRows();
    if (reading_ < 0) {
        for (int i = 0; i < rows && top_ + i < int(topics_.size()); ++i) {
            const int t = top_ + i;
            const bool chosen = t == row_;
            if (chosen)
                fill(o, kLeft - 4, kTop + i * kRow - 2, kFrameW - 2 * kLeft + 8, kRow, {60, 60, 140}, 255);
            font::drawText(o, kLeft, kTop + i * kRow, topics_[size_t(t)].title,
                           chosen ? Color{255, 255, 120} : Color{210, 210, 255});
        }
        font::drawText(o, kLeft, kFrameH - 20, "Up/Down, Enter: read   Esc: close", {150, 200, 255});
        return;
    }
    for (int i = 0; i < rows && line_ + i < int(lines_.size()); ++i)
        font::drawText(o, kLeft, kTop + i * kRow, lines_[size_t(line_ + i)], {235, 235, 235});
    char where[64];
    std::snprintf(where, sizeof where, "%d/%zu", std::min(line_ + rows, int(lines_.size())), lines_.size());
    font::drawText(o, kFrameW - kLeft - int(font::length(where)) * font::kAdvance, kFrameH - 20, where,
                   {150, 150, 200});
    font::drawText(o, kLeft, kFrameH - 20, "Up/Down, Page Up/Down: scroll   Esc: back to the topics", {150, 200, 255});
}

} // namespace reluva::age
