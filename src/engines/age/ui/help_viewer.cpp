// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/ui/help_viewer.h"

#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr int kLeft = 24;
constexpr int kTop = 52;
constexpr int kBottom = kFrameH - 30;
constexpr int kRow = 14;
constexpr int kColumns = (kFrameW - 2 * kLeft) / font::kAdvance; // characters on a line
constexpr int kTab = 24;                                         // a table's second column
constexpr size_t kBullet = 3; // characters before a tab that are a list's bullet (or number)
// the most a picture's shown at
constexpr int kPictureWidth = kFrameW - 2 * kLeft;
constexpr int kPictureHeight = 300;
const Color kText{235, 235, 235};
const Color kLink{120, 230, 140};
const Color kChosen{255, 255, 120};

void fill(Surface &o, int x, int y, int w, int h, Color c, uint8_t alpha) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t *p = o.row(yy) + xx * 4;
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = alpha;
        }
}

// The size a picture is shown at: as it is, or smaller to fit.
void shownSize(const winhelp::Picture &p, int &w, int &h) {
    const float scale = std::min({1.0f, float(kPictureWidth) / float(std::max(1, p.width)),
                                  float(kPictureHeight) / float(std::max(1, p.height))});
    w = std::max(1, int(float(p.width) * scale));
    h = std::max(1, int(float(p.height) * scale));
}

} // namespace

Error HelpViewer::load(const SearchSet &files) {
    if (Error err = files.read("MM2HELP.HLP", _file))
        return err;
    if (Error err = winhelp::readHelp(_file, _topics))
        return Error::fail("MM2HELP.HLP: " + err.message);
    _list.clear();
    for (size_t i = 0; i < _topics.size(); ++i)
        if (!_topics[i].title.empty() && !winhelp::plainText(_topics[i].text).empty())
            _list.push_back(int(i));
    return Error::success();
}

void HelpViewer::open() {
    _open = loaded();
    _reading = -1;
    _back.clear();
}

int HelpViewer::visibleRows() const {
    return (kBottom - kTop) / kRow;
}

const winhelp::Picture *HelpViewer::picture(int number) const {
    auto it = _pictures.find(number);
    if (it == _pictures.end()) {
        winhelp::Picture p;
        if (winhelp::readPicture(_file, number, p))
            p = winhelp::Picture{}; // not read: nothing shown
        it = _pictures.emplace(number, std::move(p)).first;
    }
    return it->second.width > 0 ? &it->second : nullptr;
}

int HelpViewer::pictureLines(const winhelp::Picture &p) const {
    int w = 0, h = 0;
    shownSize(p, w, h);
    return (h + kRow - 1) / kRow + 1;
}

void HelpViewer::openTopic(int index, bool remember) {
    if (index < 0 || index >= int(_topics.size()))
        return;
    if (remember && _reading >= 0)
        _back.push_back(_reading);
    _reading = index;
    layOut();
    _line = 0;
    _link = _links.empty() ? -1 : 0;
}

void HelpViewer::layOut() {
    // The topic's text in lines of at most kColumns characters, broken
    // between words, each character with its link; a tab moves on to the
    // next column (a table's second goes on under it); a picture on lines
    // of its own.
    _lines.clear();
    _links.clear();
    const std::string &text = _topics[size_t(_reading)].text;
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
                if (const winhelp::Picture *p = picture(number)) {
                    Line l;
                    l.picture = number;
                    _lines.push_back(l);
                    for (int k = 1; k < pictureLines(*p); ++k)
                        _lines.emplace_back();
                }
            } else if (c == winhelp::kLinkMark) {
                const size_t close = text.find(c, i + 1);
                _links.push_back(std::atoi(text.c_str() + i + 1));
                link = int(_links.size()) - 1;
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
        if (para.text.empty() && (_lines.empty() || _lines.back().picture < 0))
            _lines.emplace_back();
        while (!para.text.empty()) {
            if (para.text.size() <= size_t(kColumns)) {
                _lines.push_back(para);
                break;
            }
            size_t cut = para.text.rfind(' ', size_t(kColumns));
            if (cut == std::string::npos || cut <= indent)
                cut = kColumns;
            Line l;
            l.text = para.text.substr(0, cut);
            l.link.assign(para.link.begin(), para.link.begin() + std::ptrdiff_t(cut));
            _lines.push_back(l);
            const size_t from = para.text[cut] == ' ' ? cut + 1 : cut;
            para.text = std::string(indent, ' ') + para.text.substr(from);
            para.link.erase(para.link.begin(), para.link.begin() + std::ptrdiff_t(from));
            para.link.insert(para.link.begin(), indent, -1);
        }
        at = end + 1;
    }
}

