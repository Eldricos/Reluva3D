// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Bink video. The bundles, block types and Huffman trees' coding follow
// the MultimediaWiki's "Bink Video"; the DCT and residue coefficients'
// coding, the IDCT, the predefined trees and tables, and the details the
// description leaves open follow FFmpeg's decoder (libavcodec/bink.c and
// binkdsp.c, LGPL 2.1+), whose tables are third_party/ffmpeg_bink.

#include "common/video/bink_video.h"

#include "bink_tables.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::video {

namespace bt = bink_tables;

namespace {

enum Bundles { BlockTypes, SubBlockTypes, Colours, Pattern, XOff, YOff, IntraDc, InterDc, Run, BundleCount };
enum BlockType { Skip, Scaled, Motion, RunFill, Residue, Intra, Fill, Inter, PatternFill, Raw };

int log2Floor(unsigned v) {
    int n = -1;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

// The 16 predefined Huffman trees as lookup tables: the next maxLen bits
// (first bit lowest) give the leaf and its code's length.
struct Vlc {
    int maxLen = 0;
    std::vector<uint8_t> leaf, length;
};
const std::array<Vlc, 16>& trees() {
    static const std::array<Vlc, 16> vlcs = [] {
        std::array<Vlc, 16> v;
        for (size_t t = 0; t < 16; t++) {
            Vlc& vlc = v[t];
            for (size_t s = 0; s < 16; s++)
                vlc.maxLen = std::max<int>(vlc.maxLen, bt::bink_tree_lens[t][s]);
            vlc.leaf.assign(size_t(1) << vlc.maxLen, 0);
            vlc.length.assign(size_t(1) << vlc.maxLen, 0);
            for (size_t s = 0; s < 16; s++) {
                const int len = bt::bink_tree_lens[t][s];
                const unsigned code = bt::bink_tree_bits[t][s];
                // Codes are read first bit lowest: every index whose low
                // `len` bits are the code.
                for (unsigned rest = 0; rest < (1u << (vlc.maxLen - len)); rest++) {
                    const unsigned index = code | rest << len;
                    vlc.leaf[index] = uint8_t(s);
                    vlc.length[index] = uint8_t(len);
                }
            }
        }
        return v;
    }();
    return vlcs;
}

constexpr uint8_t kRunLengths[4] = {4, 8, 12, 32};

// The 8x8 IDCT with Bink's integer constants.
constexpr int kA1 = 2896, kA2 = 2217, kA3 = 3784, kA4 = -5352;
inline int mul(int x, int y) { return int(unsigned(x) * unsigned(y)) >> 11; }

template <typename Out, typename Munge>
void idct8(const int32_t* in, int inStep, Out* out, int outStep, Munge munge) {
    auto s = [&](int i) { return in[i * inStep]; };
    const int a0 = s(0) + s(4), a1 = s(0) - s(4);
    const int a2 = s(2) + s(6), a3 = mul(kA1, s(2) - s(6));
    const int a4 = s(5) + s(3), a5 = s(5) - s(3);
    const int a6 = s(1) + s(7), a7 = s(1) - s(7);
    const int b0 = a4 + a6, b1 = mul(kA3, a5 + a7);
    const int b2 = mul(kA4, a5) - b0 + b1;
    const int b3 = mul(kA1, a6 - a4) - b2;
    const int b4 = mul(kA2, a7) + b3 - b1;
    out[0 * outStep] = Out(munge(a0 + a2 + b0));
    out[1 * outStep] = Out(munge(a1 + a3 - a2 + b2));
    out[2 * outStep] = Out(munge(a1 - a3 + a2 + b3));
    out[3 * outStep] = Out(munge(a0 - a2 - b4));
    out[4 * outStep] = Out(munge(a0 - a2 + b4));
    out[5 * outStep] = Out(munge(a1 - a3 + a2 - b3));
    out[6 * outStep] = Out(munge(a1 + a3 - a2 - b2));
    out[7 * outStep] = Out(munge(a0 + a2 - b0));
}

// Columns, then rows (rounded and scaled down by 256) into `out`.
template <typename Out> void idct(const int32_t block[64], Out* out, int stride) {
    int32_t temp[64];
    for (int c = 0; c < 8; c++) {
        bool dcOnly = true;
        for (int r = 1; r < 8; r++)
            dcOnly &= block[c + 8 * r] == 0;
        if (dcOnly)
            for (int r = 0; r < 8; r++)
                temp[c + 8 * r] = block[c];
        else
            idct8(block + c, 8, temp + c, 8, [](int x) { return x; });
    }
    for (int r = 0; r < 8; r++)
        idct8(temp + 8 * r, 1, out + r * stride, 1, [](int x) { return (x + 0x7f) >> 8; });
}

} // namespace

// Bits read first bit lowest, from 32-bit little-endian words (the same
// as byte by byte, lowest bit first).
class BinkVideoDecoder::Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size), bits_(size * 8) {}

    uint32_t get(int n) {
        const uint32_t v = peek(n);
        pos_ += size_t(n);
        return v;
    }
    bool bit() { return get(1) != 0; }
    uint32_t peek(int n) const {
        if (n == 0)
            return 0;
        uint64_t v = 0;
        const size_t byte = pos_ >> 3;
        for (size_t i = 0; i < 8 && byte + i < size_; i++)
            v |= uint64_t(data_[byte + i]) << (8 * i);
        return uint32_t((v >> (pos_ & 7)) & ((uint64_t(1) << n) - 1));
    }
    void skip(size_t n) { pos_ += n; }
    void align32() { pos_ = (pos_ + 31) & ~size_t(31); }
    size_t pos() const { return pos_; }
    ptrdiff_t left() const { return ptrdiff_t(bits_) - ptrdiff_t(pos_); }

    int huff(const Tree& t) {
        const Vlc& v = trees()[size_t(t.table)];
        const uint32_t index = peek(v.maxLen);
        pos_ += v.length[index];
        return t.syms[v.leaf[index]];
    }

    // A tree's leaves' symbols: the identity, a list of symbols put
    // first, or the identity shuffled by merging.
    Error tree(Tree& t) {
        if (left() < 4)
            return Error::fail("Bink: data ends in a tree");
        t.table = int(get(4));
        if (t.table == 0) {
            for (int i = 0; i < 16; i++)
                t.syms[i] = uint8_t(i);
            return Error::success();
        }
        if (bit()) {
            bool used[16] = {};
            int len = int(get(3));
            for (int i = 0; i <= len; i++) {
                t.syms[i] = uint8_t(get(4));
                used[t.syms[i]] = true;
            }
            for (int i = 0; i < 16 && len < 15; i++)
                if (!used[i])
                    t.syms[++len] = uint8_t(i);
        } else {
            uint8_t a[16], b[16];
            for (int i = 0; i < 16; i++)
                a[i] = uint8_t(i);
            uint8_t *in = a, *out = b;
            const int depth = int(get(2));
            for (int d = 0; d <= depth; d++) {
                const int size = 1 << d;
                // Merge each pair of neighbouring runs of `size`, a bit
                // choosing which run gives the next symbol.
                for (int start = 0; start < 16; start += size * 2) {
                    const uint8_t *s1 = in + start, *s2 = in + start + size;
                    int n1 = size, n2 = size;
                    uint8_t* o = out + start;
                    do {
                        if (!bit())
                            *o++ = *s1++, n1--;
                        else
                            *o++ = *s2++, n2--;
                    } while (n1 && n2);
                    while (n1--)
                        *o++ = *s1++;
                    while (n2--)
                        *o++ = *s2++;
                }
                std::swap(in, out);
            }
            std::memcpy(t.syms, in, 16);
        }
        return Error::success();
    }

