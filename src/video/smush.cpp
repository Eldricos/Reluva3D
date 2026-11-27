// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/smush.h"

#include <algorithm>
#include <cstring>

namespace reluva::smush {

namespace {

uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint16_t le16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint32_t tag(const char *t) {
    return be32(reinterpret_cast<const uint8_t *>(t));
}

// Calls `f(tag, data, size)` for each chunk in [p, end): a tag, a size
// (big-endian) and that many bytes, padded to an even length.
template<typename F>
void chunks(const uint8_t *p, const uint8_t *end, F f) {
    while (end - p >= 8) {
        const uint32_t size = be32(p + 4);
        if (size > uint32_t(end - p - 8))
            return;
        f(be32(p), p + 8, size_t(size));
        p += 8 + size + (size & 1);
    }
}

} // namespace

// Codec 48: the picture in 8x8 blocks on two buffers, each frame decoded
// into one from the other (JKM.EXE 0x567b00, its blocks at 0x56afc3).
struct Decoder::Codec48 {
    int width = 0;
    int height = 0;
    int blocksWide = 0;
    int blocksHigh = 0;
    int pitch = 0;
    std::array<std::vector<uint8_t>, 2> buffers;
    int current = 0;
    int lastSequence = -1;
    // Its motion vectors as offsets in the buffers; and an average of
    // every two colours, as the frames give it.
    std::array<int, 255> offsets{};
    std::vector<uint8_t> blend = std::vector<uint8_t>(65536);

    Codec48(int w, int h) : width(w), height(h) {
        blocksWide = (w + 7) / 8;
        blocksHigh = (h + 7) / 8;
        pitch = blocksWide * 8;
        for (auto &b : buffers)
            b.assign(size_t(pitch) * size_t(blocksHigh) * 8, 0);
        // The vectors' steps on each axis; vector i is (step[i % 16],
        // step[i / 16]), the last of the 256 left out (its table at
        // 0x5a9a7c).
        static constexpr int kSteps[16] = {0, 1, 2, 3, 5, 8, 13, 21, -1, -2, -3, -5, -8, -13, -17, -21};
        for (int i = 0; i < 255; ++i)
            offsets[size_t(i)] = kSteps[i % 16] + kSteps[i / 16] * pitch;
    }

    const std::vector<uint8_t> &shown() const { return buffers[size_t(current)]; }

    void decode(const uint8_t *data, size_t size) {
        if (size < 16)
            return;
        const uint8_t type = data[0];
        const int sequence = le16(data + 2);
        const uint32_t length = le32(data + 4);
        const uint8_t flags = data[12];
        const uint8_t *p = data + 16;
        const uint8_t *end = data + size;
        if (sequence == 0)
            for (auto &b : buffers)
                std::fill(b.begin(), b.end(), 0);
        // A new table of averages: the half above the diagonal, row by row.
        if (flags & 8) {
            if (end - p < 256 * 257 / 2)
                return;
            for (int i = 0; i < 256; ++i)
                for (int j = i; j < 256; ++j)
                    blend[size_t(i * 256 + j)] = blend[size_t(j * 256 + i)] = *p++;
        }
        std::vector<uint8_t> &out = buffers[size_t(current)];
        switch (type) {
        case 0: // raw
            std::copy_n(p, std::min({size_t(length), size_t(end - p), out.size()}), out.begin());
            break;
        case 2: { // runs: half the code byte, plus one, of the next byte (bit 0 set) or of literals
            size_t o = 0;
            while (o < length && o < out.size() && p < end) {
                const uint8_t code = *p++;
                const size_t n = std::min(size_t(code >> 1) + 1, out.size() - o);
                if (code & 1) {
                    if (p >= end)
                        break;
                    std::fill_n(out.begin() + std::ptrdiff_t(o), n, *p++);
                } else {
                    const size_t m = std::min(n, size_t(end - p));
                    std::copy_n(p, m, out.begin() + std::ptrdiff_t(o));
                    p += m;
                }
                o += n;
            }
            break;
        }
        case 3:
            // A frame out of order isn't decoded; the picture stays.
            if (sequence != 0 && sequence != lastSequence + 1)
                break;
            if ((sequence & 1) || !(flags & 1) || (flags & 0x10))
                current ^= 1;
            blocks(p, end);
            break;
        default:
            break;
        }
        lastSequence = sequence;
    }

