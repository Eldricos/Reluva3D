// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/mad.h"

#include "common/audio/ea_audio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

constexpr size_t kFrameHeader = 24; // tag, size, then 16 bytes (mad.md)

// The AC coefficients' codes: MPEG-1's table B.14 (ISO/IEC 11172-2), each
// a run of zeros, a level and the code's bits, a sign bit following. The
// first-coefficient form of run 0 level 1 ("1s") isn't used: it is "11s"
// throughout.
struct AcCode {
    uint8_t run, level;
    const char* bits;
};
constexpr AcCode kAcCodes[] = {
    {0, 1, "11"}, {0, 2, "0100"}, {0, 3, "00101"}, {0, 4, "0000110"}, {0, 5, "00100110"}, {0, 6, "00100001"},
    {0, 7, "0000001010"}, {0, 8, "000000011101"}, {0, 9, "000000011000"}, {0, 10, "000000010011"},
    {0, 11, "000000010000"}, {0, 12, "0000000011010"}, {0, 13, "0000000011001"}, {0, 14, "0000000011000"},
    {0, 15, "0000000010111"}, {0, 16, "00000000011111"}, {0, 17, "00000000011110"},
    {0, 18, "00000000011101"}, {0, 19, "00000000011100"}, {0, 20, "00000000011011"},
    {0, 21, "00000000011010"}, {0, 22, "00000000011001"}, {0, 23, "00000000011000"},
    {0, 24, "00000000010111"}, {0, 25, "00000000010110"}, {0, 26, "00000000010101"},
    {0, 27, "00000000010100"}, {0, 28, "00000000010011"}, {0, 29, "00000000010010"},
    {0, 30, "00000000010001"}, {0, 31, "00000000010000"}, {0, 32, "000000000011000"},
    {0, 33, "000000000010111"}, {0, 34, "000000000010110"}, {0, 35, "000000000010101"},
    {0, 36, "000000000010100"}, {0, 37, "000000000010011"}, {0, 38, "000000000010010"},
    {0, 39, "000000000010001"}, {0, 40, "000000000010000"},
    {1, 1, "011"}, {1, 2, "000110"}, {1, 3, "00100101"}, {1, 4, "0000001100"}, {1, 5, "000000011011"},
    {1, 6, "0000000010110"}, {1, 7, "0000000010101"}, {1, 8, "000000000011111"}, {1, 9, "000000000011110"},
    {1, 10, "000000000011101"}, {1, 11, "000000000011100"}, {1, 12, "000000000011011"},
    {1, 13, "000000000011010"}, {1, 14, "000000000011001"}, {1, 15, "0000000000010011"},
    {1, 16, "0000000000010010"}, {1, 17, "0000000000010001"}, {1, 18, "0000000000010000"},
    {2, 1, "0101"}, {2, 2, "0000100"}, {2, 3, "0000001011"}, {2, 4, "000000010100"}, {2, 5, "0000000010100"},
    {3, 1, "00111"}, {3, 2, "00100100"}, {3, 3, "000000011100"}, {3, 4, "0000000010011"},
    {4, 1, "00110"}, {4, 2, "0000001111"}, {4, 3, "000000010010"},
    {5, 1, "000111"}, {5, 2, "0000001001"}, {5, 3, "0000000010010"},
    {6, 1, "000101"}, {6, 2, "000000011110"}, {6, 3, "0000000000010100"},
    {7, 1, "000100"}, {7, 2, "000000010101"},
    {8, 1, "0000111"}, {8, 2, "000000010001"},
    {9, 1, "0000101"}, {9, 2, "0000000010001"},
    {10, 1, "00100111"}, {10, 2, "0000000010000"},
    {11, 1, "00100011"}, {11, 2, "0000000000011010"},
    {12, 1, "00100010"}, {12, 2, "0000000000011001"},
    {13, 1, "00100000"}, {13, 2, "0000000000011000"},
    {14, 1, "0000001110"}, {14, 2, "0000000000010111"},
    {15, 1, "0000001101"}, {15, 2, "0000000000010110"},
    {16, 1, "0000001000"}, {16, 2, "0000000000010101"},
    {17, 1, "000000011111"}, {18, 1, "000000011010"}, {19, 1, "000000011001"}, {20, 1, "000000010111"},
    {21, 1, "000000010110"}, {22, 1, "0000000011111"}, {23, 1, "0000000011110"}, {24, 1, "0000000011101"},
    {25, 1, "0000000011100"}, {26, 1, "0000000011011"}, {27, 1, "0000000000011111"},
    {28, 1, "0000000000011110"}, {29, 1, "0000000000011101"}, {30, 1, "0000000000011100"},
    {31, 1, "0000000000011011"},
};
constexpr const char* kEscape = "000001"; // then a 10-bit signed level and a 6-bit run
constexpr const char* kEndOfBlock = "10";

