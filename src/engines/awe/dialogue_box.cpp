// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/dialogue_box.h"

#include <algorithm>

namespace reluva::awe {

namespace {

// Over an overlay: blended by alpha, which is kept.
void blitOver(Surface& dst, const image::Image& img, int x, int y) {
    for (int sy = std::max(0, -y); sy < img.h && y + sy < dst.h; sy++) {
        uint8_t* row = dst.row(y + sy);
        for (int sx = std::max(0, -x); sx < img.w && x + sx < dst.w; sx++) {
            const uint8_t* s = img.pixel(sx, sy);
            uint8_t* d = row + (x + sx) * 4;
            const int a = s[3], da = d[3] * (255 - a) / 255, out = a + da;
            if (out == 0)
                continue;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t((s[c] * a + d[c] * da) / out);
            d[3] = uint8_t(out);
        }
    }
}

} // namespace

std::vector<DialogueChoiceArea> drawDialogueBox(Surface& dst, const GameFonts& fonts, const DialogueBoxPictures& pictures,
                                                const std::string& text, Color colour,
                                                const std::vector<std::string>& choices, int highlighted) {
    std::vector<DialogueChoiceArea> areas;
    const int boxH = pictures.window && pictures.window->h > 0 ? pictures.window->h : 160;
    const int top = dst.h - boxH;
    if (pictures.window && pictures.window->w > 0)
        blitOver(dst, *pictures.window, (dst.w - pictures.window->w) / 2, top);
    const TextStyle* style = fonts.style("dialogue");
    if (!style || !style->font)
        return areas;
    ttf::Font& font = *style->font;
    const int lh = font.lineHeight(style->pixels);
    const int margin = 80, width = dst.w - 2 * margin;
    if (choices.empty()) {
        // The line, centred both ways in the box.
        const std::vector<std::string> lines = font.wrap(text, style->pixels, width);
        int y = top + (boxH - int(lines.size()) * lh) / 2;
        for (const std::string& l : lines) {
            fonts.draw(dst, *style, (dst.w - font.width(l, style->pixels)) / 2, y, l, colour);
            y += lh;
        }
        return areas;
    }
    // The choices, one under another from the top, each after its bullet.
    const Color plain = fonts.colour("dialogue text when choosing", {255, 255, 130});
    const Color lit = fonts.colour("highlighted dialogue text when choosing", {255, 255, 180});
    int y = top + 28;
    for (size_t i = 0; i < choices.size(); i++) {
        const std::vector<std::string> lines = font.wrap(choices[i], style->pixels, width - 24);
        const bool on = int(i) == highlighted;
        const image::Image* bullet = on && pictures.bulletOn ? pictures.bulletOn : pictures.bullet;
        if (bullet && bullet->w > 0)
            blitOver(dst, *bullet, margin, y + (lh - bullet->h) / 2);
        DialogueChoiceArea area{margin, y, width, int(lines.size()) * lh};
        for (const std::string& l : lines) {
            fonts.draw(dst, *style, margin + 24, y, l, on ? lit : plain);
            y += lh;
        }
        areas.push_back(area);
        y += 4;
    }
    return areas;
}

} // namespace reluva::awe