private:
    const uint8_t* data_;
    size_t size_, bits_, pos_ = 0;
};

Error BinkVideoDecoder::init(int width, int height, char revision, uint32_t flags) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return Error::fail("Bink: bad size");
    if (revision < 'd' || revision > 'i')
        return Error::fail(std::string("Bink revision '") + revision + "' isn't supported");
    width_ = width;
    height_ = height;
    revision_ = revision;
    alpha_ = flags & (1u << 20);
    // Revisions h and on have the chroma planes the other way round.
    swapPlanes_ = revision >= 'h';
    first_ = true;
    const int bw = (width + 7) >> 3, bh = (height + 7) >> 3;
    const int cw = (width + 15) >> 4, ch = (height + 15) >> 4;
    // Room for a 16x16 block at the right or bottom edge, and for motion
    // that reaches past it.
    for (int p = 0; p < 4; p++) {
        const bool chroma = p == 1 || p == 2;
        strides_[size_t(p)] = (chroma ? cw : bw) * 8 + 16;
        rows_[size_t(p)] = (chroma ? ch : bh) * 8 + 16;
        for (auto& frame : planes_)
            frame[size_t(p)].assign(size_t(strides_[size_t(p)]) * size_t(rows_[size_t(p)]),
                                    p == 0 || p == 3 ? (p == 3 ? 255 : 16) : 128);
    }
    for (Bundle& b : bundles_)
        b.data.assign(size_t(bw) * size_t(bh) * 64, 0);
    return Error::success();
}

