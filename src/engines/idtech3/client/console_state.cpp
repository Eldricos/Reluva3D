// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/console_state.h"

#include "engines/idtech3/client/page.h"

#include "graphics/font.h"

#include <algorithm>

namespace reluva::idtech3 {

namespace {

constexpr size_t kMaxLines = 1000;
constexpr size_t kMaxNotify = 4; // the original's notify lines
constexpr size_t kMaxField = 255;

} // namespace

void ConsoleState::print(const std::string &text, bool notify, uint64_t nowMs, int stream) {
    std::string &partial = _partial[size_t(stream) & 1];
    partial += text;
    size_t end;
    while ((end = partial.find('\n')) != std::string::npos) {
        const std::string line = partial.substr(0, end);
        partial.erase(0, end + 1);
        _lines.push_back(line);
        while (_lines.size() > kMaxLines)
            _lines.pop_front();
        if (_scroll > 0)
            ++_scroll;
        if (!notify)
            continue;
        _notify.emplace_back(nowMs, line);
        while (_notify.size() > kMaxNotify)
            _notify.pop_front();
    }
}

std::vector<std::string> ConsoleState::notify(uint64_t nowMs, float forMs) const {
    std::vector<std::string> out;
    for (const auto &[when, line] : _notify)
        if (float(nowMs - std::min(nowMs, when)) <= forMs)
            out.push_back(line);
    return out;
}

void ConsoleState::expireNotify(uint64_t nowMs, uint64_t forMs) {
    // (A line printed during this frame is stamped a little after nowMs.)
    while (!_notify.empty() && _notify.front().first + forMs < nowMs)
        _notify.pop_front();
}

void ConsoleState::beginTyping(uint32_t swallow) {
    _field.clear();
    _cursor = 0;
    _scroll = 0;
    _historyPos = _history.size();
    _swallow = swallow;
}

void ConsoleState::clearField() {
    _field.clear();
    _cursor = 0;
}

ConsoleState::Action ConsoleState::handle(const Event &ev, bool console) {
    if (ev.type == EventType::kTextInput) {
        // Characters typed (Latin-1, as the original's), but the one of the
        // key that opened the typing, and the console key in the console.
        for (uint32_t cp : decodeUtf8(ev.text)) {
            if (cp == _swallow) {
                _swallow = 0;
                continue;
            }
            _swallow = 0;
            if (cp < 32 || cp == 127 || cp > 255 || _field.size() >= kMaxField)
                continue;
            if (console && (cp == '`' || cp == '~'))
                continue;
            _field.insert(_cursor++, 1, char(cp));
        }
        return Action::kNone;
    }
    if (ev.type != EventType::kKeyDown)
        return Action::kNone;
    _swallow = 0;
    switch (ev.key) {
    case key::kEscape:
        return Action::kClose;
    case key::kReturn:
    case key::kKpEnter:
        return Action::kSubmit;
    case key::kBackspace:
        if (_cursor > 0)
            _field.erase(--_cursor, 1);
        return Action::kNone;
    case key::kDelete:
        if (_cursor < _field.size())
            _field.erase(_cursor, 1);
        return Action::kNone;
    case key::kLeft:
        _cursor = _cursor > 0 ? _cursor - 1 : 0;
        return Action::kNone;
    case key::kRight:
        _cursor = std::min(_field.size(), _cursor + 1);
        return Action::kNone;
    case key::kHome:
        _cursor = 0;
        return Action::kNone;
    case key::kEnd:
        _cursor = _field.size();
        return Action::kNone;
    default:
        break;
    }
    if (!console)
        return Action::kNone;
    switch (ev.key) {
    case '`':
        return Action::kClose; // the console key closes it again
    case key::kUp:
    case key::kDown:
        // Earlier lines typed, back and forth.
        if (_history.empty())
            return Action::kNone;
        if (ev.key == key::kUp && _historyPos > 0)
            --_historyPos;
        else if (ev.key == key::kDown && _historyPos < _history.size())
            ++_historyPos;
        _field = _historyPos < _history.size() ? _history[_historyPos] : std::string();
        _cursor = _field.size();
        return Action::kNone;
    case key::kPageUp:
        _scroll = std::min(_scroll + 4, int(_lines.size()));
        return Action::kNone;
    case key::kPageDown:
        _scroll = std::max(_scroll - 4, 0);
        return Action::kNone;
    default:
        return Action::kNone;
    }
}

std::string ConsoleState::submit(bool console) {
    std::string text = std::move(_field);
    clearField();
    if (!console || text.empty())
        return text;
    // Quotes would end the line early: they become apostrophes.
    std::replace(text.begin(), text.end(), '"', '\'');
    if (_history.empty() || _history.back() != text)
        _history.push_back(text);
    _historyPos = _history.size();
    _lines.push_back("]" + text);
    _scroll = 0;
    return text;
}

float ConsoleState::slide(uint64_t nowMs, bool open, float speed) {
    const float dt = _frameMs ? float(nowMs - _frameMs) / 1000.0f : 0.0f;
    _frameMs = nowMs;
    const float target = open ? 0.5f : 0.0f;
    _frac = _frac < target ? std::min(target, _frac + speed * dt) : std::max(target, _frac - speed * dt);
    return _frac;
}

void drawColouredText(Surface &surface, int x, int y, const std::string &line) {
    static const Color kColors[8] = {{0, 0, 0},   {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255}, {0, 255, 255}, {255, 0, 255}, {255, 255, 255}};
    Color color{255, 255, 255};
    std::string run;
    auto flush = [&] {
        if (run.empty())
            return;
        font::drawText(surface, x + 1, y + 1, run, {0, 0, 0});
        font::drawText(surface, x, y, run, color);
        x += int(font::length(run)) * font::kAdvance;
        run.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '^' && i + 1 < line.size() && line[i + 1] != '^') {
            flush();
            color = kColors[(line[i + 1] - '0') & 7];
            ++i;
            continue;
        }
        run += line[i];
    }
    flush();
}

void drawOwnConsole(Surface &surface, const ConsoleState &console, bool blink) {
    const int lh = font::kLineHeight;
    const int height = surface.h / 2;
    for (int yy = 0; yy < height; ++yy) {
        uint8_t *p = surface.row(yy);
        for (int x = 0; x < surface.w; ++x) {
            p[x * 4 + 0] = 10, p[x * 4 + 1] = 14, p[x * 4 + 2] = 24;
            p[x * 4 + 3] = 215;
        }
    }
    for (int x = 0; x < surface.w; ++x) {
        uint8_t *p = surface.row(height) + x * 4;
        p[0] = 200, p[1] = 140, p[2] = 40, p[3] = 255;
    }
    const std::string &field = console.field();
    drawColouredText(surface, 4, height - lh - 4,
                     "]" + field.substr(0, console.cursor()) + (blink ? "_" : " ") + field.substr(console.cursor()));
    const auto &lines = console.lines();
    int y = height - 2 * lh - 4;
    for (int i = int(lines.size()) - 1 - console.scroll(); i >= 0 && y >= 0; --i, y -= lh)
        drawColouredText(surface, 4, y, lines[size_t(i)]);
}

} // namespace reluva::idtech3
