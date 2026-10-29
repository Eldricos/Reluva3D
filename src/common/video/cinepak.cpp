// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Written from the format's public description (see docs/formats/video.md).

#include "common/video/cinepak.h"

#include <algorithm>

namespace reluva::video {

namespace {

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return uint32_t(be16(p)) << 16 | be16(p + 2); }
uint8_t clampByte(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// The chunks of a strip.
constexpr int kCodebook = 0x20;     // 0x20-0x27: bits 0x01 partial, 0x02 V1, 0x04 grey
constexpr int kIntraVectors = 0x30; // a V1-or-V4 bit per block
constexpr int kInterVectors = 0x31; // a changed bit per block, then V1-or-V4 for those that did
constexpr int kV1Vectors = 0x32;    // every block V1

// Reads the per-block bits, 32 at a time (most significant first),
// interleaved with the codebook indices in the same data.
class Bits {
public:
    Bits(const uint8_t*& p, const uint8_t* end) : p_(p), end_(end) {}
    bool next(bool& bit) {
        if (left_ == 0) {
            if (end_ - p_ < 4)
                return false;
            bits_ = be32(p_);
            p_ += 4;
            left_ = 32;
        }
        bit = bits_ & 0x80000000u;
        bits_ <<= 1;
        --left_;
        return true;
    }

private:
    const uint8_t*& p_;
    const uint8_t* end_;
    uint32_t bits_ = 0;
    int left_ = 0;
};

} // namespace

void CinepakDecoder::reset() {
    picture_ = {};
    strips_.clear();
}

void CinepakDecoder::drawV1(const Entry& e, int x, int y) {
    // Each of the entry's four colours fills a 2x2 quarter of the block.
    for (int dy = 0; dy < 4; ++dy)
        for (int dx = 0; dx < 4; ++dx) {
            const int px = x + dx, py = y + dy;
            if (px >= picture_.w || py >= picture_.h)
                continue;
            const uint8_t* c = e.rgb[(dy / 2) * 2 + dx / 2];
            uint8_t* d = picture_.pixel(px, py);
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
        }
}

void CinepakDecoder::drawV4(const Entry& a, const Entry& b, const Entry& c, const Entry& d, int x, int y) {
    // Each entry fills a 2x2 quarter of the block, a pixel per colour.
    const Entry* quarters[4] = {&a, &b, &c, &d};
    for (int dy = 0; dy < 4; ++dy)
        for (int dx = 0; dx < 4; ++dx) {
            const int px = x + dx, py = y + dy;
            if (px >= picture_.w || py >= picture_.h)
                continue;
            const uint8_t* col = quarters[(dy / 2) * 2 + dx / 2]->rgb[(dy % 2) * 2 + dx % 2];
            uint8_t* o = picture_.pixel(px, py);
            o[0] = col[0];
            o[1] = col[1];
            o[2] = col[2];
        }
}

void CinepakDecoder::decodeVectors(int kind, const uint8_t* q, const uint8_t* end, int top, int bottom, int width,
                                   const Strip& strip) {
    Bits bits(q, end);
    for (int y = top; y < bottom; y += 4)
        for (int x = 0; x < width; x += 4) {
            bool bit = false;
            if (kind == kInterVectors) {
                if (!bits.next(bit))
                    return;
                if (!bit)
                    continue; // unchanged
            }
            bool v4 = false;
            if (kind != kV1Vectors && !bits.next(v4))
                return;
            if (v4) {
                if (end - q < 4)
                    return;
                drawV4(strip.v4[q[0]], strip.v4[q[1]], strip.v4[q[2]], strip.v4[q[3]], x, y);
                q += 4;
            } else {
                if (end - q < 1)
                    return;
                drawV1(strip.v1[q[0]], x, y);
                q += 1;
            }
        }
}

Error CinepakDecoder::decodeStrip(const uint8_t* p, size_t size, int top, int bottom, int width, Strip& strip) {
    const uint8_t* const end = p + size;
    while (end - p >= 4) {
        const int id = be16(p);
        const size_t chunkSize = be16(p + 2);
        if (chunkSize < 4 || chunkSize > size_t(end - p))
            return Error::fail("Cinepak chunk size out of range");
        const uint8_t* q = p + 4;
        const uint8_t* const chunkEnd = p + chunkSize;
        p = chunkEnd;
        const int kind = id >> 8;
        if ((kind & ~0x07) == kCodebook) {
            const bool partial = kind & 0x01, v1 = kind & 0x02, grey = kind & 0x04;
            const size_t entrySize = grey ? 4 : 6;
            auto& book = v1 ? strip.v1 : strip.v4;
            const auto read = [&](Entry& e) {
                const int u = grey ? 0 : int8_t(q[4]), v = grey ? 0 : int8_t(q[5]);
                for (int i = 0; i < 4; ++i) {
                    const int y = q[i];
                    e.rgb[i][0] = clampByte(y + 2 * v);
                    e.rgb[i][1] = clampByte(y - (u >> 1) - v);
                    e.rgb[i][2] = clampByte(y + 2 * u);
                }
                q += entrySize;
            };
            if (!partial) {
                for (size_t i = 0; i < book.size() && size_t(chunkEnd - q) >= entrySize; ++i)
                    read(book[i]);
                continue;
            }
            // Partial: a 32-bit mask before each 32 entries says which are given.
            for (size_t i = 0; i < book.size() && chunkEnd - q >= 4;) {
                const uint32_t mask = be32(q);
                q += 4;
                for (int bit = 31; bit >= 0 && i < book.size(); --bit, ++i)
                    if (mask >> bit & 1) {
                        if (size_t(chunkEnd - q) < entrySize)
                            break;
                        read(book[i]);
                    }
            }
            continue;
        }
        if (kind != kIntraVectors && kind != kInterVectors && kind != kV1Vectors)
            continue; // not one we know: skipped
        decodeVectors(kind, q, chunkEnd, top, bottom, width, strip);
    }
    return Error::success();
}

Error CinepakDecoder::decode(const uint8_t* data, size_t size) {
    if (size == 0)
        return Error::success(); // the last frame again
    if (size < 10)
        return Error::fail("Cinepak frame cut short");
    const int flags = data[0];
    const size_t length = std::min<size_t>(size, size_t(data[1]) << 16 | size_t(data[2]) << 8 | data[3]);
    const int width = be16(data + 4), height = be16(data + 6), stripCount = be16(data + 8);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Cinepak frame size out of range");
    if (picture_.w != width || picture_.h != height) {
        picture_.w = width;
        picture_.h = height;
        picture_.rgba.assign(size_t(width) * height * 4, 0);
        for (size_t i = 3; i < picture_.rgba.size(); i += 4)
            picture_.rgba[i] = 255;
    }
    if (strips_.size() < size_t(stripCount))
        strips_.resize(size_t(stripCount));
    size_t p = 10;
    int y = 0;
    for (int i = 0; i < stripCount; ++i) {
        if (length - p < 12)
            return Error::fail("Cinepak strip cut short");
        const uint8_t* s = data + p;
        const size_t stripSize = be16(s + 2);
        const int y1 = be16(s + 4), y2 = be16(s + 8);
        if (stripSize < 12 || stripSize > length - p)
            return Error::fail("Cinepak strip size out of range");
        // A strip starts with the codebooks of the one before, unless the
        // frame says each keeps its own from the last frame.
        if (i > 0 && !(flags & 0x01))
            strips_[size_t(i)] = strips_[size_t(i) - 1];
        // Strips are placed by their own coordinates when given, or else
        // one under the other.
        const int top = y1 != 0 ? y1 : y;
        const int bottom = std::min(height, top + (y2 - y1));
        if (bottom > top)
            if (Error err = decodeStrip(s + 12, stripSize - 12, top, bottom, width, strips_[size_t(i)]))
                return err;
        y = std::max(y, bottom);
        p += stripSize;
    }
    return Error::success();
}

} // namespace reluva::video