int BinkVideoDecoder::value(int bundle) {
    Bundle& b = bundles_[size_t(bundle)];
    if (b.read >= b.data.size())
        return 0;
    return b.data[b.read++];
}

Error BinkVideoDecoder::decode(const uint8_t* data, size_t size) {
    if (!width_)
        return Error::fail("Bink: not initialised");
    cur_ ^= 1;
    Bits bits(data, size);
    if (alpha_) {
        if (revision_ >= 'i')
            bits.skip(32); // the alpha plane's size
        if (Error e = decodePlane(bits, 3, false))
            return e;
    }
    if (revision_ >= 'i')
        bits.skip(32); // the luma plane's size
    for (int p = 0; p < 3; p++) {
        const int index = (p == 0 || !swapPlanes_) ? p : (p ^ 3);
        if (Error e = decodePlane(bits, index, p != 0))
            return e;
        if (bits.left() <= 0)
            break;
    }
    first_ = false;
    return Error::success();
}

Error BinkVideoDecoder::readBundles(Bits& bits) {
    for (int i = 0; i < BundleCount; i++) {
        if (i == Colours) {
            for (Tree& t : colourHigh_)
                if (Error e = bits.tree(t))
                    return e;
            lastColourHigh_ = 0;
        }
        Bundle& b = bundles_[size_t(i)];
        if (i != IntraDc && i != InterDc)
            if (Error e = bits.tree(b.tree))
                return e;
        b.decoded = b.read = 0;
        b.finished = false;
    }
    return Error::success();
}

namespace {

// Reads a bundle's next run of values, when its earlier ones have all
// been used: returns how many (0: none to read now).
template <typename BundleT, typename BitsT> int runCount(BundleT& b, BitsT& bits) {
    if (b.finished || b.decoded > b.read)
        return 0;
    const int n = int(bits.get(b.lengthBits));
    if (n == 0)
        b.finished = true;
    return n;
}

} // namespace

