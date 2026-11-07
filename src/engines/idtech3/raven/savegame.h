// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3::raven {

// Elite Force single player's saved games (saves/<name>.sav), as its
// engine writes them (docs/engines/idtech3/formats/eliteforce-sp-interface.md,
// "Saved games"): a run of chunks, each
//
//   id (4 bytes, its four letters as a little-endian number: 'COMM' is
//   0x434f4d4d), length (4), checksum of the data (4: MD4's four words
//   XOR'd, as the engine's block checksum), the data, 0x1234abcd (4).
//
// Chunks are read back in the order they were written; the engine's and
// the game's (AppendToSaveGame / ReadFromSaveGame) are interleaved in one
// file.
constexpr uint32_t chunkId(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 |
           uint32_t(uint8_t(s[3]));
}
std::string chunkName(uint32_t id);

class SaveWriter {
public:
    void add(uint32_t id, const void* data, size_t length);
    void add(uint32_t id, const std::vector<uint8_t>& data) { add(id, data.data(), data.size()); }
    void addInt(uint32_t id, int32_t v) { add(id, &v, 4); }
    // Text in a block of `size` bytes, zero-filled (size 0: the text and
    // its NUL).
    void addText(uint32_t id, std::string_view text, size_t size = 0);
    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

class SaveReader {
public:
    SaveReader() = default;
    explicit SaveReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    // The next chunk, which must be `id`; `length` (0: any) must match its
    // length. Fails naming what was found instead, or a damaged chunk.
    Error next(uint32_t id, std::vector<uint8_t>& out, size_t length = 0);
    Error nextInt(uint32_t id, int32_t& v);
    // Its data up to the first NUL.
    Error nextText(uint32_t id, std::string& out, size_t length = 0);
    // Whether the next chunk is `id` (without reading it).
    bool peek(uint32_t id) const;
    bool atEnd() const { return position_ >= bytes_.size(); }

private:
    std::vector<uint8_t> bytes_;
    size_t position_ = 0;
};

} // namespace reluva::idtech3::raven
