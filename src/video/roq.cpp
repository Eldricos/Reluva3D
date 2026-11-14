// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// RoQ movies. The chunks, the cells and the 2-bit codes that build each
// frame, and the DPCM sound were read from the format's public
// descriptions (the MultimediaWiki's "RoQ" page, the GPL Quake III Arena
// source's cl_cin.c as documentation); docs/common/video/roq.md describes
// them. This is new code written from that description.

#include "video/roq.h"

#include <algorithm>

namespace reluva::roq {

namespace {

constexpr uint16_t kSignature = 0x1084;
constexpr uint16_t kInfo = 0x1001;
constexpr uint16_t kCodebook = 0x1002;
constexpr uint16_t kFrame = 0x1011;
constexpr uint16_t kJpegFrame = 0x1012;
constexpr uint16_t kSoundMono = 0x1020;
constexpr uint16_t kSoundStereo = 0x1021;
constexpr size_t kChunkHeader = 8;

uint16_t u16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}
uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

Error Movie::open(std::vector<uint8_t> file) {
    _file = std::move(file);
    _width = _height = _channels = 0;
    if (_file.size() < kChunkHeader || u16(_file.data()) != kSignature)
        return Error::fail("Not a RoQ movie");
    _fps = u16(&_file[6]);
    if (_fps <= 0)
        _fps = 30;
    _start = kChunkHeader;
    // Its size, and whether its sound is mono or stereo, from the first
    // chunks that say.
    for (size_t at = _start; at + kChunkHeader <= _file.size();) {
        const uint16_t id = u16(&_file[at]);
        const uint32_t size = u32(&_file[at + 2]);
        if (size > _file.size() - at - kChunkHeader)
            break;
        if (id == kInfo && size >= 4 && !_width) {
            _width = u16(&_file[at + kChunkHeader]);
            _height = u16(&_file[at + kChunkHeader + 2]);
        } else if ((id == kSoundMono || id == kSoundStereo) && !_channels) {
            _channels = id == kSoundStereo ? 2 : 1;
        }
        if (_width && _channels)
            break;
        at += kChunkHeader + size;
    }
    // Frames are built of 16x16 blocks.
    if (_width <= 0 || _height <= 0 || _width % 16 || _height % 16 || _width > 4096 || _height > 4096)
        return Error::fail("RoQ movie has no size, or one not made of 16x16 blocks");
    rewind();
    return Error::success();
}

void Movie::rewind() {
    _pos = _start;
    _frame = 0;
    // The frame before the first is black.
    const size_t pixels = size_t(_width) * size_t(_height);
    _last = {std::vector<uint8_t>(pixels, 0), std::vector<uint8_t>(pixels, 128), std::vector<uint8_t>(pixels, 128)};
    _planes = _last;
}

Error Movie::nextFrame(bool &more, std::vector<int16_t> *sound) {
    more = false;
    while (_pos + kChunkHeader <= _file.size()) {
        const uint16_t id = u16(&_file[_pos]);
        const uint32_t size = u32(&_file[_pos + 2]);
        const uint16_t arg = u16(&_file[_pos + 6]);
        if (size > _file.size() - _pos - kChunkHeader)
            return Error::fail("RoQ chunk " + std::to_string(id) + " runs past the file's end");
        const uint8_t *data = &_file[_pos + kChunkHeader];
        _pos += kChunkHeader + size;
        if (id == kCodebook) {
            // As many 2x2 cells as the argument's high byte says and 4x4
            // ones as its low byte, 0 meaning 256 (for the 4x4 ones, when
            // there's room left for them).
            size_t n2 = arg >> 8;
            size_t n4 = arg & 0xff;
            if (n2 == 0)
                n2 = 256;
            if (n4 == 0 && n2 * 6 < size)
                n4 = 256;
            if (n2 * 6 + n4 * 4 > size)
                return Error::fail("RoQ codebook is shorter than its cells");
            for (size_t i = 0; i < n2; ++i) {
                const uint8_t *c = data + i * 6;
                _cells2[i] = {{c[0], c[1], c[2], c[3]}, c[4], c[5]};
            }
            for (size_t i = 0; i < n4; ++i)
                std::copy_n(data + n2 * 6 + i * 4, 4, _cells4[i].begin());
        } else if (id == kFrame) {
            if (Error err = decodeFrame(data, size, arg))
                return err;
            ++_frame;
            more = true;
            return Error::success();
        } else if (id == kSoundMono || id == kSoundStereo) {
            if (sound)
                decodeSound(data, size, arg, id == kSoundStereo, *sound);
        } else if (id == kJpegFrame) {
            return Error::fail("RoQ JPEG frames aren't supported");
        }
    }
    return Error::success();
}

void Movie::putCell2(int x, int y, const Cell2 &c, int scale) {
    for (int i = 0; i < 4; ++i)
        for (int dy = 0; dy < scale; ++dy)
            for (int dx = 0; dx < scale; ++dx) {
                const size_t at = size_t(y + (i >> 1) * scale + dy) * size_t(_width) + size_t(x + (i & 1) * scale + dx);
                _planes[0][at] = c.y[size_t(i)];
                _planes[1][at] = c.u;
                _planes[2][at] = c.v;
            }
}

void Movie::putCell4(int x, int y, const Cell4 &c, int scale) {
    for (int i = 0; i < 4; ++i)
        putCell2(x + (i & 1) * 2 * scale, y + (i >> 1) * 2 * scale, _cells2[c[size_t(i)]], scale);
}

void Movie::copyFromLast(int x, int y, int size, int dx, int dy) {
    // A source past the frame's edge takes its nearest pixel.
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const int sx = std::clamp(x + i + dx, 0, _width - 1);
            const int sy = std::clamp(y + j + dy, 0, _height - 1);
            const size_t to = size_t(y + j) * size_t(_width) + size_t(x + i);
            const size_t from = size_t(sy) * size_t(_width) + size_t(sx);
            for (size_t p = 0; p < 3; ++p)
                _planes[p][to] = _last[p][from];
        }
}