Error BinkVideoDecoder::decodePlane(Bits& bits, int p, bool chroma) {
    const int w = width_ >> (chroma ? 1 : 0);
    const int bw = chroma ? (width_ + 15) >> 4 : (width_ + 7) >> 3;
    const int bh = chroma ? (height_ + 15) >> 4 : (height_ + 7) >> 3;
    const int stride = strides_[size_t(p)];
    uint8_t* const cur = planes_[size_t(cur_)][size_t(p)].data();
    // The frame before (or, for the first, this one).
    const uint8_t* const prev = first_ ? cur : planes_[size_t(cur_ ^ 1)][size_t(p)].data();
    const ptrdiff_t lastRef = ptrdiff_t(bw - 1 + stride * (bh - 1)) * 8;

    // How many bits a run's count takes, from the plane's width.
    const int alignedW = (std::max(w, 8) + 7) & ~7;
    bundles_[BlockTypes].lengthBits = log2Floor(unsigned(alignedW >> 3) + 511) + 1;
    bundles_[SubBlockTypes].lengthBits = log2Floor(unsigned(alignedW >> 4) + 511) + 1;
    bundles_[Colours].lengthBits = log2Floor(unsigned(bw * 64) + 511) + 1;
    bundles_[IntraDc].lengthBits = bundles_[InterDc].lengthBits = bundles_[XOff].lengthBits =
        bundles_[YOff].lengthBits = log2Floor(unsigned(alignedW >> 3) + 511) + 1;
    bundles_[Pattern].lengthBits = log2Floor(unsigned(bw << 3) + 511) + 1;
    bundles_[Run].lengthBits = log2Floor(unsigned(bw * 48) + 511) + 1;
    if (Error e = readBundles(bits))
        return e;

    auto room = [&](const Bundle& b, int n) { return n >= 0 && b.decoded + size_t(n) <= b.data.size(); };
    auto bad = [](const char* what) { return Error::fail(std::string("Bink: ") + what); };

    for (int by = 0; by < bh; by++) {
        // This row's values, for each bundle that has used up its last run.
        for (int kind : {BlockTypes, SubBlockTypes}) {
            Bundle& b = bundles_[size_t(kind)];
            const int n = runCount(b, bits);
            if (!n)
                continue;
            if (!room(b, n))
                return bad("too many block types");
            if (bits.bit()) {
                std::fill_n(b.data.begin() + ptrdiff_t(b.decoded), n, int16_t(bits.get(4)));
                b.decoded += size_t(n);
            } else {
                const size_t end = b.decoded + size_t(n);
                int last = 0;
                while (b.decoded < end) {
                    const int v = bits.huff(b.tree);
                    if (v < 12) {
                        last = v;
                        b.data[b.decoded++] = int16_t(v);
                    } else {
                        // A run of the last type.
                        const int run = kRunLengths[v - 12];
                        if (end - b.decoded < size_t(run))
                            return bad("block type run too long");
                        std::fill_n(b.data.begin() + ptrdiff_t(b.decoded), run, int16_t(last));
                        b.decoded += size_t(run);
                    }
                }
            }
        }
        {
            // Colours: a byte from two nibbles, the high one's tree chosen
            // by the high nibble before it.
            Bundle& b = bundles_[Colours];
            if (const int n = runCount(b, bits)) {
                if (!room(b, n))
                    return bad("too many colours");
                auto colour = [&] {
                    lastColourHigh_ = bits.huff(colourHigh_[size_t(lastColourHigh_)]);
                    int v = lastColourHigh_ << 4 | bits.huff(b.tree);
                    if (revision_ < 'i') {
                        // Before revision i, signed around 128.
                        const int sign = int8_t(v) >> 7;
                        v = (((v & 0x7f) ^ sign) - sign) + 0x80;
                    }
                    return int16_t(uint8_t(v));
                };
                if (bits.bit()) {
                    std::fill_n(b.data.begin() + ptrdiff_t(b.decoded), n, colour());
                    b.decoded += size_t(n);
                } else {
                    for (int i = 0; i < n; i++)
                        b.data[b.decoded++] = colour();
                }
            }
        }
        {
            // Patterns: a byte from two nibbles, low first.
            Bundle& b = bundles_[Pattern];
            if (const int n = runCount(b, bits)) {
                if (!room(b, n))
                    return bad("too many patterns");
                for (int i = 0; i < n; i++) {
                    int v = bits.huff(b.tree);
                    v |= bits.huff(b.tree) << 4;
                    b.data[b.decoded++] = int16_t(v);
                }
            }
        }
        for (int kind : {XOff, YOff}) {
            // Motion: signed nibbles.
            Bundle& b = bundles_[size_t(kind)];
            const int n = runCount(b, bits);
            if (!n)
                continue;
            if (!room(b, n))
                return bad("too many motion values");
            auto signedValue = [&](int v) { return v && bits.bit() ? -v : v; };
            if (bits.bit()) {
                const int v = signedValue(int(bits.get(4)));
                std::fill_n(b.data.begin() + ptrdiff_t(b.decoded), n, int16_t(v));
                b.decoded += size_t(n);
            } else {
                for (int i = 0; i < n; i++)
                    b.data[b.decoded++] = int16_t(signedValue(bits.huff(b.tree)));
            }
        }
        for (int kind : {IntraDc, InterDc}) {
            // DCs: a first value of 11 bits (signed for inter blocks), then
            // groups of up to 8 differences, each group's width first.
            Bundle& b = bundles_[size_t(kind)];
            int n = runCount(b, bits);
            if (!n)
                continue;
            if (!room(b, n))
                return bad("too many DC values");
            const bool isSigned = kind == InterDc;
            int v = int(bits.get(11 - (isSigned ? 1 : 0)));
            if (v && isSigned && bits.bit())
                v = -v;
            b.data[b.decoded++] = int16_t(v);
            n--;
            for (int i = 0; i < n; i += 8) {
                const int group = std::min(n - i, 8);
                const int width = int(bits.get(4));
                for (int j = 0; j < group; j++) {
                    if (width) {
                        int d = int(bits.get(width));
                        if (d && bits.bit())
                            d = -d;
                        v += d;
                        if (v < -32768 || v > 32767)
                            return bad("DC out of range");
                    }
                    b.data[b.decoded++] = int16_t(v);
                }
            }
        }
        {
            Bundle& b = bundles_[Run];
            if (const int n = runCount(b, bits)) {
                if (!room(b, n))
                    return bad("too many runs");
                if (bits.bit()) {
                    std::fill_n(b.data.begin() + ptrdiff_t(b.decoded), n, int16_t(bits.get(4)));
                    b.decoded += size_t(n);
                } else {
                    for (int i = 0; i < n; i++)
                        b.data[b.decoded++] = int16_t(bits.huff(b.tree));
                }
            }
        }
        if (bits.left() < 0)
            return bad("data ends early");

        for (int bx = 0; bx < bw; bx++) {
            uint8_t* dst = cur + (by * 8) * stride + bx * 8;
            const ptrdiff_t at = ptrdiff_t(by * 8) * stride + bx * 8;
            int type = value(BlockTypes);
            // A 16x16 block's other three quarters say so too: skip them.
            if (type == Scaled && ((by & 1) || (bx & 1))) {
                bx++;
                continue;
            }
            // Copies the 8x8 block the motion values point at in the frame
            // before.
            auto motion = [&]() -> bool {
                const int x = int8_t(value(XOff)), y = int8_t(value(YOff));
                const ptrdiff_t from = at + x + ptrdiff_t(y) * stride;
                if (from < 0 || from > lastRef)
                    return false;
                for (int r = 0; r < 8; r++)
                    std::memcpy(dst + r * stride, prev + from + r * stride, 8);
                return true;
            };
            // Fills 64 values along one of the 16 scan patterns, in runs of
            // one colour or of colours each.
            auto runFill = [&](uint8_t* out, int outStride) -> bool {
                const uint8_t* scan = bt::bink_patterns[bits.get(4)];
                int i = 0;
                do {
                    const int run = value(Run) + 1;
                    i += run;
                    if (i > 64)
                        return false;
                    if (bits.bit()) {
                        const int v = value(Colours);
                        for (int j = 0; j < run; j++, scan++)
                            out[(*scan >> 3) * outStride + (*scan & 7)] = uint8_t(v);
                    } else {
                        for (int j = 0; j < run; j++, scan++)
                            out[(*scan >> 3) * outStride + (*scan & 7)] = uint8_t(value(Colours));
                    }
                } while (i < 63);
                if (i == 63)
                    out[(*scan >> 3) * outStride + (*scan & 7)] = uint8_t(value(Colours));
                return true;
            };
            auto patternFill = [&](uint8_t* out, int outStride) {
                const int c[2] = {value(Colours), value(Colours)};
                for (int r = 0; r < 8; r++) {
                    int v = value(Pattern);
                    for (int x = 0; x < 8; x++, v >>= 1)
                        out[r * outStride + x] = uint8_t(c[v & 1]);
                }
            };
            auto rawFill = [&](uint8_t* out, int outStride) {
                for (int r = 0; r < 8; r++)
                    for (int x = 0; x < 8; x++)
                        out[r * outStride + x] = uint8_t(value(Colours));
            };

            switch (type) {
            case Skip:
                for (int r = 0; r < 8; r++)
                    std::memcpy(dst + r * stride, prev + at + r * stride, 8);
                break;
            case Scaled: {
                // An 8x8 block drawn at twice the size.
                uint8_t small[64];
                const int sub = value(SubBlockTypes);
                switch (sub) {
                case RunFill:
                    if (!runFill(small, 8))
                        return bad("run fill too long");
                    break;
                case Intra: {
                    int32_t block[64] = {};
                    block[0] = value(IntraDc);
                    int q = 0;
                    if (Error e = dctCoefficients(bits, block, q))
                        return e;
                    dequantise(block, bt::bink_intra_quant[q]);
                    idct(block, small, 8);
                    break;
                }
                case Fill: {
                    const uint8_t v = uint8_t(value(Colours));
                    for (int r = 0; r < 16; r++)
                        std::memset(dst + r * stride, v, 16);
                    break;
                }
                case PatternFill:
                    patternFill(small, 8);
                    break;
                case Raw:
                    rawFill(small, 8);
                    break;
                default:
                    return bad("bad 16x16 block type");
                }
                if (sub != Fill)
                    for (int r = 0; r < 16; r++)
                        for (int x = 0; x < 16; x++)
                            dst[r * stride + x] = small[(r >> 1) * 8 + (x >> 1)];
                bx++;
                break;
            }
            case Motion:
                if (!motion())
                    return bad("motion out of the frame");
                break;
            case RunFill:
                if (!runFill(dst, stride))
                    return bad("run fill too long");
                break;
            case Residue: {
                if (!motion())
                    return bad("motion out of the frame");
                int16_t block[64] = {};
                residue(bits, block, int(bits.get(7)));
                for (int r = 0; r < 8; r++)
                    for (int x = 0; x < 8; x++)
                        dst[r * stride + x] = uint8_t(dst[r * stride + x] + block[r * 8 + x]);
                break;
            }
            case Intra: {
                int32_t block[64] = {};
                block[0] = value(IntraDc);
                int q = 0;
                if (Error e = dctCoefficients(bits, block, q))
                    return e;
                dequantise(block, bt::bink_intra_quant[q]);
                idct(block, dst, stride);
                break;
            }
            case Fill: {
                const uint8_t v = uint8_t(value(Colours));
                for (int r = 0; r < 8; r++)
                    std::memset(dst + r * stride, v, 8);
                break;
            }
            case Inter: {
                if (!motion())
                    return bad("motion out of the frame");
                int32_t block[64] = {};
                block[0] = value(InterDc);
                int q = 0;
                if (Error e = dctCoefficients(bits, block, q))
                    return e;
                dequantise(block, bt::bink_inter_quant[q]);
                int32_t out[64];
                idct(block, out, 8);
                for (int r = 0; r < 8; r++)
                    for (int x = 0; x < 8; x++)
                        dst[r * stride + x] = uint8_t(dst[r * stride + x] + out[r * 8 + x]);
                break;
            }
            case PatternFill:
                patternFill(dst, stride);
                break;
            case Raw:
                rawFill(dst, stride);
                break;
            default:
                return bad("bad block type");
            }
        }
    }
    bits.align32();
    return Error::success();
}