    // Each 8x8 block by its first byte (0x56b048).
    void blocks(const uint8_t *p, const uint8_t *end) {
        std::vector<uint8_t> &cur = buffers[size_t(current)];
        const std::vector<uint8_t> &prev = buffers[size_t(current ^ 1)];
        const int size = int(cur.size());
        auto copy = [&](int at, int offset, int w, int h) {
            for (int r = 0; r < h; ++r) {
                const int to = at + r * pitch;
                const int from = to + offset;
                if (from >= 0 && from + w <= size && to + w <= size)
                    std::memcpy(&cur[size_t(to)], &prev[size_t(from)], size_t(w));
            }
        };
        auto pixel = [&](int at) { return at >= 0 && at < size ? cur[size_t(at)] : uint8_t(0); };
        auto avg = [&](uint8_t a, uint8_t b) { return blend[size_t(a) * 256 + b]; };
        auto s16 = [](const uint8_t *q) { return int(int16_t(le16(q))); };
        // A 4x4 grid of colours, each made a 2x2 square (0x56b8f2).
        auto grid = [&](int at, const uint8_t *g) {
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    const int o = at + 2 * r * pitch + 2 * c;
                    cur[size_t(o)] = cur[size_t(o + 1)] = cur[size_t(o + pitch)] = cur[size_t(o + pitch + 1)] =
                        g[r * 4 + c];
                }
        };
        for (int by = 0; by < blocksHigh; ++by)
            for (int bx = 0; bx < blocksWide; ++bx) {
                if (p >= end)
                    return;
                const int at = by * 8 * pitch + bx * 8;
                const uint8_t op = *p++;
                auto has = [&](ptrdiff_t n) { return end - p >= n; };
                if (op <= 0xf6) { // the block as it was, moved by a vector of the table
                    copy(at, offsets[op], 8, 8);
                } else if (op == 0xfe && has(2)) { // moved by any offset
                    copy(at, s16(p), 8, 8);
                    p += 2;
                } else if ((op == 0xfc && has(4)) || (op == 0xfb && has(8))) { // each 4x4 quarter moved
                    for (int q = 0; q < 4; ++q) {
                        const int qa = at + (q / 2) * 4 * pitch + (q % 2) * 4;
                        copy(qa, op == 0xfc ? offsets[p[q]] : s16(p + 2 * q), 4, 4);
                    }
                    p += op == 0xfc ? 4 : 8;
                } else if ((op == 0xf9 && has(16)) || (op == 0xf8 && has(32))) { // each 2x2 moved
                    for (int q = 0; q < 16; ++q) {
                        const int qa = at + (q / 4) * 2 * pitch + (q % 4) * 2;
                        copy(qa, op == 0xf9 ? offsets[p[q]] : s16(p + 2 * q), 2, 2);
                    }
                    p += op == 0xf9 ? 16 : 32;
                } else if ((op == 0xff && has(1)) || (op == 0xfd && has(4))) {
                    // One colour, or four, for the grid's lower right
                    // squares; the rest averaged with each other and with the
                    // pixels above and left of the block (0x56b71d, 0x56b7ef).
                    auto up = [&](int c) { return pixel(at - pitch + c); };
                    auto left = [&](int r) { return pixel(at + r * pitch - 1); };
                    uint8_t g[16];
                    if (op == 0xff) {
                        g[15] = *p++;
                        g[7] = avg(g[15], up(7));
                        g[3] = avg(g[7], up(7));
                        g[11] = avg(g[7], g[15]);
                        g[1] = avg(g[3], left(0));
                    } else {
                        g[5] = p[0];
                        g[7] = p[1];
                        g[13] = p[2];
                        g[15] = p[3];
                        p += 4;
                        g[1] = avg(g[5], up(3));
                        g[3] = avg(g[7], up(7));
                        g[11] = avg(g[7], g[15]);
                        g[9] = avg(g[5], g[13]);
                    }
                    g[0] = avg(g[1], left(0));
                    g[2] = avg(g[1], g[3]);
                    if (op == 0xff)
                        g[5] = avg(g[7], left(2));
                    g[4] = avg(g[5], left(2));
                    g[6] = avg(g[5], g[7]);
                    if (op == 0xff)
                        g[9] = avg(g[11], left(4));
                    g[8] = avg(g[9], left(4));
                    g[10] = avg(g[9], g[11]);
                    if (op == 0xff)
                        g[13] = avg(g[15], left(6));
                    g[12] = avg(g[13], left(6));
                    g[14] = avg(g[13], g[15]);
                    grid(at, g);
                } else if (op == 0xfa && has(16)) { // sixteen colours, each a 2x2 square
                    grid(at, p);
                    p += 16;
                } else if (has(64)) { // 0xf7: the block's 64 pixels
                    for (int r = 0; r < 8; ++r)
                        std::memcpy(&cur[size_t(at + r * pitch)], p + r * 8, 8);
                    p += 64;
                } else {
                    return;
                }
            }
    }
};

