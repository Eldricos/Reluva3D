// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/dissolve.h"

#include <algorithm>
#include <vector>

namespace reluva::stgenerations {
namespace Dissolve {

namespace {

const int kHalf = 0x200;

void copyRow(uint8_t *row, const uint8_t *source, int width, int count, RandomSource &random) {
    for (int i = 0; i < count; i++) {
        int x = (int)random.getRandomNumber(width - 1);
        row[x] = source[x];
    }
}

} // namespace

void speckle(Canvas &surface, uint8_t colour, int amount, int style, RandomSource &random) {
    int width = surface.w;
    int height = surface.h;
    if (width <= 0 || height <= 0 || amount <= 0)
        return;
    amount = std::min(amount, 0x3FF);
    bool fill = amount >= kHalf;
    if (fill)
        amount = 0x3FF - amount;

    // Past half, the colour first and the picture over it.
    std::vector<uint8_t> picture;
    if (fill) {
        picture.resize((size_t)width * height);
        for (int y = 0; y < height; y++)
            std::copy_n((const uint8_t *)surface.getBasePtr(0, y), width, &picture[(size_t)y * width]);
        surface.fillRect(Rect(0, 0, width, height), colour);
    }

    if (style == 2) {
        int run = std::max(1, width / 16);
        int streaks = height * amount / 64;
        for (int i = 0; i < streaks; i++) {
            int y = (int)random.getRandomNumber(height - 1);
            int x = (int)random.getRandomNumber(width - 1);
            int n = std::min(run, width - x);
            uint8_t *row = (uint8_t *)surface.getBasePtr(x, y);
            if (fill)
                std::copy_n(&picture[(size_t)y * width + x], n, row);
            else
                std::fill_n(row, n, colour);
        }
        return;
    }

    int count = width * amount / 1024;
    for (int y = 0; y < height; y++) {
        uint8_t *row = (uint8_t *)surface.getBasePtr(0, y);
        if (fill) {
            copyRow(row, &picture[(size_t)y * width], width, count, random);
        } else {
            for (int i = 0; i < count; i++)
                row[random.getRandomNumber(width - 1)] = colour;
        }
    }
}

void crossfade(Canvas &surface, const Canvas &from, const Canvas &to, int amount, RandomSource &random) {
    int width = std::min<int>({surface.w, from.w, to.w});
    int height = std::min<int>({surface.h, from.h, to.h});
    if (width <= 0 || height <= 0)
        return;
    amount = std::max(0, std::min(amount, 0x3FF));
    const Canvas *base = &from;
    const Canvas *over = &to;
    if (amount >= kHalf) {
        std::swap(base, over);
        amount = 0x3FF - amount;
    }
    int count = width * amount / 1024;
    for (int y = 0; y < height; y++) {
        uint8_t *row = (uint8_t *)surface.getBasePtr(0, y);
        if (row != base->getBasePtr(0, y))
            std::copy_n((const uint8_t *)base->getBasePtr(0, y), width, row);
        copyRow(row, (const uint8_t *)over->getBasePtr(0, y), width, count, random);
    }
}

} // namespace Dissolve
} // namespace reluva::stgenerations
