// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How Bink movies are laid out and decoded follows the published
// descriptions of the format and the GPL decoders that document it
// (FFmpeg's, as carried by ScummVM); this is new code written from that
// description, checked against FFmpeg's output (docs/formats/bink.md).

#include "common/video/bink.h"

#include "common/video/bink_tables.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace reluva::bink {

namespace {

enum BlockType { Skip, Scaled, Motion, RunBlock, Residue, Intra, Fill, Inter, PatternBlock, Raw };

constexpr uint32_t kAlphaFlag = 0x00100000;
constexpr uint16_t kAudioDct = 0x1000, kAudioStereo = 0x2000;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

int log2Floor(uint32_t v) {
    int n = 0;
    while (v >>= 1)
        ++n;
    return n;
}

uint8_t clampByte(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// The 8x8 inverse DCT the format is coded for, in its fixed-point form
// (constants 1/sqrt(2) and the rotations, << 11).
constexpr int kA1 = 2896, kA2 = 2217, kA3 = 3784, kA4 = -5352;

template <typename Out, typename Munge>
void idct8(const int32_t* s, int step, Out* d, int dstep, Munge munge) {
    const int a0 = s[0] + s[4 * step], a1 = s[0] - s[4 * step];
    const int a2 = s[2 * step] + s[6 * step], a3 = (kA1 * (s[2 * step] - s[6 * step])) >> 11;
    const int a4 = s[5 * step] + s[3 * step], a5 = s[5 * step] - s[3 * step];
    const int a6 = s[1 * step] + s[7 * step], a7 = s[1 * step] - s[7 * step];
    const int b0 = a4 + a6, b1 = (kA3 * (a5 + a7)) >> 11;
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
        const int32_t* s = in + c;
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
void inverseFft(std::vector<std::complex<double>>& a) {
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
        const double ang = 2 * M_PI / double(len);
        const std::complex<double> w(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> wn(1, 0);
            for (size_t k = 0; k < len / 2; ++k, wn *= w) {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// DCT-III of n values in place: x[m] = X[0] / 2 + sum X[k] cos(pi k (m + 1/2) / n).
void dct3(float* x, int n) {
    std::vector<std::complex<double>> z(size_t(2 * n));
    for (int k = 0; k < n; ++k)
        z[size_t(k)] = std::polar(double(k == 0 ? x[0] * 0.5f : x[k]), M_PI * k / (2.0 * n));
    inverseFft(z);
    for (int m = 0; m < n; ++m)
        x[m] = float(z[size_t(m)].real());
}

// The real inverse DFT of a packed spectrum (re 0, re n/2, then re and im
// of 1 .. n/2-1, the imaginary parts as Bink stores them, negated) in
// place: x[m] = (X0 + Xn/2 (-1)^m) / 2 + sum Re(conj(Xk) e^(2 pi i k m / n)).
void inverseRealDft(float* x, int n) {
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
    file_ = std::move(file);
    const uint8_t* p = file_.data();
    if (file_.size() < 44 || p[0] != 'B' || p[1] != 'I' || p[2] != 'K')
        return Error::fail("Not a Bink movie");
    revision_ = char(p[3]);
    if (revision_ < 'f' || revision_ > 'i')
        return Error::fail(std::string("Bink revision ") + revision_ + " isn't supported (f to i are)");
    const uint32_t count = le32(p + 8);
    width_ = int(le32(p + 20));
    height_ = int(le32(p + 24));
    rateNum_ = le32(p + 28);
    rateDen_ = le32(p + 32);
    const uint32_t flags = le32(p + 36), tracks = le32(p + 40);
    if (width_ <= 0 || height_ <= 0 || width_ > 4096 || height_ > 4096 || !rateNum_ || !rateDen_ || tracks > 256)
        return Error::fail("The Bink header is damaged");
    alpha_ = (flags & kAlphaFlag) != 0;
    swapChroma_ = revision_ >= 'h';
    size_t at = 44 + 4 * size_t(tracks); // each track's largest decoded size, unused
    if (file_.size() < at + 8 * size_t(tracks) + 4 * (size_t(count) + 1))
        return Error::fail("The Bink header is cut short");
    audio_.clear();
    for (uint32_t t = 0; t < tracks; ++t, at += 4) {
        Audio a;
        a.rate = p[at] | p[at + 1] << 8;
        const uint16_t f = uint16_t(p[at + 2] | p[at + 3] << 8);
        a.channels = a.outChannels = (f & kAudioStereo) ? 2 : 1;
        a.dct = (f & kAudioDct) != 0;
        audio_.push_back(a);
    }
    at += 4 * size_t(tracks); // track ids
    frames_.assign(count, Frame{});
    for (uint32_t i = 0; i <= count; ++i) {
        const uint32_t v = i < count ? le32(p + at + 4 * i) : uint32_t(file_.size());
        if (i < count) {
            frames_[i].offset = v & ~1u;
            frames_[i].key = v & 1;
        }
        if (i > 0) {
            const uint32_t end = std::min<uint32_t>(v & ~1u, uint32_t(file_.size()));
            frames_[i - 1].size = end > frames_[i - 1].offset ? end - frames_[i - 1].offset : 0;
        }
    }

    // The audio: blocks of 2^9 to 2^11 samples (by rate), overlapping by a
    // sixteenth; the spectrum in bands of the critical frequencies.
    for (Audio& a : audio_) {
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
        for (auto& c : a.coeffs)
            c.assign(size_t(a.frameLength), 0);
        a.previous.assign(size_t(a.overlap * a.channels), 0);
    }

    // The planes, in whole 8x8 blocks (chroma at half size).
    lumaPitch_ = ((width_ + 7) >> 3) * 8;
    lumaRows_ = ((height_ + 7) >> 3) * 8;
    chromaPitch_ = ((width_ + 15) >> 4) * 8;
    chromaRows_ = ((height_ + 15) >> 4) * 8;
    for (int i = 0; i < 4; ++i) {
        const size_t size = i == 0 || i == 3 ? size_t(lumaPitch_) * size_t(lumaRows_)
                                             : size_t(chromaPitch_) * size_t(chromaRows_);
        cur_[size_t(i)].assign(size, i == 3 ? 255 : 0);
        old_[size_t(i)].assign(size, i == 3 ? 255 : 0);
    }
    // How many bits count each bundle's values for a row.
    const size_t blocks = size_t((width_ + 7) >> 3) * size_t((height_ + 7) >> 3);
    for (Bundle& b : bundles_)
        b.data.assign(blocks * 64, 0);
    const int cbw[2] = {(width_ + 7) >> 3, (width_ + 15) >> 4};
    const int cw[2] = {width_, width_ >> 1};
    for (int i = 0; i < 2; ++i) {
        const int w = std::max(cw[i], 8);
        auto bitsFor = [](int v) { return log2Floor(uint32_t(v + 511)) + 1; };
        bundles_[BlockTypes].countBits[i] = bitsFor(w >> 3);
        bundles_[SubBlockTypes].countBits[i] = bitsFor((w + 7) >> 4);
        bundles_[Colors].countBits[i] = bitsFor(cbw[i] * 64);
        bundles_[IntraDC].countBits[i] = bitsFor(w >> 3);
        bundles_[InterDC].countBits[i] = bitsFor(w >> 3);
        bundles_[XOff].countBits[i] = bitsFor(w >> 3);
        bundles_[YOff].countBits[i] = bitsFor(w >> 3);
        bundles_[Pattern].countBits[i] = bitsFor(cbw[i] << 3);
        bundles_[Run].countBits[i] = bitsFor(cbw[i] * 48);
    }
    next_ = 0;
    return Error::success();
}

Error Movie::nextFrame(bool& more, std::vector<int16_t>* sound) {
    more = next_ < int(frames_.size());
    if (!more)
        return Error::success();
    const Frame& f = frames_[size_t(next_++)];
    if (size_t(f.offset) + f.size > file_.size())
        return Error::fail("A Bink frame runs past the end of the file");
    const uint8_t* p = file_.data() + f.offset;
    size_t left = f.size;
    // Each audio track's packet: its length, then (if any) the decoded
    // size and the blocks.
    for (size_t t = 0; t < audio_.size(); ++t) {
        if (left < 4)
            return Error::fail("A Bink frame is cut short");
        const uint32_t length = le32(p);
        p += 4;
        left -= 4;
        if (length > left)
            return Error::fail("A Bink audio packet runs past its frame");
        if (length >= 4 && t == 0 && sound) {
            Bits b{p + 4, size_t(length - 4) * 8, 0};
            decodeAudio(audio_[t], b, sound);
        }
        p += length;
        left -= length;
    }
    // The video: alpha, then Y and the two chroma planes, each from a
    // 32-bit boundary.
    Bits b{p, left * 8, 0};
    if (alpha_) {
        if (revision_ == 'i')
            b.skip(32);
        decodePlane(b, 3, false);
    }
    if (revision_ == 'i')
        b.skip(32);
    for (int i = 0; i < 3; ++i) {
        const int plane = i == 0 || !swapChroma_ ? i : (i ^ 3);
        decodePlane(b, plane, i != 0);
        if (b.done())
            break;
    }
    std::swap(cur_, old_);
    return Error::success();
}

Error Movie::decodeAllAudio(std::vector<int16_t>& sound) {
    if (audio_.empty())
        return Error::success();
    for (const Frame& f : frames_) {
        if (size_t(f.offset) + f.size > file_.size() || f.size < 4)
            break;
        const uint8_t* p = file_.data() + f.offset;
        const uint32_t length = le32(p);
        if (length > f.size - 4)
            return Error::fail("A Bink audio packet runs past its frame");
        if (length >= 4) {
            Bits b{p + 8, size_t(length - 4) * 8, 0};
            decodeAudio(audio_[0], b, &sound);
        }
    }
    return Error::success();
}

void Movie::toRgba(std::vector<uint8_t>& out) const {
    // The newest frame is in old_ once swapped.
    const auto& y = old_[0];
    const auto& u = old_[1];
    const auto& v = old_[2];
    const auto& a = old_[3];
    out.resize(size_t(width_) * size_t(height_) * 4);
    for (int row = 0; row < height_; ++row)
        for (int col = 0; col < width_; ++col) {
            const int Y = (y[size_t(row) * size_t(lumaPitch_) + size_t(col)] - 16) * 298;
            const size_t c = size_t(row / 2) * size_t(chromaPitch_) + size_t(col / 2);
            const int U = u[c] - 128, V = v[c] - 128;
            uint8_t* o = &out[(size_t(row) * size_t(width_) + size_t(col)) * 4];
            o[0] = clampByte((Y + 409 * V + 128) >> 8);
            o[1] = clampByte((Y - 100 * U - 208 * V + 128) >> 8);
            o[2] = clampByte((Y + 516 * U + 128) >> 8);
            o[3] = alpha_ ? a[size_t(row) * size_t(lumaPitch_) + size_t(col)] : 255;
        }
}

// --- Video ----------------------------------------------------------------------

void Movie::readTree(Bits& b, Tree& t) {
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
            if (!listed[i])
                t.symbols[size_t(++n)] = uint8_t(i);
        return;
    }
    std::array<uint8_t, 16> in = t.symbols, out{};
    const int depth = int(b.get(2));
    for (int level = 0; level <= depth; ++level) {
        const int size = 1 << level;
        for (int j = 0; j < 16; j += size * 2) {
            int a = j, c = j + size, o = j;
            int left = size, right = size;
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

int Movie::symbol(Bits& b, const Tree& t) {
    uint32_t code = 0;
    for (int length = 1; length <= 8; ++length) {
        code |= b.bit() << (length - 1);
        for (int s = 0; s < 16; ++s)
            if (kTreeLengths[t.index][s] == length && kTreeCodes[t.index][s] == code)
                return t.symbols[size_t(s)];
    }
    return 0;
}

void Movie::readBundle(Bits& b, Source s) {
    if (s == Colors) {
        for (Tree& t : colorHigh_)
            readTree(b, t);
        lastColorHigh_ = 0;
    }
    if (s != IntraDC && s != InterDC)
        readTree(b, bundles_[s].tree);
    bundles_[s].decoded = bundles_[s].read = 0;
    bundles_[s].finished = false;
}

uint32_t Movie::bundleCount(Bits& b, Bundle& bundle) {
    // More values only once those decoded are used up.
    if (bundle.finished || bundle.decoded > bundle.read)
        return 0;
    const uint32_t n = b.get(bundle.bits);
    if (!n)
        bundle.finished = true;
    return std::min<uint32_t>(n, uint32_t(bundle.data.size() - bundle.decoded));
}

int Movie::value(Source s) {
    Bundle& b = bundles_[s];
    return b.read < b.data.size() ? b.data[b.read++] : 0;
}

void Movie::decodePlane(Bits& b, int planeIndex, bool chroma) {
    const int blocksW = chroma ? (width_ + 15) >> 4 : (width_ + 7) >> 3;
    const int blocksH = chroma ? (height_ + 15) >> 4 : (height_ + 7) >> 3;
    const int pitch = blocksW * 8;
    uint8_t* const dst = cur_[size_t(planeIndex)].data();
    const uint8_t* const prev = old_[size_t(planeIndex)].data();
    const int size = pitch * blocksH * 8;
    for (int s = 0; s < SourceCount; ++s) {
        bundles_[size_t(s)].bits = bundles_[size_t(s)].countBits[chroma ? 1 : 0];
        readBundle(b, Source(s));
    }
    auto push = [](Bundle& bundle, int v) {
        if (bundle.decoded < bundle.data.size())
            bundle.data[bundle.decoded++] = int16_t(v);
    };
    auto signedValue = [&](int v) { return v && b.bit() ? -v : v; };

    for (int by = 0; by < blocksH; ++by) {
        // This row's values, for those bundles that need more.
        for (Source s : {BlockTypes, SubBlockTypes}) {
            Bundle& bundle = bundles_[s];
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
            Bundle& bundle = bundles_[Colors];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                auto colour = [&]() {
                    lastColorHigh_ = symbol(b, colorHigh_[size_t(lastColorHigh_)]);
                    int v = (lastColorHigh_ << 4) | symbol(b, bundle.tree);
                    if (revision_ != 'i') {
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
            Bundle& bundle = bundles_[Pattern];
            if (const uint32_t n = bundleCount(b, bundle)) {
                const size_t end = bundle.decoded + n;
                while (bundle.decoded < end) {
                    const int lo = symbol(b, bundle.tree);
                    push(bundle, lo | symbol(b, bundle.tree) << 4);
                }
            }
        }
        for (Source s : {XOff, YOff}) {
            Bundle& bundle = bundles_[s];
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
        for (Source s : {IntraDC, InterDC}) {
            // The first in 11 bits (inter: 10 and a sign), then deltas in
            // groups of 8, each group's width in 4 bits.
            Bundle& bundle = bundles_[s];
            uint32_t n = bundleCount(b, bundle);
            if (!n)
                continue;
            int v = s == IntraDC ? int(b.get(11)) : signedValue(int(b.get(10)));
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
            Bundle& bundle = bundles_[Run];
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
            uint8_t* d = dst + at;
            const uint8_t* old = prev + at;
            int type = value(BlockTypes);
            // A 16x16 block's second row of 8x8 blocks: done already.
            if ((by & 1) && type == Scaled) {
                ++bx;
                continue;
            }
            auto copyFrom = [&](const uint8_t* src, int n) {
                for (int r = 0; r < n; ++r)
                    std::memcpy(d + r * pitch, src + r * pitch, size_t(n));
            };
            auto motion = [&]() {
                const int dx = int8_t(value(XOff)), dy = int8_t(value(YOff));
                const int from = at + dy * pitch + dx;
                if (from >= 0 && from + 7 * pitch + 8 <= size)
                    copyFrom(prev + from, 8);
                else
                    copyFrom(old, 8);
            };
            // Fills an 8x8 block, or (scaled) a 16x16 one at half resolution.
            auto put = [&](int i, int v, bool scaled) {
                const int x = i & 7, y = i >> 3;
                if (!scaled) {
                    d[y * pitch + x] = uint8_t(v);
                } else {
                    uint8_t* q = d + 2 * y * pitch + 2 * x;
                    q[0] = q[1] = q[pitch] = q[pitch + 1] = uint8_t(v);
                }
            };
            auto runs = [&](bool scaled) {
                const uint8_t* scan = kPatterns[b.get(4)];
                int i = 0;
                do {
                    const int run = value(Run) + 1;
                    i += run;
                    if (i > 64)
                        break;
                    if (b.bit()) {
                        const int v = value(Colors);
                        for (int j = 0; j < run; ++j)
                            put(*scan++, v, scaled);
                    } else {
                        for (int j = 0; j < run; ++j)
                            put(*scan++, value(Colors), scaled);
                    }
                } while (i < 63);
                if (i == 63)
                    put(*scan, value(Colors), scaled);
            };
            auto pattern = [&](bool scaled) {
                const int c0 = value(Colors), c1 = value(Colors);
                for (int y = 0; y < 8; ++y) {
                    int v = value(Pattern);
                    for (int x = 0; x < 8; ++x, v >>= 1)
                        put(y * 8 + x, (v & 1) ? c1 : c0, scaled);
                }
            };
            auto raw = [&](bool scaled) {
                for (int i = 0; i < 64; ++i)
                    put(i, value(Colors), scaled);
            };
            auto intra = [&](bool scaled) {
                int32_t block[64] = {}, out[64];
                block[0] = value(IntraDC);
                readDCTCoefficients(b, block, true);
                idct(block, out);
                for (int i = 0; i < 64; ++i)
                    put(i, out[i], scaled);
            };

            switch (type) {
            case Skip: copyFrom(old, 8); break;
            case Scaled: {
                const int sub = value(SubBlockTypes);
                switch (sub) {
                case RunBlock: runs(true); break;
                case Intra: intra(true); break;
                case Fill: {
                    const int v = value(Colors);
                    for (int r = 0; r < 16; ++r)
                        std::memset(d + r * pitch, v, 16);
                    break;
                }
                case PatternBlock: pattern(true); break;
                case Raw: raw(true); break;
                default: break;
                }
                ++bx;
                break;
            }
            case Motion: motion(); break;
            case RunBlock: runs(false); break;
            case Residue: {
                motion();
                const int masks = int(b.get(7));
                int32_t block[64] = {};
                readResidue(b, block, masks);
                for (int i = 0; i < 64; ++i)
                    d[(i >> 3) * pitch + (i & 7)] = uint8_t(d[(i >> 3) * pitch + (i & 7)] + block[i]);
                break;
            }
            case Intra: intra(false); break;
            case Fill: {
                const int v = value(Colors);
                for (int r = 0; r < 8; ++r)
                    std::memset(d + r * pitch, v, 8);
                break;
            }
            case Inter: {
                motion();
                int32_t block[64] = {}, out[64];
                block[0] = value(InterDC);
                readDCTCoefficients(b, block, false);
                idct(block, out);
                for (int i = 0; i < 64; ++i)
                    d[(i >> 3) * pitch + (i & 7)] = uint8_t(d[(i >> 3) * pitch + (i & 7)] + out[i]);
                break;
            }
            case PatternBlock: pattern(false); break;
            case Raw: raw(false); break;
            default: break;
            }
        }
    }
    b.align32();
}

void Movie::readDCTCoefficients(Bits& b, int32_t block[64], bool intra) {
    // Coefficients by bit planes, from the top: groups of four split as
    // the bits say (a list of groups to look at, and single ones found).
    int found[64], foundCount = 0;
    int listStart = 64, listEnd = 64;
    int coef[128], mode[128];
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
    const int32_t* quant = intra ? kIntraQuant[q] : kInterQuant[q];
    block[0] = (block[0] * quant[0]) >> 11;
    for (int i = 0; i < foundCount; ++i) {
        const int c = found[i];
        block[kScan[c]] = (block[kScan[c]] * quant[c]) >> 11;
    }
}

void Movie::readResidue(Bits& b, int32_t block[64], int masks) {
    // The difference after motion, bit plane by bit plane from a mask of
    // 1 << (3 bits): first refining those found, then finding more as
    // for the DCT; at most `masks` values in all.
    int found[64], foundCount = 0;
    int listStart = 64, listEnd = 64;
    int coef[128], mode[128];
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
            int32_t& v = block[found[i]];
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

void Movie::readAudioCoefficients(Audio& a, Bits& b, float* coeffs) {
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

void Movie::decodeAudio(Audio& a, Bits& b, std::vector<int16_t>* sound) {
    // Blocks from each 32-bit boundary: a transform per channel, then the
    // first sixteenth faded in over the last block's tail.
    std::vector<int16_t> out(size_t(a.frameLength * a.channels));
    while (!b.done()) {
        if (a.dct)
            b.skip(2);
        for (int c = 0; c < a.channels; ++c) {
            float* coeffs = a.coeffs[c].data();
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
