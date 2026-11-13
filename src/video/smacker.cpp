// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Smacker decoding follows the format as documented by the open-source
// decoders (FFmpeg's libavcodec/smacker.c, libsmacker, ScummVM's video/smk).

#include "video/smacker.h"

#include "common/bytes.h"
#include "common/log.h"

#include <cstring>

namespace reluva::smacker {

namespace {

const uint32_t kFlagRingFrame = 1;
const uint32_t kFlagYInterlace = 2;
const uint32_t kFlagYDouble = 4;

const uint32_t kAudioPacked = 0x80000000;
const uint32_t kAudio16Bit = 0x20000000;
const uint32_t kAudioStereo = 0x10000000;
const uint32_t kAudioBink = 0x08000000;

enum BlockType { kBlockMono = 0, kBlockFull = 1, kBlockSkip = 2, kBlockFill = 3 };

// Run lengths of the block type codes.
const int kBlockRuns[64] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,  13,  14,  15,   16,
                            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,  29,  30,  31,   32,
                            33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,  45,  46,  47,   48,
                            49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048};

} // namespace

// Bits are read least significant first.
struct Decoder::BitReader {
    const uint8_t *data;
    uint32_t size;
    uint32_t bit = 0;

    BitReader(const uint8_t *d, uint32_t s) : data(d), size(s) {}

    bool overrun() const { return bit > size * 8; }
    uint32_t getBit() {
        if (bit >= size * 8) {
            bit++;
            return 0;
        }
        uint32_t v = (data[bit >> 3] >> (bit & 7)) & 1;
        bit++;
        return v;
    }
    uint32_t getBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++)
            v |= getBit() << i;
        return v;
    }
};

namespace {

// An 8-bit Huffman tree: nodes hold child indexes, leaves a byte.
struct SmallTree {
    struct Node {
        int child[2] = {-1, -1};
        uint8_t value = 0;
    };
    std::vector<Node> nodes; // empty: always 0

    template<class Reader>
    bool read(Reader &br) {
        nodes.clear();
        if (!br.getBit())
            return true; // not present
        if (!readNode(br, 0))
            return false;
        br.getBit(); // end of tree
        return !br.overrun();
    }

    template<class Reader>
    bool readNode(Reader &br, int depth) {
        if (depth > 32 || br.overrun() || nodes.size() > 512)
            return false;
        int index = int(nodes.size());
        nodes.emplace_back();
        if (br.getBit()) {
            int left = int(nodes.size());
            if (!readNode(br, depth + 1))
                return false;
            int right = int(nodes.size());
            if (!readNode(br, depth + 1))
                return false;
            nodes[index].child[0] = left;
            nodes[index].child[1] = right;
        } else {
            nodes[index].value = uint8_t(br.getBits(8));
        }
        return true;
    }

    template<class Reader>
    uint8_t decode(Reader &br) const {
        if (nodes.empty())
            return 0;
        int i = 0;
        while (nodes[i].child[0] >= 0)
            i = nodes[i].child[br.getBit()];
        return nodes[i].value;
    }
};

} // namespace

// A 16-bit tree built from two 8-bit trees. Three "escape" leaves don't hold a
// value: they return the last three values decoded (a recency cache).
struct Decoder::BigTree {
    struct Node {
        int child[2] = {-1, -1};
        uint16_t value = 0;
    };
    std::vector<Node> nodes;
    int last[3] = {-1, -1, -1}; // the escape leaves

    bool read(BitReader &br, uint32_t maxNodes) {
        nodes.clear();
        last[0] = last[1] = last[2] = -1;
        if (!br.getBit()) {
            // Not present: every code is 0.
            nodes.emplace_back();
            return true;
        }
        SmallTree lo;
        SmallTree hi;
        if (!lo.read(br) || !hi.read(br))
            return false;
        uint16_t escapes[3];
        for (int i = 0; i < 3; i++)
            escapes[i] = uint16_t(br.getBits(16));
        if (!readNode(br, lo, hi, escapes, 0, maxNodes))
            return false;
        br.getBit(); // end of tree
        // Escape values that never occur still need a cache slot.
        for (int i = 0; i < 3; i++) {
            if (last[i] < 0) {
                last[i] = int(nodes.size());
                nodes.emplace_back();
            }
        }
        return !br.overrun();
    }

