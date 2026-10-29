// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
// Derived from FFmpeg's Indeo 4/5 decoder, Copyright (c) 2009-2011 Maxim
// Poliakovski, LGPL 2.1 or later; used under the GPL 3 as the LGPL's
// section 3 allows.

// Indeo 5's bitstream as described in docs/formats/video/indeo5.md: the
// picture and GOP headers, then each band of each plane, tile by tile, its
// macroblocks' types, coded-block patterns, quantiser deltas and motion
// vectors, then their blocks. Intel never published the format; it's
// known from FFmpeg's LGPL decoder (libavcodec/indeo5.c, ivi.c,
// ivi_dsp.c), which this follows, decoding the same pictures (checked
// frame by frame on LEGO Alpha Team's intro).

#include "common/video/indeo5.h"
#include "common/video/indeo5_tables.h"

#include <algorithm>
#include <cstring>

namespace reluva::video {

using namespace indeo5;

namespace {

enum FrameType { kIntra = 0, kInter = 1, kInterScalable = 2, kInterNoRef = 3, kNull = 4 };

// Scans: the order a block's coefficients come in.
const uint8_t kZigzag8x8[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};
const uint8_t kDirect4x4[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};

struct Scans {
    uint8_t vertical[64], horizontal[64]; // down each column in turn; along each row
    Scans() {
        for (int i = 0; i < 64; ++i) {
            vertical[i] = uint8_t((i % 8) * 8 + i / 8);
            horizontal[i] = uint8_t(i);
        }
    }
};
const Scans kScans;

// A value with its sign in the lowest bit: odd positive, even negative.
int toSigned(uint32_t v) { return (v & 1) ? int(v >> 1) + 1 : -int(v >> 1); }

// The inverse slant transforms, one row or column of 8 or 4 at a time.
// `round` halves the results (rounding), as the second pass does.
void slant8(const int (&s)[8], int (&d)[8], bool round) {
    // The inputs in the order the butterflies take them.
    const int s1 = s[0], s4 = s[1], s8 = s[2], s5 = s[3], s2 = s[4], s6 = s[5], s3 = s[6], s7 = s[7];
    int t1, t2, t3, t4, t5, t6, t7, t8, t;
    // Reflections a, b = 1/2, 7/8 and 1/2, 5/4; butterflies.
    t4 = s5 + ((s4 * 4 - s5 + 4) >> 3);
    t5 = s4 + ((-s4 - s5 * 4 + 4) >> 3);
    auto bfly = [&](int a, int b, int& o1, int& o2) {
        t = a - b;
        o1 = a + b;
        o2 = t;
    };
    auto reflect = [&](int a, int b, int& o1, int& o2) {
        t = ((a + b * 2 + 2) >> 2) + a;
        o2 = ((a * 2 - b + 2) >> 2) - b;
        o1 = t;
    };
    bfly(s1, t5, t1, t5);
    bfly(s2, s6, t2, t6);
    bfly(s7, s3, t7, t3);
    bfly(t4, s8, t4, t8);
    bfly(t1, t2, t1, t2);
    reflect(t4, t3, t4, t3);
    bfly(t5, t6, t5, t6);
    reflect(t8, t7, t8, t7);
    bfly(t1, t4, t1, t4);
    bfly(t2, t3, t2, t3);
    bfly(t5, t8, t5, t8);
    bfly(t6, t7, t6, t7);
    const int out[8] = {t1, t2, t3, t4, t5, t6, t7, t8};
    for (int i = 0; i < 8; ++i)
        d[i] = round ? (out[i] + 1) >> 1 : out[i];
}

void slant4(const int (&s)[4], int (&d)[4], bool round) {
    const int s1 = s[0], s4 = s[1], s2 = s[2], s3 = s[3];
    int t1 = s1 + s2, t2 = s1 - s2;
    int t4 = ((s4 + s3 * 2 + 2) >> 2) + s4;
    int t3 = ((s4 * 2 - s3 + 2) >> 2) - s3;
    int t = t1 - t4;
    t1 += t4;
    t4 = t;
    t = t2 - t3;
    t2 += t3;
    t3 = t;
    const int out[4] = {t1, t2, t3, t4};
    for (int i = 0; i < 4; ++i)
        d[i] = round ? (out[i] + 1) >> 1 : out[i];
}

// A block's coefficients (row by row) to pixels at `out`; `cols` marks
// the columns with any non-zero coefficient.
void inverse(int kind, const int32_t* in, int16_t* out, ptrdiff_t pitch, const uint8_t* cols) {
    switch (kind) {
    case 0: { // 2D slant 8x8: columns, then rows
        int tmp[64];
        for (int c = 0; c < 8; ++c) {
            int s[8], d[8] = {};
            if (cols[c]) {
                for (int r = 0; r < 8; ++r)
                    s[r] = in[r * 8 + c];
                slant8(s, d, false);
            }
            for (int r = 0; r < 8; ++r)
                tmp[r * 8 + c] = d[r];
        }
        for (int r = 0; r < 8; ++r, out += pitch) {
            int s[8], d[8] = {};
            bool any = false;
            for (int c = 0; c < 8; ++c)
                any |= (s[c] = tmp[r * 8 + c]) != 0;
            if (any)
                slant8(s, d, true);
            for (int c = 0; c < 8; ++c)
                out[c] = int16_t(d[c]);
        }
        break;
    }
    case 1: // slant along each row only
        for (int r = 0; r < 8; ++r, out += pitch) {
            int s[8], d[8] = {};
            bool any = false;
            for (int c = 0; c < 8; ++c)
                any |= (s[c] = in[r * 8 + c]) != 0;
            if (any)
                slant8(s, d, true);
            for (int c = 0; c < 8; ++c)
                out[c] = int16_t(d[c]);
        }
        break;
    case 2: // slant down each column only
        for (int c = 0; c < 8; ++c) {
            int s[8], d[8] = {};
            if (cols[c]) {
                for (int r = 0; r < 8; ++r)
                    s[r] = in[r * 8 + c];
                slant8(s, d, true);
            }
            for (int r = 0; r < 8; ++r)
                out[r * pitch + c] = int16_t(d[r]);
        }
        break;
    case 3: // no transform
        for (int r = 0; r < 8; ++r, out += pitch)
            for (int c = 0; c < 8; ++c)
                out[c] = int16_t(in[r * 8 + c]);
        break;
    case 4: { // 2D slant 4x4
        int tmp[16];
        for (int c = 0; c < 4; ++c) {
            int s[4], d[4] = {};
            if (cols[c]) {
                for (int r = 0; r < 4; ++r)
                    s[r] = in[r * 4 + c];
                slant4(s, d, false);
            }
            for (int r = 0; r < 4; ++r)
                tmp[r * 4 + c] = d[r];
        }
        for (int r = 0; r < 4; ++r, out += pitch) {
            int s[4], d[4] = {};
            bool any = false;
            for (int c = 0; c < 4; ++c)
                any |= (s[c] = tmp[r * 4 + c]) != 0;
            if (any)
                slant4(s, d, true);
            for (int c = 0; c < 4; ++c)
                out[c] = int16_t(d[c]);
        }
        break;
    }
    }
}

// A block with only its DC coefficient: what each transform makes of it.
void inverseDc(int kind, int dc, int16_t* out, ptrdiff_t pitch, int size) {
    const int16_t v = int16_t((dc + 1) >> 1);
    for (int r = 0; r < size; ++r, out += pitch)
        for (int c = 0; c < size; ++c) {
            switch (kind) {
            case 0:
            case 4: out[c] = v; break;                                   // the whole block
            case 1: out[c] = r == 0 ? v : 0; break;                      // the first row
            case 2: out[c] = c == 0 ? v : 0; break;                      // the first column
            default: out[c] = r == 0 && c == 0 ? int16_t(dc) : 0; break; // the first pixel, unhalved
            }
        }
}

// Motion compensation: the block at `ref` (half a pixel on across if
// type & 1, down if type & 2) put at `out`, or added to it.
void compensate(int16_t* out, const int16_t* ref, ptrdiff_t pitch, int size, int type, bool add) {
    for (int r = 0; r < size; ++r, out += pitch, ref += pitch)
        for (int c = 0; c < size; ++c) {
            int v;
            switch (type) {
            case 1: v = (ref[c] + ref[c + 1]) >> 1; break;
            case 2: v = (ref[c] + ref[c + pitch]) >> 1; break;
            case 3: v = (ref[c] + ref[c + 1] + ref[c + pitch] + ref[c + pitch + 1]) >> 2; break;
            default: v = ref[c]; break;
            }
            out[c] = int16_t(add ? out[c] + v : v);
        }
}

// A motion vector of the first luma band scaled down for a band of
// smaller macroblocks.
int scaleMv(int mv, int scale) { return (mv + (mv > 0) + (scale - 1)) >> scale; }

} // namespace

// Bits are taken from each byte's lowest first; a field's first bit is
// its lowest. Huffman codes are the other way: their first bit is their
// highest.
struct Indeo5Decoder::Bits {
    const uint8_t* data;
    size_t size; // bytes
    size_t pos = 0; // bits
    bool overrun = false;

