// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// FLIC animations: docs/common/video/flic.md.

#include "video/flic.h"

#include "common/bytes.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace reluva::flic {

namespace {

constexpr size_t kHeaderSize = 128;
constexpr size_t kFrameHeaderSize = 16;
constexpr size_t kChunkHeaderSize = 6;
// A chunk before the first frame holding Animator's own settings.
constexpr uint16_t kPrefixType = 0xF100;

// The chunks a frame can hold.
enum ChunkType : uint16_t {
    kColor256 = 4,
    kDeltaFlc = 7,
    kColor64 = 11,
    kDeltaFli = 12,
    kBlack = 13,
    kByteRun = 15,
    kCopy = 16,
};

} // namespace

bool Decoder::loadHeader(const uint8_t *data, size_t size) {
    if (size < kHeaderSize)
        return false;
    const uint16_t magic = readLE16(data + 4);
    if (magic != kFli && magic != kFlc)
        return false;
    const int w = readLE16(data + 8);
    const int h = readLE16(data + 10);
    const uint16_t depth = readLE16(data + 12);
    if (w <= 0 || h <= 0 || (depth != 8 && depth != 0))
        return false;
    _frameCount = readLE16(data + 6);
    // FLI counts in 70ths of a second, FLC in milliseconds.
    const uint32_t speed = readLE32(data + 16);
    _frameMicros = magic == kFli ? uint32_t((uint64_t(speed ? speed : 1) * 1000000) / 70) : (speed ? speed : 1) * 1000;
    _surface.create(w, h, PixelFormat::kCLUT8);
    std::memset(_palette, 0, sizeof _palette);
    _curFrame = -1;
    _paletteChanged = false;
    const uint32_t firstFrame = magic == kFlc ? readLE32(data + 80) : 0;
    _nextFramePos = firstFrame >= kHeaderSize ? firstFrame : int64_t(kHeaderSize);
    return true;
}

bool Decoder::load(std::unique_ptr<SeekableReadStream> stream) {
    _stream.reset();
    uint8_t header[kHeaderSize];
    if (!stream || stream->read(header, kHeaderSize) != kHeaderSize || !loadHeader(header, kHeaderSize))
        return false;
    _stream = std::move(stream);
    return true;
}

bool Decoder::decodeNextFrame() {
    if (!_stream || endOfVideo())
        return false;
    for (;;) {
        uint8_t head[kChunkHeaderSize];
        if (!_stream->seek(_nextFramePos) || _stream->read(head, kChunkHeaderSize) != kChunkHeaderSize)
            return false;
        const uint32_t size = readLE32(head);
        if (size < kChunkHeaderSize || _nextFramePos + size > _stream->size())
            return false;
        _nextFramePos += size;
        if (readLE16(head + 4) == kPrefixType)
            continue;
        std::vector<uint8_t> frame(size);
        std::memcpy(frame.data(), head, kChunkHeaderSize);
        if (_stream->read(frame.data() + kChunkHeaderSize, size - uint32_t(kChunkHeaderSize)) !=
            size - kChunkHeaderSize)
            return false;
        return decodeFrame(frame.data(), frame.size());
    }
}

bool Decoder::decodeFrame(const uint8_t *data, size_t size) {
    _paletteChanged = false;
    if (size < kFrameHeaderSize || readLE16(data + 4) != kFrameType)
        return false;
    const size_t frameSize = readLE32(data);
    if (frameSize < kFrameHeaderSize || frameSize > size)
        return false;
    const int chunks = readLE16(data + 6);
    size_t at = kFrameHeaderSize;
    for (int i = 0; i < chunks; i++) {
        if (at + kChunkHeaderSize > frameSize)
            return false;
        const uint32_t chunkSize = readLE32(data + at);
        if (chunkSize < kChunkHeaderSize || at + chunkSize > frameSize)
            return false;
        const uint8_t *body = data + at + kChunkHeaderSize;
        const size_t bodySize = chunkSize - kChunkHeaderSize;
        bool ok = true;
        switch (readLE16(data + at + 4)) {
        case kColor256:
            ok = decodeColor(body, bodySize, 0);
            break;
        case kColor64:
            ok = decodeColor(body, bodySize, 2);
            break;
        case kDeltaFlc:
            ok = decodeDeltaFlc(body, bodySize);
            break;
        case kDeltaFli:
            ok = decodeDeltaFli(body, bodySize);
            break;
        case kBlack:
            std::fill(_surface.pixels.begin(), _surface.pixels.end(), 0);
            break;
        case kByteRun:
            ok = decodeByteRun(body, bodySize);
            break;
        case kCopy:
            // Some writers sized this chunk 2 bytes short of its picture; the
            // frame's size holds it all (one of LEGO Island's).
            if (frameSize - (at + kChunkHeaderSize) < size_t(_surface.w) * size_t(_surface.h))
                return false;
            for (int y = 0; y < _surface.h; y++)
                std::memcpy(_surface.row(y), body + size_t(y) * size_t(_surface.w), size_t(_surface.w));
            break;
        default: // a postage stamp, or a chunk of a writer's own: nothing to draw
            break;
        }
        if (!ok)
            return false;
        at += chunkSize;
    }
    _curFrame++;
    return true;
}

