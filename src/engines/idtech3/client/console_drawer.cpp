// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/console_drawer.h"

#include <algorithm>
#include <array>

namespace reluva::idtech3 {

namespace {

constexpr int kSmallW = 8; // pixels, whatever the screen's size
constexpr int kSmallH = 16;
constexpr float kBig = 16; // in the 640 x 480 screen
constexpr int kInputWidth = 78;
using Colour = std::array<float, 4>;
constexpr Colour kWhite{1, 1, 1, 1};
// The line under the console and its version (0x406477).
constexpr Colour kLineColour{0.509f, 0.609f, 0.847f, 1};

Colour codeColour(char code) {
    static const Colour kTable[8] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 0, 1},
                                     {0, 0, 1, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 1, 1}};
    return kTable[(code - '0') & 7];
}

bool isColourCode(std::string_view s, size_t i) {
    return s[i] == '^' && i + 1 < s.size() && s[i + 1] != '^';
}

class Painter {
public:
    Painter(const ConsoleFrame &f, const ConsoleShaders &s, std::vector<Picture> &out)
        : _sx(640.0f / float(f.screenW)), _sy(480.0f / float(f.screenH)), _shaders(s), _out(out) {}

    float xAdjust() const { return _sx; }

    void picture(float x, float y, float w, float h, int32_t shader, const Colour &c) {
        _out.push_back({x, y, w, h, 0, 0, 1, 1, shader, c});
    }

    // A character of the grid at (x, y), in pixels (0x40ef40): each a
    // cell's left half, 8 by 16 on the screen.
    void small(int x, int y, unsigned char ch, const Colour &c) {
        if (ch == ' ' || y < -kSmallH)
            return;
        glyph(float(x) * _sx, float(y) * _sy, float(kSmallW) * _sx, float(kSmallH) * _sy, ch, c);
    }

    // A string at (x, y) in pixels, its colour codes followed from white.
    void smallString(int x, int y, std::string_view s, Colour c = kWhite) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (isColourCode(s, i)) {
                c = codeColour(s[++i]);
                continue;
            }
            small(x, y, static_cast<unsigned char>(s[i]), c);
            x += kSmallW;
        }
    }

    // A big string at (x, y) in the 640 x 480 screen, with its shadow 2
    // down and right.
    void bigString(float x, float y, std::string_view s) {
        for (int pass = 0; pass < 2; ++pass) {
            Colour c = pass == 0 ? Colour{0, 0, 0, 1} : kWhite;
            float xx = x;
            for (size_t i = 0; i < s.size(); ++i) {
                if (isColourCode(s, i)) {
                    if (pass == 1)
                        c = codeColour(s[i + 1]);
                    ++i;
                    continue;
                }
                const auto ch = static_cast<unsigned char>(s[i]);
                if (ch != ' ' && y >= -kBig)
                    glyph(pass == 0 ? xx + 2 : xx, pass == 0 ? y + 2 : y, kBig, kBig, ch, c);
                xx += kBig;
            }
        }
    }

private:
    void glyph(float x, float y, float w, float h, unsigned char ch, const Colour &c) {
        const float s = float(ch & 15) * 0.0625f;
        const float t = float(ch >> 4) * 0.0625f;
        _out.push_back({x, y, w, h, s, t, s + 0.03125f, t + 0.0625f, _shaders.charSet, c});
    }

    float _sx;
    float _sy;
    const ConsoleShaders &_shaders;
    std::vector<Picture> &_out;
};

// A typing field as the original draws it (Field_Draw, 0x4084c0): as
// much as fits from its scroll (`prestep`, ConsoleState::fieldScroll),
// and the cursor when it's on.
struct FieldView {
    std::string shown;
    int cursorAt = 0; // in characters from the field's left
};

FieldView fieldView(std::string_view text, size_t cursor, int widthInChars, int prestep) {
    const int len = int(text.size()) + 1;
    prestep = std::clamp(prestep, 0, std::max(0, len - 1));
    const int drawLen = std::min(widthInChars, len - prestep);
    FieldView v;
    v.shown = std::string(text.substr(size_t(prestep), size_t(std::max(0, drawLen - 1))));
    int printable = 0;
    for (size_t i = 0; i < v.shown.size(); ++i) {
        if (isColourCode(v.shown, i)) {
            ++i;
            continue;
        }
        ++printable;
    }
    // Shifted back by the colour codes shown, as the original's is.
    const int codes = drawLen - (printable + 1);
    v.cursorAt = int(cursor) - prestep - codes;
    return v;
}

} // namespace