    uint32_t bit() {
        if (pos >= size * 8) {
            overrun = true;
            return 0;
        }
        const uint32_t b = (data[pos >> 3] >> (pos & 7)) & 1;
        ++pos;
        return b;
    }
    uint32_t get(int n) {
        if (pos + size_t(n) + 64 <= size * 8) {
            uint64_t w;
            std::memcpy(&w, data + (pos >> 3), 8); // little-endian hosts, as elsewhere
            const uint32_t v = uint32_t((w >> (pos & 7)) & ((uint64_t(1) << n) - 1));
            pos += size_t(n);
            return v;
        }
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= bit() << i;
        return v;
    }
    void align() { pos = (pos + 7) & ~size_t(7); }
    // A symbol of the code whose rows are `xbits`; -1 if it's past 255.
    int symbol(const uint8_t* xbits, int rows) {
        if (rows == 1 && xbits[0] == 0) // a single symbol: coded as one bit
            return bit() ? -1 : 0;
        int row = 0, first = 0;
        while (row < rows - 1 && bit()) {
            first += 1 << xbits[row];
            ++row;
        }
        int index = 0;
        for (int i = 0; i < xbits[row]; ++i)
            index = index << 1 | int(bit());
        const int sym = first + index;
        return sym < 256 ? sym : -1;
    }
};

Indeo5Decoder::Indeo5Decoder() = default;

void Indeo5Decoder::layOut(int width, int height, int lumaBands, int chromaBands, int tileSize) {
    const bool same = planes_[0].width == width && planes_[0].height == height && lumaBands_ == lumaBands &&
                      chromaBands_ == chromaBands && int(planes_[0].bands.size()) == lumaBands;
    lumaBands_ = lumaBands;
    chromaBands_ = chromaBands;
    tileSize_ = tileSize;
    if (!same) {
        for (int p = 0; p < 3; ++p) {
            Plane& plane = planes_[size_t(p)];
            plane.width = p ? (width + 3) >> 2 : width;
            plane.height = p ? (height + 3) >> 2 : height;
            const int count = p ? chromaBands : lumaBands;
            plane.bands.assign(size_t(count), Band{});
            const int bw = count == 1 ? plane.width : (plane.width + 1) >> 1;
            const int bh = count == 1 ? plane.height : (plane.height + 1) >> 1;
            const int align = p ? 8 : 16;
            for (int b = 0; b < count; ++b) {
                Band& band = plane.bands[size_t(b)];
                band.plane = p;
                band.number = b;
                band.width = bw;
                band.height = bh;
                band.pitch = (bw + align - 1) / align * align;
                band.alignedHeight = (bh + align - 1) / align * align;
            }
        }
        decoded_ = false;
    }
}

Error Indeo5Decoder::decodeGop(Bits& b) {
    gopFlags_ = int(b.get(8));
    if (gopFlags_ & 1)
        b.get(16); // the header's size
    if (gopFlags_ & 0x20)
        return Error::fail("Indeo 5: a password-protected picture");
    const int tileSize = (gopFlags_ & 0x40) ? 64 << b.get(2) : 0;
    if (tileSize > 256)
        return Error::fail("Indeo 5: tiles of " + std::to_string(tileSize));
    const int lumaBands = int(b.get(2)) * 3 + 1, chromaBands = int(b.get(1)) * 3 + 1;
    const bool scalable = lumaBands != 1 || chromaBands != 1;
    if (scalable && (lumaBands != 4 || chromaBands != 1))
        return Error::fail("Indeo 5: unsupported bands");
    int width, height;
    const uint32_t sizeIndex = b.get(4);
    if (sizeIndex == 15) {
        height = int(b.get(13));
        width = int(b.get(13));
    } else {
        width = kPictureSizes[sizeIndex * 2] * 4;
        height = kPictureSizes[sizeIndex * 2 + 1] * 4;
    }
    if (gopFlags_ & 2)
        return Error::fail("Indeo 5: YV12 pictures aren't supported");
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Indeo 5: a picture of " + std::to_string(width) + "x" + std::to_string(height));
    layOut(width, height, lumaBands, chromaBands, tileSize);
    scalable_ = scalable;

    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < (p ? chromaBands : lumaBands); ++i) {
            Band& band = planes_[size_t(p)].bands[size_t(i)];
            band.halfPel = b.bit();
            const bool mbSame = b.bit();
            band.blkSize = 8 >> b.bit();
            band.mbSize = band.blkSize << (mbSame ? 0 : 1);
            if (p == 0 && band.blkSize == 4)
                return Error::fail("Indeo 5: 4x4 luma blocks aren't supported");
            if (b.bit())
                return Error::fail("Indeo 5: extended transforms aren't supported");
            static const Transform kTransforms[5] = {Transform::Slant8x8, Transform::RowSlant8, Transform::ColSlant8,
                                                     Transform::Put8x8, Transform::Slant4x4};
            const uint8_t* const kScanOf[5] = {kZigzag8x8, kScans.vertical, kScans.horizontal, kScans.horizontal,
                                               kDirect4x4};
            const int which = (p << 2) + i;
            if (which > 4)
                return Error::fail("Indeo 5: unsupported band");
            band.transform = kTransforms[which];
            band.scan = kScanOf[which];
            if ((which == 4 ? 4 : 8) != band.blkSize)
                return Error::fail("Indeo 5: transform and block sizes differ");
            const int matrix = p ? 5 : (lumaBands > 1 ? i + 1 : 0);
            if (band.blkSize == 8) {
                if (matrix >= 5)
                    return Error::fail("Indeo 5: no dequantisation matrix");
                band.intraBase = kBaseQuant8x8Intra[matrix];
                band.interBase = kBaseQuant8x8Inter[matrix];
                band.intraScale = kScaleQuant8x8Intra[matrix];
                band.interScale = kScaleQuant8x8Inter[matrix];
            } else {
                band.intraBase = kBaseQuant4x4Intra;
                band.interBase = kBaseQuant4x4Inter;
                band.intraScale = kScaleQuant4x4Intra;
                band.interScale = kScaleQuant4x4Inter;
            }
            if (b.get(2))
                return Error::fail("Indeo 5: no end marker in the GOP header");
        }
    // The second chroma plane is coded as the first.
    for (size_t i = 0; i < planes_[1].bands.size(); ++i) {
        const Band& from = planes_[1].bands[i];
        Band& to = planes_[2].bands[i];
        to.mbSize = from.mbSize;
        to.blkSize = from.blkSize;
        to.halfPel = from.halfPel;
        to.transform = from.transform;
        to.scan = from.scan;
        to.intraBase = from.intraBase;
        to.interBase = from.interBase;
        to.intraScale = from.intraScale;
        to.interScale = from.interScale;
    }

