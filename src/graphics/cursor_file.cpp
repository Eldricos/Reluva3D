// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/cursor_file.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>

namespace reluva {

namespace {

uint16_t u16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}

uint32_t u32(const uint8_t *p) {
    return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16;
}

// One picture: a BITMAPINFOHEADER, its colours, then the colour (XOR)
// bitmap and the 1-bit AND mask, both bottom up, the header's height
// counting both.
Error readPicture(const uint8_t *p, size_t size, int hotX, int hotY, Cursor &out) {
    if (size >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G')
        return Error::fail("has a PNG picture, which isn't read");
    if (size < 40 || u32(p) < 40)
        return Error::fail("has a picture too short for its header");
    const uint32_t header = u32(p);
    const int w = int(int32_t(u32(p + 4)));
    const int h = int(int32_t(u32(p + 8))) / 2;
    const int bits = u16(p + 14);
    const uint32_t compression = u32(p + 16);
    if (w <= 0 || h <= 0 || w > 256 || h > 256)
        return Error::fail("has a picture of a size not allowed");
    if (compression != 0)
        return Error::fail("has a compressed picture");
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)
        return Error::fail("has a picture of " + std::to_string(bits) + " bits a pixel");
    size_t colours = 0;
    if (bits <= 8) {
        colours = u32(p + 32) ? u32(p + 32) : size_t(1) << bits;
        if (colours > 256)
            return Error::fail("has too many colours");
    }
    const size_t xorPitch = (size_t(w) * size_t(bits) + 31) / 32 * 4;
    const size_t andPitch = (size_t(w) + 31) / 32 * 4;
    const size_t palette = header;
    const size_t xorAt = palette + colours * 4;
    const size_t andAt = xorAt + xorPitch * size_t(h);
    if (andAt + andPitch * size_t(h) > size)
        return Error::fail("has a picture running past its end");

    out = Cursor();
    out.w = w, out.h = h, out.hotX = hotX, out.hotY = hotY;
    out.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    bool alpha = false; // a 32-bit picture's own alpha, used if any is set
    if (bits == 32)
        for (int y = 0; y < h && !alpha; ++y)
            for (int x = 0; x < w && !alpha; ++x)
                alpha = p[xorAt + size_t(y) * xorPitch + size_t(x) * 4 + 3] != 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t *row = p + xorAt + size_t(h - 1 - y) * xorPitch;
        const uint8_t *mask = p + andAt + size_t(h - 1 - y) * andPitch;
        for (int x = 0; x < w; ++x) {
            uint8_t b = 0;
            uint8_t g = 0;
            uint8_t r = 0;
            uint8_t a = 255;
            if (bits <= 8) {
                const int perByte = 8 / bits;
                const int shift = (perByte - 1 - x % perByte) * bits;
                const size_t index = (row[x / perByte] >> shift) & ((1 << bits) - 1);
                if (index < colours) {
                    const uint8_t *c = p + palette + index * 4;
                    b = c[0], g = c[1], r = c[2];
                }
            } else {
                const uint8_t *c = row + size_t(x) * (size_t(bits) / 8);
                b = c[0], g = c[1], r = c[2];
                if (alpha)
                    a = c[3];
            }
            const bool see = (mask[x / 8] >> (7 - x % 8)) & 1;
            if (!alpha && see) {
                // Black under the mask leaves the screen as it is; any
                // other colour inverts it there, which a picture can't
                // do: drawn black, as it shows on light screens.
                if (b || g || r)
                    b = g = r = 0;
                else
                    a = 0;
            }
            uint8_t *o = out.pixel(x, y);
            o[0] = r, o[1] = g, o[2] = b, o[3] = a;
        }
    }
    return Error::success();
}

// An icon or cursor file: a directory of pictures of different sizes, of
// which the one nearest 32 by 32, the pointer's size, is taken.
Error readIconFile(const uint8_t *p, size_t size, Cursor &out) {
    if (size < 6 || u16(p) != 0 || (u16(p + 2) != 1 && u16(p + 2) != 2))
        return Error::fail("isn't a cursor or icon file");
    const bool cursor = u16(p + 2) == 2;
    const size_t count = u16(p + 4);
    if (count == 0 || 6 + count * 16 > size)
        return Error::fail("has no pictures");
    size_t best = 0;
    int bestScore = 1 << 30;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *e = p + 6 + i * 16;
        const int w = e[0] ? e[0] : 256; // 0 stands for 256
        const int h = e[1] ? e[1] : 256;
        const int score = std::abs(w - 32) + std::abs(h - 32);
        if (score < bestScore)
            best = i, bestScore = score;
    }
    const uint8_t *e = p + 6 + best * 16;
    const uint32_t length = u32(e + 8);
    const uint32_t at = u32(e + 12);
    if (at >= size || length > size - at)
        return Error::fail("has a picture past its end");
    // A cursor's entry gives its hotspot; an icon's is taken to be its
    // middle.
    const int w = e[0] ? e[0] : 256;
    const int h = e[1] ? e[1] : 256;
    const int hotX = cursor ? u16(e + 4) : w / 2;
    const int hotY = cursor ? u16(e + 6) : h / 2;
    return readPicture(p + at, length, hotX, hotY, out);
}