// A lookup by the next 16 bits: the code's length (0: no code), then the
// run and level; level 0 is the escape, run 0xff the end of the block.
struct AcEntry {
    uint8_t length = 0, run = 0;
    int8_t level = 0;
};
const std::vector<AcEntry>& acTable() {
    static const std::vector<AcEntry> table = [] {
        std::vector<AcEntry> t(1 << 16);
        auto add = [&](const char* bits, uint8_t run, int8_t level) {
            const size_t n = std::strlen(bits);
            const uint32_t code = static_cast<uint32_t>(std::strtoul(bits, nullptr, 2));
            const uint32_t first = code << (16 - n), count = 1u << (16 - n);
            for (uint32_t i = 0; i < count; ++i)
                t[first + i] = {static_cast<uint8_t>(n), run, level};
        };
        for (const AcCode& c : kAcCodes)
            add(c.bits, c.run, static_cast<int8_t>(c.level));
        add(kEscape, 0, 0);
        add(kEndOfBlock, 0xff, 0);
        return t;
    }();
    return table;
}

// The default intra quantiser matrix of MPEG-1 (ISO/IEC 11172-2), in
// raster order.
constexpr uint8_t kIntraMatrix[64] = {
    8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
    34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
    35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

// The zigzag scan: position in the scan to raster index.
const std::array<uint8_t, 64>& zigzag() {
    static const std::array<uint8_t, 64> order = [] {
        std::array<uint8_t, 64> z{};
        int n = 0;
        for (int s = 0; s < 15; ++s) {
            // Odd diagonals run down-left, even ones up-right.
            for (int k = 0; k < 8; ++k) {
                const int row = (s & 1) ? k : s - k, col = s - row;
                if (row >= 0 && row < 8 && col >= 0 && col < 8)
                    z[n++] = static_cast<uint8_t>(row * 8 + col);
            }
        }
        return z;
    }();
    return order;
}

// The inverse of the AAN transform's scale factors, times 4096: the
// quantiser folds them in, as the transform below leaves them out.
const std::array<int, 64>& inverseAanScales() {
    static const std::array<int, 64> scales = [] {
        std::array<int, 64> s{};
        const double pi = std::acos(-1.0);
        auto a = [&](int k) { return k ? std::sqrt(2.0) * std::cos(k * pi / 16) : 1.0; };
        for (int u = 0; u < 8; ++u)
            for (int v = 0; v < 8; ++v)
                s[u * 8 + v] = static_cast<int>(std::lround(4096 / (a(u) * a(v))));
        return s;
    }();
    return scales;
}

// EA's 8-point inverse transform (shared with their TGQ and TQI video),
// fixed point: 181 is 1/sqrt(2), 669 cos(pi/8)*sqrt(2), 277 sin(pi/8)*sqrt(2)
// and 196 sin(pi/8), in 8 and 9 fraction bits. `in` and `out` step by
// `step`.
template <typename In, typename Out, typename Store>
void inverse8(const In* in, int step, Out* out, Store store) {
    auto at = [&](int k) { return static_cast<int>(in[k * step]); };
    const int sum17 = at(1) + at(7), diff17 = at(1) - at(7);
    const int sum53 = at(5) + at(3), diff53 = at(5) - at(3);
    const int sum26 = at(2) + at(6), diff26 = (181 * (at(2) - at(6))) >> 8;
    const int sum04 = at(0) + at(4), diff04 = at(0) - at(4);
    const int odd1 = ((669 - 196) * diff17 - 196 * diff53) >> 9;
    const int odd2 = ((277 + 196) * diff53 + 196 * diff17) >> 9;
    const int mid = (181 * (sum17 - sum53)) >> 8;
    const int e0 = sum04 + sum26 + diff26, e1 = diff04 + diff26, e2 = diff04 - diff26, e3 = sum04 - sum26 - diff26;
    const int o0 = odd1 + sum17 + sum53, o1 = odd1 + mid, o2 = odd2 + mid, o3 = odd2;
    const int v[8] = {e0 + o0, e1 + o1, e2 + o2, e3 + o3, e3 - o3, e2 - o2, e1 - o1, e0 - o0};
    for (int k = 0; k < 8; ++k)
        out[k * step] = store(v[k]);
}

void inverseTransform(int16_t block[64], uint8_t* dst, int stride) {
    // Columns into 16-bit values, then rows to pixels, rounded: the 4 added
    // to the DC term is a half in the rows' 4 fraction bits.
    block[0] = static_cast<int16_t>(block[0] + 4);
    int16_t temp[64];
    for (int i = 0; i < 8; ++i)
        inverse8(block + i, 8, temp + i, [](int x) { return static_cast<int16_t>(x); });
    for (int i = 0; i < 8; ++i)
        inverse8(temp + 8 * i, 1, dst + i * stride,
                 [](int x) { return static_cast<uint8_t>(std::clamp(x >> 4, 0, 255)); });
}

} // namespace