    bool readNode(BitReader &br, const SmallTree &lo, const SmallTree &hi, const uint16_t *escapes, int depth,
                  uint32_t maxNodes) {
        if (depth > 64 || br.overrun() || nodes.size() >= maxNodes)
            return false;
        int index = int(nodes.size());
        nodes.emplace_back();
        if (br.getBit()) {
            int left = int(nodes.size());
            if (!readNode(br, lo, hi, escapes, depth + 1, maxNodes))
                return false;
            int right = int(nodes.size());
            if (!readNode(br, lo, hi, escapes, depth + 1, maxNodes))
                return false;
            nodes[index].child[0] = left;
            nodes[index].child[1] = right;
            return true;
        }
        uint16_t v = uint16_t(lo.decode(br) | (hi.decode(br) << 8));
        for (int i = 0; i < 3; i++) {
            if (v == escapes[i]) {
                last[i] = index;
                v = 0;
                break;
            }
        }
        nodes[index].value = v;
        return true;
    }

    void resetCache() {
        for (int i = 0; i < 3; i++)
            if (last[i] >= 0)
                nodes[last[i]].value = 0;
    }

    uint16_t decode(BitReader &br) {
        int i = 0;
        while (nodes[i].child[0] >= 0)
            i = nodes[i].child[br.getBit()];
        uint16_t v = nodes[i].value;
        if (last[0] >= 0 && v != nodes[last[0]].value) {
            nodes[last[2]].value = nodes[last[1]].value;
            nodes[last[1]].value = nodes[last[0]].value;
            nodes[last[0]].value = v;
        }
        return v;
    }
};

Decoder::Decoder() {
}

Decoder::~Decoder() {
}

void Decoder::close() {
    _stream.reset();
    _frameSizes.clear();
    _frameTypes.clear();
    _curFrame = -1;
    _audioTrack = -1;
    _audio.clear();
}

bool Decoder::load(std::unique_ptr<SeekableReadStream> stream) {
    close();
    if (!stream)
        return false;
    uint8_t header[104];
    if (stream->read(header, sizeof(header)) != sizeof(header))
        return false;
    if (std::memcmp(header, "SMK2", 4) != 0 && std::memcmp(header, "SMK4", 4) != 0)
        return false;
    _smk4 = header[3] == '4';
    _width = int(readLE32(header + 4));
    _height = int(readLE32(header + 8));
    _frameCount = int(readLE32(header + 12));
    int32_t rate = int32_t(readLE32(header + 16));
    _flags = readLE32(header + 20);
    if (_width <= 0 || _height <= 0 || _width > 4096 || _height > 4096 || _frameCount <= 0 || _frameCount > 1000000)
        return false;
    // Frame rate: positive = ms per frame, negative = units of 10 us, 0 = 10 fps.
    _frameMicros = rate > 0 ? uint32_t(rate) * 1000 : rate < 0 ? uint32_t(-rate) * 10 : 100000;
    uint32_t treesSize = readLE32(header + 52);
    uint32_t treeAlloc[4];
    for (int i = 0; i < 4; i++)
        treeAlloc[i] = readLE32(header + 56 + i * 4);
    for (int i = 0; i < 7; i++) {
        _audioRates[i] = readLE32(header + 72 + i * 4);
        if (_audioTrack < 0 && (_audioRates[i] & 0xFFFFFF) && !(_audioRates[i] & kAudioBink))
            _audioTrack = i;
    }

    int frames = _frameCount + ((_flags & kFlagRingFrame) ? 1 : 0);
    _frameSizes.resize(frames);
    _frameTypes.resize(frames);
    for (int i = 0; i < frames; i++)
        _frameSizes[i] = stream->readUint32LE();
    for (int i = 0; i < frames; i++)
        _frameTypes[i] = stream->readByte();
    std::vector<uint8_t> trees(treesSize);
    if (stream->err() || stream->read(trees.data(), treesSize) != treesSize)
        return false;

    _mmap.reset(new BigTree());
    _mclr.reset(new BigTree());
    _full.reset(new BigTree());
    _type.reset(new BigTree());
    BitReader br(trees.data(), treesSize);
    // The header gives each tree's allocation in bytes (4 per node in RAD's decoder).
    BigTree *bigTrees[4] = {_mmap.get(), _mclr.get(), _full.get(), _type.get()};
    for (int i = 0; i < 4; i++) {
        if (!bigTrees[i]->read(br, std::max<uint32_t>(treeAlloc[i] / 4, 16) + 16)) {
            log::warning("Smacker: bad Huffman tree %d", i);
            return false;
        }
    }

    draw::create(_frame, _width, _height);
    _displayHeight = (_flags & (kFlagYInterlace | kFlagYDouble)) ? _height * 2 : _height;
    draw::create(_surface, _width, _displayHeight);
    std::memset(_palette, 0, sizeof(_palette));
    _nextFramePos = stream->pos();
    _stream = std::move(stream);
    return true;
}

int Decoder::audioRate() const {
    return _audioTrack >= 0 ? int(_audioRates[_audioTrack] & 0xFFFFFF) : 0;
}

bool Decoder::audioStereo() const {
    return _audioTrack >= 0 && (_audioRates[_audioTrack] & kAudioStereo);
}