// The DCT coefficients after the DC: in passes from the most significant
// bit down, a list of coefficient groups (4, then their 3 neighbouring
// groups of 4) each either split further or holding values, in scan
// order. Then the quantiser.
Error BinkVideoDecoder::dctCoefficients(Bits& bits, int32_t block[64], int& quantiser) {
    if (bits.left() < 4)
        return Error::fail("Bink: data ends in a block");
    int list[128], mode[128];
    int start = 64, end = 64;
    coefCount_ = 0;
    for (const auto& [c, m] : {std::pair{4, 0}, {24, 0}, {44, 0}, {1, 3}, {2, 3}, {3, 3}})
        list[end] = c, mode[end++] = m;
    auto readValue = [&](int nbits) {
        if (!nbits)
            return bits.bit() ? -1 : 1;
        int t = int(bits.get(nbits)) | 1 << nbits;
        return bits.bit() ? -t : t;
    };
    for (int nbits = int(bits.get(4)) - 1; nbits >= 0; nbits--) {
        for (int pos = start; pos < end;) {
            if (!(mode[pos] | list[pos]) || !bits.bit()) {
                pos++;
                continue;
            }
            int c = list[pos];
            const int m = mode[pos];
            if (m == 0 || m == 2) {
                if (m == 0) {
                    // A group of 4 the first time: its 3 neighbours follow it.
                    list[pos] = c + 4;
                    mode[pos] = 1;
                } else {
                    list[pos] = 0;
                    mode[pos++] = 0;
                }
                for (int i = 0; i < 4; i++, c++) {
                    if (bits.bit()) {
                        list[--start] = c;
                        mode[start] = 3;
                    } else {
                        block[bt::bink_scan[c]] = readValue(nbits);
                        coefIndex_[coefCount_++] = c;
                    }
                }
            } else if (m == 1) {
                mode[pos] = 2;
                for (int i = 0; i < 3; i++) {
                    c += 4;
                    list[end] = c;
                    mode[end++] = 2;
                }
            } else { // 3: one coefficient
                block[bt::bink_scan[c]] = readValue(nbits);
                coefIndex_[coefCount_++] = c;
                list[pos] = 0;
                mode[pos++] = 0;
            }
        }
    }
    quantiser = int(bits.get(4));
    return Error::success();
}

