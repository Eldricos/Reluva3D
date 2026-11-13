// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How Bink movies are laid out and decoded follows the published
// descriptions of the format and the GPL decoders that document it
// (FFmpeg's, as carried by ScummVM); this is new code written from that
// description, checked against FFmpeg's output (docs/common/video/bink.md).

#include "video/bink.h"

#include "video/bink_tables.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace reluva::bink {

namespace {

enum BlockType { kSkip, kScaled, kMotion, kRunBlock, kResidue, kIntra, kFill, kInter, kPatternBlock, kRaw };

constexpr double kPi = 3.14159265358979323846; // not M_PI: MSVC only has it with _USE_MATH_DEFINES
constexpr uint32_t kAlphaFlag = 0x00100000;
constexpr uint16_t kAudioDct = 0x1000;
constexpr uint16_t kAudioStereo = 0x2000;

uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

int log2Floor(uint32_t v) {
    int n = 0;
    while (v >>= 1)
        ++n;
    return n;
}

uint8_t clampByte(int v) {
    return uint8_t(std::clamp(v, 0, 255));
}

// The 8x8 inverse DCT the format is coded for, in its fixed-point form
// (constants 1/sqrt(2) and the rotations, << 11).
constexpr int kA1 = 2896;
constexpr int kA2 = 2217;
constexpr int kA3 = 3784;
constexpr int kA4 = -5352;

template<typename Out, typename Munge>
void idct8(const int32_t *s, int step, Out *d, int dstep, Munge munge) {
    const int a0 = s[0] + s[4 * step];
    const int a1 = s[0] - s[4 * step];
    const int a2 = s[2 * step] + s[6 * step];
    const int a3 = (kA1 * (s[2 * step] - s[6 * step])) >> 11;
    const int a4 = s[5 * step] + s[3 * step];
    const int a5 = s[5 * step] - s[3 * step];
    const int a6 = s[1 * step] + s[7 * step];
    const int a7 = s[1 * step] - s[7 * step];
    const int b0 = a4 + a6;
    const int b1 = (kA3 * (a5 + a7)) >> 11;
    const int b2 = ((kA4 * a5) >> 11) - b0 + b1;
    const int b3 = ((kA1 * (a6 - a4)) >> 11) - b2;
    const int b4 = ((kA2 * a7) >> 11) + b3 - b1;
    d[0] = munge(a0 + a2 + b0);
    d[1 * dstep] = munge(a1 + a3 - a2 + b2);
    d[2 * dstep] = munge(a1 - a3 + a2 + b3);
    d[3 * dstep] = munge(a0 - a2 - b4);
    d[4 * dstep] = munge(a0 - a2 + b4);
    d[5 * dstep] = munge(a1 - a3 + a2 - b3);
    d[6 * dstep] = munge(a1 + a3 - a2 - b2);
    d[7 * dstep] = munge(a0 + a2 - b0);
}

// Columns, then rows (rounded, / 256) into `out` (64 values, row by row).
void idct(const int32_t in[64], int32_t out[64]) {
    int32_t tmp[64];
    for (int c = 0; c < 8; ++c) {
        const int32_t *s = in + c;
        if (!(s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56])) {
            for (int r = 0; r < 8; ++r)
                tmp[c + 8 * r] = s[0];
        } else {
            idct8(s, 8, tmp + c, 8, [](int v) { return v; });
        }
    }
    for (int r = 0; r < 8; ++r)
        idct8(tmp + 8 * r, 1, out + 8 * r, 1, [](int v) { return (v + 0x7f) >> 8; });
}

