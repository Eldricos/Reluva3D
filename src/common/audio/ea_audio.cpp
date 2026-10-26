// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// EA-XA: CD-XA ADPCM's filters and shifts, the frame layout found on Need for
// Speed III's banks (docs/formats/nfs/bnk.md).
//
// MicroTalk: written from the algorithm as Andrew D'Addesio's public-domain
// utkencode describes it (github.com/daddesio/utkencode, read as a
// reference): its tables are the format's constants.

#include "common/audio/ea_audio.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace reluva::audio {

// --- EA-XA ----------------------------------------------------------------------

namespace {

// CD-XA's four prediction filters, in 1/256ths: the coefficients of the
// previous sample and the one before.
constexpr int kXaFilters[4][2] = {{0, 0}, {240, 0}, {460, -208}, {392, -220}};

int16_t clamp16(int v) { return static_cast<int16_t>(std::clamp(v, -32768, 32767)); }

} // namespace

void decodeEaXaFrame(const uint8_t* frame, EaXaChannel& ch, int16_t* out, int count, int stride) {
    // Filter numbers above 3 appear only in final, partly used frames; they
    // count as no prediction.
    const int filter = frame[0] >> 4;
    const int c1 = filter < 4 ? kXaFilters[filter][0] : 0;
    const int c2 = filter < 4 ? kXaFilters[filter][1] : 0;
    const int shift = frame[0] & 0x0f; // a residual is worth 2^(12 - shift)
    for (int i = 0; i < count && i < kEaXaFrameSamples; ++i) {
        const int nibble = (frame[1 + i / 2] >> (i % 2 ? 0 : 4)) & 0x0f;
        const int residual = (nibble >= 8 ? nibble - 16 : nibble) * (1 << 12) >> shift;
        const int sample = clamp16(residual + ((c1 * ch.previous + c2 * ch.beforePrevious + 128) >> 8));
        ch.beforePrevious = ch.previous;
        ch.previous = sample;
        out[static_cast<size_t>(i) * stride] = static_cast<int16_t>(sample);
    }
}

Error decodeEaXa(const uint8_t* data, size_t size, size_t samples, std::vector<int16_t>& out) {
    const size_t frames = (samples + kEaXaFrameSamples - 1) / kEaXaFrameSamples;
    if (frames * kEaXaFrameBytes > size)
        return Error::fail("EA-XA data is shorter than its " + std::to_string(samples) + " samples");
    out.resize(samples);
    EaXaChannel ch;
    for (size_t f = 0; f < frames; ++f) {
        const size_t first = f * kEaXaFrameSamples;
        decodeEaXaFrame(data + f * kEaXaFrameBytes, ch, out.data() + first,
                        static_cast<int>(std::min<size_t>(kEaXaFrameSamples, samples - first)));
    }
    return Error::success();
}

// --- MicroTalk ------------------------------------------------------------------

