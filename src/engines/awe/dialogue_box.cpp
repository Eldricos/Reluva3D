// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/dialogue_box.h"

#include <algorithm>

namespace reluva::awe {

std::vector<DialogueChoiceArea> drawDialogueBox(Surface& dst, const GameFonts& fonts, const DialogueBoxPictures& pictures,
                                                int top, const std::string& text, Color colour,
                                                const std::vector<std::string>& choices, int highlighted,
                                                const std::vector<const image::Image*>& choicePictures) {
    std::vector<DialogueChoiceArea> areas;
    const int boxH = pictures.window && pictures.window->h > 0 ? pictures.window->h : 160;
    if (pictures.window && pictures.window->w > 0)
        image::blitOver(dst, *pictures.window, (dst.w - pictures.window->w) / 2, top);
    const TextStyle* style = fonts.style("dialogue");
    if (!style || !style->font)
        return areas;
    ttf::Font& font = *style->font;
    const int lh = font.lineHeight(style->pixels);
    const int width = 944;
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
    // The pictures: n in a row from x = (744 - (112n - 16)) / 2 + 280, at
    // 42 down the box (0x43d104, 0x43d16c); each hit from 8 before it to
    // 8 after its 96 pixels (0x43cdc3).
    int pictureCount = 0;
    for (size_t i = 0; i < choices.size() && i < choicePictures.size(); i++)
        pictureCount += choicePictures[i] != nullptr;
    if (pictureCount > 0) {
        int x = (744 - (112 * pictureCount - 16)) / 2 + 280;
        for (size_t i = 0; i < choices.size() && i < choicePictures.size(); i++) {
            const image::Image* pic = choicePictures[i];
            if (!pic)
                continue;
            if (pic->w > 0)
                image::blitOver(dst, *pic, x, top + 42);
            areas.push_back({x - 8, top + 42, 112, 96, int(i), true});
            x += 112;
        }
    }
    // The choices, one under another, each after its bullet.
    const Color plain = fonts.colour("dialogue text when choosing", {255, 255, 130});
    const Color lit = fonts.colour("highlighted dialogue text when choosing", {255, 255, 180});
    constexpr int kLeft = 40;
    int y = top + 25;
    for (size_t i = 0; i < choices.size(); i++) {
        const std::vector<std::string> lines = font.wrap(choices[i], style->pixels, width);
        const bool on = int(i) == highlighted;
        const image::Image* bullet = on && pictures.bulletOn ? pictures.bulletOn : pictures.bullet;
        if (bullet && bullet->w > 0)
            image::blitOver(dst, *bullet, kLeft - 14, y + 5);
        DialogueChoiceArea area{0, y, dst.w, int(lines.size()) * lh + 4, int(i), false};
        for (const std::string& l : lines) {
            fonts.draw(dst, *style, kLeft, y, l, on ? lit : plain);
            y += lh;
        }
        areas.push_back(area);
        y += 4;
    }
    return areas;
}

} // namespace reluva::awe