void HelpViewer::scroll(int lines) {
    if (_reading >= 0) {
        _line = std::clamp(_line + lines, 0, std::max(0, int(_lines.size()) - visibleRows()));
        return;
    }
    _row = std::clamp(_row + lines, 0, int(_list.size()) - 1);
    if (_row < _top)
        _top = _row;
    if (_row >= _top + visibleRows())
        _top = _row - visibleRows() + 1;
}

void HelpViewer::showLink(int link) {
    for (int i = 0; i < int(_lines.size()); ++i)
        if (std::find(_lines[size_t(i)].link.begin(), _lines[size_t(i)].link.end(), link) !=
            _lines[size_t(i)].link.end()) {
            if (i < _line || i >= _line + visibleRows())
                _line = std::clamp(i - visibleRows() / 3, 0, std::max(0, int(_lines.size()) - visibleRows()));
            return;
        }
}

void HelpViewer::follow(int link) {
    if (link >= 0 && link < int(_links.size()))
        openTopic(_links[size_t(link)]);
}

void HelpViewer::handle(const Event &ev) {
    if (!_open)
        return;
    const int page = visibleRows() - 1;
    auto back = [&] {
        if (!_back.empty()) {
            const int to = _back.back();
            _back.pop_back();
            openTopic(to, false);
        } else {
            _reading = -1;
        }
    };
    if (ev.type == EventType::kMouseWheel) {
        scroll(-ev.wheelY * 3);
        return;
    }
    if (ev.type == EventType::kMouseDown) {
        if (ev.button == MouseButton::kRight) {
            if (_reading >= 0)
                back();
            else
                _open = false;
        } else if (ev.button == MouseButton::kLeft && ev.mouseY >= kTop && ev.mouseY < kBottom) {
            const int row = (ev.mouseY - kTop) / kRow;
            if (_reading < 0) {
                const int at = _top + row;
                if (at < int(_list.size())) {
                    _row = at;
                    _back.clear();
                    openTopic(_list[size_t(at)], false);
                }
            } else if (_line + row < int(_lines.size())) {
                // A link's words clicked.
                const Line &l = _lines[size_t(_line + row)];
                const int column = (ev.mouseX - kLeft) / font::kAdvance;
                if (column >= 0 && column < int(l.link.size()) && l.link[size_t(column)] >= 0)
                    follow(l.link[size_t(column)]);
            }
        }
        return;
    }
    if (ev.type != EventType::kKeyDown)
        return;
    switch (ev.key) {
    case key::kUp:
        scroll(-1);
        break;
    case key::kDown:
        scroll(1);
        break;
    case key::kPageUp:
        scroll(-page);
        break;
    case key::kPageDown:
    case key::kSpace:
        scroll(page);
        break;
    case key::kHome:
        scroll(-100000);
        break;
    case key::kEnd:
        scroll(100000);
        break;
    case key::kTab: // the next link (Shift: the one before)
        if (_reading >= 0 && !_links.empty()) {
            const int n = int(_links.size());
            _link = ((_link < 0 ? 0 : _link) + ((ev.mods & keymod::kShift) ? n - 1 : 1)) % n;
            showLink(_link);
        }
        break;
    case key::kReturn:
    case key::kKpEnter:
    case key::kRight:
        if (_reading < 0) {
            _back.clear();
            openTopic(_list[size_t(_row)], false);
        } else if (ev.key != key::kRight) {
            follow(_link);
        }
        break;
    case key::kEscape:
        if (_reading >= 0) {
            _reading = -1;
            _back.clear();
        } else {
            _open = false;
        }
        break;
    case key::kBackspace:
    case key::kLeft:
        if (_reading >= 0)
            back();
        break;
    default:
        break;
    }
}