namespace {

// The reflection coefficients' quantisation levels.
constexpr float kReflection[64] = {
    +0.0f, -.99677598476409912109375f, -.99032700061798095703125f, -.983879029750823974609375f,
    -.977430999279022216796875f, -.970982015132904052734375f, -.964533984661102294921875f,
    -.958085000514984130859375f, -.9516370296478271484375f, -.930754005908966064453125f,
    -.904959976673126220703125f, -.879167020320892333984375f, -.853372991085052490234375f,
    -.827579021453857421875f, -.801786005496978759765625f, -.775991976261138916015625f,
    -.75019800662994384765625f, -.724404990673065185546875f, -.6986110210418701171875f,
    -.6706349849700927734375f, -.61904799938201904296875f, -.567460000514984130859375f,
    -.515873014926910400390625f, -.4642859995365142822265625f, -.4126980006694793701171875f,
    -.361110985279083251953125f, -.309523999691009521484375f, -.257937014102935791015625f,
    -.20634900033473968505859375f, -.1547619998455047607421875f, -.10317499935626983642578125f,
    -.05158700048923492431640625f, +0.0f, +.05158700048923492431640625f, +.10317499935626983642578125f,
    +.1547619998455047607421875f, +.20634900033473968505859375f, +.257937014102935791015625f,
    +.309523999691009521484375f, +.361110985279083251953125f, +.4126980006694793701171875f,
    +.4642859995365142822265625f, +.515873014926910400390625f, +.567460000514984130859375f,
    +.61904799938201904296875f, +.6706349849700927734375f, +.6986110210418701171875f,
    +.724404990673065185546875f, +.75019800662994384765625f, +.775991976261138916015625f,
    +.801786005496978759765625f, +.827579021453857421875f, +.853372991085052490234375f,
    +.879167020320892333984375f, +.904959976673126220703125f, +.930754005908966064453125f,
    +.9516370296478271484375f, +.958085000514984130859375f, +.964533984661102294921875f,
    +.970982015132904052734375f, +.977430999279022216796875f, +.983879029750823974609375f,
    +.99032700061798095703125f, +.99677598476409912109375f};

// The multi-pulse excitation's codes: the next 8 bits pick a code, by the
// model (after a large pulse or not); each code is so many bits long and is
// a pulse, a run of zeros (2, 3) or an escape for a large pulse (0, 1).
struct PulseCode {
    bool largeModelNext;
    int bits;
    float pulse;
};
constexpr PulseCode kPulseCodes[29] = {
    {true, 8, 0},   {true, 7, 0},   {false, 8, 0},  {false, 7, 0},  {false, 2, 0},  {false, 2, -1},
    {false, 2, 1},  {false, 3, -1}, {false, 3, 1},  {true, 4, -2},  {true, 4, 2},   {true, 3, -2},
    {true, 3, 2},   {true, 5, -3},  {true, 5, 3},   {true, 4, -3},  {true, 4, 3},   {true, 6, -4},
    {true, 6, 4},   {true, 5, -4},  {true, 5, 4},   {true, 7, -5},  {true, 7, 5},   {true, 6, -5},
    {true, 6, 5},   {true, 8, -6},  {true, 8, 6},   {true, 7, -6},  {true, 7, 6}};

// Which code the next 8 bits make, in the normal model and after a large
// pulse.
constexpr uint8_t kCodeTables[2][256] = {
    {4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 21,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 25,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 22,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 0,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 21,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 26,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 17, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 22,
     4, 6, 5, 9, 4, 6, 5, 13, 4, 6, 5, 10, 4, 6, 5, 18, 4, 6, 5, 9, 4, 6, 5, 14, 4, 6, 5, 10, 4, 6, 5, 2},
    {4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 27,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 1,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 28,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 3,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 27,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 1,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 23, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 28,
     4, 11, 7, 15, 4, 12, 8, 19, 4, 11, 7, 16, 4, 12, 8, 24, 4, 11, 7, 15, 4, 12, 8, 20, 4, 11, 7, 16, 4, 12, 8, 3}};

// The reflection coefficients as a 12th-order prediction filter.
void reflectionToPrediction(const float* rc, float* lpc) {
    float a[12], b[12];
    b[0] = 1.0f;
    for (int i = 10; i >= 0; --i)
        b[1 + i] = rc[i];
    for (int i = 0; i < 12; ++i) {
        float x = -b[11] * rc[11];
        for (int j = 10; j >= 0; --j) {
            x -= b[j] * rc[j];
            b[j + 1] = x * rc[j] + b[j];
        }
        a[i] = b[0] = x;
        for (int j = 0; j < i; ++j)
            x -= a[i - 1 - j] * lpc[j];
        lpc[i] = x;
    }
}

} // namespace