struct Chunk {
    std::string id;
    const uint8_t *data = nullptr;
    size_t size = 0;
};

// The chunks within `size` bytes; one claiming more than is left is cut
// short, as some files' sizes count their own header.
std::vector<Chunk> chunks(const uint8_t *p, size_t size) {
    std::vector<Chunk> list;
    size_t at = 0;
    while (at + 8 <= size) {
        Chunk c;
        c.id.assign(reinterpret_cast<const char *>(p + at), 4);
        c.size = std::min<size_t>(u32(p + at + 4), size - at - 8);
        c.data = p + at + 8;
        list.push_back(c);
        at += 8 + c.size + (c.size & 1);
    }
    return list;
}

constexpr uint32_t kAfIcon = 1; // the frames are icon or cursor files
constexpr uint32_t kJiffy = 60; // the rates' unit, a sixtieth of a second

Error readAnimated(const uint8_t *p, size_t size, CursorFile &out) {
    const std::vector<Chunk> top = chunks(p + 12, std::min<size_t>(size - 12, u32(p + 4) - 4));
    const Chunk *header = nullptr;
    const Chunk *rate = nullptr;
    const Chunk *seq = nullptr;
    const Chunk *frames = nullptr;
    for (const Chunk &c : top) {
        if (c.id == "anih" && c.size >= 36)
            header = &c;
        else if (c.id == "rate")
            rate = &c;
        else if (c.id == "seq ")
            seq = &c;
        else if (c.id == "LIST" && c.size >= 4 && std::string(reinterpret_cast<const char *>(c.data), 4) == "fram")
            frames = &c;
    }
    if (!header || !frames)
        return Error::fail("is an animated cursor without its header or frames");
    const uint32_t frameCount = u32(header->data + 4);
    const uint32_t stepCount = u32(header->data + 8);
    const uint32_t defaultRate = u32(header->data + 28);
    if (!(u32(header->data + 32) & kAfIcon))
        return Error::fail("is an animated cursor of raw pictures, which aren't read");
    for (const Chunk &c : chunks(frames->data + 4, frames->size - 4)) {
        if (c.id != "icon")
            continue;
        Cursor frame;
        if (Error e = readIconFile(c.data, c.size, frame))
            return Error::fail("has a frame that " + e.message);
        out.frames.push_back(std::move(frame));
    }
    if (out.frames.empty() || out.frames.size() < frameCount)
        return Error::fail("is an animated cursor missing frames");
    for (uint32_t i = 0; i < stepCount; ++i) {
        CursorFile::Step s;
        s.frame = seq && seq->size >= 4 * (i + 1) ? int(u32(seq->data + 4 * i)) : int(i);
        const uint32_t jiffies = rate && rate->size >= 4 * (i + 1) ? u32(rate->data + 4 * i) : defaultRate;
        s.ms = jiffies * 1000 / kJiffy;
        if (s.frame < 0 || size_t(s.frame) >= out.frames.size())
            return Error::fail("is an animated cursor whose steps name a frame it hasn't");
        out.steps.push_back(s);
    }
    if (out.steps.empty())
        out.steps.push_back({0, 0});
    return Error::success();
}

} // namespace

const Cursor *CursorFile::at(uint64_t ms) const {
    if (steps.empty() || frames.empty())
        return nullptr;
    uint64_t total = 0;
    for (const Step &s : steps)
        total += s.ms;
    if (total == 0)
        return &frames[size_t(steps[0].frame)];
    uint64_t t = ms % total;
    for (const Step &s : steps) {
        if (t < s.ms)
            return &frames[size_t(s.frame)];
        t -= s.ms;
    }
    return &frames[size_t(steps.back().frame)];
}

Error readCursorFile(const std::vector<uint8_t> &data, CursorFile &out) {
    out = CursorFile();
    if (data.size() >= 12 && std::string(data.begin(), data.begin() + 4) == "RIFF" &&
        std::string(data.begin() + 8, data.begin() + 12) == "ACON")
        return readAnimated(data.data(), data.size(), out);
    Cursor c;
    if (Error e = readIconFile(data.data(), data.size(), c))
        return e;
    out.frames.push_back(std::move(c));
    out.steps.push_back({0, 0});
    return Error::success();
}

Cursor standardArrow() {
    // Drawn for Reluva: '#' black, 'W' white, '.' see-through.
    static const char *const kRows[] = {
        "#.........", "##........", "#W#.......", "#WW#......", "#WWW#.....", "#WWWW#....",
        "#WWWWW#...", "#WWWWWW#..", "#WWWWWWW#.", "#WWWWWWWW#", "#WWWWW####", "#WW#WW#...",
        "#W#.#WW#..", "##..#WW#..", "#....#WW#.", ".....#WW#.", "......##..",
    };
    Cursor c;
    c.w = 10, c.h = int(std::size(kRows));
    c.rgba.assign(size_t(c.w) * size_t(c.h) * 4, 0);
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const char k = kRows[y][x];
            if (k == '.')
                continue;
            uint8_t *o = c.pixel(x, y);
            o[0] = o[1] = o[2] = k == 'W' ? 255 : 0;
            o[3] = 255;
        }
    return c;
}

} // namespace reluva
