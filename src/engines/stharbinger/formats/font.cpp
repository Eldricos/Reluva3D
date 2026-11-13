// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/font.h"

namespace reluva::stharbinger {

bool Font::load(SeekableReadStream& stream) {
    glyphs_.clear();
    int count = stream.readUint16LE();
    if (stream.err() || count <= 0 || count > 224)
        return false;
    for (int i = 0; i < count; i++) {
        Glyph g;
        g.height = stream.readByte();
        g.width = stream.readByte();
        int length = stream.readByte();
        g.bits.resize(length);
        if (length && stream.read(g.bits.data(), uint32_t(length)) != uint32_t(length)) {
            glyphs_.clear();
            return false;
        }
        g.pitch = g.height ? length / g.height : 0;
        glyphs_.push_back(std::move(g));
    }
    return true;
}

const Font::Glyph* Font::getGlyph(char c) const {
    int index = int(uint8_t(c)) - 0x20;
    if (index < 0 || index >= int(glyphs_.size()))
        index = 0;
    return glyphs_.empty() ? nullptr : &glyphs_[index];
}

int Font::getStringWidth(const std::string& text) const {
    int w = 0;
    for (char c : text)
        if (const Glyph* g = getGlyph(c))
            w += g->width;
    return w;
}

void Font::drawGlyph(Surface& dst, int x, int y, const Glyph& g, uint8_t color, bool shadow) const {
    for (int r = 0; r < g.height; r++) {
        for (int c = 0; c < g.pitch * 8; c++) {
            if (!(g.bits[r * g.pitch + c / 8] & (0x80 >> (c % 8))))
                continue;
            // The shadow covers the pixels right, below and diagonally below.
            static const int kOffsets[3][2] = {{1, 0}, {0, 1}, {1, 1}};
            int n = shadow ? 3 : 1;
            for (int k = 0; k < n; k++) {
                int px = x + c + (shadow ? kOffsets[k][0] : 0);
                int py = y + r + (shadow ? kOffsets[k][1] : 0);
                if (px >= 0 && py >= 0 && px < dst.w && py < dst.h)
                    *draw::at(dst, px, py) = color;
            }
        }
    }
}

void Font::drawString(Surface& dst, int x, int y, const std::string& text, uint8_t color, uint8_t shadow) const {
    // Shadows first, so every character's pixels end up on top.
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (char ch : text) {
            const Glyph* g = getGlyph(ch);
            if (!g)
                return;
            drawGlyph(dst, cx, y, *g, pass == 0 ? shadow : color, pass == 0);
            cx += g->width;
        }
    }
}

} // namespace reluva::stharbinger