    // Tiles: a chroma tile is a quarter of a luma one each way; with four
    // luma bands, each band's tiles are half as big again.
    for (int p = 0; p < 3; ++p) {
        int tw = tileSize ? tileSize : width, th = tileSize ? tileSize : height;
        if (p) {
            tw = (tw + 3) >> 2;
            th = (th + 3) >> 2;
        } else if (lumaBands == 4) {
            tw >>= 1;
            th >>= 1;
        }
        if (tw <= 0 || th <= 0)
            return Error::fail("Indeo 5: empty tiles");
        for (Band& band : planes_[size_t(p)].bands) {
            band.tiles.clear();
            size_t t = 0;
            for (int y = 0; y < band.height; y += th)
                for (int x = 0; x < band.width; x += tw) {
                    Tile tile;
                    tile.x = x;
                    tile.y = y;
                    tile.width = std::min(band.width - x, tw);
                    tile.height = std::min(band.height - y, th);
                    tile.mbs.resize(size_t(((tile.width + band.mbSize - 1) / band.mbSize) *
                                           ((tile.height + band.mbSize - 1) / band.mbSize)));
                    band.tiles.push_back(std::move(tile));
                    ++t;
                }
        }
    }
    // Other bands inherit from the first luma band's macroblocks.
    const std::vector<Tile>& first = planes_[0].bands[0].tiles;
    for (int p = 0; p < 3; ++p)
        for (Band& band : planes_[size_t(p)].bands) {
            if (!p && !band.number)
                continue;
            if (band.tiles.size() != first.size())
                return Error::fail("Indeo 5: bands with different tiles");
            for (size_t t = 0; t < band.tiles.size(); ++t) {
                if (band.tiles[t].mbs.size() != first[t].mbs.size())
                    return Error::fail("Indeo 5: tiles with different macroblocks");
                band.tiles[t].ref = &first[t].mbs;
            }
        }