MicroTalkDecoder::MicroTalkDecoder(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

int MicroTalkDecoder::readByte() {
    if (p_ < end_)
        return *p_++;
    overrun_ = true;
    return 0;
}

int MicroTalkDecoder::readBits(int count) {
    const int value = static_cast<int>(bitsValue_ & ((1u << count) - 1));
    bitsValue_ >>= count;
    bitsCount_ -= count;
    if (bitsCount_ < 8) {
        bitsValue_ |= static_cast<unsigned>(readByte()) << bitsCount_;
        bitsCount_ += 8;
    }
    return value;
}

void MicroTalkDecoder::parseHeader() {
    // Reduced bandwidth (1 bit), the multi-pulse threshold (4), the first
    // fixed gain (4) and the gains' ratio (6).
    reducedBandwidth_ = readBits(1) != 0;
    multipulseThreshold_ = 32 - readBits(4);
    fixedGains_[0] = 8.0f * static_cast<float>(1 + readBits(4));
    const float ratio = 1.04f + static_cast<float>(readBits(6)) * 0.001f;
    for (int i = 1; i < 64; ++i)
        fixedGains_[i] = fixedGains_[i - 1] * ratio;
}

void MicroTalkDecoder::decodeExcitation(bool multipulse, float* out, int stride) {
    constexpr int kSubframe = 108;
    if (multipulse) {
        // Pulses and runs of zeros, coded by the two models.
        bool large = false;
        int i = 0;
        while (i < kSubframe) {
            const int code = kCodeTables[large ? 1 : 0][bitsValue_ & 0xff];
            large = kPulseCodes[code].largeModelNext;
            readBits(kPulseCodes[code].bits);
            if (code > 3) {
                out[i] = kPulseCodes[code].pulse;
                i += stride;
            } else if (code > 1) {
                int zeros = 7 + readBits(6);
                if (i + zeros * stride > kSubframe)
                    zeros = (kSubframe - i) / stride;
                for (; zeros > 0; --zeros, i += stride)
                    out[i] = 0;
            } else {
                // A pulse of 7 or more: one more per 1 bit, then its sign.
                int magnitude = 7;
                while (readBits(1))
                    ++magnitude;
                out[i] = static_cast<float>(readBits(1) ? magnitude : -magnitude);
                i += stride;
            }
        }
    } else {
        // Every sample: 0, or -2 / +2.
        for (int i = 0; i < kSubframe; i += stride)
            out[i] = !readBits(1) ? 0.0f : readBits(1) ? 2.0f : -2.0f;
    }
}

void MicroTalkDecoder::synthesise(int offset, int blocks) {
    float lpc[12];
    reflectionToPrediction(reflection_, lpc);
    float* p = frame_ + offset;
    for (int b = 0; b < blocks; ++b)
        for (int j = 0; j < 12; ++j) {
            float x = *p;
            for (int k = 0; k < j; ++k)
                x += lpc[k] * history_[k - j + 12];
            for (int k = j; k < 12; ++k)
                x += lpc[k] * history_[k - j];
            history_[11 - j] = x;
            *p++ = x;
        }
}

const float* MicroTalkDecoder::decodeFrame() {
    if (!bitsCount_) {
        bitsValue_ = static_cast<unsigned>(readByte());
        bitsCount_ = 8;
    }
    if (!headerParsed_) {
        parseHeader();
        headerParsed_ = true;
    }

    // 12 reflection coefficients, reached in four steps over the frame.
    bool multipulse = false;
    float step[12];
    for (int i = 0; i < 12; ++i) {
        int index;
        if (i == 0) {
            index = readBits(6);
            multipulse = index < multipulseThreshold_;
        } else if (i < 4) {
            index = readBits(6);
        } else {
            index = 16 + readBits(5);
        }
        step[i] = (kReflection[index] - reflection_[i]) * 0.25f;
    }

    // Four subframes of 108 samples: pitch (lag, gain) and the excitation.
    float excitation[5 + 108 + 5] = {};
    for (int s = 0; s < 4; ++s) {
        const int lag = readBits(8);
        const float pitchGain = static_cast<float>(readBits(4)) / 15.0f;
        float fixedGain = fixedGains_[readBits(6)];
        if (!reducedBandwidth_) {
            decodeExcitation(multipulse, excitation + 5, 1);
        } else {
            // Every other sample coded; the rest zero or interpolated.
            const int align = readBits(1);
            const bool zero = readBits(1) != 0;
            decodeExcitation(multipulse, excitation + 5 + align, 2);
            float* other = excitation + 5 + (1 - align);
            if (zero) {
                for (int j = 0; j < 108; j += 2)
                    other[j] = 0;
            } else {
                for (int j = 0; j < 108; j += 2)
                    other[j] = other[j - 5] * 0.01803267933428287506103515625f -
                               other[j - 3] * 0.114591561257839202880859375f +
                               other[j - 1] * 0.597385942935943603515625f +
                               other[j + 1] * 0.597385942935943603515625f -
                               other[j + 3] * 0.114591561257839202880859375f +
                               other[j + 5] * 0.01803267933428287506103515625f;
                fixedGain *= 0.5f;
            }
        }
        for (int j = 0; j < 108; ++j) {
            // From the adaptive codebook, read on into this frame's earlier
            // subframes; a lag over 216 in the first subframe reads before
            // it, into the state before it (state_), as in the original.
            const int from = 108 * s + 216 - lag + j;
            frame_[108 * s + j] = fixedGain * excitation[5 + j] + pitchGain * adaptive_[from];
        }
    }
    std::copy(frame_ + 108, frame_ + 432, adaptive_);

    for (int s = 0; s < 4; ++s) {
        for (int j = 0; j < 12; ++j)
            reflection_[j] += step[j];
        synthesise(12 * s, s < 3 ? 1 : 33);
    }
    return frame_;
}

Error decodeMicroTalk(const uint8_t* data, size_t size, size_t samples, std::vector<int16_t>& out) {
    out.resize(samples);
    MicroTalkDecoder decoder(data, size);
    for (size_t done = 0; done < samples; done += MicroTalkDecoder::kFrameSamples) {
        const float* frame = decoder.decodeFrame();
        const size_t n = std::min<size_t>(MicroTalkDecoder::kFrameSamples, samples - done);
        for (size_t i = 0; i < n; ++i)
            out[done + i] = static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(frame[i])), -32768, 32767));
    }
    // The reader keeps a byte ahead, so the last frame may read a byte past
    // the data; a stream that ran out sooner is short.
    if (samples && size == 0)
        return Error::fail("MicroTalk data is empty");
    return Error::success();
}

} // namespace reluva::audio