// Packets of colours: how many entries to skip, then how many to set (0 for
// all 256), each 3 bytes of 0-63 (`shift` 2) or 0-255.
bool Decoder::decodeColor(const uint8_t *data, size_t size, int shift) {
    if (size < 2)
        return false;
    const int packets = readLE16(data);
    size_t at = 2;
    int index = 0;
    for (int p = 0; p < packets; p++) {
        if (at + 2 > size)
            return false;
        index += data[at];
        int count = data[at + 1];
        at += 2;
        if (count == 0)
            count = 256;
        if (at + size_t(count) * 3 > size)
            return false;
        for (int c = 0; c < count && index < 256; c++, index++)
            for (int k = 0; k < 3; k++) {
                const uint8_t v = data[at + size_t(c) * 3 + k];
                _palette[index * 3 + k] = shift ? uint8_t((v << shift) | (v >> (6 - shift))) : v;
            }
        at += size_t(count) * 3;
    }
    _paletteChanged = true;
    return true;
}

// Every line in runs: a positive count repeats the next byte, a negative
// one copies that many bytes. The line's leading packet count is ignored,
// as writers didn't keep it right for wide pictures.
bool Decoder::decodeByteRun(const uint8_t *data, size_t size) {
    size_t at = 0;
    for (int y = 0; y < _surface.h; y++) {
        uint8_t *row = _surface.row(y);
        if (at >= size)
            return false;
        at++;
        for (int x = 0; x < _surface.w;) {
            if (at >= size)
                return false;
            const int count = int8_t(data[at++]);
            if (count > 0) {
                if (at >= size)
                    return false;
                const int n = std::min(count, _surface.w - x);
                std::memset(row + x, data[at++], size_t(n));
                x += count;
            } else if (count < 0) {
                const int n = -count;
                if (at + size_t(n) > size)
                    return false;
                std::memcpy(row + x, data + at, size_t(std::min(n, _surface.w - x)));
                at += size_t(n);
                x += n;
            }
        }
    }
    return true;
}

// FLI's changes: lines to skip, lines changed, then for each a packet
// count and packets of a column skip and a count: positive copies that
// many bytes, negative repeats the next byte.
bool Decoder::decodeDeltaFli(const uint8_t *data, size_t size) {
    if (size < 4)
        return false;
    int y = readLE16(data);
    const int lines = readLE16(data + 2);
    size_t at = 4;
    for (int l = 0; l < lines; l++, y++) {
        if (at >= size || y >= _surface.h)
            return false;
        uint8_t *row = _surface.row(y);
        const int packets = data[at++];
        int x = 0;
        for (int p = 0; p < packets; p++) {
            if (at + 2 > size)
                return false;
            x += data[at];
            const int count = int8_t(data[at + 1]);
            at += 2;
            const int n = count < 0 ? -count : count;
            if (x + n > _surface.w || at + size_t(count < 0 ? 1 : n) > size)
                return false;
            if (count < 0)
                std::memset(row + x, data[at++], size_t(n));
            else
                std::memcpy(row + x, data + at, size_t(n)), at += size_t(n);
            x += n;
        }
    }
    return true;
}

// FLC's changes, by 16-bit words: for each changed line, words before its
// packet count may skip lines (top bits 11, as a negative count) or set the
// line's last byte (top bits 10, in their low byte); then packets of a
// column skip in bytes and a count of words, copied if positive, the next
// word repeated if negative.
bool Decoder::decodeDeltaFlc(const uint8_t *data, size_t size) {
    if (size < 2)
        return false;
    const int lines = readLE16(data);
    size_t at = 2;
    int y = 0;
    for (int l = 0; l < lines; l++, y++) {
        int packets = -1;
        while (packets < 0) {
            if (at + 2 > size)
                return false;
            const uint16_t word = readLE16(data + at);
            at += 2;
            if ((word & 0xC000) == 0xC000)
                y += -int16_t(word);
            else if ((word & 0xC000) == 0x8000) {
                if (y >= _surface.h)
                    return false;
                _surface.row(y)[_surface.w - 1] = uint8_t(word);
            } else if ((word & 0xC000) == 0)
                packets = word;
            else
                return false;
        }
        if (y >= _surface.h)
            return false;
        uint8_t *row = _surface.row(y);
        int x = 0;
        for (int p = 0; p < packets; p++) {
            if (at + 2 > size)
                return false;
            x += data[at];
            const int count = int8_t(data[at + 1]);
            at += 2;
            const int bytes = 2 * (count < 0 ? -count : count);
            if (x + bytes > _surface.w || at + size_t(count < 0 ? 2 : bytes) > size)
                return false;
            if (count < 0) {
                for (int i = 0; i < bytes; i += 2) {
                    row[x + i] = data[at];
                    row[x + i + 1] = data[at + 1];
                }
                at += 2;
            } else {
                std::memcpy(row + x, data + at, size_t(bytes));
                at += size_t(bytes);
            }
            x += bytes;
        }
    }
    return true;
}

} // namespace reluva::flic