Decoder::Decoder() = default;
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder &&) noexcept = default;
Decoder &Decoder::operator=(Decoder &&) noexcept = default;

bool Decoder::load(std::vector<uint8_t> data) {
    *this = Decoder();
    _data = std::move(data);
    const uint8_t *begin = _data.data();
    if (_data.size() < 8 || be32(begin) != tag("ANIM"))
        return false;
    const uint8_t *end = begin + std::min(_data.size(), size_t(be32(begin + 4)) + 8);
    bool header = false;
    chunks(begin + 8, end, [&](uint32_t t, const uint8_t *p, size_t size) {
        // AHDR: a version, the frames' count, a word, the palette, then the
        // frames a second and the sound's rate (in the third word after).
        if (t == tag("AHDR") && size >= 6 + 768) {
            header = true;
            std::memcpy(_palette.data(), p + 6, 768);
            if (size >= 6 + 768 + 12) {
                const uint32_t fps = le32(p + 6 + 768);
                if (fps > 0 && fps <= 100)
                    _frameMicros = 1000000 / fps;
                _audioRate = int(le32(p + 6 + 768 + 8));
            }
        } else if (t == tag("FRME")) {
            _frames.push_back(size_t(p - begin));
            _frameSizes.push_back(uint32_t(size));
            // The picture's size, from the first frame object.
            if (_width == 0)
                chunks(p, p + size, [&](uint32_t c, const uint8_t *q, size_t n) {
                    if (c == tag("FOBJ") && n >= 14 && _width == 0) {
                        _width = le16(q + 6);
                        _height = le16(q + 8);
                    }
                });
        }
    });
    if (!header || _frames.empty() || _width <= 0 || _height <= 0 || _width > 4096 || _height > 4096)
        return false;
    _surface.create(_width, _height, PixelFormat::kCLUT8);
    return true;
}

