// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of text: the .sft fonts and the .uni string tables.

#include "common/events.h"
#include "common/fs.h"
#include "engines/sith/formats/font.h"
#include "engines/sith/formats/strings.h"
#include "engines/sith/pages/pages.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr int kGap = 8;

// Copies `src`'s rectangle to `dst` at (x, y), each pixel `scale` times,
// over what's there by its alpha.
void blitPart(Surface &dst, const image::Image &src, int sx, int sy, int w, int h, int x, int y, int scale) {
    image::Image part;
    part.w = w * scale;
    part.h = h * scale;
    part.rgba.resize(size_t(part.w) * part.h * 4);
    for (int py = 0; py < part.h; ++py)
        for (int px = 0; px < part.w; ++px)
            std::copy_n(src.pixel(sx + px / scale, sy + py / scale), 4, part.pixel(px, py));
    image::blit(dst, part, x, y);
}

/**
 * The .sft fonts: the glyph strip, wrapped to the screen, and sample text
 * drawn with the font. Fonts whose strip has no palette of its own are
 * drawn with uicolormap.cmp's.
 */
class FontPage : public Page {
public:
    explicit FontPage(const ViewerData &data) : _data(data), _files(filesEnding(data.files, ".sft")) {}

    const char *name() const override { return "Fonts"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override { return "Left/Right, +/-: size"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index))
            return true;
        if (pressed == '+' || pressed == '=')
            _scale = std::min(4, _scale + 1);
        else if (pressed == '-')
            _scale = std::max(1, _scale - 1);
        else
            return false;
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &file = _files[_index];
        std::vector<uint8_t> bytes;
        Font font;
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        if (Error err = file.first->read(file.second, bytes); err || (err = parseFont(bytes, font))) {
            drawStatus(screen, line + "  " + err.message);
            return;
        }
        const Palette pal = _data.palettes.empty() ? Palette{} : _data.palettes[0].second;
        const image::Image strip = bitmapToImage(font.strip, font.strip.cels[0], pal, true);

        // The strip, cut into rows as wide as the screen.
        int y = kGap;
        const int rowW = (kViewerW - 2 * kGap) / _scale;
        for (int x = 0; x < strip.w && y < kViewerH / 2; x += rowW) {
            blitPart(screen, strip, x, 0, std::min(rowW, strip.w - x), strip.h, kGap, y, _scale);
            y += strip.h * _scale + 4;
        }

        y += kGap;
        for (const char *sample : {"The quick brown fox jumps over the lazy dog.", "0123456789 :;!?()-+",
                                   "\xc0\xc9\xcd\xd1\xd6\xdc \xe0\xe7\xe9\xe8\xea\xf1\xf6\xfc\xdf\xbf\xa1"}) {
            drawFontText(screen, font, strip, kGap, y, sample, _scale, kViewerW - kGap);
            y += (strip.h + 6) * _scale;
        }
        line += "  " + std::to_string(font.glyphs.size()) + " glyphs, " + std::to_string(strip.h) + " high, " +
                std::to_string(font.strip.bitsPerPixel) + "-bit, gap " + std::to_string(font.gap) + ", space " +
                std::to_string(font.spaceWidth) + ", fallback " + std::to_string(font.fallback);
        drawStatus(screen, line);
    }

private:
    const ViewerData &_data;
    std::vector<FileRef> _files;
    size_t _index = 0;
    int _scale = 2;
};

/** The .uni string tables, a key and its string a line. */
class StringPage : public Page {
public:
    explicit StringPage(const ViewerData &data) : _data(data), _files(filesEnding(data.files, ".uni")) {}

    const char *name() const override { return "Strings"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override { return "Left/Right: table, Up/Down, PageUp/PageDown: scroll"; }

    bool handleKey(uint32_t pressed) override {
        if (pressed == key::kLeft || pressed == key::kRight) {
            stepIndex(pressed, _files.size(), _index);
            _top = 0;
        } else if (pressed == key::kDown)
            ++_top;
        else if (pressed == key::kUp)
            _top = _top > 0 ? _top - 1 : 0;
        else if (pressed == key::kPageDown)
            _top += kLines;
        else if (pressed == key::kPageUp)
            _top = _top > kLines ? _top - kLines : 0;
        else
            return false;
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &file = _files[_index];
        std::vector<uint8_t> bytes;
        StringTable table;
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        if (Error err = file.first->read(file.second, bytes); err || (err = parseStrings(bytes, table))) {
            drawStatus(screen, line + "  " + err.message);
            return;
        }
        _top = std::min(_top, table.entries.empty() ? 0 : table.entries.size() - 1);
        for (size_t i = 0; i < kLines && _top + i < table.entries.size(); ++i) {
            const auto &[key, value] = table.entries[_top + i];
            const int y = kGap + int(i) * 12;
            font::drawText(screen, kGap, y, key, {150, 200, 255});
            font::drawText(screen, kGap + 200, y, value, {230, 230, 230});
        }
        line += "  strings " + std::to_string(table.entries.empty() ? 0 : _top + 1) + "-" +
                std::to_string(std::min(_top + kLines, table.entries.size())) + " of " +
                std::to_string(table.entries.size()) + " (MSGS " + std::to_string(table.declaredCount) + ")";
        drawStatus(screen, line);
    }

private:
    static constexpr size_t kLines = (kViewerH - 32) / 12;

    const ViewerData &_data;
    std::vector<FileRef> _files;
    size_t _index = 0;
    size_t _top = 0;
};

} // namespace

// A character's glyph as the game finds it: the font's own, else its
// fallback's; nullptr for white space, or if the font has neither.
static const Font::Glyph *glyphFor(const Font &font, char ch) {
    if (ch == ' ' || ch == '\t')
        return nullptr;
    const Font::Glyph *g = font.glyph(uint8_t(ch));
    return g ? g : font.glyph(font.fallback);
}

int fontTextWidth(const Font &font, std::string_view text) {
    int w = 0;
    for (const char ch : text) {
        if (ch == ' ' || ch == '\t')
            w += font.spaceWidth;
        else if (const Font::Glyph *g = glyphFor(font, ch))
            w += g->width + font.gap;
        else
            w += font.gap;
    }
    return w;
}

void drawFontText(Surface &s, const Font &font, const image::Image &strip, int x, int y, std::string_view text,
                  int scale, int right) {
    for (const char ch : text) {
        const Font::Glyph *g = glyphFor(font, ch);
        if (!g) {
            x += ((ch == ' ' || ch == '\t') ? font.spaceWidth : font.gap) * scale;
            continue;
        }
        if (x + g->width * scale > right)
            break;
        blitPart(s, strip, g->x, 0, g->width, strip.h, x, y, scale);
        x += (g->width + font.gap) * scale;
    }
}

std::unique_ptr<Page> makeFontPage(const ViewerData &data) {
    return std::make_unique<FontPage>(data);
}

std::unique_ptr<Page> makeStringPage(const ViewerData &data) {
    return std::make_unique<StringPage>(data);
}

} // namespace reluva::sith
