// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/raven/savegame.h"

#include "common/md4.h"

#include <algorithm>
#include <cstring>

namespace reluva::idtech3::raven {

namespace {

constexpr uint32_t kMarker = 0x1234abcd;

uint32_t blockChecksum(const uint8_t* data, size_t length) {
    const std::array<uint8_t, 16> d = md4(data, length);
    uint32_t sum = 0;
    for (int i = 0; i < 4; ++i)
        sum ^= uint32_t(d[size_t(4 * i)]) | uint32_t(d[size_t(4 * i + 1)]) << 8 |
               uint32_t(d[size_t(4 * i + 2)]) << 16 | uint32_t(d[size_t(4 * i + 3)]) << 24;
    return sum;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

std::string chunkName(uint32_t id) {
    std::string s;
    for (int i = 3; i >= 0; --i) {
        const char c = char((id >> (8 * i)) & 0xff);
        s += c >= 32 && c < 127 ? c : '?';
    }
    return s;
}

void SaveWriter::add(uint32_t id, const void* data, size_t length) {
    const auto* p = static_cast<const uint8_t*>(data);
    put32(bytes_, id);
    put32(bytes_, uint32_t(length));
    put32(bytes_, blockChecksum(p, length));
    if (length)
        bytes_.insert(bytes_.end(), p, p + length);
    put32(bytes_, kMarker);
}

void SaveWriter::addText(uint32_t id, std::string_view text, size_t size) {
    std::vector<uint8_t> block(size ? size : text.size() + 1, 0);
    std::memcpy(block.data(), text.data(), std::min(text.size(), block.size() - 1));
    add(id, block);
}

bool SaveReader::peek(uint32_t id) const {
    return position_ + 8 <= bytes_.size() && get32(bytes_.data() + position_) == id;
}

Error SaveReader::next(uint32_t id, std::vector<uint8_t>& out, size_t length) {
    if (position_ + 12 > bytes_.size())
        return Error::fail("The saved game ends before its " + chunkName(id) + " chunk");
    const uint8_t* p = bytes_.data() + position_;
    const uint32_t found = get32(p), size = get32(p + 4), checksum = get32(p + 8);
    if (found != id)
        return Error::fail("Expected a " + chunkName(id) + " chunk, found " + chunkName(found));
    if (length && size != length)
        return Error::fail("The " + chunkName(id) + " chunk is " + std::to_string(size) + " bytes, not " +
                           std::to_string(length));
    if (position_ + 16 + size_t(size) > bytes_.size())
        return Error::fail("The " + chunkName(id) + " chunk runs past the end of the file");
    const uint8_t* data = p + 12;
    if (blockChecksum(data, size) != checksum || get32(data + size) != kMarker)
        return Error::fail("The " + chunkName(id) + " chunk is damaged");
    out.assign(data, data + size);
    position_ += 16 + size_t(size);
    return Error::success();
}

Error SaveReader::nextInt(uint32_t id, int32_t& v) {
    std::vector<uint8_t> data;
    if (Error e = next(id, data, 4))
        return e;
    v = int32_t(get32(data.data()));
    return Error::success();
}

Error SaveReader::nextText(uint32_t id, std::string& out, size_t length) {
    std::vector<uint8_t> data;
    if (Error e = next(id, data, length))
        return e;
    const auto end = std::find(data.begin(), data.end(), uint8_t(0));
    out.assign(data.begin(), end);
    return Error::success();
}

} // namespace reluva::idtech3::raven
