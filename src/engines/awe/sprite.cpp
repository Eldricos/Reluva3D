// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/sprite.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

void SpriteTransform::apply(float u, float v, float& outX, float& outY) const {
    // D3DX's row vectors: p * Scale * RotationZ(roll) + position.
    const float c = std::cos(roll), s = std::sin(roll);
    const float px = u * scaleX, py = v * scaleY;
    outX = px * c - py * s + x;
    outY = px * s + py * c + y;
}

void drawSprite(Surface& dst, const image::Image& img, const SpriteTransform& t, bool keepAlpha) {
    if (img.w <= 0 || img.h <= 0 || t.scaleX == 0 || t.scaleY == 0)
        return;
    // The screen area it covers.
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int corner = 0; corner < 4; corner++) {
        float px, py;
        t.apply(corner & 1 ? float(img.w) : 0, corner & 2 ? float(img.h) : 0, px, py);
        minX = std::min(minX, px), maxX = std::max(maxX, px);
        minY = std::min(minY, py), maxY = std::max(maxY, py);
    }
    const int x0 = std::max(0, int(std::floor(minX))), x1 = std::min(dst.w, int(std::ceil(maxX)));
    const int y0 = std::max(0, int(std::floor(minY))), y1 = std::min(dst.h, int(std::ceil(maxY)));
    const float c = std::cos(t.roll), s = std::sin(t.roll);
    for (int y = y0; y < y1; y++) {
        uint8_t* row = dst.row(y);
        for (int x = x0; x < x1; x++) {
            // Back to the picture, from the pixel's centre; sampled
            // bilinearly between texel centres, as the game's filtering.
            const float dx = float(x) + 0.5f - t.x, dy = float(y) + 0.5f - t.y;
            const float u = (dx * c + dy * s) / t.scaleX, v = (-dx * s + dy * c) / t.scaleY;
            if (u < 0 || v < 0 || u >= float(img.w) || v >= float(img.h))
                continue;
            const float fu = u - 0.5f, fv = v - 0.5f;
            const int iu = int(std::floor(fu)), iv = int(std::floor(fv));
            const float au = fu - float(iu), av = fv - float(iv);
            float px[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                const int su = std::clamp(iu + (k & 1), 0, img.w - 1), sv = std::clamp(iv + (k >> 1), 0, img.h - 1);
                const float w = (k & 1 ? au : 1 - au) * (k & 2 ? av : 1 - av);
                const uint8_t* p = img.pixel(su, sv);
                for (int ch = 0; ch < 4; ch++)
                    px[ch] += float(p[ch]) * w;
            }
            uint8_t* d = row + x * 4;
            const float a = px[3] / 255.0f;
            if (a <= 0)
                continue;
            if (keepAlpha) {
                const float da = float(d[3]) / 255.0f * (1 - a), out = a + da;
                for (int ch = 0; ch < 3; ch++)
                    d[ch] = uint8_t(std::lround((px[ch] * a + float(d[ch]) * da) / out));
                d[3] = uint8_t(std::lround(out * 255));
            } else {
                for (int ch = 0; ch < 3; ch++)
                    d[ch] = uint8_t(std::lround(px[ch] * a + float(d[ch]) * (1 - a)));
                d[3] = 255;
            }
        }
    }
}

} // namespace reluva::awe
