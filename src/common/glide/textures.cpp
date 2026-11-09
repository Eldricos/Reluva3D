// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/glide/textures.h"

#include "common/glide/glide_defs.h"

#include <algorithm>

namespace reluva::glide {

namespace {

uint32_t widen3(uint32_t v) { return (v << 5 | v << 2 | v >> 1) & 0xff; } // {v[2:0], v[2:0], v[2:1]}
uint32_t widen2(uint32_t v) { return v * 0x55; }                          // {v, v, v, v}
uint32_t widen4(uint32_t v) { return v * 0x11; }
uint32_t widen5(uint32_t v) { return v << 3 | v >> 2; }
uint32_t widen6(uint32_t v) { return v << 2 | v >> 4; }

uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return a << 24 | r << 16 | g << 8 | b; }

uint32_t rgb332(uint32_t v) { return widen3(v >> 5 & 7) << 16 | widen3(v >> 2 & 7) << 8 | widen2(v & 3); }

// Y in bits 7-4, I in 3-2, Q in 1-0: Y + I + Q per channel, clamped.
uint32_t ncc(uint32_t v, const NccTable& t) {
    const int y = t.y[v >> 4 & 15];
    const int16_t* i = t.i[v >> 2 & 3];
    const int16_t* q = t.q[v & 3];
    const int r = std::clamp(y + i[0] + q[0], 0, 255);
    const int g = std::clamp(y + i[1] + q[1], 0, 255);
    const int b = std::clamp(y + i[2] + q[2], 0, 255);
    return uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}

} // namespace

int texelBytes(int format) { return format >= kTexArgb8332 ? 2 : 1; }

bool knownFormat(int format) {
    return (format >= kTexRgb332 && format <= 6) || (format >= kTexArgb8332 && format <= kTexAp88);
}

void levelSize(int lod, int aspect, int& width, int& height) {
    const int side = 1 << std::clamp(lod, 0, 8);
    if (aspect >= 0) {
        width = side;
        height = std::max(1, side >> aspect);
    } else {
        height = side;
        width = std::max(1, side >> -aspect);
    }
}

bool levelIncluded(int lod, int evenOdd) { return (evenOdd & (lod % 2 == 0 ? kLevelsEven : kLevelsOdd)) != 0; }

uint32_t levelBytes(int lod, int aspect, int format) {
    int w = 0, h = 0;
    levelSize(lod, aspect, w, h);
    // In 8-byte units for 16-bit texels: four texels a unit, the sides
    // counted as at least 2 (the spec's table: 1 unit for 2x2, 2x1 and 1x1,
    // 2 for 4x1, 4 for 8x1). 8-bit levels take half a unit for each.
    const uint32_t units = uint32_t(std::max(w, 2)) * uint32_t(std::max(h, 2)) / 4;
    return units * (texelBytes(format) == 2 ? 8 : 4);
}

uint32_t levelOffset(const TextureInfo& info, int evenOdd, int lod) {
    uint32_t offset = 0;
    for (int l = info.largeLod; l > lod; --l)
        if (levelIncluded(l, evenOdd))
            offset += levelBytes(l, info.aspect, info.format);
    return offset;
}

uint32_t textureBytes(const TextureInfo& info, int evenOdd) {
    uint32_t bytes = 0;
    for (int l = info.largeLod; l >= info.smallLod; --l)
        if (levelIncluded(l, evenOdd))
            bytes += levelBytes(l, info.aspect, info.format);
    return (bytes + 7) & ~7u; // a half unit left over isn't used by the next texture
}

uint32_t decodeTexel(int format, uint32_t raw, const uint32_t* palette, const NccTable& ncc_) {
    switch (format) {
    case kTexRgb332: return 0xff000000u | rgb332(raw & 0xff);
    case kTexYiq422: return 0xff000000u | ncc(raw & 0xff, ncc_);
    case kTexAlpha8: return (raw & 0xff) * 0x01010101u;
    case kTexIntensity8: return 0xff000000u | (raw & 0xff) * 0x010101u;
    case kTexAlphaIntensity44: return widen4(raw >> 4 & 15) << 24 | widen4(raw & 15) * 0x010101u;
    case kTexP8: return 0xff000000u | (palette[raw & 0xff] & 0xffffff);
    case 6: { // 8-bit palette to RGBA: the entry's 24 bits as 6:6:6:6 (A R G B)
        const uint32_t p = palette[raw & 0xff] & 0xffffff;
        const uint32_t a = p >> 18 & 63, r = p >> 12 & 63, g = p >> 6 & 63, b = p & 63;
        return argb(widen6(a), widen6(r), widen6(g), widen6(b));
    }
    case kTexArgb8332: return (raw >> 8 & 0xff) << 24 | rgb332(raw & 0xff);
    case kTexAyiq8422: return (raw >> 8 & 0xff) << 24 | ncc(raw & 0xff, ncc_);
    case kTexRgb565: return argb(0xff, widen5(raw >> 11 & 31), widen6(raw >> 5 & 63), widen5(raw & 31));
    case kTexArgb1555:
        return argb(raw & 0x8000 ? 0xff : 0, widen5(raw >> 10 & 31), widen5(raw >> 5 & 31), widen5(raw & 31));
    case kTexArgb4444: return argb(widen4(raw >> 12 & 15), widen4(raw >> 8 & 15), widen4(raw >> 4 & 15), widen4(raw & 15));
    case kTexAlphaIntensity88: return (raw >> 8 & 0xff) << 24 | (raw & 0xff) * 0x010101u;
    case kTexAp88: return (raw >> 8 & 0xff) << 24 | (palette[raw & 0xff] & 0xffffff);
    default: return 0xffffffffu;
    }
}

} // namespace reluva::glide
