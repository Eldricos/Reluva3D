// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/scene/sprite_font.h"

#include "engines/storm3d/formats/sprite_table.h"

#include <string>

namespace reluva::storm3d {

Error SpriteFont::load(const GameFiles &files) {
    _letters.clear();
    SpriteTable table;
    const SpriteTable::Loader loader = [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> bytes;
        if (Error err = files.read("Sprites/" + std::string(name), bytes))
            return err;
        text.assign(bytes.begin(), bytes.end());
        return Error::success();
    };
    if (Error err = table.load("font.spr", loader))
        return err;
    auto uv = table.animations().find("font_uv");
    auto w = table.animations().find("font_w");
    if (uv == table.animations().end() || w == table.animations().end())
        return Error::fail("font.spr: no font_uv or font_w");
    for (size_t i = 0; i < uv->second.keyframes.size(); ++i) {
        const auto &at = uv->second.keyframes[i];
        const int width =
            i < w->second.keyframes.size() && !w->second.keyframes[i].empty() ? int(w->second.keyframes[i][0]) : 0;
        if (at.size() >= 2)
            _letters.push_back({int(at[0]), int(at[1]), width});
    }
    std::vector<uint8_t> data;
    if (Error err = files.read("Textures/RGB/font.tga", data))
        return err;
    if (Error err = image::decode(data, _sheet))
        return Error::fail("font.tga: " + err.message);
    return Error::success();
}

int SpriteFont::width(std::string_view text) const {
    int w = 0;
    for (char c : text) {
        const size_t i = size_t(uint8_t(c)) - 32;
        if (i < _letters.size())
            w += _letters[i][2] + 1;
    }
    return w;
}

void SpriteFont::draw(Surface &screen, int x, int y, std::string_view text, const std::array<uint8_t, 3> &tint,
                      bool opaque) const {
    auto texel = [this](int tx, int ty) -> const uint8_t * {
        static const uint8_t kNone[4] = {0, 0, 0, 0};
        if (tx < 0 || ty < 0 || tx >= _sheet.w || ty >= _sheet.h)
            return kNone;
        return &_sheet.rgba[(size_t(ty) * size_t(_sheet.w) + size_t(tx)) * 4];
    };
    int pen = x;
    for (char c : text) {
        const size_t i = size_t(uint8_t(c)) - 32;
        if (i >= _letters.size() || !loaded())
            break;
        const auto &[lx, ly, lw] = _letters[i];
        for (int row = 0; row < kHeight; ++row)
            for (int col = 0; col < lw; ++col) {
                const int sx = pen + col;
                const int sy = y + row;
                if (sx < 0 || sy < 0 || sx >= screen.w || sy >= screen.h)
                    continue;
                // The mean of the four texels whose corner the pixel's centre is on.
                int sum[4] = {0, 0, 0, 0};
                for (int dy = -1; dy <= 0; ++dy)
                    for (int dx = -1; dx <= 0; ++dx) {
                        const uint8_t *t = texel(lx + col + dx, ly + row + dy);
                        for (int k = 0; k < 4; ++k)
                            sum[k] += t[k];
                    }
                const int a = (sum[3] + 2) / 4;
                uint8_t *p = screen.row(sy) + size_t(sx) * 4;
                if (opaque) {
                    // Point sampled, the game's frame's thin strokes say,
                    // and kept where the alpha test passes.
                    const uint8_t *t = texel(lx + col, ly + row);
                    if (t[3] < 128)
                        continue;
                    for (size_t k = 0; k < 3; ++k)
                        p[k] = uint8_t(tint[k] * t[k] / 255);
                    p[3] = 255;
                    continue;
                }
                for (size_t k = 0; k < 3; ++k)
                    p[k] = uint8_t((tint[k] * a + p[k] * (255 - a)) / 255);
            }
        pen += lw + 1;
    }
}

} // namespace reluva::storm3d