// An inverse FFT (e^+i) in place, n a power of two.
void inverseFft(std::vector<std::complex<double>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / double(len);
        const std::complex<double> w(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> wn(1, 0);
            for (size_t k = 0; k < len / 2; ++k, wn *= w) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// DCT-III of n values in place: x[m] = X[0] / 2 + sum X[k] cos(pi k (m + 1/2) / n).
void dct3(float *x, int n) {
    std::vector<std::complex<double>> z(size_t(2 * n));
    for (int k = 0; k < n; ++k)
        // (Not std::polar: its magnitude must not be negative.)
        z[size_t(k)] = double(k == 0 ? x[0] * 0.5f : x[k]) * std::exp(std::complex<double>(0, kPi * k / (2.0 * n)));
    inverseFft(z);
    for (int m = 0; m < n; ++m)
        x[m] = float(z[size_t(m)].real());
}

// The real inverse DFT of a packed spectrum (re 0, re n/2, then re and im
// of 1 .. n/2-1, the imaginary parts as Bink stores them, negated) in
// place: x[m] = (X0 + Xn/2 (-1)^m) / 2 + sum Re(conj(Xk) e^(2 pi i k m / n)).
void inverseRealDft(float *x, int n) {
    std::vector<std::complex<double>> z(static_cast<size_t>(n));
    z[0] = x[0] * 0.5;
    z[size_t(n / 2)] = x[1] * 0.5;
    for (int k = 1; k < n / 2; ++k) {
        z[size_t(k)] = std::complex<double>(x[2 * k], -x[2 * k + 1]);
        z[size_t(n - k)] = std::conj(z[size_t(k)]);
    }
    for (int k = 1; k < n / 2; ++k) {
        z[size_t(k)] *= 0.5;
        z[size_t(n - k)] *= 0.5;
    }
    inverseFft(z);
    for (int m = 0; m < n; ++m)
        x[m] = float(z[size_t(m)].real());
}

} // namespace

// --- Bits ------------------------------------------------------------------------

uint32_t Movie::Bits::get(int n) {
    // Least significant bit first, little-endian.
    uint32_t v = 0;
    for (int i = 0; i < n; ++i, ++pos)
        if (pos < size && (data[pos >> 3] >> (pos & 7)) & 1)
            v |= 1u << i;
    return v;
}

// --- The container -------------------------------------------------------------

Error Movie::open(std::vector<uint8_t> file) {
    _file = std::move(file);
    const uint8_t *p = _file.data();
    if (_file.size() < 44 || p[0] != 'B' || p[1] != 'I' || p[2] != 'K')
        return Error::fail("Not a Bink movie");
    _revision = char(p[3]);
    if (_revision < 'f' || _revision > 'i')
        return Error::fail(std::string("Bink revision ") + _revision + " isn't supported (f to i are)");
    const uint32_t count = le32(p + 8);
    _width = int(le32(p + 20));
    _height = int(le32(p + 24));
    _rateNum = le32(p + 28);
    _rateDen = le32(p + 32);
    const uint32_t flags = le32(p + 36);
    const uint32_t tracks = le32(p + 40);
    _flags = flags;
    if (_width <= 0 || _height <= 0 || _width > 4096 || _height > 4096 || !_rateNum || !_rateDen || tracks > 256)
        return Error::fail("The Bink header is damaged");
    _alpha = (flags & kAlphaFlag) != 0;
    _swapChroma = _revision >= 'h';
    size_t at = 44 + 4 * size_t(tracks); // each track's largest decoded size, unused
    if (_file.size() < at + 8 * size_t(tracks) + 4 * (size_t(count) + 1))
        return Error::fail("The Bink header is cut short");
    _audio.clear();
    for (uint32_t t = 0; t < tracks; ++t, at += 4) {
        Audio a;
        a.rate = p[at] | p[at + 1] << 8;
        const uint16_t f = uint16_t(p[at + 2] | p[at + 3] << 8);
        a.channels = a.outChannels = (f & kAudioStereo) ? 2 : 1;
        a.dct = (f & kAudioDct) != 0;
        _audio.push_back(a);
    }
    at += 4 * size_t(tracks); // track ids
    _frames.assign(count, Frame{});
    for (uint32_t i = 0; i <= count; ++i) {
        const uint32_t v = i < count ? le32(p + at + 4 * i) : uint32_t(_file.size());
        if (i < count) {
            _frames[i].offset = v & ~1u;
            _frames[i].key = v & 1;
        }
        if (i > 0) {
            const uint32_t end = std::min<uint32_t>(v & ~1u, uint32_t(_file.size()));
            _frames[i - 1].size = end > _frames[i - 1].offset ? end - _frames[i - 1].offset : 0;
        }
    }

    // The audio: blocks of 2^9 to 2^11 samples (by rate), overlapping by a
    // sixteenth; the spectrum in bands of the critical frequencies.
    for (Audio &a : _audio) {
        int bits = a.rate < 22050 ? 9 : a.rate < 44100 ? 10 : 11;
        int rate = a.rate;
        a.frameLength = 1 << bits;
        if (!a.dct) {
            // Stereo RDFT blocks carry both channels interleaved.
            if (a.channels == 2)
                ++bits;
            rate *= a.channels;
            a.frameLength *= a.channels;
            a.channels = 1;
        }
        a.overlap = a.frameLength / 16;
        a.blockSize = (a.frameLength - a.overlap) * a.channels;
        a.root = float(2.0 / std::sqrt(double(a.frameLength)));
        const int half = (rate + 1) / 2;
        for (a.bandCount = 1; a.bandCount < 25; ++a.bandCount)
            if (half <= kCriticalFrequencies[a.bandCount - 1])
                break;
        a.bands.assign(size_t(a.bandCount + 1), 0);
        a.bands[0] = 1;
        for (int i = 1; i < a.bandCount; ++i)
            a.bands[size_t(i)] = kCriticalFrequencies[i - 1] * (a.frameLength / 2) / half;
        a.bands[size_t(a.bandCount)] = a.frameLength / 2;
        for (auto &c : a.coeffs)
            c.assign(size_t(a.frameLength), 0);
        a.previous.assign(size_t(a.overlap * a.channels), 0);
    }

    // The planes, in whole 8x8 blocks (chroma at half size).
    _lumaPitch = ((_width + 7) >> 3) * 8;
    _lumaRows = ((_height + 7) >> 3) * 8;
    _chromaPitch = ((_width + 15) >> 4) * 8;
    _chromaRows = ((_height + 15) >> 4) * 8;
    clearPictures();
    // How many bits count each bundle's values for a row.
    const size_t blocks = size_t((_width + 7) >> 3) * size_t((_height + 7) >> 3);
    for (Bundle &b : _bundles)
        b.data.assign(blocks * 64, 0);
    const int cbw[2] = {(_width + 7) >> 3, (_width + 15) >> 4};
    const int cw[2] = {_width, _width >> 1};
    for (int i = 0; i < 2; ++i) {
        const int w = std::max(cw[i], 8);
        auto bitsFor = [](int v) { return log2Floor(uint32_t(v + 511)) + 1; };
        _bundles[kBlockTypes].countBits[i] = bitsFor(w >> 3);
        _bundles[kSubBlockTypes].countBits[i] = bitsFor((w + 7) >> 4);
        _bundles[kColors].countBits[i] = bitsFor(cbw[i] * 64);
        _bundles[kIntraDC].countBits[i] = bitsFor(w >> 3);
        _bundles[kInterDC].countBits[i] = bitsFor(w >> 3);
        _bundles[kXOff].countBits[i] = bitsFor(w >> 3);
        _bundles[kYOff].countBits[i] = bitsFor(w >> 3);
        _bundles[kPattern].countBits[i] = bitsFor(cbw[i] << 3);
        _bundles[kRun].countBits[i] = bitsFor(cbw[i] * 48);
    }
    _next = 0;
    return Error::success();
}

Error Movie::nextFrame(bool &more, std::vector<int16_t> *sound) {
    more = _next < int(_frames.size());
    if (!more)
        return Error::success();
    const Frame &f = _frames[size_t(_next++)];
    if (size_t(f.offset) + f.size > _file.size())
        return Error::fail("A Bink frame runs past the end of the file");
    const uint8_t *p = _file.data() + f.offset;
    size_t left = f.size;
    // Each audio track's packet: its length, then (if any) the decoded
    // size and the blocks.
    for (size_t t = 0; t < _audio.size(); ++t) {
        if (left < 4)
            return Error::fail("A Bink frame is cut short");
        const uint32_t length = le32(p);
        p += 4;
        left -= 4;
        if (length > left)
            return Error::fail("A Bink audio packet runs past its frame");
        if (length >= 4 && t == 0 && sound)
            decodePacket(_audio[t], p, length, *sound);
        p += length;
        left -= length;
    }
    // The video: alpha, then Y and the two chroma planes, each from a
    // 32-bit boundary.
    Bits b{p, left * 8, 0};
    if (_alpha) {
        if (_revision == 'i')
            b.skip(32);
        decodePlane(b, 3, false);
    }
    if (_revision == 'i')
        b.skip(32);
    for (int i = 0; i < 3; ++i) {
        const int plane = i == 0 || !_swapChroma ? i : (i ^ 3);
        decodePlane(b, plane, i != 0);
        if (b.done())
            break;
    }
    std::swap(_cur, _old);
    return Error::success();
}

Error Movie::decodeAllAudio(std::vector<int16_t> &sound) {
    if (_audio.empty())
        return Error::success();
    for (const Frame &f : _frames) {
        if (size_t(f.offset) + f.size > _file.size() || f.size < 4)
            break;
        const uint8_t *p = _file.data() + f.offset;
        const uint32_t length = le32(p);
        if (length > f.size - 4)
            return Error::fail("A Bink audio packet runs past its frame");
        if (length >= 4)
            decodePacket(_audio[0], p + 4, length, sound);
    }
    return Error::success();
}

void Movie::clearPictures() {
    for (int i = 0; i < 4; ++i) {
        const size_t pitch = size_t(i == 0 || i == 3 ? _lumaPitch : _chromaPitch);
        // Room past the end for a scaled (16 x 16) block in the last row or
        // column of 8 x 8 ones, which reaches up to 8 rows and 8 bytes on.
        const size_t size = pitch * size_t(i == 0 || i == 3 ? _lumaRows : _chromaRows) + 8 * pitch + 8;
        _cur[size_t(i)].assign(size, i == 3 ? 255 : 0);
        _old[size_t(i)].assign(size, i == 3 ? 255 : 0);
    }
}

void Movie::rewind() {
    _next = 0;
    clearPictures();
    _lastColorHigh = 0;
}

uint32_t Movie::largestFrame() const {
    uint32_t most = 0;
    for (const Frame &f : _frames)
        most = std::max(most, f.size);
    return most;
}

std::vector<Movie::Track> Movie::audioTracks() const {
    std::vector<Track> out;
    for (const Audio &a : _audio)
        out.push_back({a.rate, a.outChannels, a.dct});
    return out;
}

void Movie::decodePacket(Audio &a, const uint8_t *packet, uint32_t length, std::vector<int16_t> &sound) {
    // The packet starts with the size it decodes to, in bytes: the last
    // block of a movie runs past it, and is cut there (so the sound lasts
    // as long as the frames).
    const uint32_t decoded = le32(packet);
    const size_t before = sound.size();
    Bits b{packet + 4, size_t(length - 4) * 8, 0};
    decodeAudio(a, b, &sound);
    if (decoded > 0 && sound.size() > before + decoded / 2)
        sound.resize(before + decoded / 2);
}

void Movie::toRgba(std::vector<uint8_t> &out, Rgb how) const {
    // The newest frame is in old_ once swapped.
    const auto &y = _old[0];
    const auto &u = _old[1];
    const auto &v = _old[2];
    const auto &a = _old[3];
    out.resize(size_t(_width) * size_t(_height) * 4);
    if (how == Rgb::kBinkw32) {
        // As binkw32.dll converts (its MMX path, 0x30013d00 in the AWE
        // games' DLL, which any machine since the late 1990s takes):
        // each term in fixed point and truncated on its own, before they're
        // added and clamped. Y' = ((Y - 16, not below 0) * 4 * 0x4a85) >> 16;
        // the chroma terms from tables of (c - 128) times 2.01723, 0.39177,
        // 0.81300 and 1.59603, in 1/32768ths, truncated toward zero (built
        // at 0x3000a990). See docs/common/video/bink.md.
        struct Tables {
            int b[256], gu[256], gv[256], r[256];
            Tables() {
                for (int c = 0; c < 256; c++) {
                    b[c] = (-0x811a80 + 0x10235 * c) / 32768;
                    gu[c] = (0x191280 - 0x3225 * c) / 32768;
                    gv[c] = (0x340780 - 0x680f * c) / 32768;
                    r[c] = (-0x662580 + 0xcc4b * c) / 32768;
                }
            }
        };
        static const Tables kT;
        for (int row = 0; row < _height; ++row)
            for (int col = 0; col < _width; ++col) {
                const int l =
                    ((std::max(y[size_t(row) * size_t(_lumaPitch) + size_t(col)] - 16, 0) << 2) * 0x4a85) >> 16;
                const size_t c = size_t(row / 2) * size_t(_chromaPitch) + size_t(col / 2);
                const int U = u[c];
                const int V = v[c];
                uint8_t *o = &out[(size_t(row) * size_t(_width) + size_t(col)) * 4];
                o[0] = clampByte(l + kT.r[V]);
                o[1] = clampByte(l + kT.gu[U] + kT.gv[V]);
                o[2] = clampByte(l + kT.b[U]);
                o[3] = _alpha ? a[size_t(row) * size_t(_lumaPitch) + size_t(col)] : 255;
            }
        return;
    }
    for (int row = 0; row < _height; ++row)
        for (int col = 0; col < _width; ++col) {
            const int Y = (y[size_t(row) * size_t(_lumaPitch) + size_t(col)] - 16) * 298;
            const size_t c = size_t(row / 2) * size_t(_chromaPitch) + size_t(col / 2);
            const int U = u[c] - 128;
            const int V = v[c] - 128;
            uint8_t *o = &out[(size_t(row) * size_t(_width) + size_t(col)) * 4];
            o[0] = clampByte((Y + 409 * V + 128) >> 8);
            o[1] = clampByte((Y - 100 * U - 208 * V + 128) >> 8);
            o[2] = clampByte((Y + 516 * U + 128) >> 8);
            o[3] = _alpha ? a[size_t(row) * size_t(_lumaPitch) + size_t(col)] : 255;
        }
}

// --- Video ----------------------------------------------------------------------

void Movie::readTree(Bits &b, Tree &t) {
    // Which of the 16 code trees, and in what order its symbols come: a
    // list of the first few (the rest after in order), or a merge of
    // halves, quarters... as told bit by bit.
    t.index = int(b.get(4));
    for (int i = 0; i < 16; ++i)
        t.symbols[size_t(i)] = uint8_t(i);
    if (t.index == 0)
        return;
    if (b.bit()) {
        bool listed[16] = {};
        const int length = int(b.get(3));
        for (int i = 0; i <= length; ++i) {
            t.symbols[size_t(i)] = uint8_t(b.get(4));
            listed[t.symbols[size_t(i)]] = true;
        }
        int n = length;
        for (int i = 0; i < 16; ++i)
            if (!listed[i] && n < 15) // a damaged list can repeat symbols
                t.symbols[size_t(++n)] = uint8_t(i);
        return;
    }
    std::array<uint8_t, 16> in = t.symbols;
    std::array<uint8_t, 16> out{};
    const int depth = int(b.get(2));
    for (int level = 0; level <= depth; ++level) {
        const int size = 1 << level;
        for (int j = 0; j < 16; j += size * 2) {
            int a = j;
            int c = j + size;
            int o = j;
            int left = size;
            int right = size;
            while (left && right) {
                if (!b.bit()) {
                    out[size_t(o++)] = in[size_t(a++)];
                    --left;
                } else {
                    out[size_t(o++)] = in[size_t(c++)];
                    --right;
                }
            }
            while (left--)
                out[size_t(o++)] = in[size_t(a++)];
            while (right--)
                out[size_t(o++)] = in[size_t(c++)];
        }
        std::swap(in, out);
    }
    t.symbols = in;
}

int Movie::symbol(Bits &b, const Tree &t) {
    uint32_t code = 0;
    for (int length = 1; length <= 8; ++length) {
        code |= b.bit() << (length - 1);
        for (int s = 0; s < 16; ++s)
            if (kTreeLengths[t.index][s] == length && kTreeCodes[t.index][s] == code)
                return t.symbols[size_t(s)];
    }
    return 0;
}

void Movie::readBundle(Bits &b, Source s) {
    if (s == kColors) {
        for (Tree &t : _colorHigh)
            readTree(b, t);
        _lastColorHigh = 0;
    }
    if (s != kIntraDC && s != kInterDC)
        readTree(b, _bundles[s].tree);
    _bundles[s].decoded = _bundles[s].read = 0;
    _bundles[s].finished = false;
}

uint32_t Movie::bundleCount(Bits &b, Bundle &bundle) {
    // More values only once those decoded are used up.
    if (bundle.finished || bundle.decoded > bundle.read)
        return 0;
    const uint32_t n = b.get(bundle.bits);
    if (!n)
        bundle.finished = true;
    return std::min<uint32_t>(n, uint32_t(bundle.data.size() - bundle.decoded));
}

int Movie::value(Source s) {
    Bundle &b = _bundles[s];
    return b.read < b.data.size() ? b.data[b.read++] : 0;
}

void Movie::decodePlane(Bits &b, int planeIndex, bool chroma) {
    const int blocksW = chroma ? (_width + 15) >> 4 : (_width + 7) >> 3;
    const int blocksH = chroma ? (_height + 15) >> 4 : (_height + 7) >> 3;
    const int pitch = blocksW * 8;
    uint8_t *const dst = _cur[size_t(planeIndex)].data();
    const uint8_t *const prev = _old[size_t(planeIndex)].data();
    const int size = pitch * blocksH * 8;
    for (int s = 0; s < kSourceCount; ++s) {
        _bundles[size_t(s)].bits = _bundles[size_t(s)].countBits[chroma ? 1 : 0];
        readBundle(b, Source(s));
    }
    auto push = [](Bundle &bundle, int v) {
        if (bundle.decoded < bundle.data.size())
            bundle.data[bundle.decoded++] = int16_t(v);
    };
    auto signedValue = [&](int v) { return v && b.bit() ? -v : v; };

    for (int by = 0; by < blocksH; ++by) {
        // This row's values, for those bundles that need more.
        for (Source s : {kBlockTypes, kSubBlockTypes}) {
            Bundle &bundle = _bundles[s];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                if (b.bit()) {
                    const int v = int(b.get(4));
                    while (bundle.decoded < end)
                        push(bundle, v);
                } else {
                    int last = 0;
                    static const int kRuns[4] = {4, 8, 12, 32};
                    while (bundle.decoded < end) {
                        const int v = symbol(b, bundle.tree);
                        if (v < 12) {
                            last = v;
                            push(bundle, v);
                        } else {
                            for (int k = 0; k < kRuns[v - 12] && bundle.decoded < bundle.data.size(); ++k)
                                push(bundle, last);
                        }
                    }
                }
            }
        }
        {
            // Colours: a high nibble from a tree chosen by the last one,
            // then a low nibble (before revision i, signed around 128).
            Bundle &bundle = _bundles[kColors];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                auto colour = [&]() {
                    _lastColorHigh = symbol(b, _colorHigh[size_t(_lastColorHigh)]);
                    int v = (_lastColorHigh << 4) | symbol(b, bundle.tree);
                    if (_revision != 'i') {
                        const int sign = (v & 0x80) ? -1 : 0;
                        v = (((v & 0x7f) ^ sign) - sign + 0x80) & 0xff;
                    }
                    return v;
                };
                if (b.bit()) {
                    const int v = colour();
                    while (bundle.decoded < end)
                        push(bundle, v);
                } else {
                    while (bundle.decoded < end)
                        push(bundle, colour());
                }
            }
        }
        {
            Bundle &bundle = _bundles[kPattern];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                while (bundle.decoded < end) {
                    const int lo = symbol(b, bundle.tree);
                    push(bundle, lo | symbol(b, bundle.tree) << 4);
                }
            }
        }
        for (Source s : {kXOff, kYOff}) {
            Bundle &bundle = _bundles[s];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                if (b.bit()) {
                    const int v = signedValue(int(b.get(4)));
                    while (bundle.decoded < end)
                        push(bundle, v);
                } else {
                    while (bundle.decoded < end)
                        push(bundle, signedValue(symbol(b, bundle.tree)));
                }
            }
        }
        for (Source s : {kIntraDC, kInterDC}) {
            // The first in 11 bits (inter: 10 and a sign), then deltas in
            // groups of 8, each group's width in 4 bits.
            Bundle &bundle = _bundles[s];
            uint32_t n = bundleCount(b, bundle);
            if (!n)
                continue;
            int v = s == kIntraDC ? int(b.get(11)) : signedValue(int(b.get(10)));
            push(bundle, v);
            --n;
            for (uint32_t i = 0; i < n; i += 8) {
                const uint32_t group = std::min<uint32_t>(n - i, 8);
                const int width = int(b.get(4));
                for (uint32_t j = 0; j < group; ++j) {
                    if (width)
                        v += signedValue(int(b.get(width)));
                    push(bundle, std::clamp(v, -32768, 32767));
                }
            }
        }
        {
            Bundle &bundle = _bundles[kRun];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                if (b.bit()) {
                    const int v = int(b.get(4));
                    while (bundle.decoded < end)
                        push(bundle, v);
                } else {
                    while (bundle.decoded < end)
                        push(bundle, symbol(b, bundle.tree));
                }
            }
        }

        for (int bx = 0; bx < blocksW; ++bx) {
            const int at = by * 8 * pitch + bx * 8;
            uint8_t *d = dst + at;
            const uint8_t *old = prev + at;
            int type = value(kBlockTypes);
            // A 16x16 block's second row of 8x8 blocks: done already.
            if ((by & 1) && type == kScaled) {
                ++bx;
                continue;
            }
            auto copyFrom = [&](const uint8_t *src, int n) {
                for (int r = 0; r < n; ++r)
                    std::memcpy(d + r * pitch, src + r * pitch, size_t(n));
            };
            auto motion = [&]() {
                const int dx = int8_t(value(kXOff));
                const int dy = int8_t(value(kYOff));
                const int from = at + dy * pitch + dx;
                if (from >= 0 && from + 7 * pitch + 8 <= size)
                    copyFrom(prev + from, 8);
                else
                    copyFrom(old, 8);
            };
            // Fills an 8x8 block, or (scaled) a 16x16 one at half resolution.
            auto put = [&](int i, int v, bool scaled) {
                const int x = i & 7;
                const int y = i >> 3;
                if (!scaled) {
                    d[y * pitch + x] = uint8_t(v);
                } else {
                    uint8_t *q = d + 2 * y * pitch + 2 * x;
                    q[0] = q[1] = q[pitch] = q[pitch + 1] = uint8_t(v);
                }
            };
            auto runs = [&](bool scaled) {
                const uint8_t *scan = kPatterns[b.get(4)];
                int i = 0;
                do {
                    const int run = value(kRun) + 1;
                    i += run;
                    if (i > 64)
                        break;
                    if (b.bit()) {
                        const int v = value(kColors);
                        for (int j = 0; j < run; ++j)
                            put(*scan++, v, scaled);
                    } else {
                        for (int j = 0; j < run; ++j)
                            put(*scan++, value(kColors), scaled);
                    }
                } while (i < 63);
                if (i == 63)
                    put(*scan, value(kColors), scaled);
            };
            auto pattern = [&](bool scaled) {
                const int c0 = value(kColors);
                const int c1 = value(kColors);
                for (int y = 0; y < 8; ++y) {
                    int v = value(kPattern);
                    for (int x = 0; x < 8; ++x, v >>= 1)
                        put(y * 8 + x, (v & 1) ? c1 : c0, scaled);
                }
            };
            auto raw = [&](bool scaled) {
                for (int i = 0; i < 64; ++i)
                    put(i, value(kColors), scaled);
            };
            auto intra = [&](bool scaled) {
                int32_t block[64] = {};
                int32_t out[64];
                block[0] = value(kIntraDC);
                readDCTCoefficients(b, block, true);
                idct(block, out);
                for (int i = 0; i < 64; ++i)
                    put(i, out[i], scaled);
            };

            switch (type) {
            case kSkip:
                copyFrom(old, 8);
                break;
            case kScaled: {
                const int sub = value(kSubBlockTypes);
                switch (sub) {
                case kRunBlock:
                    runs(true);
                    break;
                case kIntra:
                    intra(true);
                    break;
                case kFill: {
                    const int v = value(kColors);
                    for (int r = 0; r < 16; ++r)
                        std::memset(d + r * pitch, v, 16);
                    break;
                }
                case kPatternBlock:
                    pattern(true);
                    break;
                case kRaw:
                    raw(true);
                    break;
                default:
                    break;
                }
                ++bx;
                break;
            }
            case kMotion:
                motion();
                break;
            case kRunBlock:
                runs(false);
                break;
            case kResidue: {
                motion();
                const int masks = int(b.get(7));
                int32_t block[64] = {};
                readResidue(b, block, masks);
                for (int i = 0; i < 64; ++i)
                    d[(i >> 3) * pitch + (i & 7)] = uint8_t(d[(i >> 3) * pitch + (i & 7)] + block[i]);
                break;
            }
            case kIntra:
                intra(false);
                break;
            case kFill: {
                const int v = value(kColors);
                for (int r = 0; r < 8; ++r)
                    std::memset(d + r * pitch, v, 8);
                break;
            }
            case kInter: {
                motion();
                int32_t block[64] = {};
                int32_t out[64];
                block[0] = value(kInterDC);
                readDCTCoefficients(b, block, false);
                idct(block, out);
                for (int i = 0; i < 64; ++i)
                    d[(i >> 3) * pitch + (i & 7)] = uint8_t(d[(i >> 3) * pitch + (i & 7)] + out[i]);
                break;
            }
            case kPatternBlock:
                pattern(false);
                break;
            case kRaw:
                raw(false);
                break;
            default:
                break;
            }
        }
    }
    b.align32();
}