uint32_t MadDecoder::peek(int n) const {
    // Two words from the one holding the next bit.
    const size_t word = bitPos_ >> 4;
    auto get = [&](size_t w) -> uint32_t { return 2 * w + 1 < bitCount_ / 8 ? le16(bits_ + 2 * w) : 0; };
    const uint32_t window = get(word) << 16 | get(word + 1);
    return (window << (bitPos_ & 15)) >> (32 - n);
}

uint32_t MadDecoder::read(int n) {
    const uint32_t v = peek(n);
    bitPos_ += n;
    return v;
}

int MadDecoder::readSigned(int n) {
    const uint32_t v = read(n);
    return (v & (1u << (n - 1))) ? static_cast<int>(v) - (1 << n) : static_cast<int>(v);
}

int MadDecoder::readMotion() {
    // 0, or a 4-bit magnitude less one, its sign biased: 1..16 or -16..-1.
    if (!read(1))
        return 0;
    const int value = read(1) ? -17 : 0;
    return value + static_cast<int>(read(4)) + 1;
}

bool MadDecoder::decodeIntraBlock(int16_t block[64]) {
    const auto& table = acTable();
    const auto& scan = zigzag();
    block[0] = static_cast<int16_t>((128 + readSigned(8)) * quant_[0]);
    int i = 0;
    while (true) {
        const AcEntry e = table[peek(16)];
        if (e.length == 0)
            return false;
        bitPos_ += e.length;
        if (e.run == 0xff)
            return true;
        int level, run;
        bool negative;
        if (e.level == 0) {
            level = readSigned(10);
            run = static_cast<int>(read(6));
            negative = level < 0;
            level = std::abs(level);
        } else {
            level = e.level;
            run = e.run;
            negative = read(1);
        }
        i += run + 1;
        if (i > 63)
            return false;
        const int j = scan[i];
        // Scaled and forced odd, as MPEG-1 does against mismatch.
        int value = (((level * quant_[j]) >> 4) - 1) | 1;
        if (negative)
            value = -value;
        block[j] = static_cast<int16_t>(value);
    }
}

void MadDecoder::copyBlock(int index, int mbX, int mbY, int mvX, int mvY, int add) {
    // From the previous picture, moved by the macroblock's vector (halved
    // for chroma, toward zero), plus a brightness change. A source outside
    // the picture leaves the block as it was.
    int plane, x, y, rows, s;
    if (index < 4) {
        plane = 0;
        x = mbX * 16 + (index & 1) * 8;
        y = mbY * 16 + (index & 2) * 4;
        rows = height_;
        s = stride_;
    } else {
        plane = index - 3;
        x = mbX * 8;
        y = mbY * 8;
        mvX /= 2;
        mvY /= 2;
        rows = height_ / 2;
        s = stride_ / 2;
    }
    const long from = static_cast<long>(y + mvY) * s + x + mvX;
    if (from < 0 || from >= static_cast<long>(rows - 7) * s - 7)
        return;
    const uint8_t* src = previous_[plane].data() + from;
    uint8_t* dst = current_[plane].data() + static_cast<size_t>(y) * s + x;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            dst[r * s + c] = static_cast<uint8_t>(std::clamp(src[r * s + c] + add, 0, 255));
}

