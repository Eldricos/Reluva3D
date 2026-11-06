// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/bink_texture.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::awe {

namespace {

// The range decoder. It keeps 31 bits of the stream in `code`, taking
// each byte's top seven bits and holding its lowest bit back for the
// next. Out-of-range reads give 0, as the DLL's zeroed padding does.
class RangeDecoder {
public:
    RangeDecoder(const std::vector<uint8_t>& in, size_t at) : in_(in), pos_(at + 1) {
        const uint32_t first = byte(at);
        code_ = first >> 1;
        // The DLL keeps the held-back bit in the low bit of a pointer to
        // the stream; it starts as that bit plus the address, so the
        // address's parity counts (relative to the input, which the DLL
        // is given aligned).
        bit_ = (first + uint32_t(at)) & 1;
    }

    // A value in [0, total): call update() with its interval after.
    uint32_t decode(uint32_t total) {
        normalize();
        scale_ = range_ / total;
        return std::min(code_ / scale_, total - 1);
    }
    // The same for a total of 1 << bits.
    uint32_t decodeShift(int bits) {
        normalize();
        scale_ = range_ >> bits;
        return std::min(code_ / scale_, (1u << bits) - 1);
    }
    void update(uint32_t start, uint32_t size, uint32_t total) {
        code_ -= scale_ * start;
        range_ = start + size < total ? scale_ * size : range_ - scale_ * start;
    }
    // A value in [0, n), all equally likely.
    uint32_t uniform(uint32_t n) {
        normalize();
        const uint32_t r = range_ / n;
        const uint32_t v = std::min(code_ / r, n - 1);
        code_ -= v * r;
        range_ = v + 1 < n ? r : range_ - v * r;
        return v;
    }

private:
    uint32_t byte(size_t at) const { return at < in_.size() ? in_[at] : 0; }
    void normalize() {
        while (range_ <= 0x800000) {
            const uint32_t b = byte(pos_++);
            code_ = ((code_ * 2 + bit_) << 7) | (b >> 1);
            bit_ = b & 1;
            range_ <<= 8;
        }
    }

    const std::vector<uint8_t>& in_;
    size_t pos_;
    uint32_t code_ = 0, range_ = 0x80, scale_ = 1, bit_ = 0;
};

// Raw bits, least significant first, from little-endian 32-bit words.
class BitReader {
public:
    BitReader(const std::vector<uint8_t>& in, size_t at) : in_(in), pos_(at) {}

    uint32_t bit() {
        if (count_ == 0) {
            const uint32_t w = word();
            buf_ = w >> 1;
            count_ = 31;
            return w & 1;
        }
        const uint32_t b = buf_ & 1;
        buf_ >>= 1;
        count_--;
        return b;
    }
    uint32_t bits(int n) {
        const uint32_t mask = (1u << n) - 1;
        if (count_ < n) {
            const uint32_t w = word();
            const uint32_t v = ((w << count_) | buf_) & mask;
            buf_ = w >> (n - count_);
            count_ += 32 - n;
            return v;
        }
        const uint32_t v = buf_ & mask;
        buf_ >>= n;
        count_ -= n;
        return v;
    }

private:
    uint32_t word() {
        uint32_t w = 0;
        for (int k = 0; k < 4; k++, pos_++)
            if (pos_ < in_.size())
                w |= uint32_t(in_[pos_]) << (8 * k);
        return w;
    }

    const std::vector<uint8_t>& in_;
    size_t pos_;
    uint32_t buf_ = 0;
    int count_ = 0;
};

// An adaptive model of up to `symbols` values, each first sent through
// an escape (index 0) and then given its own index. Counts are turned
// into a table of intervals out of 1 << 14 only now and then, when the
// total passes a threshold that grows each time.
class Model {
public:
    static constexpr int kNew = -1; // decode(): a new value, for the caller to read and set()

    Model(uint32_t largest, uint32_t symbols) : symbols_(uint16_t(symbols)) {
        const size_t n = (symbols + 5) & ~3u;
        cdf_.assign(n + 6, 0);
        freq_.assign(n, 0);
        values_.assign(n, 0);
        for (int i = 1; i <= 5; i++)
            cdf_[size_t(i)] = 0xc000;
        cdf_[0] = 0x8000;
        freq_[0] = 4;
        total_ = 4;
        threshold_ = 8;
        step_ = 4;
        limit_ = uint16_t(std::clamp<uint32_t>(largest << 5, 0x100, 0x3b38));
        const uint32_t s = 2 * largest;
        stepLimit_ = uint16_t(s < 0x80 ? 0x80 : std::min<uint32_t>(s, limit_ / 2u - 0x20));
    }