void Movie::readDCTCoefficients(Bits &b, int32_t block[64], bool intra) {
    // Coefficients by bit planes, from the top: groups of four split as
    // the bits say (a list of groups to look at, and single ones found).
    int found[64];
    int foundCount = 0;
    int listStart = 64;
    int listEnd = 64;
    int coef[128];
    int mode[128];
    auto add = [&](int c, int m) {
        coef[listEnd] = c;
        mode[listEnd++] = m;
    };
    add(4, 0);
    add(24, 0);
    add(44, 0);
    add(1, 3);
    add(2, 3);
    add(3, 3);
    int bits = int(b.get(4)) - 1;
    auto readValue = [&](int bitsNow, int mask) {
        if (!bitsNow)
            return 1 - int(b.bit() << 1);
        int t = int(b.get(bitsNow)) | mask;
        return b.bit() ? -t : t;
    };
    for (int mask = bits >= 0 ? 1 << bits : 0; bits >= 0; mask >>= 1, --bits) {
        int pos = listStart;
        while (pos < listEnd) {
            if (!(mode[pos] | coef[pos]) || !b.bit()) {
                ++pos;
                continue;
            }
            int c = coef[pos];
            const int m = mode[pos];
            if (m == 0 || m == 2) {
                if (m == 0) {
                    coef[pos] = c + 4;
                    mode[pos] = 1;
                } else {
                    coef[pos] = 0;
                    mode[pos++] = 0;
                }
                for (int i = 0; i < 4; ++i, ++c) {
                    if (b.bit()) {
                        coef[--listStart] = c;
                        mode[listStart] = 3;
                    } else {
                        block[kScan[c]] = readValue(bits, mask);
                        found[foundCount++] = c;
                    }
                }
            } else if (m == 1) {
                mode[pos] = 2;
                for (int i = 0; i < 3; ++i) {
                    c += 4;
                    add(c, 2);
                }
            } else if (m == 3) {
                block[kScan[c]] = readValue(bits, mask);
                found[foundCount++] = c;
                coef[pos] = 0;
                mode[pos++] = 0;
            }
        }
    }
    // Dequantised (<< 11) with the block's quantiser.
    const int q = int(b.get(4));
    const int32_t *quant = intra ? kIntraQuant[q] : kInterQuant[q];
    block[0] = (block[0] * quant[0]) >> 11;
    for (int i = 0; i < foundCount; ++i) {
        const int c = found[i];
        block[kScan[c]] = (block[kScan[c]] * quant[c]) >> 11;
    }
}

