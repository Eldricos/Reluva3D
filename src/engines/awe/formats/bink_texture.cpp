// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/formats/bink_texture.h"

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
    RangeDecoder(const std::vector<uint8_t> &in, size_t at) : _in(in), _pos(at + 1) {
        const uint32_t first = byte(at);
        _code = first >> 1;
        // The DLL keeps the held-back bit in the low bit of a pointer to
        // the stream; it starts as that bit plus the address, so the
        // address's parity counts (relative to the input, which the DLL
        // is given aligned).
        _bit = (first + uint32_t(at)) & 1;
    }

    // A value in [0, total): call update() with its interval after.
    uint32_t decode(uint32_t total) {
        normalize();
        _scale = _range / total;
        return std::min(_code / _scale, total - 1);
    }
    // The same for a total of 1 << bits.
    uint32_t decodeShift(int bits) {
        normalize();
        _scale = _range >> bits;
        return std::min(_code / _scale, (1u << bits) - 1);
    }
    void update(uint32_t start, uint32_t size, uint32_t total) {
        _code -= _scale * start;
        _range = start + size < total ? _scale * size : _range - _scale * start;
    }
    // A value in [0, n), all equally likely.
    uint32_t uniform(uint32_t n) {
        normalize();
        const uint32_t r = _range / n;
        const uint32_t v = std::min(_code / r, n - 1);
        _code -= v * r;
        _range = v + 1 < n ? r : _range - v * r;
        return v;
    }

private:
    uint32_t byte(size_t at) const { return at < _in.size() ? _in[at] : 0; }
    void normalize() {
        while (_range <= 0x800000) {
            const uint32_t b = byte(_pos++);
            _code = ((_code * 2 + _bit) << 7) | (b >> 1);
            _bit = b & 1;
            _range <<= 8;
        }
    }

    const std::vector<uint8_t> &_in;
    size_t _pos;
    uint32_t _code = 0;
    uint32_t _range = 0x80;
    uint32_t _scale = 1;
    uint32_t _bit = 0;
};

// Raw bits, least significant first, from little-endian 32-bit words.
class BitReader {
public:
    BitReader(const std::vector<uint8_t> &in, size_t at) : _in(in), _pos(at) {}

    uint32_t bit() {
        if (_count == 0) {
            const uint32_t w = word();
            _buf = w >> 1;
            _count = 31;
            return w & 1;
        }
        const uint32_t b = _buf & 1;
        _buf >>= 1;
        _count--;
        return b;
    }
    uint32_t bits(int n) {
        const uint32_t mask = (1u << n) - 1;
        if (_count < n) {
            const uint32_t w = word();
            const uint32_t v = ((w << _count) | _buf) & mask;
            _buf = w >> (n - _count);
            _count += 32 - n;
            return v;
        }
        const uint32_t v = _buf & mask;
        _buf >>= n;
        _count -= n;
        return v;
    }

private:
    uint32_t word() {
        uint32_t w = 0;
        for (int k = 0; k < 4; k++, _pos++)
            if (_pos < _in.size())
                w |= uint32_t(_in[_pos]) << (8 * k);
        return w;
    }

    const std::vector<uint8_t> &_in;
    size_t _pos;
    uint32_t _buf = 0;
    int _count = 0;
};

// An adaptive model of up to `symbols` values, each first sent through
// an escape (index 0) and then given its own index. Counts are turned
// into a table of intervals out of 1 << 14 only now and then, when the
// total passes a threshold that grows each time.
class Model {
public:
    static constexpr int kNew = -1; // decode(): a new value, for the caller to read and set()

    Model(uint32_t largest, uint32_t symbols) : _symbols(uint16_t(symbols)) {
        const size_t n = (symbols + 5) & ~3u;
        _cdf.assign(n + 6, 0);
        _freq.assign(n, 0);
        _values.assign(n, 0);
        for (int i = 1; i <= 5; i++)
            _cdf[size_t(i)] = 0xc000;
        _cdf[0] = 0x8000;
        _freq[0] = 4;
        _total = 4;
        _threshold = 8;
        _step = 4;
        _limit = uint16_t(std::clamp<uint32_t>(largest << 5, 0x100, 0x3b38));
        const uint32_t s = 2 * largest;
        _stepLimit = uint16_t(s < 0x80 ? 0x80 : std::min<uint32_t>(s, _limit / 2u - 0x20));
    }