Error Movie::decodeFrame(const uint8_t *data, size_t size, uint16_t arg) {
    // Two pictures in turn: the frame is drawn over the one from two
    // frames ago (the second over a copy of the first), moves taking from
    // the last.
    std::swap(_planes, _last);
    if (_frame == 1)
        _planes = _last;
    // The frame starts as that picture; each 8x8 block of each 16x16 one (left
    // to right, top to bottom) is then kept, moved from the last frame,
    // filled from a 4x4 cell doubled, or cut in four 4x4 blocks, each kept,
    // moved, a 4x4 cell, or four 2x2 cells. Each choice is a 2-bit code,
    // eight to a 16-bit word, highest first, the words among the bytes
    // they're for. A move is from the frame's mean motion, in the
    // argument, and a byte's two 4-bit offsets.
    const int meanX = int8_t(arg >> 8);
    const int meanY = int8_t(arg & 0xff);
    size_t at = 0;
    bool shortData = false;
    auto byte = [&]() -> uint8_t {
        if (at >= size) {
            shortData = true;
            return 0;
        }
        return data[at++];
    };
    uint16_t codes = 0;
    int left = 0;
    auto code = [&]() {
        if (left == 0) {
            const uint8_t lo = byte();
            codes = uint16_t(lo | byte() << 8);
            left = 8;
        }
        --left;
        return (codes >> (left * 2)) & 3;
    };
    auto move = [&](int x, int y, int blockSize) {
        const uint8_t b = byte();
        copyFromLast(x, y, blockSize, 8 - (b >> 4) - meanX, 8 - (b & 15) - meanY);
    };
    for (int my = 0; my < _height; my += 16)
        for (int mx = 0; mx < _width; mx += 16)
            for (int j = 0; j < 4; ++j) {
                const int bx = mx + 8 * (j & 1);
                const int by = my + 8 * (j >> 1);
                switch (code()) {
                case 0:
                    break;
                case 1:
                    move(bx, by, 8);
                    break;
                case 2:
                    putCell4(bx, by, _cells4[byte()], 2);
                    break;
                default:
                    for (int k = 0; k < 4; ++k) {
                        const int sx = bx + 4 * (k & 1);
                        const int sy = by + 4 * (k >> 1);
                        switch (code()) {
                        case 0:
                            break;
                        case 1:
                            move(sx, sy, 4);
                            break;
                        case 2:
                            putCell4(sx, sy, _cells4[byte()], 1);
                            break;
                        default:
                            for (int c = 0; c < 4; ++c)
                                putCell2(sx + 2 * (c & 1), sy + 2 * (c >> 1), _cells2[byte()], 1);
                        }
                    }
                }
            }
    if (shortData)
        return Error::fail("RoQ frame " + std::to_string(_frame) + " is shorter than its codes");
    return Error::success();
}

void Movie::decodeSound(const uint8_t *data, size_t size, uint16_t arg, bool stereo, std::vector<int16_t> &out) {
    // Each byte a change to its channel's last sample: its square, below
    // 128; less 128 and squared, taken off, from 128. Each chunk starts
    // anew from the argument: mono, the whole of it; stereo, its high byte
    // as the left's high byte and its low byte as the right's.
    int last[2] = {int16_t(arg), 0};
    if (stereo) {
        last[0] = int16_t(arg & 0xff00);
        last[1] = int16_t((arg & 0xff) << 8);
    }
    for (size_t i = 0; i < size; ++i) {
        int &s = last[stereo ? i & 1 : 0];
        const int b = data[i];
        s += b < 128 ? b * b : -((b - 128) * (b - 128));
        s = std::clamp(s, -32768, 32767);
        out.push_back(int16_t(s));
    }
}

void Movie::toRgba(std::vector<uint8_t> &out) const {
    const size_t pixels = size_t(_width) * size_t(_height);
    out.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
        const float y = _planes[0][i];
        const float u = float(_planes[1][i]) - 128;
        const float v = float(_planes[2][i]) - 128;
        auto clamp = [](float c) { return uint8_t(std::clamp(int(c + 0.5f), 0, 255)); };
        out[i * 4] = clamp(y + 1.402f * v);
        out[i * 4 + 1] = clamp(y - 0.344136f * u - 0.714136f * v);
        out[i * 4 + 2] = clamp(y + 1.772f * u);
        out[i * 4 + 3] = 255;
    }
}

} // namespace reluva::roq