void Movie::readResidue(Bits &b, int32_t block[64], int masks) {
    // The difference after motion, bit plane by bit plane from a mask of
    // 1 << (3 bits): first refining those found, then finding more as
    // for the DCT; at most `masks` values in all.
    int found[64];
    int foundCount = 0;
    int listStart = 64;
    int listEnd = 64;
    int coef[128];
    int mode[128];
    auto add = [&](int c, int m) {
        coef[listEnd] = c;
        mode[listEnd++] = m;
    };
    add(4, 0);
    add(24, 0);
    add(44, 0);
    add(0, 2);
    auto set = [&](int c, int mask) {
        found[foundCount++] = kScan[c];
        block[kScan[c]] = b.bit() ? -mask : mask;
        return --masks >= 0;
    };
    for (int mask = 1 << b.get(3); mask; mask >>= 1) {
        for (int i = 0; i < foundCount; ++i) {
            if (!b.bit())
                continue;
            int32_t &v = block[found[i]];
            v += v < 0 ? -mask : mask;
            if (--masks < 0)
                return;
        }
        int pos = listStart;
        while (pos < listEnd) {
            if (!(coef[pos] | mode[pos]) || !b.bit()) {
                ++pos;
                continue;
            }
            int c = coef[pos];
            const int m = mode[pos];
            if (m == 0 || m == 2) {
                if (m == 0) {
                    coef[pos] = c + 4;
                    mode[pos] = 1;
                } else {
                    coef[pos] = 0;
                    mode[pos++] = 0;
                }
                for (int i = 0; i < 4; ++i, ++c) {
                    if (b.bit()) {
                        coef[--listStart] = c;
                        mode[listStart] = 3;
                    } else if (!set(c, mask)) {
                        return;
                    }
                }
            } else if (m == 1) {
                mode[pos] = 2;
                for (int i = 0; i < 3; ++i) {
                    c += 4;
                    add(c, 2);
                }
            } else if (m == 3) {
                coef[pos] = 0;
                mode[pos++] = 0;
                if (!set(c, mask))
                    return;
            }
        }
    }
}

