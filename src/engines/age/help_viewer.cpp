// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/help_viewer.h"

#include "common/font.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr int kLeft = 24, kTop = 52, kBottom = kFrameH - 30, kRow = 14;
constexpr int kColumns = (kFrameW - 2 * kLeft) / font::kAdvance; // characters on a line
constexpr int kTab = 24;                                         // a table's second column
constexpr size_t kBullet = 3; // characters before a tab that are a list's bullet (or number)
constexpr int kPictureWidth = kFrameW - 2 * kLeft, kPictureHeight = 300; // the most a picture's shown at
const Color kText{235, 235, 235}, kLink{120, 230, 140}, kChosen{255, 255, 120};

void fill(Surface& o, int x, int y, int w, int h, Color c, uint8_t alpha) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t* p = o.row(yy) + xx * 4;
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = alpha;
        }
}

// The size a picture is shown at: as it is, or smaller to fit.
void shownSize(const winhelp::Picture& p, int& w, int& h) {
    const float scale = std::min({1.0f, float(kPictureWidth) / float(std::max(1, p.width)),
                                  float(kPictureHeight) / float(std::max(1, p.height))});
    w = std::max(1, int(float(p.width) * scale));
    h = std::max(1, int(float(p.height) * scale));
}

} // namespace

Error HelpViewer::load(const SearchSet& files) {
    if (Error err = files.read("MM2HELP.HLP", file_))
        return err;
    if (Error err = winhelp::readHelp(file_, topics_))
        return Error::fail("MM2HELP.HLP: " + err.message);
    list_.clear();
    for (size_t i = 0; i < topics_.size(); ++i)
        if (!topics_[i].title.empty() && !winhelp::plainText(topics_[i].text).empty())
            list_.push_back(int(i));
    return Error::success();
}

void HelpViewer::open() {
    open_ = loaded();
    reading_ = -1;
    back_.clear();
}

int HelpViewer::visibleRows() const { return (kBottom - kTop) / kRow; }

const winhelp::Picture* HelpViewer::picture(int number) const {
    auto it = pictures_.find(number);
    if (it == pictures_.end()) {
        winhelp::Picture p;
        if (winhelp::readPicture(file_, number, p))
            p = winhelp::Picture{}; // not read: nothing shown
        it = pictures_.emplace(number, std::move(p)).first;
    }
    return it->second.width > 0 ? &it->second : nullptr;
}

int HelpViewer::pictureLines(const winhelp::Picture& p) const {
    int w = 0, h = 0;
    shownSize(p, w, h);
    return (h + kRow - 1) / kRow + 1;
}

void HelpViewer::openTopic(int index, bool remember) {
    if (index < 0 || index >= int(topics_.size()))
        return;
    if (remember && reading_ >= 0)
        back_.push_back(reading_);
    reading_ = index;
    layOut();
    line_ = 0;
    link_ = links_.empty() ? -1 : 0;
}

