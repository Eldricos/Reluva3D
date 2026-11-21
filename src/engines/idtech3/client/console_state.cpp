// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/console_state.h"

#include "engines/idtech3/client/console_drawer.h"
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
    // As jk2mp.exe's CL_ConsolePrint (0x406040): each print starts white
    // (0x4060ac), so a line carried on from an earlier one goes back to
    // white; a colour set goes on past a line's end to the next.
    std::string &partial = _partial[size_t(stream) & 1];
    char &colour = _partialColour[size_t(stream) & 1];
    if (colour != '7' && !partial.empty())
        partial += "^7";
    colour = '7';
    // A line after a newline starts in the colour in force, if this print
    // puts anything on it.
    char carried = '7';
    auto put = [&](std::string_view s) {
        if (partial.empty() && carried != '7')
            partial = {'^', carried};
        partial += s;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '^' && i + 1 < text.size() && text[i + 1] != '^') {
            colour = char('0' + (text[i + 1] & 7));
            put(std::string_view(text).substr(i, 2));
            ++i;
            continue;
        }
        if (c != '\n') {
            put(std::string_view(&c, 1));
            continue;
        }
        addLine(partial);
        if (notify) {
            _notify.emplace_back(nowMs, partial);
            while (_notify.size() > kMaxNotify)
                _notify.pop_front();
        }
        partial.clear();
        carried = colour;
    }
}

void ConsoleState::addLine(const std::string &line) {
    // Scrolled back, the view stays on the lines it shows (Con_Linefeed
    // moves it along only from the newest, 0x405fe4).
    _lines.push_back(line);
    while (_lines.size() > kMaxLines)
        _lines.pop_front();
    if (_scroll > 0)
        _scroll = std::min(_scroll + rows(line), std::max(0, totalRows() - 1));
}

int ConsoleState::rows(const std::string &line) const {
    return _wrapWidth > 0 ? int(wrapConsoleLine(line, _wrapWidth).size()) : 1;
}