void HelpViewer::draw(Surface &o) const {
    if (!_open)
        return;
    fill(o, 0, 0, kFrameW, kFrameH, {10, 12, 40}, 235);
    fill(o, 0, 0, kFrameW, 40, {30, 30, 90}, 255);
    const std::string heading = _reading >= 0 ? _topics[size_t(_reading)].title : std::string("Midtown Madness 2 Help");
    font::drawText(o, kLeft, 12, heading, {255, 230, 140}, 2);
    const int rows = visibleRows();
    if (_reading < 0) {
        for (int i = 0; i < rows && _top + i < int(_list.size()); ++i) {
            const int t = _top + i;
            const bool chosen = t == _row;
            if (chosen)
                fill(o, kLeft - 4, kTop + i * kRow - 2, kFrameW - 2 * kLeft + 8, kRow, {60, 60, 140}, 255);
            font::drawText(o, kLeft, kTop + i * kRow, _topics[size_t(_list[size_t(t)])].title,
                           chosen ? Color{255, 255, 120} : Color{210, 210, 255});
        }
        font::drawText(o, kLeft, kFrameH - 20, "Up/Down, Enter: read   Esc: close", {150, 200, 255});
        return;
    }
    // Pictures first (one starting above the top shows its lower part),
    // clipped to the page; then the words, links coloured.
    for (int i = std::max(0, _line - kPictureHeight / kRow - 1); i < _line + rows && i < int(_lines.size()); ++i) {
        const Line &l = _lines[size_t(i)];
        const winhelp::Picture *p = l.picture >= 0 ? picture(l.picture) : nullptr;
        if (!p)
            continue;
        int w = 0, h = 0;
        shownSize(*p, w, h);
        const int y0 = kTop + (i - _line) * kRow, x0 = kLeft;
        for (int y = std::max(0, kTop - y0); y < h && y0 + y < kBottom; ++y) {
            uint8_t *to = o.row(y0 + y) + x0 * 4;
            const int sy = y * p->height / h;
            for (int x = 0; x < w && x0 + x < o.w; ++x, to += 4) {
                const uint8_t *from = p->rgba.data() + (size_t(sy) * size_t(p->width) + size_t(x * p->width / w)) * 4;
                to[0] = from[0], to[1] = from[1], to[2] = from[2], to[3] = 255;
            }
        }
    }
    for (int i = 0; i < rows && _line + i < int(_lines.size()); ++i) {
        const Line &l = _lines[size_t(_line + i)];
        const int y = kTop + i * kRow;
        // Runs of the same link (or none) drawn together.
        for (size_t a = 0; a < l.text.size();) {
            size_t b = a;
            while (b < l.text.size() && l.link[b] == l.link[a])
                ++b;
            const int link = l.link[a];
            const Color c = link < 0 ? kText : link == _link ? kChosen : kLink;
            const int x = kLeft + int(a) * font::kAdvance;
            font::drawText(o, x, y, l.text.substr(a, b - a), c);
            if (link >= 0) // underlined
                fill(o, x, y + font::kLineHeight - 1, int(b - a) * font::kAdvance, 1, c, 255);
            a = b;
        }
    }
    char where[64];
    std::snprintf(where, sizeof where, "%d/%zu", std::min(_line + rows, int(_lines.size())), _lines.size());
    font::drawText(o, kFrameW - kLeft - int(font::length(where)) * font::kAdvance, kFrameH - 20, where,
                   {150, 150, 200});
    font::drawText(o, kLeft, kFrameH - 20, "Tab: next link  Enter: follow  Backspace: back  Esc: topics",
                   {150, 200, 255});
}

} // namespace reluva::age