    if (gopFlags_ & 8) {
        if (b.get(3))
            return Error::fail("Indeo 5: alignment bits aren't zero");
        if (b.bit())
            b.get(24); // the transparent colour
    }
    b.align();
    b.get(23); // meaning unknown
    if (b.bit()) // an extension: 16-bit words while the top bit is set
        while (b.get(16) & 0x8000 && !b.overrun) {
        }
    b.align();
    return Error::success();
}

Error Indeo5Decoder::decodePictureHeader(Bits& b) {
    if (b.get(5) != 0x1f)
        return Error::fail("Indeo 5: no picture start code");
    prevFrameType_ = frameType_;
    frameType_ = int(b.get(3));
    if (frameType_ > kNull) {
        frameType_ = kIntra;
        return Error::fail("Indeo 5: frame type " + std::to_string(frameType_));
    }
    b.get(8); // the frame's number
    if (frameType_ == kIntra) {
        if (Error err = decodeGop(b)) {
            gopValid_ = false;
            return err;
        }
        gopValid_ = true;
    }
    if (frameType_ == kInterScalable && !scalable_)
        return Error::fail("Indeo 5: a scalable frame in a stream that isn't");
    if (frameType_ != kNull) {
        frameFlags_ = int(b.get(8));
        if (frameFlags_ & 1)
            b.get(24); // the header's size
        if (frameFlags_ & 0x10)
            b.get(16); // a checksum
        if (frameFlags_ & 0x20) // an extension: lengths, then that many bytes, to a 0
            for (uint32_t n; (n = b.get(8)) && !b.overrun;)
                b.pos += size_t(n) * 8;
        // The macroblock code: the default (7), another fixed one, or its own.
        mbCodebook_ = 7;
        if (frameFlags_ & 0x40) {
            mbCodebook_ = int(b.get(3));
            if (mbCodebook_ == 7) {
                mbOwnRows_ = int(b.get(4));
                if (!mbOwnRows_)
                    return Error::fail("Indeo 5: an empty Huffman code");
                for (int i = 0; i < mbOwnRows_; ++i)
                    mbOwnCodebook_[size_t(i)] = uint8_t(b.get(4));
                mbCodebook_ = 8;
            }
        }
        b.get(3); // meaning unknown
    }
    b.align();
    return Error::success();
}

void Indeo5Decoder::switchBuffers() {
    switch (prevFrameType_) {
    case kIntra:
    case kInter:
        bufSwitch_ ^= 1;
        dstBuf_ = bufSwitch_;
        refBuf_ = bufSwitch_ ^ 1;
        break;
    case kInterScalable:
        if (!interScalable_) {
            ref2Buf_ = 2;
            interScalable_ = true;
        }
        std::swap(dstBuf_, ref2Buf_);
        refBuf_ = ref2Buf_;
        break;
    default: break;
    }
    switch (frameType_) {
    case kIntra: bufSwitch_ = 0; [[fallthrough]];
    case kInter:
        interScalable_ = false;
        dstBuf_ = bufSwitch_;
        refBuf_ = bufSwitch_ ^ 1;
        break;
    default: break;
    }
}