void HelpViewer::layOut() {
    // The topic's text in lines of at most kColumns characters, broken
    // between words, each character with its link; a tab moves on to the
    // next column (a table's second goes on under it); a picture on lines
    // of its own.
    lines_.clear();
    links_.clear();
    const std::string& text = topics_[size_t(reading_)].text;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        Line para;
        int link = -1;
        size_t firstStop = 0;
        for (size_t i = at; i < end; ++i) {
            const char c = text[i];
            if (c == winhelp::kPictureMark) {
                const size_t close = text.find(c, i + 1);
                const int number = std::atoi(text.c_str() + i + 1);
                i = close == std::string::npos ? end : close;
                if (const winhelp::Picture* p = picture(number)) {
                    Line l;
                    l.picture = number;
                    lines_.push_back(l);
                    for (int k = 1; k < pictureLines(*p); ++k)
                        lines_.emplace_back();
                }
            } else if (c == winhelp::kLinkMark) {
                const size_t close = text.find(c, i + 1);
                links_.push_back(std::atoi(text.c_str() + i + 1));
                link = int(links_.size()) - 1;
                i = close == std::string::npos ? end : close;
            } else if (c == winhelp::kLinkEnd) {
                link = -1;
            } else if (c == '\t') {
                // After a list's bullet, a little way on; else to the
                // second column.
                const size_t stop = para.text.size() <= kBullet ? kBullet + 1 : size_t(kTab);
                const size_t n = para.text.size() < stop ? stop - para.text.size() : 2;
                if (firstStop == 0)
                    firstStop = para.text.size() + n;
                para.text.append(n, ' ');
                para.link.insert(para.link.end(), n, -1);
            } else {
                para.text += c;
                para.link.push_back(link);
            }
        }
        const size_t indent = firstStop; // a wrapped line goes on under the first tab's column
        if (para.text.empty() && (lines_.empty() || lines_.back().picture < 0))
            lines_.emplace_back();
        while (!para.text.empty()) {
            if (para.text.size() <= size_t(kColumns)) {
                lines_.push_back(para);
                break;
            }
            size_t cut = para.text.rfind(' ', size_t(kColumns));
            if (cut == std::string::npos || cut <= indent)
                cut = kColumns;
            Line l;
            l.text = para.text.substr(0, cut);
            l.link.assign(para.link.begin(), para.link.begin() + std::ptrdiff_t(cut));
            lines_.push_back(l);
            const size_t from = para.text[cut] == ' ' ? cut + 1 : cut;
            para.text = std::string(indent, ' ') + para.text.substr(from);
            para.link.erase(para.link.begin(), para.link.begin() + std::ptrdiff_t(from));
            para.link.insert(para.link.begin(), indent, -1);
        }
        at = end + 1;
    }
}

void HelpViewer::scroll(int lines) {
    if (reading_ >= 0) {
        line_ = std::clamp(line_ + lines, 0, std::max(0, int(lines_.size()) - visibleRows()));
        return;
    }
    row_ = std::clamp(row_ + lines, 0, int(list_.size()) - 1);
    if (row_ < top_)
        top_ = row_;
    if (row_ >= top_ + visibleRows())
        top_ = row_ - visibleRows() + 1;
}

void HelpViewer::showLink(int link) {
    for (int i = 0; i < int(lines_.size()); ++i)
        if (std::find(lines_[size_t(i)].link.begin(), lines_[size_t(i)].link.end(), link) !=
            lines_[size_t(i)].link.end()) {
            if (i < line_ || i >= line_ + visibleRows())
                line_ = std::clamp(i - visibleRows() / 3, 0, std::max(0, int(lines_.size()) - visibleRows()));
            return;
        }
}

void HelpViewer::follow(int link) {
    if (link >= 0 && link < int(links_.size()))
        openTopic(links_[size_t(link)]);
}