std::vector<int16_t> Decoder::takeAudio() {
    std::vector<int16_t> out;
    out.swap(_audio);
    return out;
}

bool Decoder::decodeNextFrame() {
    if (!_stream || _curFrame + 1 >= _frameCount)
        return false;
    _curFrame++;
    uint32_t size = _frameSizes[_curFrame] & ~3u;
    std::vector<uint8_t> data(size);
    _stream->seek(_nextFramePos);
    if (_stream->read(data.data(), size) != size)
        return false;
    _nextFramePos += size;

    const uint8_t *p = data.data();
    const uint8_t *end = p + size;
    uint8_t type = _frameTypes[_curFrame];
    _paletteChanged = false;
    if (type & 1) {
        // Palette chunk: its size in units of 4 bytes comes first.
        uint32_t chunk = p < end ? uint32_t(*p) * 4 : 0;
        if (chunk == 0 || chunk > uint32_t(end - p))
            return false;
        decodePalette(p + 1, chunk - 1);
        p += chunk;
        _paletteChanged = true;
    }
    for (int track = 0; track < 7; track++) {
        if (!(type & (2 << track)))
            continue;
        if (end - p < 4)
            return false;
        uint32_t chunk = readLE32(p);
        if (chunk < 4 || chunk > uint32_t(end - p))
            return false;
        if (track == _audioTrack)
            decodeAudio(p + 4, chunk - 4);
        p += chunk;
    }
    return decodeVideo(p, uint32_t(end - p));
}

void Decoder::decodePalette(const uint8_t *data, uint32_t size) {
    uint8_t old[768];
    std::memcpy(old, _palette, sizeof(old));
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    int i = 0;
    while (i < 256 && p < end) {
        uint8_t b = *p++;
        if (b & 0x80) {
            // Keep the next n entries.
            int n = (b & 0x7F) + 1;
            for (int k = 0; k < n && i < 256; k++, i++)
                std::memcpy(_palette + i * 3, old + i * 3, 3);
        } else if (b & 0x40) {
            // Copy n entries from elsewhere in the old palette.
            int n = (b & 0x3F) + 1;
            if (p >= end)
                break;
            int src = *p++;
            for (int k = 0; k < n && i < 256 && src + k < 256; k++, i++)
                std::memcpy(_palette + i * 3, old + (src + k) * 3, 3);
        } else {
            // One colour, 6 bits per channel.
            if (end - p < 2)
                break;
            uint8_t c[3] = {uint8_t(b & 0x3F), uint8_t(p[0] & 0x3F), uint8_t(p[1] & 0x3F)};
            p += 2;
            for (int k = 0; k < 3; k++)
                _palette[i * 3 + k] = uint8_t((c[k] << 2) | (c[k] >> 4));
            i++;
        }
    }
}

void Decoder::decodeAudio(const uint8_t *data, uint32_t size) {
    uint32_t rate = _audioRates[_audioTrack];
    bool stereo = (rate & kAudioStereo) != 0;
    bool bits16 = (rate & kAudio16Bit) != 0;
    if (!(rate & kAudioPacked)) {
        // Raw PCM.
        if (bits16) {
            for (uint32_t i = 0; i + 1 < size; i += 2)
                _audio.push_back(int16_t(readLE16(data + i)));
        } else {
            for (uint32_t i = 0; i < size; i++)
                _audio.push_back(int16_t((int(data[i]) - 128) << 8));
        }
        return;
    }
    // Packed: u32 unpacked size, then a bit stream of Huffman-coded deltas.
    if (size < 4)
        return;
    uint32_t unpacked = readLE32(data);
    BitReader br(data + 4, size - 4);
    if (!br.getBit())
        return; // no data in this frame
    stereo = br.getBit() != 0;
    bits16 = br.getBit() != 0;
    int numTrees = 1 << ((stereo ? 1 : 0) + (bits16 ? 1 : 0));
    SmallTree trees[4];
    for (int i = 0; i < numTrees; i++)
        if (!trees[i].read(br))
            return;
    int channels = stereo ? 2 : 1;
    if (bits16) {
        int16_t pred[2] = {0, 0};
        for (int c = channels - 1; c >= 0; c--) {
            uint16_t v = uint16_t(br.getBits(16));
            pred[c] = int16_t((v >> 8) | (v << 8));
        }
        uint32_t samples = unpacked / 2;
        for (int c = 0; c < channels && uint32_t(c) < samples; c++)
            _audio.push_back(pred[c]);
        for (uint32_t i = uint32_t(channels); i < samples && !br.overrun(); i++) {
            int c = stereo ? int(i & 1) : 0;
            uint16_t lo = trees[c * 2].decode(br);
            uint16_t hi = trees[c * 2 + 1].decode(br);
            pred[c] = int16_t(pred[c] + int16_t(lo | (hi << 8)));
            _audio.push_back(pred[c]);
        }
    } else {
        uint8_t pred[2] = {0, 0};
        for (int c = channels - 1; c >= 0; c--)
            pred[c] = uint8_t(br.getBits(8));
        for (int c = 0; c < channels && uint32_t(c) < unpacked; c++)
            _audio.push_back(int16_t((int(pred[c]) - 128) << 8));
        for (uint32_t i = uint32_t(channels); i < unpacked && !br.overrun(); i++) {
            int c = stereo ? int(i & 1) : 0;
            pred[c] = uint8_t(pred[c] + int8_t(trees[c].decode(br)));
            _audio.push_back(int16_t((int(pred[c]) - 128) << 8));
        }
    }
}