Error Indeo5Decoder::decode(const uint8_t* data, size_t size) {
    if (!size)
        return Error::success();
    Bits b{data, size};
    if (Error err = decodePictureHeader(b))
        return err;
    if (!gopValid_)
        return Error::fail("Indeo 5: no key frame yet");
    switchBuffers();
    if (frameType_ == kNull)
        return Error::success();
    for (Plane& plane : planes_)
        for (Band& band : plane.bands) {
            auto buffer = [&](int i) -> int16_t* {
                if (lumaBands_ <= 1 && i == 2)
                    return nullptr;
                std::vector<int16_t>& buf = band.bufs[size_t(i)];
                if (buf.empty())
                    buf.assign(size_t(band.pitch) * size_t(band.alignedHeight), 0);
                return buf.data();
            };
            band.buf = buffer(dstBuf_);
            band.ref = buffer(refBuf_);
            if (!band.buf || !band.ref)
                return Error::fail("Indeo 5: no buffer for a band");
            if (Error err = decodeBand(b, band)) {
                decoded_ = false;
                return err;
            }
        }
    if (b.overrun)
        return Error::fail("Indeo 5: a frame cut short");
    decoded_ = true;
    return Error::success();
}

Error Indeo5Decoder::decodeBand(Bits& b, Band& band) {
    const int flags = int(b.get(8));
    if (flags & 1)
        return Error::fail("Indeo 5: an empty band");
    if (frameFlags_ & 0x80)
        b.get(24); // the band's size
    band.inheritMv = flags & 2;
    band.inheritQDelta = flags & 8;
    band.qDeltaPresent = flags & 4;
    if (!band.qDeltaPresent)
        band.inheritQDelta = true;
    band.corrections.clear();
    if (flags & 0x10) {
        const int n = int(b.get(8));
        if (n > 61)
            return Error::fail("Indeo 5: too many run-value corrections");
        for (int i = 0; i < n; ++i) {
            const uint8_t a = uint8_t(b.get(8));
            band.corrections.push_back({a, uint8_t(b.get(8))});
        }
    }
    band.runValueMap = (flags & 0x40) ? int(b.get(3)) : 8;
    band.codebook = 7;
    if (flags & 0x80) {
        band.codebook = int(b.get(3));
        if (band.codebook == 7) {
            band.ownRows = int(b.get(4));
            if (!band.ownRows)
                return Error::fail("Indeo 5: an empty Huffman code");
            for (int i = 0; i < band.ownRows; ++i)
                band.ownCodebook[size_t(i)] = uint8_t(b.get(4));
            band.codebook = 8;
        }
    }
    if (b.bit())
        b.get(16); // a checksum
    band.globalQuant = int(b.get(5));
    if (flags & 0x20) {
        b.align();
        for (uint32_t n; (n = b.get(8)) && !b.overrun;)
            b.pos += size_t(n) * 8;
    }
    b.align();

    size_t pos = b.pos;
    for (Tile& tile : band.tiles) {
        if (b.bit()) {
            if (Error err = emptyTile(band, tile))
                return err;
            continue;
        }
        // The tile's size in bytes, from where the tile began.
        size_t bytes = 0;
        if (b.bit()) {
            bytes = b.get(8);
            if (bytes == 255)
                bytes = b.get(24);
        }
        b.align();
        if (!bytes)
            return Error::fail("Indeo 5: a tile of no size");
        if (Error err = decodeMacroblocks(b, band, tile))
            return err;
        if (Error err = decodeBlocks(b, band, tile))
            return err;
        pos += bytes * 8;
        b.pos = pos;
    }
    b.align();
    return Error::success();
}

Error Indeo5Decoder::decodeMacroblocks(Bits& b, Band& band, Tile& tile) {
    const std::vector<Macroblock>* ref = tile.ref;
    if (!ref && ((band.qDeltaPresent && band.inheritQDelta) || band.inheritMv))
        return Error::fail("Indeo 5: nothing to inherit from");
    const uint8_t* xbits = mbCodebook_ == 8 ? mbOwnCodebook_.data() : kMacroblockCodebooks[mbCodebook_].xbits;
    const int rows = mbCodebook_ == 8 ? mbOwnRows_ : kMacroblockCodebooks[mbCodebook_].rows;
    auto value = [&]() { return toSigned(uint32_t(std::max(0, b.symbol(xbits, rows)))); };
    const int mvScale = (planes_[0].bands[0].mbSize >> 3) - (band.mbSize >> 3);
    const bool firstBand = !band.plane && !band.number;
    int mvX = 0, mvY = 0;
    size_t i = 0;
    for (int y = tile.y; y < tile.y + tile.height; y += band.mbSize)
        for (int x = tile.x; x < tile.x + tile.width; x += band.mbSize, ++i) {
            Macroblock& mb = tile.mbs[i];
            const Macroblock* from = ref ? &(*ref)[i] : nullptr;
            mb.x = x;
            mb.y = y;
            mb.offset = y * band.pitch + x;
            auto inherit = [&] {
                mb.mvX = int8_t(mvScale ? scaleMv(from->mvX, mvScale) : from->mvX);
                mb.mvY = int8_t(mvScale ? scaleMv(from->mvY, mvScale) : from->mvY);
            };
            if (b.bit()) { // empty: inter, no blocks coded
                if (frameType_ == kIntra)
                    return Error::fail("Indeo 5: an empty macroblock in a key frame");
                mb.type = 1;
                mb.cbp = 0;
                mb.qDelta = 0;
                if (firstBand && (frameFlags_ & 8))
                    mb.qDelta = int8_t(value());
                mb.mvX = mb.mvY = 0;
                if (band.inheritMv && from)
                    inherit();
            } else {
                if (band.inheritMv && from)
                    mb.type = from->type;
                else if (frameType_ == kIntra)
                    mb.type = 0;
                else
                    mb.type = uint8_t(b.bit());
                mb.cbp = uint8_t(b.get(band.mbSize != band.blkSize ? 4 : 1));
                mb.qDelta = 0;
                if (band.qDeltaPresent) {
                    if (band.inheritQDelta) {
                        if (from)
                            mb.qDelta = from->qDelta;
                    } else if (mb.cbp || (firstBand && (frameFlags_ & 8))) {
                        mb.qDelta = int8_t(value());
                    }
                }
                if (!mb.type) {
                    mb.mvX = mb.mvY = 0;
                } else if (band.inheritMv && from) {
                    inherit();
                } else {
                    mvY += value();
                    mvX += value();
                    mb.mvX = int8_t(mvX);
                    mb.mvY = int8_t(mvY);
                }
            }
            if (mb.type) {
                // The block it copies must be within the reference.
                const int s = band.halfPel;
                const int dx = mb.mvX >> s, dy = mb.mvY >> s;
                if (x + dx < 0 || y + dy < 0 || x + dx + band.mbSize + (mb.mvX & s) > band.pitch ||
                    y + dy + band.mbSize + (mb.mvY & s) > band.alignedHeight)
                    return Error::fail("Indeo 5: a motion vector outside the picture");
            }
        }
    b.align();
    return Error::success();
}

