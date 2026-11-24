// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the frames are laid out by direction is worked out from the guests'
// walks: docs/engines/tpw/games/simcoaster/formats/sprites.md.

#include "engines/tpw/ui/sprites.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr float kPi = 3.14159265f;
// Five drawn directions, an eighth of a turn apart: away, away and right,
// right, towards and right, towards.
constexpr int kDrawnDirections = 5;
constexpr int kTurnEighths = 8;

} // namespace

Error LoadedSprite::load(const SearchSet &files, const std::string &name) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name + ".ESP", data))
        return err;
    if (Error err = set.parse(data))
        return Error::fail(name + ".ESP: " + err.message);
    if (Error err = files.read(name + ".TPC", data))
        return err;
    if (Error err = sheet.parse(data))
        return Error::fail(name + ".TPC: " + err.message);
    return Error::success();
}

SpritePose spritePose(const SpriteSet::Animation &animation, float heading, uint32_t step) {
    SpritePose pose;
    const int frames = std::max(1, int(animation.frames));
    const int frame = int(step % uint32_t(frames));
    int direction = 0;
    if (animation.directions == kDrawnDirections) {
        // The eighth of a turn the heading is nearest, 0 to 7, clockwise
        // from away; 5 to 7 are 3 to 1 mirrored.
        float turns = heading / (2 * kPi);
        turns -= std::floor(turns);
        const int eighth = int(std::lround(turns * kTurnEighths)) % kTurnEighths;
        direction = eighth <= 4 ? eighth : kTurnEighths - eighth;
        pose.mirror = eighth > 4;
    }
    pose.frame = size_t(animation.first + direction * frames + frame);
    return pose;
}

namespace {

// So much nearer the scene must be to hide a sprite: the ground it stands
// on, and the seat a head sits in, are as near as it.
constexpr float kDepthSlack = 1.03f;

void drawFrame(Surface &screen, const SpriteSheet &sheet, size_t frame, float x, float y, float scale, bool mirror,
               const soft3d::Rasterizer *depth, float invZ) {
    if (frame >= sheet.frames().size() || scale <= 0)
        return;
    const SpriteSheet::Frame &f = sheet.frames()[frame];
    const float left = x + (mirror ? -float(f.left + f.width) : float(f.left)) * scale;
    const float top = y + float(f.top) * scale;
    const int x0 = std::max(0, int(std::floor(left)));
    const int y0 = std::max(0, int(std::floor(top)));
    const int x1 = std::min(screen.w, int(std::ceil(left + float(f.width) * scale)));
    const int y1 = std::min(screen.h, int(std::ceil(top + float(f.height) * scale)));
    for (int sy = y0; sy < y1; ++sy) {
        const int fy = int((float(sy) + 0.5f - top) / scale);
        if (fy < 0 || fy >= f.height)
            continue;
        uint8_t *row = screen.row(sy);
        for (int sx = x0; sx < x1; ++sx) {
            int fx = int((float(sx) + 0.5f - left) / scale);
            if (fx < 0 || fx >= f.width)
                continue;
            if (mirror)
                fx = f.width - 1 - fx;
            const SpriteSheet::Colour &c = sheet.palette()[f.pixels[size_t(fy) * size_t(f.width) + size_t(fx)]];
            if (c.a == 0 || (depth && depth->depthAt(sx, sy) > invZ * kDepthSlack))
                continue;
            uint8_t *p = row + 4 * sx;
            p[0] = uint8_t((p[0] * (255 - c.a) + c.r * c.a) / 255);
            p[1] = uint8_t((p[1] * (255 - c.a) + c.g * c.a) / 255);
            p[2] = uint8_t((p[2] * (255 - c.a) + c.b * c.a) / 255);
        }
    }
}

} // namespace

void drawSpriteFrame(Surface &screen, const SpriteSheet &sheet, size_t frame, float x, float y, float scale,
                     bool mirror) {
    drawFrame(screen, sheet, frame, x, y, scale, mirror, nullptr, 0);
}

void drawSpriteFrame(Surface &screen, const SpriteSheet &sheet, size_t frame, float x, float y, float scale,
                     bool mirror, const soft3d::Rasterizer &depth, float invZ) {
    drawFrame(screen, sheet, frame, x, y, scale, mirror, &depth, invZ);
}

} // namespace reluva::tpw
