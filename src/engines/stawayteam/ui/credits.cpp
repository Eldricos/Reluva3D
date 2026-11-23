// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/credits.h"

#include "engines/stawayteam/ui/layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace reluva::stawayteam {

namespace {

// The program reads up to this many lines, of up to 255 characters each
// (0x4873f0).
constexpr size_t kMaxLines = 999;
constexpr size_t kMaxLineLength = 255;
// The roll moves up its line's height every 500 ms (0x487438).
constexpr float kLinesPerMs = 0.002f;
// Under a heading ('_') and a role ('+'): the program's colours (0x4876f8, 0x48777c).
constexpr uint8_t kHeadingLine[3] = {0x9c, 0, 0};
constexpr uint8_t kRoleLine[3] = {0, 0x96, 0};

// The RESUME button: its picture, and its place on the window, from its
// bottom (0x5bb5b5, 0x5bb674).
const char *const kButtonPicture = "Escape Menu/Button Background";
constexpr int kButtonX = 0x45;
constexpr int kButtonBottomGap = 0x1b;

// The part of `screen` at (x, y), w by h, clipped to it, as a surface of its own.
Surface cut(const Surface &screen, int x, int y, int w, int h) {
    Surface area;
    area.create(w, h, PixelFormat::kRGBA32);
    for (int row = 0; row < h; ++row) {
        const int sy = y + row;
        if (sy < 0 || sy >= screen.h)
            continue;
        const int from = std::max(0, -x);
        const int to = std::min(w, screen.w - x);
        if (from < to)
            std::memcpy(area.row(row) + from * 4, screen.row(sy) + (x + from) * 4, size_t(to - from) * 4);
    }
    return area;
}

// Puts `area`, made by cut(), back where it came from.
void paste(Surface &screen, const Surface &area, int x, int y) {
    for (int row = 0; row < area.h; ++row) {
        const int sy = y + row;
        if (sy < 0 || sy >= screen.h)
            continue;
        const int from = std::max(0, -x);
        const int to = std::min(area.w, screen.w - x);
        if (from < to)
            std::memcpy(screen.row(sy) + (x + from) * 4, area.row(row) + from * 4, size_t(to - from) * 4);
    }
}

// Blends a row over what's there, `alpha` 0-255.
void fillRow(Surface &area, int left, int right, int y, const uint8_t (&rgb)[3], int alpha) {
    if (y < 0 || y >= area.h)
        return;
    for (int x = std::max(0, left); x < std::min(right, area.w); ++x) {
        uint8_t *p = area.row(y) + x * 4;
        for (int c = 0; c < 3; ++c)
            p[c] = uint8_t((rgb[c] * alpha + p[c] * (255 - alpha) + 127) / 255);
        p[3] = 255;
    }
}

std::string readCredits(const Archive &data) {
    std::vector<uint8_t> bytes;
    if (data.read("Credits.txt", bytes))
        return {};
    return {bytes.begin(), bytes.end()};
}

} // namespace

CreditsRoll::CreditsRoll(std::string_view text, const Font &font) : _font(font) {
    while (!text.empty() && _lines.size() < kMaxLines) {
        const size_t end = text.find('\n');
        std::string line(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // Lines starting '/' are the file's comments.
        if (!line.empty() && line[0] == '/')
            continue;
        if (line.size() > kMaxLineLength)
            line.resize(kMaxLineLength);
        _lines.push_back(std::move(line));
    }
}

void CreditsRoll::drawLine(Surface &area, const std::string &line, int y, int w, float brightness, int level) const {
    const int center = w / 2;
    // "Role: name": the role ends at the middle, the name starts there.
    const size_t colon = line.rfind(':');
    if (colon != std::string::npos && colon != 0) {
        const std::string left = line.substr(0, colon);
        _font.draw(area, center - _font.width(left), y, left, 255, brightness);
        _font.draw(area, center, y, line.substr(colon + 1), 255, brightness);
        return;
    }
    if (!line.empty() && (line[0] == '_' || line[0] == '+')) {
        const std::string text = line.substr(1);
        const int width = _font.width(text);
        _font.draw(area, center - width / 2, y, text, 255, brightness);
        const int under = y + _font.height() + 1;
        // The line's colour takes the brightness's level as its alpha
        // (0x4876f8, 0x48777c).
        fillRow(area, center - width / 2, center + width / 2 + 1, under, line[0] == '_' ? kHeadingLine : kRoleLine,
                level);
        return;
    }
    _font.draw(area, center - _font.width(line) / 2, y, line, 255, brightness);
}

void CreditsRoll::draw(Surface &screen, int x, int y, int w, int h, uint64_t now) {
    if (!_font.loaded() || w <= 0 || h <= 0)
        return;
    // As 0x487530 does: the lines a line's height and 2 apart, the first
    // coming up from the bottom, the ones on show and one either side drawn.
    const int lineHeight = _font.height() + 2;
    const int offset = int(std::lround(float(now - _start) * (float(lineHeight) * kLinesPerMs)));
    const int shown = h / lineHeight;
    const int first = std::max(0, offset / lineHeight - shown - 1);
    const int last = first + shown + 2;
    const int count = int(_lines.size());
    if (last < count + shown + 1) {
        Surface area = cut(screen, x, y, w, h);
        for (int i = first; i < last && i < count; ++i) {
            const int top = h - offset + i * lineHeight;
            // Brightest across the middle, darkest at the edges: the
            // toolkit's brightness, 0-255, shifts each colour right by
            // 5 less its sixths, darkening it in six steps (0x44c4f0,
            // 0x44c1e8).
            const float bright = std::clamp(std::sin(float(top) * std::numbers::pi_v<float> / float(h)), 0.0f, 1.0f);
            const int level = int(std::lround(bright * 255.0f));
            const int shift = 5 - level * 6 / 256;
            drawLine(area, _lines[size_t(i)], top, w, 1.0f / float(1 << shift), level);
        }
        paste(screen, area, x, y);
    }
    if (last > count + 2 * shown)
        _start = now;
}

CreditsWindow::CreditsWindow(const Archive &data, const TextObject &rules, int screenW, int screenH, uint64_t now)
    : _background(screenPicture(data, "Escape Menu/Escape Menu Background")), _roll(readCredits(data), _font),
      _resume(screenPicture(data, kButtonPicture), "&RESUME", &_buttonFont), _x((screenW - _background.w) / 2),
      _y((screenH - _background.h) / 2) {
    // Both fonts named by the program (0x5bb4a2, 0x5bb547), not Rules.txt's Menu Font.
    (void)_font.load(data, "Placar Blue");
    (void)_buttonFont.load(data, "Placar Condensed");
    const TextObject *def = menuDefinition(rules, "Credits");
    readPoint(def, "Credit Pos", _rollX, _rollY);
    readPoint(def, "Credit Size", _rollW, _rollH);
    _resume.setPosition(kButtonX, _background.h - _resume.height() - kButtonBottomGap);
    _resume.setSound(gameSound(data, "UI/Button Action.wav"));
    _roll.start(now);
}

bool CreditsWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    // RESUME is the window's default and cancel button (0x5bb614).
    if ((isEnter(ev) || isEscape(ev)) && _resume.press(mixer))
        return true;
    return _resume.handleEvent(ev, _x, _y, mixer);
}

void CreditsWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    _roll.draw(screen, _x + _rollX, _y + _rollY, _rollW, _rollH, now);
    _resume.draw(screen, _x, _y, now);
}

} // namespace reluva::stawayteam