Error Indeo5Decoder::decodeBlocks(Bits& b, Band& band, Tile& tile) {
    // The run-value map, with this band's corrections swapped in.
    RunValueMap map = kRunValueMaps[band.runValueMap];
    for (auto [a, c] : band.corrections) {
        std::swap(map.run[a], map.run[c]);
        std::swap(map.value[a], map.value[c]);
        if (a == map.endOfBlock || c == map.endOfBlock)
            map.endOfBlock ^= a ^ c;
        if (a == map.escape || c == map.escape)
            map.escape ^= a ^ c;
    }
    const uint8_t* xbits = band.codebook == 8 ? band.ownCodebook.data() : kBlockCodebooks[band.codebook].xbits;
    const int rows = band.codebook == 8 ? band.ownRows : kBlockCodebooks[band.codebook].rows;
    const int kind = int(band.transform);
    const bool twoD = band.transform == Transform::Slant8x8 || band.transform == Transform::Slant4x4;
    const int size = band.blkSize, coeffs = size * size;
    const int blocks = band.mbSize != size ? 4 : 1;
    const ptrdiff_t pitch = band.pitch;
    const int bufSize = band.pitch * band.alignedHeight;
    int prevDc = 0;

    for (Macroblock& mb : tile.mbs) {
        const bool intra = !mb.type;
        int quant = std::clamp(band.globalQuant + mb.qDelta, 0, 23);
        quant = (intra ? band.intraScale : band.interScale)[quant];
        const uint16_t* base = intra ? band.intraBase : band.interBase;
        int mvX = mb.mvX, mvY = mb.mvY, mcType = 0;
        if (!intra && band.halfPel) {
            mcType = ((mvY & 1) << 1) | (mvX & 1);
            mvX >>= 1;
            mvY >>= 1;
        }
        int offset = mb.offset;
        uint32_t cbp = mb.cbp;
        for (int blk = 0; blk < blocks; ++blk, cbp >>= 1) {
            if (blk & 1)
                offset += size;
            else if (blk == 2)
                offset += size * band.pitch - size;
            if (offset < 0 || offset + (size - 1) * band.pitch + size > bufSize)
                return Error::fail("Indeo 5: a block outside the picture");
            const int refOffset = offset + mvY * band.pitch + mvX;
            if (!intra && (refOffset < 0 || refOffset + (size - 1) * band.pitch + size + (mcType > 1) * band.pitch +
                                                    (mcType & 1) > bufSize))
                return Error::fail("Indeo 5: a motion vector outside the picture");
            if (!(cbp & 1)) {
                // Not coded: an intra block is the DC carried on; an inter
                // one is the reference copied.
                if (intra)
                    inverseDc(kind, prevDc, band.buf + offset, pitch, size);
                else
                    compensate(band.buf + offset, band.ref + refOffset, pitch, size, mcType, false);
                continue;
            }
            int32_t coef[64] = {};
            uint8_t cols[8] = {};
            int scanPos = -1, sym = 0;
            while (scanPos <= coeffs) {
                sym = b.symbol(xbits, rows);
                if (sym < 0 || b.overrun)
                    return Error::fail("Indeo 5: a bad block symbol");
                if (sym == map.endOfBlock)
                    break;
                int run, val;
                if (sym == map.escape) {
                    // Run and value given in full: the run, then the
                    // value's low six bits and its high bits.
                    run = b.symbol(xbits, rows) + 1;
                    const int lo = b.symbol(xbits, rows), hi = b.symbol(xbits, rows);
                    if (run < 1 || lo < 0 || hi < 0)
                        return Error::fail("Indeo 5: a bad escape");
                    val = toSigned(uint32_t(hi << 6 | lo));
                } else {
                    run = map.run[sym];
                    val = map.value[sym];
                }
                scanPos += run;
                if (scanPos >= coeffs || scanPos < 0)
                    break;
                const int at = band.scan[scanPos];
                const int q = (base[at] * quant) >> 9;
                if (q > 1)
                    val = val * q + (val > 0 ? 1 : -1) * (((q ^ 1) - 1) >> 1);
                coef[at] = val;
                cols[at & (size - 1)] |= val != 0;
            }
            if (scanPos < 0 || (scanPos >= coeffs && sym != map.endOfBlock))
                return Error::fail("Indeo 5: a corrupt block");
            // Intra blocks' DC is coded as the difference from the last.
            if (intra && twoD) {
                prevDc += coef[0];
                coef[0] = prevDc;
                cols[0] |= prevDc != 0;
            }
            inverse(kind, coef, band.buf + offset, pitch, cols);
            if (!intra)
                compensate(band.buf + offset, band.ref + refOffset, pitch, size, mcType, true);
        }
    }
    b.align();
    return Error::success();
}