    // The next value, or kNew.
    int decode(RangeDecoder &rc) {
        if (_total >= _threshold) {
            if (_threshold >= _limit)
                rescale();
            rebuild();
            for (size_t i = size_t(_inTable) + 2; i <= size_t(_inTable) + 6; i++)
                _cdf[i] = 0xc000;
        }
        const uint32_t v = rc.decodeShift(14) + 0x8000;
        // The interval holding v: the last start at or below it.
        size_t i = 0;
        while (i < _inTable && _cdf[i + 1] <= v)
            i++;
        rc.update(_cdf[i] - 0x8000u, uint32_t(_cdf[i + 1] - _cdf[i]), 0x4000);
        _freq[i]++;
        _total++;
        if (i > 0)
            return _values[i];
        // The escape: one of the values added since the table was built,
        // or a new one.
        if (_used != _inTable && rc.uniform(2)) {
            const size_t k = rc.uniform(uint32_t(_used - _inTable)) + _inTable + 1u;
            _freq[k] += 2;
            _total += 2;
            return _values[k];
        }
        _used++;
        _freq[_used] += 2;
        _total += 2;
        if (_used == _symbols) {
            _total = uint16_t(_total - _freq[0]);
            _freq[0] = 0;
        }
        return kNew;
    }
    // The value of the symbol decode() just added.
    void set(uint32_t v) { _values[_used] = uint16_t(v); }

private:
    void rebuild() {
        const uint32_t scale = 0x20000u / std::max<uint32_t>(_total, 1);
        uint32_t at = 0x8000 + ((_freq[0] * scale) >> 3);
        _cdf[0] = 0x8000;
        _cdf[1] = uint16_t(at);
        for (size_t i = 1; i <= _used; i++) {
            at += (_freq[i] * scale) >> 3;
            _cdf[i + 1] = uint16_t(at);
        }
        if (2u * _step > _stepLimit) {
            _threshold = uint16_t(_total + _stepLimit);
        } else {
            _step = uint16_t(_step * 2);
            _threshold = uint16_t(_total + _step);
        }
        _inTable = _used;
        _cdf[size_t(_used) + 1] = 0xc000;
    }
    // Halves the counts, dropping values seen at most once (the last
    // value takes a dropped one's place), and moves the commonest value
    // to the end.
    void rescale() {
        _freq[0] >>= 1;
        _total = _freq[0];
        uint32_t best = 0;
        size_t bestAt = 0;
        for (size_t i = 1; i <= _used; i++) {
            bool dropped = false;
            while (_freq[i] <= 1) {
                if (i >= _used) {
                    _freq[i] = 0;
                    _used--;
                    dropped = true;
                    break;
                }
                _freq[i] = _freq[_used];
                _freq[_used] = 0;
                _values[i] = _values[_used];
                _used--;
            }
            if (dropped)
                break;
            _freq[i] >>= 1;
            _total = uint16_t(_total + _freq[i]);
            if (_freq[i] > best) {
                best = _freq[i];
                bestAt = i;
            }
        }
        if (best && _used && bestAt != _used) {
            std::swap(_freq[_used], _freq[bestAt]);
            std::swap(_values[_used], _values[bestAt]);
        }
        if (_used != _symbols && _freq[0] == 0) {
            _freq[0] = 1;
            _total++;
        }
    }