void BinkVideoDecoder::dequantise(int32_t block[64], const int32_t quant[64]) const {
    auto scale = [](int32_t v, int32_t q) { return int32_t(uint32_t(v) * uint32_t(q)) >> 11; };
    block[0] = scale(block[0], quant[0]);
    for (int i = 0; i < coefCount_; i++) {
        const int c = coefIndex_[i];
        block[bt::bink_scan[c]] = scale(block[bt::bink_scan[c]], quant[c]);
    }
}

// A residue after motion: like the DCT coefficients' coding, but values
// are built bit plane by bit plane (`mask` from the top down), up to
// `masks` values in all.
void BinkVideoDecoder::residue(Bits& bits, int16_t block[64], int masks) {
    int list[128], mode[128];
    int start = 64, end = 64;
    int nonZero[64], nonZeroCount = 0;
    for (const auto& [c, m] : {std::pair{4, 0}, {24, 0}, {44, 0}, {0, 2}})
        list[end] = c, mode[end++] = m;
    for (int mask = 1 << bits.get(3); mask; mask >>= 1) {
        for (int i = 0; i < nonZeroCount; i++) {
            if (!bits.bit())
                continue;
            int16_t& v = block[nonZero[i]];
            v = int16_t(v < 0 ? v - mask : v + mask);
            if (--masks < 0)
                return;
        }
        for (int pos = start; pos < end;) {
            if (!(list[pos] | mode[pos]) || !bits.bit()) {
                pos++;
                continue;
            }
            int c = list[pos];
            const int m = mode[pos];
            if (m == 0 || m == 2) {
                if (m == 0) {
                    list[pos] = c + 4;
                    mode[pos] = 1;
                } else {
                    list[pos] = 0;
                    mode[pos++] = 0;
                }
                for (int i = 0; i < 4; i++, c++) {
                    if (bits.bit()) {
                        list[--start] = c;
                        mode[start] = 3;
                    } else {
                        const int at = bt::bink_scan[c];
                        nonZero[nonZeroCount++] = at;
                        block[at] = int16_t(bits.bit() ? -mask : mask);
                        if (--masks < 0)
                            return;
                    }
                }
            } else if (m == 1) {
                mode[pos] = 2;
                for (int i = 0; i < 3; i++) {
                    c += 4;
                    list[end] = c;
                    mode[end++] = 2;
                }
            } else {
                const int at = bt::bink_scan[c];
                nonZero[nonZeroCount++] = at;
                block[at] = int16_t(bits.bit() ? -mask : mask);
                list[pos] = 0;
                mode[pos++] = 0;
                if (--masks < 0)
                    return;
            }
        }
    }
}

void BinkVideoDecoder::toRgba(std::vector<uint8_t>& out) const {
    out.resize(size_t(width_) * size_t(height_) * 4);
    const auto& f = planes_[size_t(cur_)];
    auto clamp = [](int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
    for (int y = 0; y < height_; y++) {
        const uint8_t* yr = f[0].data() + y * strides_[0];
        const uint8_t* ur = f[1].data() + (y >> 1) * strides_[1];
        const uint8_t* vr = f[2].data() + (y >> 1) * strides_[2];
        const uint8_t* ar = f[3].data() + y * strides_[3];
        uint8_t* o = out.data() + size_t(y) * size_t(width_) * 4;
        for (int x = 0; x < width_; x++, o += 4) {
            // BT.601, studio range (16-235), in 16.16 fixed point.
            const int c = (yr[x] - 16) * 76309, d = ur[x >> 1] - 128, e = vr[x >> 1] - 128;
            o[0] = clamp((c + 104597 * e + 32768) >> 16);
            o[1] = clamp((c - 25675 * d - 53279 * e + 32768) >> 16);
            o[2] = clamp((c + 132201 * d + 32768) >> 16);
            o[3] = alpha_ ? ar[x] : 255;
        }
    }
}

} // namespace reluva::video