Error Indeo5Decoder::emptyTile(Band& band, Tile& tile) {
    const std::vector<Macroblock>* ref = tile.ref;
    const bool clearFirst = !band.qDeltaPresent && !band.plane && !band.number;
    const int mvScale = (planes_[0].bands[0].mbSize >> 3) - (band.mbSize >> 3);
    bool moved = false;
    size_t i = 0;
    for (int y = tile.y; y < tile.y + tile.height; y += band.mbSize)
        for (int x = tile.x; x < tile.x + tile.width; x += band.mbSize, ++i) {
            Macroblock& mb = tile.mbs[i];
            mb.x = x;
            mb.y = y;
            mb.offset = y * band.pitch + x;
            mb.type = 1;
            mb.cbp = 0;
            if (clearFirst) {
                mb.qDelta = int8_t(band.globalQuant);
                mb.mvX = mb.mvY = 0;
            }
            if (ref) {
                const Macroblock& from = (*ref)[i];
                if (band.inheritQDelta)
                    mb.qDelta = from.qDelta;
                if (band.inheritMv) {
                    mb.mvX = int8_t(mvScale ? scaleMv(from.mvX, mvScale) : from.mvX);
                    mb.mvY = int8_t(mvScale ? scaleMv(from.mvY, mvScale) : from.mvY);
                    moved |= mb.mvX || mb.mvY;
                    const int s = band.halfPel;
                    const int dx = mb.mvX >> s, dy = mb.mvY >> s;
                    if (x + dx < 0 || y + dy < 0 || x + dx + band.mbSize + (mb.mvX & s) > band.pitch ||
                        y + dy + band.mbSize + (mb.mvY & s) > band.alignedHeight)
                        return Error::fail("Indeo 5: a motion vector outside the picture");
                }
            }
        }
    const ptrdiff_t pitch = band.pitch;
    if (band.inheritMv && moved) {
        const int size = band.blkSize, blocks = band.mbSize != size ? 4 : 1;
        for (const Macroblock& mb : tile.mbs) {
            int mvX = mb.mvX, mvY = mb.mvY, mcType = 0;
            if (band.halfPel) {
                mcType = ((mvY & 1) << 1) | (mvX & 1);
                mvX >>= 1;
                mvY >>= 1;
            }
            for (int blk = 0; blk < blocks; ++blk) {
                const int offset = mb.offset + size * ((blk & 1) + ((blk & 2) ? band.pitch : 0));
                compensate(band.buf + offset, band.ref + offset + mvY * band.pitch + mvX, pitch, size, mcType, false);
            }
        }
    } else {
        // Copied from the reference as it is.
        for (int y = 0; y < tile.height; ++y) {
            const ptrdiff_t at = (tile.y + y) * pitch + tile.x;
            std::memcpy(band.buf + at, band.ref + at, size_t(tile.width) * sizeof(int16_t));
        }
    }
    return Error::success();
}

void Indeo5Decoder::output(int p, std::vector<uint8_t>& out) const {
    const Plane& plane = planes_[size_t(p)];
    out.assign(size_t(plane.width) * size_t(plane.height), 128);
    if (plane.bands.empty() || !plane.bands[0].buf || !decoded_)
        return;
    if (p == 0 && plane.bands.size() == 4) {
        recompose53(out);
        return;
    }
    const Band& band = plane.bands[0];
    for (int y = 0; y < plane.height; ++y)
        for (int x = 0; x < plane.width; ++x)
            out[size_t(y) * size_t(plane.width) + size_t(x)] =
                uint8_t(std::clamp(band.buf[y * band.pitch + x] + 128, 0, 255));
}

