// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Bink audio (DCT). The frame sizes, bands, quantisers, floats and
// windowing follow the MultimediaWiki's "Bink Audio"; the coefficients'
// run coding, the scaling and the transform's details follow FFmpeg's
// decoder (libavcodec/binkaudio.c, LGPL 2.1+). The inverse DCT is done
// with an FFT (Makhoul's method).

#include "common/video/bink_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::video {

namespace {

// The critical band edges (Hz), as WMA's.
constexpr int kCriticalFrequencies[25] = {100,  200,  300,  400,  510,  630,  770,  920,   1080,  1270, 1480, 1720,
                                          2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500,
                                          24500};
// Lengths of runs of coefficients, in groups of 8.
constexpr int kRunLengths[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

class Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    uint32_t get(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++, pos_++)
            if ((pos_ >> 3) < size_)
                v |= uint32_t((data_[pos_ >> 3] >> (pos_ & 7)) & 1) << i;
        return v;
    }
    bool bit() { return get(1) != 0; }
    // A float of 5 bits of exponent, 23 of mantissa and a sign.
    float real() {
        const int power = int(get(5));
        float f = std::ldexp(float(get(23)), power - 23);
        return bit() ? -f : f;
    }
    void align32() { pos_ = (pos_ + 31) & ~size_t(31); }
    ptrdiff_t left() const { return ptrdiff_t(size_ * 8) - ptrdiff_t(pos_); }

private:
    const uint8_t* data_;
    size_t size_, pos_ = 0;
};

// An in-place radix-2 FFT, e^{+i...} (an inverse DFT without the 1/N).
void fft(std::vector<std::complex<float>>& a, const std::vector<std::complex<float>>& roots) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j |= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++) {
                const std::complex<float> w = roots[k * step];
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

} // namespace

Error BinkAudioDecoder::init(int sampleRate, int channels, bool dct) {
    if (!dct)
        return Error::fail("Bink audio: only the DCT form is supported");
    if (sampleRate <= 0 || channels < 1 || channels > 2)
        return Error::fail("Bink audio: bad format");
    rate_ = sampleRate;
    channels_ = channels;
    const int bits = sampleRate < 22050 ? 9 : sampleRate < 44100 ? 10 : 11;
    frameLength_ = 1 << bits;
    overlap_ = frameLength_ / 16;
    root_ = float(frameLength_ / (std::sqrt(double(frameLength_)) * 32768.0));
    for (int i = 0; i < 96; i++)
        quant_[i] = std::exp(float(i) * 0.15289164787221953823f) * root_; // 10^(i * 0.0664)
    const int half = (sampleRate + 1) / 2;
    for (bandCount_ = 1; bandCount_ < 25; bandCount_++)
        if (half <= kCriticalFrequencies[bandCount_ - 1])
            break;
    bands_.assign(size_t(bandCount_) + 1, 0);
    bands_[0] = 2;
    for (int i = 1; i < bandCount_; i++)
        bands_[size_t(i)] = int(int64_t(kCriticalFrequencies[i - 1]) * frameLength_ / half) & ~1;
    bands_[size_t(bandCount_)] = frameLength_;
    previous_.assign(size_t(channels), std::vector<float>(size_t(overlap_), 0.0f));
    first_ = true;
    const double pi = 3.14159265358979323846;
    twiddle_.resize(size_t(frameLength_));
    roots_.resize(size_t(frameLength_) / 2);
    for (int k = 0; k < frameLength_; k++)
        twiddle_[size_t(k)] = std::polar(1.0f, float(pi * k / (2.0 * frameLength_)));
    for (int k = 0; k < frameLength_ / 2; k++)
        roots_[size_t(k)] = std::polar(1.0f, float(2.0 * pi * k / frameLength_));
    return Error::success();
}