void MadDecoder::putBlock(int index, int mbX, int mbY, int16_t block[64]) {
    if (index < 4)
        inverseTransform(block,
                         current_[0].data() + static_cast<size_t>(mbY * 16 + (index & 2) * 4) * stride_ + mbX * 16 +
                             (index & 1) * 8,
                         stride_);
    else
        inverseTransform(block, current_[index - 3].data() + static_cast<size_t>(mbY * 8) * (stride_ / 2) + mbX * 8,
                         stride_ / 2);
}

void MadDecoder::decodeMacroblock(bool inter, int mbX, int mbY) {
    // Six blocks: four luma, then U and V. In an inter frame a prefix says
    // which are copies: "1" all, "01" those set in a 6-bit map, "00" none;
    // the copies share one motion vector.
    int map = 0, mvX = 0, mvY = 0;
    if (inter) {
        const int kind = read(1) ? 0 : 2 - static_cast<int>(read(1));
        if (kind < 2) {
            map = kind ? static_cast<int>(read(6)) : 63;
            mvX = readMotion();
            mvY = readMotion();
        }
    }
    for (int j = 0; j < 6; ++j) {
        if (map & (1 << j)) {
            const int add = 2 * readMotion();
            copyBlock(j, mbX, mbY, mvX, mvY, add);
        } else {
            int16_t block[64] = {};
            if (!decodeIntraBlock(block)) {
                damaged_ = true;
                return;
            }
            putBlock(j, mbX, mbY, block);
        }
    }
}

Error MadDecoder::decode(const uint8_t* chunk, size_t size) {
    if (size < kFrameHeader + 2)
        return Error::fail("a video frame is too short");
    const char kind = static_cast<char>(chunk[3]);
    if (std::memcmp(chunk, "MAD", 3) != 0 || (kind != 'k' && kind != 'm' && kind != 'e'))
        return Error::fail("not a MAD video frame");
    const bool inter = kind != 'k';
    const int w = le16(chunk + 16), h = le16(chunk + 18);
    if (w < 16 || h < 16 || w > 4096 || h > 4096)
        return Error::fail("a video frame of " + std::to_string(w) + "x" + std::to_string(h));
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        stride_ = (w + 15) & ~15;
        rows_ = (h + 15) & ~15;
        const size_t luma = static_cast<size_t>(stride_) * rows_;
        for (int p = 0; p < 3; ++p) {
            current_[p].assign(p ? luma / 4 : luma, p ? 0x80 : 0);
            previous_[p] = current_[p];
        }
        havePrevious_ = false;
    }
    // With no picture to refer to, an inter frame works from black.
    if (inter && !havePrevious_)
        for (int p = 0; p < 3; ++p)
            std::fill(previous_[p].begin(), previous_[p].end(), p ? 0x80 : 0);
    if (inter)
        for (int p = 0; p < 3; ++p)
            current_[p] = previous_[p];

    const int qscale = chunk[21];
    const auto& scales = inverseAanScales();
    quant_[0] = (scales[0] * kIntraMatrix[0]) >> 11;
    for (int i = 1; i < 64; ++i)
        quant_[i] = (scales[i] * kIntraMatrix[i] * qscale + 32) >> 10;

    bits_ = chunk + kFrameHeader;
    bitCount_ = (size - kFrameHeader) * 8;
    bitPos_ = 0;
    damaged_ = false;
    for (int mbY = 0; mbY < rows_ / 16 && !damaged_; ++mbY)
        for (int mbX = 0; mbX < stride_ / 16 && !damaged_; ++mbX)
            decodeMacroblock(inter, mbX, mbY);
    bits_ = nullptr;
    if (damaged_)
        return Error::fail("a video frame's blocks are damaged");
    if (bitPos_ > bitCount_)
        return Error::fail("a video frame's data ends early");
    if (kind != 'e') {
        for (int p = 0; p < 3; ++p)
            previous_[p] = current_[p];
        havePrevious_ = true;
    }
    return Error::success();
}