// --- Audio ----------------------------------------------------------------------

void Movie::readAudioCoefficients(Audio &a, Bits &b, float *coeffs) {
    // Two floats (5-bit exponent, 23-bit mantissa, sign), each band's
    // quantiser, then runs of 8 (or a coded multiple) coefficients of a
    // given width.
    auto getFloat = [&]() {
        const int power = int(b.get(5));
        float f = std::ldexp(float(b.get(23)), power - 23);
        return b.bit() ? -f : f;
    };
    coeffs[0] = getFloat() * a.root;
    coeffs[1] = getFloat() * a.root;
    float quant[25];
    for (int i = 0; i < a.bandCount; ++i)
        quant[i] = std::exp(float(std::min<uint32_t>(b.get(8), 95)) * 0.15289164787221953823f) * a.root;
    static const int kRunLengths[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};
    float q = 0;
    int k = 0;
    int i = 2;
    while (i < a.frameLength) {
        int j = b.bit() ? i + kRunLengths[b.get(4)] * 8 : i + 8;
        j = std::min(j, a.frameLength);
        const int width = int(b.get(4));
        if (!width) {
            std::fill(coeffs + i, coeffs + j, 0.0f);
            i = j;
            while (k < a.bandCount && a.bands[size_t(k)] * 2 < i)
                q = quant[k++];
        } else {
            for (; i < j; ++i) {
                if (k < a.bandCount && a.bands[size_t(k)] * 2 == i)
                    q = quant[k++];
                const int c = int(b.get(width));
                coeffs[i] = c ? (b.bit() ? -q * float(c) : q * float(c)) : 0.0f;
            }
        }
    }
}