bool Decoder::decodeNextFrame() {
    if (endOfVideo())
        return false;
    ++_frame;
    _subtitle = -1;
    const uint8_t *p = _data.data() + _frames[size_t(_frame)];
    chunks(p, p + _frameSizes[size_t(_frame)], [&](uint32_t t, const uint8_t *q, size_t size) {
        if (t == tag("FOBJ")) {
            if (_storeNext)
                _stored.assign(q, q + size);
            _storeNext = false;
            frameObject(q, size);
        } else if (t == tag("NPAL") && size >= 768) {
            std::memcpy(_palette.data(), q, 768);
        } else if (t == tag("XPAL")) {
            palette(q, size);
        } else if (t == tag("STOR")) {
            _storeNext = true;
        } else if (t == tag("FTCH") && !_stored.empty()) {
            const std::vector<uint8_t> stored = _stored;
            frameObject(stored.data(), stored.size());
        } else if (t == tag("IACT")) {
            audio(q, size);
        } else if (t == tag("TRES") && size >= 18) {
            _subtitle = le16(q + 16);
        }
    });
    if (_codec48) {
        const std::vector<uint8_t> &shown = _codec48->shown();
        for (int y = 0; y < _height && y < _codec48->blocksHigh * 8; ++y)
            std::memcpy(_surface.row(y), &shown[size_t(y) * size_t(_codec48->pitch)], size_t(_width));
    }
    return true;
}

// A frame object: its codec (a byte), place and size (words), then the
// codec's data from byte 14 (0x56a100). Only codec 48 is drawn: the game
// registers no other these files use (0x56a148).
void Decoder::frameObject(const uint8_t *data, size_t size) {
    if (size < 14 || data[0] != 48)
        return;
    if (!_codec48)
        _codec48 = std::make_unique<Codec48>(_width, _height);
    _codec48->decode(data + 14, size - 14);
}

// XPAL by its word at 2: 0x200 a palette (at 0x604) and each channel's
// step (at 4, 768 words), 0 the steps alone, any other one step: each
// channel times 128 moves by its step (JKM.EXE 0x569ce9).
void Decoder::palette(const uint8_t *data, size_t size) {
    if (size < 4)
        return;
    const uint16_t kind = le16(data + 2);
    if (kind == 0 || kind == 0x200) {
        if (kind == 0x200 && size >= 0x604 + 768)
            std::memcpy(_palette.data(), data + 0x604, 768);
        if (size >= 4 + 768 * 2)
            for (size_t i = 0; i < 768; ++i) {
                _fadeStep[i] = int16_t(le16(data + 4 + 2 * i));
                _fade[i] = int16_t(_palette[i] << 7);
            }
        return;
    }
    for (size_t i = 0; i < 768; ++i) {
        _fade[i] = int16_t(uint16_t(_fade[i]) + uint16_t(_fadeStep[i]));
        _palette[i] = uint8_t(uint16_t(_fade[i]) >> 7);
    }
}

// IACT of code 8: after its 18 bytes, packets of sound run on across
// chunks, each a length (2 bytes, big-endian), a byte of two shifts (the
// left channel's in its top four bits), and 1024 stereo samples, each a
// byte shifted up by its channel's, or 0x80 and a whole 16-bit sample,
// big-endian (JKM.EXE 0x565db0).
void Decoder::audio(const uint8_t *data, size_t size) {
    if (size < 18 || le16(data) != 8 || le16(data + 6) != 0)
        return;
    _packet.insert(_packet.end(), data + 18, data + size);
    size_t at = 0;
    while (_packet.size() - at >= 2) {
        const size_t length = size_t(_packet[at]) << 8 | _packet[at + 1];
        if (_packet.size() - at < length + 2)
            break;
        const uint8_t *q = &_packet[at + 2];
        const uint8_t *qend = q + length;
        if (length >= 1) {
            const int shift[2] = {q[0] >> 4, q[0] & 15};
            ++q;
            for (int i = 0; i < 2048 && q < qend; ++i) {
                const uint8_t b = *q++;
                if (b == 0x80 && qend - q >= 2) {
                    _audio.push_back(int16_t(q[0] << 8 | q[1]));
                    q += 2;
                } else {
                    _audio.push_back(int16_t(int(int8_t(b)) * (1 << shift[i & 1])));
                }
            }
        }
        at += length + 2;
    }
    _packet.erase(_packet.begin(), _packet.begin() + std::ptrdiff_t(at));
}

std::vector<int16_t> Decoder::takeAudio() {
    std::vector<int16_t> out;
    out.swap(_audio);
    return out;
}

} // namespace reluva::smush