// The four luma wavelet bands put back together (LL, HL, LH, HH) with the
// 5/3 filters: each band's samples spread over a 2x2 block, the edges
// repeated.
void Indeo5Decoder::recompose53(std::vector<uint8_t>& out) const {
    const Plane& plane = planes_[0];
    ptrdiff_t pitch = plane.bands[0].pitch, backPitch = 0;
    const int16_t* b0 = plane.bands[0].buf;
    const int16_t* b1 = plane.bands[1].buf;
    const int16_t* b2 = plane.bands[2].buf;
    const int16_t* b3 = plane.bands[3].buf;
    const int w = plane.width;
    uint8_t* dst = out.data();
    auto clip = [](int v) { return uint8_t(std::clamp((v >> 6) + 128, 0, 255)); };
    for (int y = 0; y < plane.height; y += 2) {
        if (y + 2 >= plane.height)
            pitch = 0;
        int b0_1 = b0[0], b0_2 = b0[pitch];
        int b1_1 = b1[backPitch], b1_2 = b1[0];
        int b1_3 = b1_1 - b1_2 * 6 + b1[pitch];
        int b2_2 = b2[0], b2_3 = b2_2, b2_5 = b2[pitch], b2_6 = b2_5;
        int b3_2 = b3[backPitch], b3_3 = b3_2, b3_5 = b3[0], b3_6 = b3_5;
        int b3_8 = b3_2 - b3_5 * 6 + b3[pitch], b3_9 = b3_8;
        for (int x = 0, i = 0; x < w; x += 2, ++i) {
            if (x + 2 >= w) {
                --b0;
                --b1;
                --b2;
                --b3;
            }
            const int b2_1 = b2_2;
            b2_2 = b2_3;
            const int b2_4 = b2_5;
            b2_5 = b2_6;
            const int b3_1 = b3_2;
            b3_2 = b3_3;
            const int b3_4 = b3_5;
            b3_5 = b3_6;
            const int b3_7 = b3_8;
            b3_8 = b3_9;

            // LL: low-pass both ways.
            int tmp0 = b0_1, tmp2 = b0_2;
            b0_1 = b0[i + 1];
            b0_2 = b0[pitch + i + 1];
            int tmp1 = tmp0 + b0_1;
            int p0 = tmp0 * 16, p1 = tmp1 * 8, p2 = (tmp0 + tmp2) * 8, p3 = (tmp1 + tmp2 + b0_2) * 4;
            // HL: high-pass down, low-pass across.
            tmp0 = b1_2;
            tmp1 = b1_1;
            b1_2 = b1[i + 1];
            b1_1 = b1[backPitch + i + 1];
            tmp2 = tmp1 - tmp0 * 6 + b1_3;
            b1_3 = b1_1 - b1_2 * 6 + b1[pitch + i + 1];
            p0 += (tmp0 + tmp1) * 8;
            p1 += (tmp0 + tmp1 + b1_1 + b1_2) * 4;
            p2 += tmp2 * 4;
            p3 += (tmp2 + b1_3) * 2;
            // LH: low-pass down, high-pass across.
            b2_3 = b2[i + 1];
            b2_6 = b2[pitch + i + 1];
            tmp0 = b2_1 + b2_2;
            tmp1 = b2_1 - b2_2 * 6 + b2_3;
            p0 += tmp0 * 8;
            p1 += tmp1 * 4;
            p2 += (tmp0 + b2_4 + b2_5) * 4;
            p3 += (tmp1 + b2_4 - b2_5 * 6 + b2_6) * 2;
            // HH: high-pass both ways.
            b3_6 = b3[i + 1];
            b3_3 = b3[backPitch + i + 1];
            tmp0 = b3_1 + b3_4;
            tmp1 = b3_2 + b3_5;
            tmp2 = b3_3 + b3_6;
            b3_9 = b3_3 - b3_6 * 6 + b3[pitch + i + 1];
            p0 += (tmp0 + tmp1) * 4;
            p1 += (tmp0 - tmp1 * 6 + tmp2) * 2;
            p2 += (b3_7 + b3_8) * 2;
            p3 += b3_7 - b3_8 * 6 + b3_9;

            dst[x] = clip(p0);
            if (x + 1 < w)
                dst[x + 1] = clip(p1);
            if (y + 1 < plane.height) {
                dst[w + x] = clip(p2);
                if (x + 1 < w)
                    dst[w + x + 1] = clip(p3);
            }
        }
        dst += size_t(w) * 2;
        backPitch = -pitch;
        b0 += pitch + 1;
        b1 += pitch + 1;
        b2 += pitch + 1;
        b3 += pitch + 1;
    }
}

void Indeo5Decoder::yuv(std::vector<uint8_t>& y, std::vector<uint8_t>& cb, std::vector<uint8_t>& cr) const {
    output(0, y);
    output(2, cb);
    output(1, cr);
}

void Indeo5Decoder::rgba(uint8_t* out, int pitch) const {
    std::vector<uint8_t> y, cb, cr;
    yuv(y, cb, cr);
    const int w = width(), h = height(), cw = planes_[1].width;
    for (int row = 0; row < h; ++row) {
        uint8_t* d = out + ptrdiff_t(row) * pitch;
        for (int x = 0; x < w; ++x, d += 4) {
            const size_t c = size_t(row >> 2) * size_t(cw) + size_t(x >> 2);
            const int l = (int(y[size_t(row) * size_t(w) + size_t(x)]) - 16) * 298;
            const int u = int(cb[c]) - 128, v = int(cr[c]) - 128;
            d[0] = uint8_t(std::clamp((l + 409 * v + 128) >> 8, 0, 255));
            d[1] = uint8_t(std::clamp((l - 100 * u - 208 * v + 128) >> 8, 0, 255));
            d[2] = uint8_t(std::clamp((l + 516 * u + 128) >> 8, 0, 255));
            d[3] = 255;
        }
    }
}

} // namespace reluva::video