// y[n] = X[0] / 2 + sum X[k] cos(pi k (2n + 1) / 2N), by one N-point FFT:
// V[k] = (X[k] - i X[N - k]) e^{i pi k / 2N}, v = IDFT(V) / 2, and y
// interleaves v from the front and from the back.
void BinkAudioDecoder::inverseDct(std::vector<float>& x, std::vector<float>& y) {
    const size_t n = size_t(frameLength_);
    std::vector<std::complex<float>> v(n);
    for (size_t k = 0; k < n; k++)
        v[k] = std::complex<float>(x[k], k ? -x[n - k] : 0.0f) * twiddle_[k];
    fft(v, roots_);
    y.resize(n);
    for (size_t i = 0; i < n / 2; i++) {
        y[2 * i] = v[i].real() / 2;
        y[2 * i + 1] = v[n - 1 - i].real() / 2;
    }
}

Error BinkAudioDecoder::decode(const uint8_t* data, size_t size, std::vector<int16_t>& out, size_t maxBytes) {
    if (!frameLength_)
        return Error::fail("Bink audio: not initialised");
    const size_t first = out.size();
    struct Cut {
        std::vector<int16_t>& out;
        size_t first, max;
        ~Cut() {
            if (out.size() - first > max / 2)
                out.resize(first + max / 2);
        }
    } cut{out, first, maxBytes};
    Bits bits(data, size);
    const size_t n = size_t(frameLength_);
    std::vector<float> coeffs(n), samples[2];
    // Blocks until the packet ends, each aligned to 32 bits.
    while (bits.left() > 0) {
        bits.get(2);
        for (int ch = 0; ch < channels_; ch++) {
            if (bits.left() < 58 + bandCount_ * 8)
                return Error::fail("Bink audio: packet ends in a block");
            coeffs[0] = bits.real() * root_;
            coeffs[1] = bits.real() * root_;
            float quant[25];
            for (int i = 0; i < bandCount_; i++)
                quant[i] = quant_[std::min<uint32_t>(bits.get(8), 95)];
            // Coefficients in runs (8, or a length from the table), each run
            // with a width in bits (0: all zero); a sign after each nonzero
            // one. The quantiser follows the bands.
            int k = 0;
            float q = quant[0];
            for (size_t i = 2; i < n;) {
                size_t j = i + (bits.bit() ? size_t(kRunLengths[bits.get(4)]) * 8 : 8);
                j = std::min(j, n);
                const int width = int(bits.get(4));
                if (width == 0) {
                    std::fill(coeffs.begin() + ptrdiff_t(i), coeffs.begin() + ptrdiff_t(j), 0.0f);
                    i = j;
                    while (bands_[size_t(k)] < int(i))
                        q = quant[k++];
                } else {
                    for (; i < j; i++) {
                        if (bands_[size_t(k)] == int(i))
                            q = quant[k++];
                        const uint32_t c = bits.get(width);
                        coeffs[i] = c ? (bits.bit() ? -q : q) * float(c) : 0.0f;
                    }
                }
            }
            coeffs[0] *= 2; // the DC term's half weight in the transform
            inverseDct(coeffs, samples[ch]);
            for (float& s : samples[ch])
                s /= float(frameLength_) / 2;
        }
        // The start of the block crossfaded with the end of the last.
        for (int ch = 0; ch < channels_; ch++) {
            std::vector<float>& s = samples[ch];
            const int count = overlap_ * channels_;
            if (!first_)
                for (int i = 0, j = ch; i < overlap_; i++, j += channels_)
                    s[size_t(i)] = (previous_[size_t(ch)][size_t(i)] * float(count - j) + s[size_t(i)] * float(j)) /
                                   float(count);
            std::copy(s.end() - overlap_, s.end(), previous_[size_t(ch)].begin());
        }
        first_ = false;
        for (int i = 0; i < frameLength_ - overlap_; i++)
            for (int ch = 0; ch < channels_; ch++) {
                const float v = samples[ch][size_t(i)] * 32768.0f;
                out.push_back(int16_t(std::clamp(std::lround(v), -32768L, 32767L)));
            }
        bits.align32();
    }
    return Error::success();
}

} // namespace reluva::video