void Movie::decodeAudio(Audio &a, Bits &b, std::vector<int16_t> *sound) {
    // Blocks from each 32-bit boundary: a transform per channel, then the
    // first sixteenth faded in over the last block's tail.
    std::vector<int16_t> out(size_t(a.frameLength * a.channels));
    while (!b.done()) {
        if (a.dct)
            b.skip(2);
        for (int c = 0; c < a.channels; ++c) {
            float *coeffs = a.coeffs[c].data();
            readAudioCoefficients(a, b, coeffs);
            if (a.dct) {
                coeffs[0] *= 2.0f;
                dct3(coeffs, a.frameLength);
            } else {
                inverseRealDft(coeffs, a.frameLength);
            }
        }
        for (int j = 0; j < a.frameLength; ++j)
            for (int c = 0; c < a.channels; ++c)
                out[size_t(j * a.channels + c)] =
                    int16_t(std::clamp<int>(int(std::floor(a.coeffs[c][size_t(j)] + 0.5f)), -32768, 32767));
        const int count = a.overlap * a.channels;
        if (!a.first) {
            const int shift = log2Floor(uint32_t(count));
            for (int j = 0; j < count; ++j)
                out[size_t(j)] = int16_t((a.previous[size_t(j)] * (count - j) + out[size_t(j)] * j) >> shift);
        }
        std::copy(out.begin() + a.blockSize, out.begin() + a.blockSize + count, a.previous.begin());
        a.first = false;
        if (sound)
            sound->insert(sound->end(), out.begin(), out.begin() + a.blockSize);
        b.align32();
    }
}

} // namespace reluva::bink
