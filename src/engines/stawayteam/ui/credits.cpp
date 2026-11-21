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

// The RESUME button: its picture, its place on the window, from its
// bottom, and its label's (0x5bb5b5, 0x5bb674).
const char *const kButtonPicture = "Escape Menu/Button Background";
constexpr int kButtonX = 0x45;
constexpr int kButtonBottomGap = 0x1b;
constexpr int kButtonTextY = -2;

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

void fillRow(Surface &area, int left, int right, int y, const uint8_t (&rgb)[3]) {
    if (y < 0 || y >= area.h)
        return;
    for (int x = std::max(0, left); x < std::min(right, area.w); ++x) {
        uint8_t *p = area.row(y) + x * 4;
        p[0] = rgb[0];
        p[1] = rgb[1];
        p[2] = rgb[2];
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

void CreditsRoll::drawLine(Surface &area, const std::string &line, int y, int w, int opacity) const {
    const int center = w / 2;
    // "Role: name": the role ends at the middle, the name starts there.
    const size_t colon = line.rfind(':');
    if (colon != std::string::npos && colon != 0) {
        const std::string left = line.substr(0, colon);
        _font.draw(area, center - _font.width(left), y, left, opacity);
        _font.draw(area, center, y, line.substr(colon + 1), opacity);
        return;
    }
    if (!line.empty() && (line[0] == '_' || line[0] == '+')) {
        const std::string text = line.substr(1);
        const int width = _font.width(text);
        _font.draw(area, center - width / 2, y, text, opacity);
        const int under = y + _font.height() + 1;
        fillRow(area, center - width / 2, center + width / 2 + 1, under, line[0] == '_' ? kHeadingLine : kRoleLine);
        return;
    }
    _font.draw(area, center - _font.width(line) / 2, y, line, opacity);
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
            // Brightest across the middle, fading to nothing at the edges.
            const float bright = std::clamp(std::sin(float(top) * std::numbers::pi_v<float> / float(h)), 0.0f, 1.0f);
            drawLine(area, _lines[size_t(i)], top, w, int(std::lround(bright * 255.0f)));
        }
        paste(screen, area, x, y);
    }
    if (last > count + 2 * shown)
        _start = now;
}

CreditsWindow::CreditsWindow(const Archive &data, const TextObject &rules, int screenW, int screenH, uint64_t now)
    : _background(screenPicture(data, "Escape Menu/Escape Menu Background")), _roll(readCredits(data), _font),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    // Both fonts named by the program (0x5bb4a2, 0x5bb547), not Rules.txt's Menu Font.
    (void)_font.load(data, "Placar Blue");
    (void)_buttonFont.load(data, "Placar Condensed");
    const TextObject *def = menuDefinition(rules, "Credits");
    readPoint(def, "Credit Pos", _rollX, _rollY);
    readPoint(def, "Credit Size", _rollW, _rollH);
    image::Image picture = screenPicture(data, kButtonPicture);
    const int buttonY = _background.h - picture.h - kButtonBottomGap;
    Button resume(std::move(picture), kButtonX, buttonY);
    resume.setLabel("&RESUME", &_buttonFont, 0, kButtonTextY);
    _buttons.add(std::move(resume));
    _buttons.setOrigin(_x, _y);
    _roll.start(now);
}

bool CreditsWindow::handleEvent(const Event &ev, audio::Mixer *mixer) {
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
        return true;
    return _buttons.handleEvent(ev, mixer) == 0;
}

void CreditsWindow::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, _x, _y);
    _roll.draw(screen, _x + _rollX, _y + _rollY, _rollW, _rollH, now);
    _buttons.draw(screen);
}

} // namespace reluva::stawayteam
