// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/font.h"
#include "common/bytes.h"

#include <algorithm>
#include <cctype>

#include "engines/stgenerations/debug.h"

namespace reluva::stgenerations {

bool Font::load(SeekableReadStream &stream) {
    _glyphs.clear();

    if (stream.readUint32BE() != tagBE('F', 'O', 'N', 'T')) {
        log::warning("Font: missing FONT header");
        return false;
    }
    uint32_t dataSize = stream.readUint32LE();
    uint16_t count = stream.readUint16LE();
    _transparent = stream.readByte();
    _height = stream.readByte();
    stream.readUint32LE(); // unknown

    int64_t end = std::min<int64_t>(8 + (int64_t)dataSize, stream.size());
    while (stream.pos() + 6 <= end) {
        uint16_t code = stream.readUint16LE();
        uint16_t size = stream.readUint16LE();
        Glyph glyph;
        glyph.width = stream.readByte();
        glyph.height = stream.readByte();
        if (size != glyph.width * glyph.height + 2) {
            log::warning("Font: glyph 0x%02x has size %u, expected %d", code, size, glyph.width * glyph.height + 2);
            return false;
        }
        glyph.pixels.resize(glyph.width * glyph.height);
        if (stream.read(glyph.pixels.data(), (uint32_t)glyph.pixels.size()) != glyph.pixels.size()) {
            log::warning("Font: truncated glyph 0x%02x", code);
            return false;
        }
        _glyphs[code] = std::move(glyph);
    }

    if (_glyphs.size() != count)
        log::warning("Font: header says %u glyphs, found %d", count, (int)_glyphs.size());
    debugC(1, "resources", "Font: %d glyphs, height %d, transparent %d", (int)_glyphs.size(), _height, _transparent);
    return !_glyphs.empty();
}

const Font::Glyph *Font::findGlyph(uint16_t c) const {
    auto it = _glyphs.find(c);
    if (it == _glyphs.end() && c >= 'a' && c <= 'z')
        it = _glyphs.find((uint16_t)toupper(c)); // some fonts may be upper case only
    return it == _glyphs.end() ? nullptr : &it->second;
}

int Font::getCharWidth(uint16_t c) const {
    const Glyph *glyph = findGlyph(c);
    return glyph ? glyph->width : 0;
}

int Font::getStringWidth(const std::string &str) const {
    int width = 0;
    for (char c : str)
        width += getCharWidth((uint8_t)c);
    return width;
}

void Font::drawChar(Canvas &dst, uint16_t c, int x, int y, const uint8_t *remap) const {
    const Glyph *glyph = findGlyph(c);
    if (!glyph)
        return;
    for (int gy = 0; gy < glyph->height; gy++) {
        for (int gx = 0; gx < glyph->width; gx++) {
            uint8_t pixel = glyph->pixels[gy * glyph->width + gx];
            if (pixel != _transparent)
                dst.setPixel(x + gx, y + gy, remap ? remap[pixel] : pixel);
        }
    }
}

void Font::drawString(Canvas &dst, const std::string &str, int x, int y, const uint8_t *remap) const {
    for (char c : str) {
        drawChar(dst, (uint8_t)c, x, y, remap);
        x += getCharWidth((uint8_t)c);
    }
}

void Font::drawStringRgb(Canvas &dst, const std::string &str, int x, int y, const uint32_t *palette,
                         const Rect *clip) const {
    Rect area(0, 0, dst.w, dst.h);
    if (clip)
        area.clip(*clip);
    for (char c : str) {
        if (const Glyph *glyph = findGlyph((uint8_t)c)) {
            for (int gy = 0; gy < glyph->height; gy++) {
                for (int gx = 0; gx < glyph->width; gx++) {
                    uint8_t pixel = glyph->pixels[gy * glyph->width + gx];
                    if (pixel != _transparent && area.contains(x + gx, y + gy))
                        *(uint32_t *)dst.getBasePtr(x + gx, y + gy) = palette[pixel];
                }
            }
        }
        x += getCharWidth((uint8_t)c);
    }
}

std::vector<std::string> Font::wordWrap(const std::string &str, int maxWidth) const {
    std::vector<std::string> lines;
    std::string line;
    for (const std::string &word : text::split(str, ' ')) {
        if (word.empty())
            continue;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && getStringWidth(candidate) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

} // namespace reluva::stgenerations