std::vector<std::string> wrapConsoleLine(std::string_view text, int width) {
    std::vector<std::string> out;
    if (width < 1)
        width = 1;
    std::string piece;
    char colour = '7';
    int x = 0;
    auto feed = [&] {
        out.push_back(std::move(piece));
        piece = colour == '7' ? std::string() : std::string{'^', colour};
        x = 0;
    };
    for (size_t i = 0; i < text.size();) {
        if (isColourCode(text, i)) {
            colour = text[i + 1];
            piece.append(text.substr(i, 2));
            i += 2;
            continue;
        }
        // A word that would reach the edge starts a new line, unless it's
        // as long as a line.
        int word = 0;
        while (word < width && i + size_t(word) < text.size() &&
               static_cast<unsigned char>(text[i + size_t(word)]) > ' ')
            ++word;
        if (word != width && x + word >= width)
            feed();
        piece += text[i++];
        if (++x >= width)
            feed();
    }
    if (!piece.empty() || out.empty())
        out.push_back(std::move(piece));
    return out;
}

std::vector<Picture> consolePictures(const ConsoleFrame &f, const ConsoleShaders &shaders) {
    std::vector<Picture> out;
    if (shaders.charSet < 0 || f.screenW <= 0 || f.screenH <= 0)
        return out;
    Painter paint(f, shaders, out);
    const int width = f.screenW / kSmallW - 2;
    const int xBase = int(paint.xAdjust()); // the original adds its scale to each x
    auto wrappedTail = [&](const std::vector<std::string> &src, size_t rows) {
        // The last `rows` lines once wrapped, oldest first.
        std::vector<std::string> tail;
        for (auto it = src.rbegin(); it != src.rend() && tail.size() < rows; ++it) {
            std::vector<std::string> pieces = wrapConsoleLine(*it, width);
            for (auto p = pieces.rbegin(); p != pieces.rend() && tail.size() < rows; ++p)
                tail.push_back(std::move(*p));
        }
        std::reverse(tail.begin(), tail.end());
        return tail;
    };

    if (f.frac <= 0) {
        // Shut: the notify lines from the top, then the chat being typed.
        int v = 0;
        for (const std::string &line : wrappedTail(f.notify, 4)) {
            paint.smallString(xBase + kSmallW, v, line);
            v += kSmallH;
        }
        if (f.chat != ConsoleFrame::Chat::kNone) {
            const bool team = f.chat == ConsoleFrame::Chat::kSayTeam;
            paint.bigString(8, float(v), team ? "say_team:" : "say:");
            const int skip = team ? 11 : 5;
            const FieldView field = fieldView(f.chatInput, f.chatCursor, team ? 25 : 30, f.fieldScroll);
            paint.bigString(float(skip) * kBig, float(v), field.shown);
            if (f.cursorOn)
                paint.bigString(float(skip + field.cursorAt) * kBig, float(v), f.overstrike ? "\v" : "\n");
        }
        return out;
    }

    const int lines = std::min(int(float(f.screenH) * f.frac), f.screenH);
    if (lines <= 0)
        return out;
    // The background down to 2 above its line, in the 640 x 480 screen.
    int y = int(f.frac * 480 - 2);
    if (y < 1)
        y = 0;
    else
        paint.picture(0, 0, 640, float(y), shaders.console, kWhite);
    paint.picture(0, float(y), 640, 2, shaders.white, kLineColour);
    paint.smallString(f.screenW - int(f.version.size()) * kSmallW, lines - (kSmallH + kSmallH / 2), f.version,
                      kLineColour);

    // The lines from the newest up, under a row of carets when scrolled back.
    int row = lines - kSmallH * 3;
    if (f.scroll > 0) {
        for (int x = 0; x < width; x += 4)
            paint.small(xBase + (x + 1) * kSmallW, row, '^', {1, 0, 0, 1});
        row -= kSmallH;
    }
    if (f.lines) {
        const std::vector<std::string> all(f.lines->begin(), f.lines->end());
        const size_t rows = size_t(std::max(0, row / kSmallH + 2)) + size_t(f.scroll);
        std::vector<std::string> tail = wrappedTail(all, rows);
        const size_t shown = tail.size() > size_t(f.scroll) ? tail.size() - size_t(f.scroll) : 0;
        for (size_t i = shown; i-- > 0 && row >= -kSmallH; row -= kSmallH)
            paint.smallString(xBase + kSmallW, row, tail[i]);
    }

    if (f.typing) {
        const int inputY = lines - kSmallH * 2;
        paint.small(xBase + kSmallW, inputY, ']', kWhite);
        const FieldView field = fieldView(f.input, f.cursor, kInputWidth, f.fieldScroll);
        const int fieldX = xBase + 2 * kSmallW;
        paint.smallString(fieldX, inputY, field.shown);
        // The cursor: character 10, or 11 while typing over (0x4085d5).
        if (f.cursorOn)
            paint.small(fieldX + field.cursorAt * kSmallW, inputY, f.overstrike ? 11 : 10, kWhite);
    }
    return out;
}

} // namespace reluva::idtech3
