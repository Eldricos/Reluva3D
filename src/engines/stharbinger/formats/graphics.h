// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"
#include "graphics/palette.h"
#include "common/stream.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace reluva::stharbinger {

// Backgrounds (.CMP): u32 length, then PKWARE DCL "implode" data that unpacks to
// 640x480 8-bit pixels (a few have a 768-byte VGA palette appended).
// `palette` is set and `hasPalette` true only for those.
bool loadCMP(SeekableReadStream &stream, Surface &surface, Palette *palette = nullptr, bool *hasPalette = nullptr);

// Palettes (.PAL): 768 bytes of 6-bit VGA values.
bool loadPAL(SeekableReadStream &stream, Palette &palette);

// Animations and sprites (.RLE): an 8-byte header, u16 frame count, then frames.
// Scene animations have the fixed header 00 00 01 01 0A FF 00 00; arcade
// sprites keep their target data there. Frames are kept compressed and decoded
// when drawn.
class RleAnimation {
public:
    struct Frame {
        uint8_t key = 0; // header byte 0, purpose unknown
        // size and position
        int w = 0;
        int h = 0;
        int x = 0;
        int y = 0;
        // arcade sprites: gun positions in the frame
        int16_t muzzleX[3] = {};
        int16_t muzzleY[3] = {};
        std::vector<uint8_t> data;
    };

    // Loads a scene animation (checks the fixed header).
    bool load(SeekableReadStream &stream);
    bool load(const uint8_t *data, uint32_t size);
    // Loads any RLE file and returns its 8 header bytes (arcade sprites).
    bool loadWithHeader(SeekableReadStream &stream, uint8_t header[8]);

    int frameCount() const { return int(_frames.size()); }
    const Frame &getFrame(int i) const { return _frames[i]; }

    // Draws frame `i` at its own position plus (dx, dy), leaving skipped pixels alone.
    void drawFrame(int i, Surface &dst, int dx = 0, int dy = 0) const;
    // Draws frame `i` with its top-left at (x, y), clipped to `clip` if given.
    void drawFrameAt(int i, Surface &dst, int x, int y, const Rect *clip = nullptr) const;

private:
    bool parse(const uint8_t *data, uint32_t size, bool checkHeader);

    std::vector<Frame> _frames;
};

// A phaser beam (DEEP.EXE 0x27215): a line drawn as vertical runs that taper
// from 13 pixels at (x0, y0) to 1 pixel at (x1, y1).
void drawPhaserBeam(Surface &dst, int x0, int y0, int x1, int y1, uint8_t color);

// An enemy beam (0x273E4 / 0x2759D): a jittery line, in thirds of width 1, 3
// and 5 in colours c, c + 1, c + 2, each point a streak of `streak` pixels
// downward, stopping at `bottom`. `random(n)` gives 0..n-1.
template<class Random>
void drawEnemyBeam(Surface &dst, int x0, int y0, int x1, int y1, uint8_t color, int streak, int bottom, Random random) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int len = std::max(1, std::max(dx, dy));
    int err = dx - dy;
    int x = x0;
    int y = y0;
    for (int i = 0; i <= len; i++) {
        int band = i < len / 3 ? 0 : (i < 2 * len / 3 ? 1 : 2);
        int width = 1 + band * 2;
        uint8_t c = uint8_t(color + band);
        int px = x + random(width) - width / 2;
        for (int k = 0; k < streak && y + k < bottom; k++)
            if (px >= 0 && px < dst.w && y + k >= 0 && y + k < dst.h)
                *draw::at(dst, px, y + k) = c;
        if (x == x1 && y == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

} // namespace reluva::stharbinger