    // The next value, or kNew.
    int decode(RangeDecoder& rc) {
        if (total_ >= threshold_) {
            if (threshold_ >= limit_)
                rescale();
            rebuild();
            for (size_t i = size_t(inTable_) + 2; i <= size_t(inTable_) + 6; i++)
                cdf_[i] = 0xc000;
        }
        const uint32_t v = rc.decodeShift(14) + 0x8000;
        // The interval holding v: the last start at or below it.
        size_t i = 0;
        while (i < inTable_ && cdf_[i + 1] <= v)
            i++;
        rc.update(cdf_[i] - 0x8000u, uint32_t(cdf_[i + 1] - cdf_[i]), 0x4000);
        freq_[i]++;
        total_++;
        if (i > 0)
            return values_[i];
        // The escape: one of the values added since the table was built,
        // or a new one.
        if (used_ != inTable_ && rc.uniform(2)) {
            const size_t k = rc.uniform(uint32_t(used_ - inTable_)) + inTable_ + 1u;
            freq_[k] += 2;
            total_ += 2;
            return values_[k];
        }
        used_++;
        freq_[used_] += 2;
        total_ += 2;
        if (used_ == symbols_) {
            total_ = uint16_t(total_ - freq_[0]);
            freq_[0] = 0;
        }
        return kNew;
    }
    // The value of the symbol decode() just added.
    void set(uint32_t v) { values_[used_] = uint16_t(v); }

private:
    void rebuild() {
        const uint32_t scale = 0x20000u / std::max<uint32_t>(total_, 1);
        uint32_t at = 0x8000 + ((freq_[0] * scale) >> 3);
        cdf_[0] = 0x8000;
        cdf_[1] = uint16_t(at);
        for (size_t i = 1; i <= used_; i++) {
            at += (freq_[i] * scale) >> 3;
            cdf_[i + 1] = uint16_t(at);
        }
        if (2u * step_ > stepLimit_) {
            threshold_ = uint16_t(total_ + stepLimit_);
        } else {
            step_ = uint16_t(step_ * 2);
            threshold_ = uint16_t(total_ + step_);
        }
        inTable_ = used_;
        cdf_[size_t(used_) + 1] = 0xc000;
    }
    // Halves the counts, dropping values seen at most once (the last
    // value takes a dropped one's place), and moves the commonest value
    // to the end.
    void rescale() {
        freq_[0] >>= 1;
        total_ = freq_[0];
        uint32_t best = 0;
        size_t bestAt = 0;
        for (size_t i = 1; i <= used_; i++) {
            bool dropped = false;
            while (freq_[i] <= 1) {
                if (i >= used_) {
                    freq_[i] = 0;
                    used_--;
                    dropped = true;
                    break;
                }
                freq_[i] = freq_[used_];
                freq_[used_] = 0;
                values_[i] = values_[used_];
                used_--;
            }
            if (dropped)
                break;
            freq_[i] >>= 1;
            total_ = uint16_t(total_ + freq_[i]);
            if (freq_[i] > best) {
                best = freq_[i];
                bestAt = i;
            }
        }
        if (best && used_ && bestAt != used_) {
            std::swap(freq_[used_], freq_[bestAt]);
            std::swap(values_[used_], values_[bestAt]);
        }
        if (used_ != symbols_ && freq_[0] == 0) {
            freq_[0] = 1;
            total_++;
        }
    }