void MadDecoder::toSurface(Surface& out) const {
    // Full-range YCbCr (the DC term is centred on 128), BT.601 weights.
    out.create(width_, height_, PixelFormat::RGBA32);
    const int cs = stride_ / 2;
    for (int y = 0; y < height_; ++y) {
        uint8_t* dst = out.row(y);
        const uint8_t* yr = current_[0].data() + static_cast<size_t>(y) * stride_;
        const uint8_t* ur = current_[1].data() + static_cast<size_t>(y / 2) * cs;
        const uint8_t* vr = current_[2].data() + static_cast<size_t>(y / 2) * cs;
        for (int x = 0; x < width_; ++x) {
            const int l = yr[x] << 16, u = ur[x / 2] - 128, v = vr[x / 2] - 128;
            // 1.402, 0.344136, 0.714136 and 1.772 in 16 fraction bits.
            const int r = (l + 91881 * v + 32768) >> 16;
            const int g = (l - 22554 * u - 46802 * v + 32768) >> 16;
            const int b = (l + 116130 * u + 32768) >> 16;
            dst[4 * x] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            dst[4 * x + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            dst[4 * x + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            dst[4 * x + 3] = 255;
        }
    }
}

Error MadMovie::open(std::vector<uint8_t> data) {
    data_ = std::move(data);
    frames_.clear();
    audioBlocks_.clear();
    haveAudio_ = false;
    width_ = height_ = 0;
    duration_ = 0;
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .mad file: " + why); };
    // Chunks as in a .mus: a tag, a u32 size with the 8 bytes included.
    size_t at = 0;
    while (at + 8 <= data_.size()) {
        const uint32_t size = le32(&data_[at + 4]);
        if (size < 8 || at + size > data_.size())
            return bad("a chunk runs past the end");
        const uint8_t* c = &data_[at];
        if (std::memcmp(c, "MAD", 3) == 0 && (c[3] == 'k' || c[3] == 'm' || c[3] == 'e')) {
            if (size < kFrameHeader)
                return bad("a frame too short for its header");
            if (frames_.empty()) {
                duration_ = le32(c + 12);
                width_ = le16(c + 16);
                height_ = le16(c + 18);
            }
            frames_.push_back({static_cast<uint32_t>(at), size, static_cast<char>(c[3])});
        } else if (std::memcmp(c, "SCHl", 4) == 0) {
            if (haveAudio_)
                return bad("a second audio header");
            if (Error err = parseSoundHeader(data_.data(), at + size, at + 8, audio_))
                return bad(err.message);
            haveAudio_ = true;
        } else if (std::memcmp(c, "SCDl", 4) == 0) {
            if (!haveAudio_)
                return bad("audio data before its header");
            audioBlocks_.push_back({static_cast<uint32_t>(at + 8), size - 8});
        }
        // SCCl (the block count), SCEl and anything else: nothing to keep.
        at += size;
    }
    if (at != data_.size())
        return bad("it ends inside a chunk");
    if (frames_.empty())
        return bad("no video frames");
    return Error::success();
}

Error MadMovie::decodeAudio(audio::Sound& out) const {
    out = {};
    if (!haveAudio_)
        return Error::fail("the movie has no audio");
    if (audio_.compression != SoundHeader::kEaXa || audio_.channels != 2)
        return Error::fail("only stereo EA-XA movie audio is known");
    out.rate = audio_.rate;
    out.channels = 2;
    out.format = audio::kFormatEaXa;
    out.samples.reserve(size_t(audio_.samples) * 2);
    for (const auto& [offset, size] : audioBlocks_)
        if (Error err = audio::decodeEaXaStereoBlock(data_.data() + offset, size, out.samples))
            return err;
    return Error::success();
}

Error MadMovie::decodeFrame(MadDecoder& decoder, size_t index) const {
    if (index >= frames_.size())
        return Error::fail("no frame " + std::to_string(index));
    return decoder.decode(data_.data() + frames_[index].offset, frames_[index].size);
}

} // namespace reluva::nfs