int ConsoleState::totalRows() const {
    if (_wrapWidth <= 0)
        return int(_lines.size());
    int n = 0;
    for (const std::string &line : _lines)
        n += rows(line);
    return n;
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

void ConsoleState::beginTyping(uint32_t swallow, int width) {
    // The field cleared (Field_Clear); where the console is scrolled to
    // stays, as the original's toggle leaves it.
    clearField();
    _fieldWidth = width;
    _historyPos = _history.size();
    _swallow = swallow;
}

void ConsoleState::clearField() {
    _field.clear();
    _cursor = 0;
    _fieldScroll = 0;
}

int ConsoleState::fieldScroll() {
    // As Field_Draw (0x4084c0), its end counted as a character: all shown
    // when it fits; else from the scroll, pulled back (and kept so) where
    // the line ends before the field does.
    const int len = int(_field.size()) + 1;
    if (len <= _fieldWidth)
        return 0;
    if (_fieldScroll + _fieldWidth > len)
        _fieldScroll = std::max(0, len - _fieldWidth);
    return _fieldScroll;
}

ConsoleState::Action ConsoleState::handle(const Event &ev, bool console) {
    // The field as jk2mp.exe 1.02a's (Field_KeyDownEvent 0x408710,
    // Field_CharEvent 0x408840): its scroll moved a character at a time,
    // as the cursor passes either side of what's shown.
    if (ev.type == EventType::kTextInput) {
        // Characters typed (Latin-1, as the original's), but the one of the
        // key that opened the typing, and the console key in the console.
        for (uint32_t cp : decodeUtf8(ev.text)) {
            if (cp == _swallow) {
                _swallow = 0;
                continue;
            }
            _swallow = 0;
            if (cp < 32 || cp == 127 || cp > 255)
                continue;
            if (console && (cp == '`' || cp == '~'))
                continue;
            if (_overstrike) {
                if (_cursor >= kMaxField)
                    continue;
                if (_cursor < _field.size())
                    _field[_cursor] = char(cp);
                else
                    _field += char(cp);
            } else {
                if (_field.size() >= kMaxField)
                    continue;
                _field.insert(_cursor, 1, char(cp));
            }
            if (int(++_cursor) >= _fieldWidth)
                ++_fieldScroll;
        }
        return Action::kNone;
    }
    if (ev.type != EventType::kKeyDown)
        return Action::kNone;
    _swallow = 0;
    const bool ctrl = (ev.mods & keymod::kCtrl) != 0;
    if (console) {
        // The console's own first (Console_Key, 0x408d6c on): two rows a
        // page, Ctrl+Home and Ctrl+End to the oldest and the newest.
        if (ev.key == key::kPageUp) {
            _scroll = std::min(_scroll + 2, std::max(0, totalRows() - 1));
            return Action::kNone;
        }
        if (ev.key == key::kPageDown) {
            _scroll = std::max(_scroll - 2, 0);
            return Action::kNone;
        }
        if (ctrl && ev.key == key::kHome) {
            _scroll = std::max(0, totalRows() - 1);
            return Action::kNone;
        }
        if (ctrl && ev.key == key::kEnd) {
            _scroll = 0;
            return Action::kNone;
        }
    }
    const auto len = _field.size();
    switch (ev.key) {
    case key::kEscape:
        return Action::kClose;
    case key::kReturn:
    case key::kKpEnter:
        return Action::kSubmit;
    case key::kBackspace:
        if (_cursor > 0) {
            _field.erase(--_cursor, 1);
            if (int(_cursor) < _fieldScroll)
                --_fieldScroll;
        }
        return Action::kNone;
    case key::kDelete:
        if (_cursor < len)
            _field.erase(_cursor, 1);
        return Action::kNone;
    case key::kLeft:
        if (_cursor > 0)
            --_cursor;
        if (int(_cursor) < _fieldScroll)
            --_fieldScroll;
        return Action::kNone;
    case key::kRight:
        if (_cursor < len)
            ++_cursor;
        if (int(_cursor) >= _fieldScroll + _fieldWidth && _cursor <= len)
            ++_fieldScroll;
        return Action::kNone;
    case key::kHome:
        _cursor = 0;
        return Action::kNone;
    case key::kEnd:
        _cursor = len;
        return Action::kNone;
    case key::kInsert:
        if (!(ev.mods & keymod::kShift)) // (Shift+Insert pastes, which Reluva can't)
            _overstrike = !_overstrike;
        return Action::kNone;
    default:
        break;
    }
    if (ctrl) {
        // Control characters: ^A to the start, ^E to the end, ^C clears.
        if (ev.key == 'a') {
            _cursor = 0;
            _fieldScroll = 0;
        } else if (ev.key == 'e') {
            _cursor = len;
            _fieldScroll = int(len) - _fieldWidth;
        } else if (ev.key == 'c') {
            clearField();
        }
        return Action::kNone;
    }
    if (!console)
        return Action::kNone;
    if (ev.consoleKey)
        return Action::kClose; // the console key closes it again
    switch (ev.key) {
    case '`':
        return Action::kClose;
    case key::kUp:
    case key::kDown: {
        // Earlier lines typed, back and forth, each as it was left: its
        // cursor and scroll too (the original copies the whole field).
        if (_history.empty())
            return Action::kNone;
        if (ev.key == key::kUp && _historyPos > 0)
            --_historyPos;
        else if (ev.key == key::kDown && _historyPos < _history.size())
            ++_historyPos;
        if (_historyPos < _history.size()) {
            _field = _history[_historyPos].text;
            _cursor = std::min(_history[_historyPos].cursor, _field.size());
            _fieldScroll = _history[_historyPos].scroll;
        } else {
            clearField();
        }
        return Action::kNone;
    }
    default:
        return Action::kNone;
    }
}

std::string ConsoleState::submit(bool console) {
    Typed typed{_field, _cursor, _fieldScroll};
    std::string text = std::move(_field);
    clearField();
    if (!console || text.empty())
        return text;
    // Quotes would end the line early: they become apostrophes.
    std::replace(text.begin(), text.end(), '"', '\'');
    typed.text = text;
    if (_history.empty() || _history.back().text != typed.text)
        _history.push_back(typed);
    _historyPos = _history.size();
    // Printed as any line is: the view moves along only from the newest.
    addLine("]" + text);
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
