// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/raven/savegame.h"

#include "common/md4.h"

#include <algorithm>
#include <cstring>

namespace reluva::idtech3::raven {

namespace {

constexpr uint32_t kMarker = 0x1234abcd;

uint32_t blockChecksum(const uint8_t *data, size_t length) {
    const std::array<uint8_t, 16> d = md4(data, length);
    uint32_t sum = 0;
    for (int i = 0; i < 4; ++i)
        sum ^= uint32_t(d[size_t(4 * i)]) | uint32_t(d[size_t(4 * i + 1)]) << 8 | uint32_t(d[size_t(4 * i + 2)]) << 16 |
               uint32_t(d[size_t(4 * i + 3)]) << 24;
    return sum;
}

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

std::vector<uint8_t> compressRle(const uint8_t *in, size_t length) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < length) {
        // A run of one byte, up to 127 long.
        size_t j = i + 1;
        while (j < length && j - i < 127 && in[j] == in[i])
            ++j;
        if (j - i > 1) {
            out.push_back(uint8_t(j - i));
            out.push_back(in[i]);
            i = j;
            continue;
        }
        // Else bytes as they are, up to 127, stopping at three equal ones
        // in a row (or at 127), then giving back the equal ones taken at
        // the end (which can start the next run).
        if (j < length) {
            bool stopped = false;
            for (size_t count = 1;;) {
                if (count >= 127 || (in[j] == in[j - 1] && (j <= 1 || in[j] == in[j - 2]))) {
                    stopped = true;
                    break;
                }
                ++j;
                ++count;
                if (j >= length)
                    break;
            }
            if (stopped)
                while (j < length && in[j] == in[j - 1])
                    --j;
        }
        out.push_back(uint8_t(-int(j - i)));
        out.insert(out.end(), in + i, in + j);
        i = j;
    }
    return out;
}

bool decompressRle(const uint8_t *data, size_t size, size_t length, std::vector<uint8_t> &out) {
    out.clear();
    out.reserve(length);
    size_t p = 0;
    while (out.size() < length) {
        if (p >= size)
            return false;
        const int n = int8_t(data[p++]);
        if (n > 0) {
            if (p >= size)
                return false;
            out.insert(out.end(), size_t(n), data[p++]);
        } else if (n < 0) {
            if (p + size_t(-n) > size)
                return false;
            out.insert(out.end(), data + p, data + p + size_t(-n));
            p += size_t(-n);
        } else {
            return false; // the original's would never finish
        }
    }
    return out.size() == length;
}

std::string chunkName(uint32_t id) {
    std::string s;
    for (int i = 3; i >= 0; --i) {
        const char c = char((id >> (8 * i)) & 0xff);
        s += c >= 32 && c < 127 ? c : '?';
    }
    return s;
}

void SaveWriter::add(uint32_t id, const void *data, size_t length) {
    const auto *p = static_cast<const uint8_t *>(data);
    if (compress) {
        // Only where it's shorter (0x439430).
        const std::vector<uint8_t> packed = compressRle(p, length);
        if (packed.size() < length) {
            put32(_bytes, id);
            put32(_bytes, uint32_t(-int32_t(length)));
            put32(_bytes, uint32_t(packed.size()));
            put32(_bytes, blockChecksum(p, length));
            _bytes.insert(_bytes.end(), packed.begin(), packed.end());
            put32(_bytes, kMarker);
            return;
        }
    }
    put32(_bytes, id);
    put32(_bytes, uint32_t(length));
    put32(_bytes, blockChecksum(p, length));
    if (length)
        _bytes.insert(_bytes.end(), p, p + length);
    put32(_bytes, kMarker);
}

void SaveWriter::addText(uint32_t id, std::string_view text, size_t size) {
    std::vector<uint8_t> block(size ? size : text.size() + 1, 0);
    std::memcpy(block.data(), text.data(), std::min(text.size(), block.size() - 1));
    add(id, block);
}

bool SaveReader::peek(uint32_t id) const {
    return _position + 8 <= _bytes.size() && get32(_bytes.data() + _position) == id;
}

Error SaveReader::next(uint32_t id, std::vector<uint8_t> &out, size_t length) {
    if (_position + 12 > _bytes.size())
        return Error::fail("The saved game ends before its " + chunkName(id) + " chunk");
    const uint8_t *p = _bytes.data() + _position;
    const uint32_t found = get32(p);
    const int32_t stated = int32_t(get32(p + 4));
    // A negative length: compressed, its stored length next.
    const bool packed = stated < 0;
    const size_t header = packed ? 16 : 12;
    if (_position + header > _bytes.size())
        return Error::fail("The saved game ends before its " + chunkName(id) + " chunk");
    const uint32_t size = packed ? uint32_t(-int64_t(stated)) : uint32_t(stated);
    const uint32_t stored = packed ? get32(p + 8) : size;
    const uint32_t checksum = get32(p + header - 4);
    if (found != id)
        return Error::fail("Expected a " + chunkName(id) + " chunk, found " + chunkName(found));
    if (length && size != length)
        return Error::fail("The " + chunkName(id) + " chunk is " + std::to_string(size) + " bytes, not " +
                           std::to_string(length));
    if (_position + header + 4 + size_t(stored) > _bytes.size())
        return Error::fail("The " + chunkName(id) + " chunk runs past the end of the file");
    const uint8_t *data = p + header;
    if (get32(data + stored) != kMarker)
        return Error::fail("The " + chunkName(id) + " chunk is damaged");
    if (packed) {
        if (!decompressRle(data, stored, size, out))
            return Error::fail("The " + chunkName(id) + " chunk is damaged");
    } else {
        out.assign(data, data + size);
    }
    if (blockChecksum(out.data(), out.size()) != checksum)
        return Error::fail("The " + chunkName(id) + " chunk is damaged");
    _position += header + 4 + size_t(stored);
    return Error::success();
}

Error SaveReader::nextInt(uint32_t id, int32_t &v) {
    std::vector<uint8_t> data;
    if (Error e = next(id, data, 4))
        return e;
    v = int32_t(get32(data.data()));
    return Error::success();
}

Error SaveReader::nextText(uint32_t id, std::string &out, size_t length) {
    std::vector<uint8_t> data;
    if (Error e = next(id, data, length))
        return e;
    const auto end = std::find(data.begin(), data.end(), uint8_t(0));
    out.assign(data.begin(), end);
    return Error::success();
}

} // namespace reluva::idtech3::raven