    std::vector<uint16_t> cdf_, freq_, values_;
    uint16_t total_ = 0, threshold_ = 0, stepLimit_ = 0, step_ = 0, limit_ = 0;
    uint16_t used_ = 0, inTable_ = 0, symbols_ = 0;
};

// The number of bits in v, at most 15.
int bitLength(uint32_t v) {
    int n = 0;
    while (v && n < 15) {
        n++;
        v >>= 1;
    }
    return n;
}

void fill(int16_t* p, size_t stride, uint32_t w, uint32_t h, uint32_t value) {
    for (uint32_t y = 0; y < h; y++)
        std::fill(p + y * stride, p + y * stride + w, int16_t(value));
}

struct Streams {
    RangeDecoder& rc;
    BitReader& bits;
};

// A value from `model` (a new one sent uniformly), with a sign bit if
// it isn't 0.
int32_t signedValue(Streams& s, Model& model, uint32_t symbols) {
    int v = model.decode(s.rc);
    if (v == Model::kNew) {
        v = int(s.rc.uniform(symbols));
        model.set(uint32_t(v));
    }
    if (v && s.bits.bit())
        v = -v;
    return v;
}

// The lowest band: each value predicted from its neighbours (the one to
// the left on the first row, the one above at the start of a row, else
// the mean of the two), plus a coded difference.
void decodeLowest(Streams& s, int16_t* p, size_t stride, uint32_t w, uint32_t h) {
    if (s.bits.bit()) {
        fill(p, stride, w, h, s.bits.bits(16));
        return;
    }
    const uint32_t largest = s.bits.bits(16), symbols = largest + 1;
    Model model(largest, symbols);
    int32_t left = int32_t(s.bits.bits(16));
    p[0] = int16_t(left);
    for (uint32_t x = 1; x < w; x++) {
        left += signedValue(s, model, symbols);
        p[x] = int16_t(left);
    }
    for (uint32_t y = 1; y < h; y++) {
        int16_t* row = p + y * stride;
        const int16_t* above = row - stride;
        left = above[0] + signedValue(s, model, symbols);
        row[0] = int16_t(left);
        for (uint32_t x = 1; x < w; x++) {
            left = (above[x] + left) / 2 + signedValue(s, model, symbols);
            row[x] = int16_t(left);
        }
    }
}

// A band of details: runs of coded values and of zeros. Each value's
// model is picked by the size of its neighbours (left, above-left, above,
// above-right), and scaled by the band's quantizer.
void decodeBand(Streams& s, int16_t* p, size_t stride, uint32_t w, uint32_t h) {
    static const uint32_t kLongRuns[4] = {0x80, 0x100, 0x200, 0x400};
    static const uint32_t kLongZeros[4] = {0x200, 0x400, 0x800, 0xc00};
    const int32_t quant = int32_t(s.bits.bits(16));
    if (s.bits.bit()) {
        fill(p, stride, w, h, s.bits.bits(16) * uint32_t(quant));
        return;
    }
    const uint32_t largest = s.bits.bits(16), symbols = largest + 1;
    std::vector<Model> models(size_t(bitLength(largest * uint32_t(quant))) + 1, Model(largest, symbols));
    Model runs(63, 64), zeroRuns(255, 256);
    auto value = [&](uint32_t context) {
        Model& model = models[std::min(size_t(bitLength(context)), models.size() - 1)];
        const int32_t v = signedValue(s, model, symbols);
        return int32_t(uint32_t(v) * uint32_t(quant));
    };

    // As the DLL walks it: `up` points at the value above and to the
    // right of the next one (on the first row, the one before it); `a`
    // is above, `b` above-left, `c` to the left.
    int16_t* out = p;
    int32_t first = int32_t(s.rc.uniform(symbols));
    if (first && s.bits.bit())
        first = -first;
    first = int32_t(uint32_t(first) * uint32_t(quant));
    *out = int16_t(first);
    const int16_t* up = out++;
    int32_t a = first, b = first, c = first;
    uint32_t rows = h, left = w - 1, coded = 0, zeros = 0;
    bool rowDone = w == 1;
    for (;;) {
        if (rowDone) {
            if (--rows == 0)
                return;
            out += stride - w;
            up = out - stride;
            a = b = c = *up++;
            left = w;
            rowDone = false;
        }
        if (coded == 0) {
            if (zeros == 0) {
                int r = runs.decode(s.rc);
                if (r == Model::kNew) {
                    r = int(s.bits.bits(6));
                    runs.set(uint32_t(r));
                }
                coded = r >= 60 ? kLongRuns[r - 60] : uint32_t(r);
                int z = zeroRuns.decode(s.rc);
                if (z == Model::kNew) {
                    z = int(s.bits.bits(8));
                    zeroRuns.set(uint32_t(z));
                }
                zeros = z >= 252 ? kLongZeros[z - 252] + 2 : z ? uint32_t(z) + 2 : 0;
                continue;
            }
            if (zeros >= left) {
                zeros -= left;
                std::fill(out, out + left, int16_t(0));
                out += left;
                rowDone = true;
                continue;
            }
            left -= zeros;
            up += zeros;
            std::fill(out, out + zeros, int16_t(0));
            out += zeros;
            b = up[-2];
            a = up[-1];
            c = 0;
            zeros = 0;
            continue;
        }
        if (left == 0) {
            rowDone = true;
        } else if (left == 1) {
            // The last of a row, with nothing above-right.
            const int32_t v = value((uint32_t(std::abs(2 * c)) + uint32_t(std::abs(b)) + uint32_t(std::abs(a))) >> 2);
            *out++ = int16_t(v);
            coded--;
            rowDone = true;
        } else {
            const int32_t d = *up++;
            const int32_t v = value(
                (uint32_t(std::abs(d)) + uint32_t(std::abs(b)) + uint32_t(std::abs(a)) + uint32_t(std::abs(c))) >> 2);
            *out++ = int16_t(v);
            left--;
            coded--;
            b = a;
            a = d;
            c = v;
        }
    }
}

// One flag per row of the full-size plane: whether its horizontal
// details are all zero (0) or not. First how many are 0, then each flag
// with that probability.
void decodeRowFlags(RangeDecoder& rc, uint8_t* flags, uint32_t count) {
    const uint32_t zeros = rc.uniform(count + 1);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = rc.decode(count);
        flags[i] = v >= zeros;
        if (flags[i])
            rc.update(zeros, count - zeros, count);
        else
            rc.update(0, zeros, count);
    }
}

