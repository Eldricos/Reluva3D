// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/font_set.h"

#include "common/fs.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3::jk2 {

namespace {

constexpr int32_t kStyleDropShadow = int32_t(0x80000000);
constexpr int32_t kStyleBlink = 0x40000000;
constexpr int32_t kFontMask = 0x00ffffff;

// Quake III's colour codes, ^0 to ^7.
constexpr float kCodeColours[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
                                      {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

} // namespace

int32_t FontSet::registerFont(const std::string &name) {
    const std::string key = fs::toLower(name);
    if (const auto it = _byName.find(key); it != _byName.end())
        return it->second;
    std::vector<uint8_t> data;
    Loaded f;
    int32_t handle = 0;
    if (!_files.read("fonts/" + name + ".fontdat", data) && !raven::parseFontdat(data, key, f.font)) {
        f.shader = _shaders.add("fonts/" + name);
        _fonts.push_back(std::move(f));
        handle = int32_t(_fonts.size());
    }
    return _byName[key] = handle;
}

const raven::Font *FontSet::font(int32_t handle) const {
    const int32_t i = (handle & kFontMask) - 1;
    return i >= 0 && i < int32_t(_fonts.size()) ? &_fonts[size_t(i)].font : nullptr;
}

void FontSet::drawString(int32_t x, int32_t y, const std::string &text, const std::array<float, 4> &color,
                         int32_t handle, int32_t limit, float scale, int32_t timeMs, const Sink &draw,
                         bool pixels) const {
    const raven::Font *f = font(handle);
    if (!f)
        return;
    // Blinking text is off every other 128 ms.
    if ((handle & kStyleBlink) && ((timeMs >> 7) & 1))
        return;
    const int32_t shader = _fonts[size_t((handle & kFontMask) - 1)].shader;
    auto pieces = [&](int32_t px, int32_t py, const std::array<float, 4> &base, bool shadow) {
        for (const raven::Font::Piece &p : f->layout(text, px, py, scale, pixels ? limit : -1, pixels ? -1 : limit)) {
            std::array<float, 4> c = base;
            // The shadow is grey throughout: the original takes colour codes out of its text.
            if (p.colour >= 0 && !shadow)
                c = {kCodeColours[p.colour][0], kCodeColours[p.colour][1], kCodeColours[p.colour][2], 1};
            draw(p.x, p.y, p.w, p.h, p.s, p.t, p.s2, p.t2, shader, c);
        }
    };
    if (handle & kStyleDropShadow) {
        const int offset = int(std::floor(float(f->pointSize) * scale * 0.075f + 0.5f));
        pieces(x + offset, y + offset, {0.15f, 0.15f, 0.15f, 1}, true);
    }
    pieces(x, y, color, false);
}

int32_t readPatchedChar(uint8_t first, uint8_t second, int32_t language, int32_t &advance, bool &trailingPunctuation) {
    const int32_t both = int32_t(first) << 8 | second;
    advance = 2;
    trailingPunctuation = false;
    // Korean: a first byte 0xb0 to 0xc8, a second 0xa1 to 0xfe (0x477300);
    // none of its letters is punctuation here.
    if (language == 4 && first >= 0xb0 && first <= 0xc8 && second > 0xa0 && second < 0xff)
        return both;
    // Big5: a first byte 0xa1 to 0xc6 or 0xc9 to 0xf9, a second 0x40 to
    // 0x7e or 0xa1 to 0xfe (0x477330); 0xa140 to 0xa153 punctuation (0x477370).
    if (language == 5 && ((first >= 0xa1 && first <= 0xc6) || (first >= 0xc9 && first <= 0xf9)) &&
        ((second >= 0x40 && second <= 0x7e) || (second >= 0xa1 && second <= 0xfe))) {
        trailingPunctuation = both >= 0xa140 && both < 0xa154;
        return both;
    }
    // Shift-JIS: a first byte 0x81 to 0x9f or 0xe0 to 0xef, a second 0x40
    // to 0x7e or 0x80 to 0xfc (0x477390); 0x8140 to 0x8151 punctuation
    // (0x4773c0).
    if (language == 8 && ((first >= 0x81 && first <= 0x9f) || (first >= 0xe0 && first <= 0xef)) &&
        ((second >= 0x40 && second <= 0x7e) || (second >= 0x80 && second <= 0xfc))) {
        trailingPunctuation = both >= 0x8140 && both < 0x8152;
        return both;
    }
    // Else a byte; ! ? , . ; : punctuation (0x4772c8 on).
    advance = 1;
    trailingPunctuation = first && std::strchr("!?,.;:", first);
    return first;
}

int32_t FontSet::call(Qvm &vm, Call which, const int32_t *a, int32_t timeMs, const Sink &draw) {
    auto str = [&](int i, size_t max) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    switch (which) {
    case kRegister:
        return registerFont(str(0, 64));
    case kStrLenPixels: {
        const raven::Font *ft = font(a[1]);
        return ft ? ft->textWidth(str(0, 4096), f(2)) : 0;
    }
    case kStrLenChars: {
        const std::string text = str(0, 4096);
        int32_t n = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '^')
                ++i;
            else if (text[i] != '\n' && text[i] != '\r')
                ++n;
        }
        return n;
    }
    case kStrHeightPixels: {
        const raven::Font *ft = font(a[0]);
        return ft ? int32_t(std::floor(float(ft->pointSize) * f(1) + 0.5f)) : 0;
    }
    case kDrawString: {
        std::array<float, 4> c{1, 1, 1, 1};
        if (a[3] && vm.valid(a[3], 16))
            for (int i = 0; i < 4; ++i)
                c[size_t(i)] = Qvm::asFloat(vm.readInt(a[3] + 4 * i));
        drawString(a[0], a[1], str(2, 4096), c, a[4], a[5], f(6), timeMs, draw);
        return 0;
    }
    case kReadChar: {
        // One byte a letter in the languages Jedi Outcast ships.
        if (!vm.valid(a[0], 4))
            return 0;
        const int32_t at = vm.readInt(a[0]);
        if (!vm.valid(at, 1))
            return 0;
        const auto c = int32_t(uint8_t(vm.memory()[size_t(at)]));
        vm.writeInt(a[0], at + 1);
        return c;
    }
    case kReadCharAdvance: {
        if (!vm.valid(a[0], 1))
            return 0;
        const auto first = uint8_t(vm.memory()[size_t(a[0])]);
        const auto second = first && vm.valid(a[0], 2) ? uint8_t(vm.memory()[size_t(a[0]) + 1]) : uint8_t(0);
        int32_t advance = 1;
        bool punctuation = false;
        const int32_t c = readPatchedChar(first, second, language, advance, punctuation);
        if (a[1] && vm.valid(a[1], 4))
            vm.writeInt(a[1], advance);
        if (a[2] && vm.valid(a[2], 4))
            vm.writeInt(a[2], punctuation ? 1 : 0);
        return c;
    }
    }
    return 0;
}

} // namespace reluva::idtech3::jk2