    std::vector<uint16_t> _cdf;
    std::vector<uint16_t> _freq;
    std::vector<uint16_t> _values;
    uint16_t _total = 0;
    uint16_t _threshold = 0;
    uint16_t _stepLimit = 0;
    uint16_t _step = 0;
    uint16_t _limit = 0;
    uint16_t _used = 0;
    uint16_t _inTable = 0;
    uint16_t _symbols = 0;
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

void fill(int16_t *p, size_t stride, uint32_t w, uint32_t h, uint32_t value) {
    for (uint32_t y = 0; y < h; y++)
        std::fill(p + y * stride, p + y * stride + w, int16_t(value));
}

struct Streams {
    RangeDecoder &rc;
    BitReader &bits;
};

// A value from `model` (a new one sent uniformly), with a sign bit if
// it isn't 0.
int32_t signedValue(Streams &s, Model &model, uint32_t symbols) {
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
void decodeLowest(Streams &s, int16_t *p, size_t stride, uint32_t w, uint32_t h) {
    if (s.bits.bit()) {
        fill(p, stride, w, h, s.bits.bits(16));
        return;
    }
    const uint32_t largest = s.bits.bits(16);
    const uint32_t symbols = largest + 1;
    Model model(largest, symbols);
    int32_t left = int32_t(s.bits.bits(16));
    p[0] = int16_t(left);
    for (uint32_t x = 1; x < w; x++) {
        left += signedValue(s, model, symbols);
        p[x] = int16_t(left);
    }
    for (uint32_t y = 1; y < h; y++) {
        int16_t *row = p + y * stride;
        const int16_t *above = row - stride;
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
void decodeBand(Streams &s, int16_t *p, size_t stride, uint32_t w, uint32_t h) {
    static const uint32_t kLongRuns[4] = {0x80, 0x100, 0x200, 0x400};
    static const uint32_t kLongZeros[4] = {0x200, 0x400, 0x800, 0xc00};
    const int32_t quant = int32_t(s.bits.bits(16));
    if (s.bits.bit()) {
        fill(p, stride, w, h, s.bits.bits(16) * uint32_t(quant));
        return;
    }
    const uint32_t largest = s.bits.bits(16);
    const uint32_t symbols = largest + 1;
    std::vector<Model> models(size_t(bitLength(largest * uint32_t(quant))) + 1, Model(largest, symbols));
    Model runs(63, 64);
    Model zeroRuns(255, 256);
    auto value = [&](uint32_t context) {
        Model &model = models[std::min(size_t(bitLength(context)), models.size() - 1)];
        const int32_t v = signedValue(s, model, symbols);
        return int32_t(uint32_t(v) * uint32_t(quant));
    };

    // As the DLL walks it: `up` points at the value above and to the
    // right of the next one (on the first row, the one before it); `a`
    // is above, `b` above-left, `c` to the left.
    int16_t *out = p;
    int32_t first = int32_t(s.rc.uniform(symbols));
    if (first && s.bits.bit())
        first = -first;
    first = int32_t(uint32_t(first) * uint32_t(quant));
    *out = int16_t(first);
    const int16_t *up = out++;
    int32_t a = first;
    int32_t b = first;
    int32_t c = first;
    uint32_t rows = h;
    uint32_t left = w - 1;
    uint32_t coded = 0;
    uint32_t zeros = 0;
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
void decodeRowFlags(RangeDecoder &rc, uint8_t *flags, uint32_t count) {
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
size_t decodePlane(const std::vector<uint8_t> &in, size_t at, int16_t *p, uint32_t w, uint32_t h, uint8_t *rowFlags) {
    auto u32 = [&](size_t i) {
        uint32_t v = 0;
        for (int k = 0; k < 4; k++)
            if (i + size_t(k) < in.size())
                v |= uint32_t(in[i + size_t(k)]) << (8 * k);
        return v;
    };
    const uint32_t rangeBytes = u32(at);
    const uint32_t bitBytes = u32(at + 4);
    RangeDecoder rc(in, at + 8);
    BitReader bits(in, at + 8 + rangeBytes);
    Streams s{rc, bits};
    decodeLowest(s, p, size_t(w) * 16, w >> 4, h >> 4);
    for (uint32_t scale = 16; scale >= 2; scale /= 2) {
        const size_t stride = size_t(w) * scale;
        const size_t below = stride / 2;
        const uint32_t bw = w / scale;
        const uint32_t bh = h / scale;
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
template<typename Lo, typename Hi, typename Put>
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
void inverseLevel(int16_t *p, size_t stride, uint32_t w, uint32_t h, const uint8_t *rowFlags,
                  std::vector<int16_t> &scratch) {
    scratch.resize(stride * h);
    const uint32_t m = w / 2;
    // A row: lows in its left half, highs in its right.
    for (uint32_t y = 0; y < h; y++) {
        const int16_t *src = p + y * stride;
        int16_t *dst = scratch.data() + y * stride;
        auto lo = [&](int32_t i) { return int32_t(src[i]); };
        auto hi = [&](int32_t i) { return int32_t(src[m + uint32_t(i)]); };
        auto put = [&](int32_t i, int16_t v) { dst[i] = v; };
        synthesize(w, w >= 12, lo, hi, !rowFlags || rowFlags[y], put);
    }
    // A column: lows in its even rows, highs in its odd ones.
    for (uint32_t x = 0; x < w; x++) {
        const int16_t *src = scratch.data() + x;
        int16_t *dst = p + x;
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

Error decodeBinkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t> &in,
                        std::vector<uint8_t> &rgba) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Bad texture size");
    // Bit 2 picks the codec, bit 1 how the planes become colours.
    if (!(flags & 4) || (flags & 2))
        return Error::fail("Bink texture kind " + std::to_string(flags) + " isn't supported");
    if (width * height <= 256)
        return Error::fail("Small Bink textures (stored raw) aren't supported");
    const uint32_t w = (uint32_t(width) + 15) & ~15u;
    const uint32_t h = (uint32_t(height) + 15) & ~15u;
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
            const int32_t luma = plane[0][i];
            const int32_t co = plane[1][i];
            const int32_t cg = plane[2][i];
            const int32_t g = luma - (co + cg) / 4;
            uint8_t *px = &rgba[(size_t(y) * size_t(width) + x) * 4];
            px[0] = clampByte(co + g);
            px[1] = clampByte(g);
            px[2] = clampByte(cg + g);
            px[3] = clampByte(plane[3][i]);
        }
    return Error::success();
}

} // namespace reluva::awe