// One plane's coefficients, laid out for the in-place transform: at each
// of the four levels the band of low frequencies takes the even rows and
// left half, the others the right half and the odd rows. Returns the
// bytes used.
size_t decodePlane(const std::vector<uint8_t>& in, size_t at, int16_t* p, uint32_t w, uint32_t h, uint8_t* rowFlags) {
    auto u32 = [&](size_t i) {
        uint32_t v = 0;
        for (int k = 0; k < 4; k++)
            if (i + size_t(k) < in.size())
                v |= uint32_t(in[i + size_t(k)]) << (8 * k);
        return v;
    };
    const uint32_t rangeBytes = u32(at), bitBytes = u32(at + 4);
    RangeDecoder rc(in, at + 8);
    BitReader bits(in, at + 8 + rangeBytes);
    Streams s{rc, bits};
    decodeLowest(s, p, size_t(w) * 16, w >> 4, h >> 4);
    for (uint32_t scale = 16; scale >= 2; scale /= 2) {
        const size_t stride = size_t(w) * scale, below = stride / 2;
        const uint32_t bw = w / scale, bh = h / scale;
        decodeBand(s, p + bw, stride, bw, bh);
        decodeBand(s, p + below, stride, bw, bh);
        decodeBand(s, p + below + bw, stride, bw, bh);
    }
    decodeRowFlags(rc, rowFlags, h);
    return size_t(rangeBytes) + bitBytes + 8;
}

// x / 2 as the DLL rounds it: (x + 1) >> 1, or (x - 1) >> 1 below 0.
int16_t half(int32_t x) {
    return int16_t((x + (x < 0 ? -2 : 1)) / 2);
}
// x / 65536 to the nearest (with wrapping 32-bit sums, as the DLL's).
int16_t fixed(uint32_t sum) {
    const int32_t x = int32_t(sum);
    return int16_t((int64_t(x) + (x < 0 ? -0x8000 : 0x7fff)) / 0x10000);
}

// One dimension of the inverse transform: n/2 low values `lo(i)` and n/2
// high values `hi(i)` to n values. Short ones use a two-tap filter,
// longer ones a 9/7 filter in 16.16 fixed point, mirrored at the ends.
// Without `highs` only the low taps count.
template <typename Lo, typename Hi, typename Put>
void synthesize(uint32_t n, bool longFilter, Lo lo, Hi hi, bool highs, Put put) {
    const int32_t m = int32_t(n / 2);
    if (!longFilter) {
        for (int32_t i = 0; i < m; i++) {
            const int32_t l = lo(i);
            if (!highs) {
                put(2 * i, int16_t(l));
                put(2 * i + 1, int16_t(l));
                continue;
            }
            const int32_t h = hi(i);
            put(2 * i, half(2 * l + h));
            put(2 * i + 1, half(2 * l - h));
        }
        return;
    }
    // Mirrored about the first and last samples of the interleaved signal.
    auto L = [&](int32_t i) {
        if (i < 0)
            i = -i;
        if (i >= m)
            i = 2 * m - 2 - i + 1;
        return uint32_t(lo(i));
    };
    auto H = [&](int32_t i) {
        if (i < 0)
            i = -1 - i;
        if (i >= m)
            i = 2 * m - 2 - i;
        return uint32_t(hi(i));
    };
    for (int32_t i = 0; i < m; i++) {
        uint32_t even = 0xc9dau * L(i) - 0xa6bu * (L(i - 1) + L(i + 1));
        uint32_t odd = 0x6b08u * (L(i) + L(i + 1)) - 0x1086u * (L(i - 1) + L(i + 2));
        if (highs) {
            even += 0x609du * (H(i - 1) + H(i)) - 0x61bu * (H(i - 2) + H(i + 1));
            odd += 0x1c52u * (H(i - 1) + H(i + 1)) - 0xda4au * H(i) - 0x9afu * (H(i - 2) + H(i + 2));
        }
        put(2 * i, fixed(even));
        put(2 * i + 1, fixed(odd));
    }
}