bool Decoder::decodeVideo(const uint8_t *data, uint32_t size) {
    BitReader br(data, size);
    _mmap->resetCache();
    _mclr->resetCache();
    _full->resetCache();
    _type->resetCache();

    int bw = _width / 4;
    int bh = _height / 4;
    int blocks = bw * bh;
    int stride = _frame.pitch;
    int blk = 0;
    while (blk < blocks) {
        if (br.overrun())
            return false;
        uint16_t type = _type->decode(br);
        int run = kBlockRuns[(type >> 2) & 0x3F];
        switch (type & 3) {
        case kBlockMono:
            while (run-- && blk < blocks) {
                uint16_t clr = _mclr->decode(br);
                uint16_t map = _mmap->decode(br);
                uint8_t *out = draw::at(_frame, (blk % bw) * 4, (blk / bw) * 4);
                uint8_t hi = uint8_t(clr >> 8);
                uint8_t lo = uint8_t(clr & 0xFF);
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++)
                        out[x] = (map & (1 << x)) ? hi : lo;
                    map >>= 4;
                    out += stride;
                }
                blk++;
            }
            break;
        case kBlockFull: {
            int mode = 0;
            if (_smk4) {
                if (br.getBit())
                    mode = 1;
                else if (br.getBit())
                    mode = 2;
            }
            while (run-- && blk < blocks) {
                uint8_t *out = draw::at(_frame, (blk % bw) * 4, (blk / bw) * 4);
                if (mode == 0) {
                    for (int y = 0; y < 4; y++) {
                        uint16_t pix = _full->decode(br);
                        out[2] = uint8_t(pix);
                        out[3] = uint8_t(pix >> 8);
                        pix = _full->decode(br);
                        out[0] = uint8_t(pix);
                        out[1] = uint8_t(pix >> 8);
                        out += stride;
                    }
                } else if (mode == 1) {
                    // Double: each code fills two rows, two pixels per byte.
                    for (int half = 0; half < 2; half++) {
                        uint16_t pix = _full->decode(br);
                        for (int y = 0; y < 2; y++) {
                            out[0] = out[1] = uint8_t(pix);
                            out[2] = out[3] = uint8_t(pix >> 8);
                            out += stride;
                        }
                    }
                } else {
                    // Half: two codes per pair of identical rows.
                    for (int half = 0; half < 2; half++) {
                        uint16_t pix2 = _full->decode(br);
                        uint16_t pix1 = _full->decode(br);
                        for (int y = 0; y < 2; y++) {
                            out[0] = uint8_t(pix1);
                            out[1] = uint8_t(pix1 >> 8);
                            out[2] = uint8_t(pix2);
                            out[3] = uint8_t(pix2 >> 8);
                            out += stride;
                        }
                    }
                }
                blk++;
            }
            break;
        }
        case kBlockSkip:
            while (run-- && blk < blocks)
                blk++;
            break;
        case kBlockFill: {
            uint8_t color = uint8_t(type >> 8);
            while (run-- && blk < blocks) {
                uint8_t *out = draw::at(_frame, (blk % bw) * 4, (blk / bw) * 4);
                for (int y = 0; y < 4; y++) {
                    std::memset(out, color, 4);
                    out += stride;
                }
                blk++;
            }
            break;
        }
        }
    }

    // The displayed picture: doubled rows, or interlaced with black lines.
    if (_displayHeight == _height) {
        std::memcpy(_surface.pixels.data(), _frame.pixels.data(), size_t(_width) * _height);
    } else {
        for (int y = 0; y < _height; y++) {
            std::memcpy(draw::at(_surface, 0, y * 2), draw::at(_frame, 0, y), _width);
            if (_flags & kFlagYDouble)
                std::memcpy(draw::at(_surface, 0, y * 2 + 1), draw::at(_frame, 0, y), _width);
            else
                std::memset(draw::at(_surface, 0, y * 2 + 1), 0, _width);
        }
    }
    return true;
}

} // namespace reluva::smacker