void HelpViewer::handle(const Event& ev) {
    if (!open_)
        return;
    const int page = visibleRows() - 1;
    auto back = [&] {
        if (!back_.empty()) {
            const int to = back_.back();
            back_.pop_back();
            openTopic(to, false);
        } else {
            reading_ = -1;
        }
    };
    if (ev.type == EventType::MouseWheel) {
        scroll(-ev.wheelY * 3);
        return;
    }
    if (ev.type == EventType::MouseDown) {
        if (ev.button == MouseButton::Right) {
            if (reading_ >= 0)
                back();
            else
                open_ = false;
        } else if (ev.button == MouseButton::Left && ev.mouseY >= kTop && ev.mouseY < kBottom) {
            const int row = (ev.mouseY - kTop) / kRow;
            if (reading_ < 0) {
                const int at = top_ + row;
                if (at < int(list_.size())) {
                    row_ = at;
                    back_.clear();
                    openTopic(list_[size_t(at)], false);
                }
            } else if (line_ + row < int(lines_.size())) {
                // A link's words clicked.
                const Line& l = lines_[size_t(line_ + row)];
                const int column = (ev.mouseX - kLeft) / font::kAdvance;
                if (column >= 0 && column < int(l.link.size()) && l.link[size_t(column)] >= 0)
                    follow(l.link[size_t(column)]);
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
    case key::Tab: // the next link (Shift: the one before)
        if (reading_ >= 0 && !links_.empty()) {
            const int n = int(links_.size());
            link_ = ((link_ < 0 ? 0 : link_) + ((ev.mods & keymod::Shift) ? n - 1 : 1)) % n;
            showLink(link_);
        }
        break;
    case key::Return:
    case key::KpEnter:
    case key::Right:
        if (reading_ < 0) {
            back_.clear();
            openTopic(list_[size_t(row_)], false);
        } else if (ev.key != key::Right) {
            follow(link_);
        }
        break;
    case key::Escape:
        if (reading_ >= 0) {
            reading_ = -1;
            back_.clear();
        } else {
            open_ = false;
        }
        break;
    case key::Backspace:
    case key::Left:
        if (reading_ >= 0)
            back();
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
        for (int i = 0; i < rows && top_ + i < int(list_.size()); ++i) {
            const int t = top_ + i;
            const bool chosen = t == row_;
            if (chosen)
                fill(o, kLeft - 4, kTop + i * kRow - 2, kFrameW - 2 * kLeft + 8, kRow, {60, 60, 140}, 255);
            font::drawText(o, kLeft, kTop + i * kRow, topics_[size_t(list_[size_t(t)])].title,
                           chosen ? Color{255, 255, 120} : Color{210, 210, 255});
        }
        font::drawText(o, kLeft, kFrameH - 20, "Up/Down, Enter: read   Esc: close", {150, 200, 255});
        return;
    }
    // Pictures first (one starting above the top shows its lower part),
    // clipped to the page; then the words, links coloured.
    for (int i = std::max(0, line_ - kPictureHeight / kRow - 1); i < line_ + rows && i < int(lines_.size()); ++i) {
        const Line& l = lines_[size_t(i)];
        const winhelp::Picture* p = l.picture >= 0 ? picture(l.picture) : nullptr;
        if (!p)
            continue;
        int w = 0, h = 0;
        shownSize(*p, w, h);
        const int y0 = kTop + (i - line_) * kRow, x0 = kLeft;
        for (int y = std::max(0, kTop - y0); y < h && y0 + y < kBottom; ++y) {
            uint8_t* to = o.row(y0 + y) + x0 * 4;
            const int sy = y * p->height / h;
            for (int x = 0; x < w && x0 + x < o.w; ++x, to += 4) {
                const uint8_t* from = p->rgba.data() + (size_t(sy) * size_t(p->width) + size_t(x * p->width / w)) * 4;
                to[0] = from[0], to[1] = from[1], to[2] = from[2], to[3] = 255;
            }
        }
    }
    for (int i = 0; i < rows && line_ + i < int(lines_.size()); ++i) {
        const Line& l = lines_[size_t(line_ + i)];
        const int y = kTop + i * kRow;
        // Runs of the same link (or none) drawn together.
        for (size_t a = 0; a < l.text.size();) {
            size_t b = a;
            while (b < l.text.size() && l.link[b] == l.link[a])
                ++b;
            const int link = l.link[a];
            const Color c = link < 0 ? kText : link == link_ ? kChosen : kLink;
            const int x = kLeft + int(a) * font::kAdvance;
            font::drawText(o, x, y, l.text.substr(a, b - a), c);
            if (link >= 0) // underlined
                fill(o, x, y + font::kLineHeight - 1, int(b - a) * font::kAdvance, 1, c, 255);
            a = b;
        }
    }
    char where[64];
    std::snprintf(where, sizeof where, "%d/%zu", std::min(line_ + rows, int(lines_.size())), lines_.size());
    font::drawText(o, kFrameW - kLeft - int(font::length(where)) * font::kAdvance, kFrameH - 20, where,
                   {150, 150, 200});
    font::drawText(o, kLeft, kFrameH - 20, "Tab: next link  Enter: follow  Backspace: back  Esc: topics",
                   {150, 200, 255});
}

} // namespace reluva::age
