// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The header as Star Trek.exe reads it (0x4b3a30); the blocks' coding
// worked out from the game's 671 frames, every row of which decodes to the
// frame's width exactly.

#include "engines/stawayteam/formats/frame.h"

#include <string>

namespace reluva::stawayteam {

namespace {

constexpr size_t kHeaderSize = 28;
// The game won't read a block over 4 MB (0x4b3ae3).
constexpr uint32_t kMaxBlock = 0x400000;

uint16_t le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// A block's data and where each of its rows starts in it.
struct Block {
    const uint8_t *data = nullptr;
    uint32_t size = 0;
    std::vector<uint32_t> rows;

    // Row `y`'s bytes: up to the next row's start, the last to the block's end.
    bool row(int y, const uint8_t *&begin, const uint8_t *&end) const {
        const uint32_t from = rows[size_t(y)];
        const uint32_t to = size_t(y) + 1 < rows.size() ? rows[size_t(y) + 1] : size;
        if (from > to || to > size)
            return false;
        begin = data + from;
        end = data + to;
        return true;
    }
};

Error damaged(const std::string &what) {
    return Error::fail("damaged frame: " + what);
}

// A row of the colour blocks (0 and 2). A byte with its top bit set skips
// (bits 0-6) pixels; else with bit 6 set (bits 0-5) pixels follow, one
// colour each; else one colour follows, for (bits 0-5) pixels. `paint` is
// called for each pixel drawn, with the index of the colour among the row's
// colours (the alpha block keeps one byte for each).
template<typename Paint>
bool decodeRow(const uint8_t *p, const uint8_t *end, int width, Paint paint) {
    int x = 0;
    int colour = 0;
    while (p < end) {
        const uint8_t code = *p++;
        const int count = code & ((code & 0x80) ? 0x7f : 0x3f);
        if (x + count > width)
            return false;
        if (code & 0x80) {
            x += count;
        } else if (code & 0x40) {
            if (end - p < 2 * count)
                return false;
            for (int i = 0; i < count; ++i, p += 2)
                paint(x++, le16(p), colour++);
        } else {
            if (end - p < 2)
                return false;
            for (int i = 0; i < count; ++i)
                paint(x++, le16(p), colour);
            p += 2;
            ++colour;
        }
    }
    return x == width;
}

} // namespace

Error readFrame(const uint8_t *data, size_t size, Frame &out, size_t *used) {
    out = Frame{};
    if (size < kHeaderSize || data[0] != 0x2e)
        return Error::fail("not one of the game's frames");
    if (data[1] != 16)
        return Error::fail("a frame of " + std::to_string(data[1]) + "-bit pixels, not 16");
    out.originX = static_cast<int16_t>(le16(data + 2));
    out.originY = static_cast<int16_t>(le16(data + 4));
    out.width = le16(data + 6);
    out.height = le16(data + 8);
    out.flags = le32(data + 12);
    const size_t pixels = size_t(out.width) * size_t(out.height);
    out.pixels.assign(pixels, 0);
    out.alpha.assign(pixels, 0);

    // Three blocks, each followed by its rows' starts: the pixels drawn
    // whole, the alpha of those blended, and those blended.
    Block blocks[3];
    size_t at = kHeaderSize;
    for (int b = 0; b < 3; ++b) {
        Block &block = blocks[b];
        block.size = le32(data + 16 + 4 * b);
        if (block.size == 0)
            continue;
        if (block.size > kMaxBlock || size - at < block.size)
            return damaged("block " + std::to_string(b) + " runs past the end");
        block.data = data + at;
        at += block.size;
        if ((size - at) / 4 < size_t(out.height))
            return damaged("block " + std::to_string(b) + "'s rows run past the end");
        block.rows.resize(size_t(out.height));
        for (uint32_t &row : block.rows) {
            row = le32(data + at);
            at += 4;
        }
    }
    if (used)
        *used = at;
    if (out.height == 0)
        return Error::success();

    // Block 0: raw when its first row starts at its beginning (the
    // textures); else run-length coded, after its own size (rows from 4).
    const Block &whole = blocks[0];
    if (whole.size == 0)
        return damaged("no pixels");
    if (whole.rows[0] == 0) {
        if (whole.size < pixels * 2)
            return damaged("too few pixels");
        for (size_t i = 0; i < pixels; ++i) {
            out.pixels[i] = le16(whole.data + 2 * i);
            out.alpha[i] = 255;
        }
        return Error::success();
    }
    for (int y = 0; y < out.height; ++y) {
        const uint8_t *begin = nullptr;
        const uint8_t *end = nullptr;
        const size_t line = size_t(y) * size_t(out.width);
        auto opaque = [&](int x, uint16_t colour, int) {
            out.pixels[line + size_t(x)] = colour;
            out.alpha[line + size_t(x)] = 255;
        };
        if (!whole.row(y, begin, end) || !decodeRow(begin, end, out.width, opaque))
            return damaged("row " + std::to_string(y));
    }

    // Blocks 1 and 2 come together: the blended pixels, and an alpha byte
    // for each of their colours, in the same order.
    const Block &alphas = blocks[1];
    const Block &blended = blocks[2];
    if ((alphas.size == 0) != (blended.size == 0))
        return damaged("blended pixels without their alpha");
    if (blended.size == 0)
        return Error::success();
    for (int y = 0; y < out.height; ++y) {
        const uint8_t *begin = nullptr;
        const uint8_t *end = nullptr;
        const uint8_t *alphaBegin = nullptr;
        const uint8_t *alphaEnd = nullptr;
        if (!blended.row(y, begin, end) || !alphas.row(y, alphaBegin, alphaEnd))
            return damaged("row " + std::to_string(y));
        const size_t line = size_t(y) * size_t(out.width);
        const size_t alphaCount = size_t(alphaEnd - alphaBegin);
        bool fits = true;
        auto blend = [&](int x, uint16_t colour, int index) {
            if (size_t(index) >= alphaCount) {
                fits = false;
                return;
            }
            out.pixels[line + size_t(x)] = colour;
            out.alpha[line + size_t(x)] = alphaBegin[index];
        };
        if (!decodeRow(begin, end, out.width, blend) || !fits)
            return damaged("row " + std::to_string(y) + " of its blended pixels");
    }
    return Error::success();
}

image::Image frameImage(const Frame &frame) {
    image::Image img;
    img.w = frame.width;
    img.h = frame.height;
    img.rgba.resize(frame.pixels.size() * 4);
    for (size_t i = 0; i < frame.pixels.size(); ++i) {
        const uint16_t c = frame.pixels[i];
        uint8_t *p = img.rgba.data() + i * 4;
        // Each field widened to 8 bits with its top bits repeated below,
        // so white stays 255.
        const int r = c >> 11;
        const int g = (c >> 5) & 0x3f;
        const int b = c & 0x1f;
        p[0] = uint8_t(r << 3 | r >> 2);
        p[1] = uint8_t(g << 2 | g >> 4);
        p[2] = uint8_t(b << 3 | b >> 2);
        p[3] = frame.alpha[i];
    }
    return img;
}

} // namespace reluva::stawayteam