// One level of the inverse transform, in place: w x h values whose rows
// are `stride` apart. Rows first (flagged rows without details), then
// columns.
void inverseLevel(int16_t* p, size_t stride, uint32_t w, uint32_t h, const uint8_t* rowFlags,
                  std::vector<int16_t>& scratch) {
    scratch.resize(stride * h);
    const uint32_t m = w / 2;
    // A row: lows in its left half, highs in its right.
    for (uint32_t y = 0; y < h; y++) {
        const int16_t* src = p + y * stride;
        int16_t* dst = scratch.data() + y * stride;
        auto lo = [&](int32_t i) { return int32_t(src[i]); };
        auto hi = [&](int32_t i) { return int32_t(src[m + uint32_t(i)]); };
        auto put = [&](int32_t i, int16_t v) { dst[i] = v; };
        synthesize(w, w >= 12, lo, hi, !rowFlags || rowFlags[y], put);
    }
    // A column: lows in its even rows, highs in its odd ones.
    for (uint32_t x = 0; x < w; x++) {
        const int16_t* src = scratch.data() + x;
        int16_t* dst = p + x;
        auto lo = [&](int32_t i) { return int32_t(src[size_t(2 * i) * stride]); };
        auto hi = [&](int32_t i) { return int32_t(src[size_t(2 * i + 1) * stride]); };
        auto put = [&](int32_t i, int16_t v) { dst[size_t(i) * stride] = v; };
        synthesize(h, h >= 10, lo, hi, true, put);
    }
}

uint8_t clampByte(int32_t v) {
    return uint8_t(std::clamp(v, 0, 255));
}

} // namespace

Error decodeBinkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t>& in,
                        std::vector<uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Bad texture size");
    // Bit 2 picks the codec, bit 1 how the planes become colours.
    if (!(flags & 4) || (flags & 2))
        return Error::fail("Bink texture kind " + std::to_string(flags) + " isn't supported");
    if (width * height <= 256)
        return Error::fail("Small Bink textures (stored raw) aren't supported");
    const uint32_t w = (uint32_t(width) + 15) & ~15u, h = (uint32_t(height) + 15) & ~15u;
    const int planes = flags & 1 ? 4 : 3;
    std::vector<int16_t> plane[4];
    std::vector<uint8_t> rowFlags(h);
    std::vector<int16_t> scratch;
    // A word first (the size of the work space the DLL wants), then each
    // plane: luma, two colour differences, alpha.
    size_t at = 4;
    for (int i = 0; i < 4; i++) {
        plane[i].assign(size_t(w) * h, 0);
        if (i >= planes)
            continue;
        if (at + 8 > in.size())
            return Error::fail("Bink texture data is cut short");
        at += decodePlane(in, at, plane[i].data(), w, h, rowFlags.data());
        for (uint32_t scale = 8; scale >= 1; scale /= 2)
            inverseLevel(plane[i].data(), size_t(w) * scale, w / scale, h / scale,
                         scale == 1 ? rowFlags.data() : nullptr, scratch);
    }
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (uint32_t y = 0; y < uint32_t(height); y++)
        for (uint32_t x = 0; x < uint32_t(width); x++) {
            const size_t i = size_t(y) * w + x;
            const int32_t luma = plane[0][i], co = plane[1][i], cg = plane[2][i];
            const int32_t g = luma - (co + cg) / 4;
            uint8_t* px = &rgba[(size_t(y) * size_t(width) + x) * 4];
            px[0] = clampByte(co + g);
            px[1] = clampByte(g);
            px[2] = clampByte(cg + g);
            px[3] = clampByte(plane[3][i]);
        }
    return Error::success();
}

} // namespace reluva::awe
