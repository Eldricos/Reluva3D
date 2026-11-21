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
// engine writes them (docs/engines/idtech3/games/stvef/systems/sp-interface.md,
// "Saved games"): a run of chunks, each
//
//   id (4 bytes, its four letters as a little-endian number: 'COMM' is
//   0x434f4d4d), length (4), checksum of the data (4: MD4's four words
//   XOR'd, as the engine's block checksum), the data, 0x1234abcd (4).
//
// Chunks are read back in the order they were written; the engine's and
// the game's (AppendToSaveGame / ReadFromSaveGame) are interleaved in one
// file.

/** A chunk's id from its four letters: chunkId("COMM") is 0x434f4d4d. */
constexpr uint32_t chunkId(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 |
           uint32_t(uint8_t(s[3]));
}
/** A chunk id's four letters, for messages ('?' for a byte that isn't printable). */
std::string chunkName(uint32_t id);

/** Writes a saved game's chunks, one after another. */
class SaveWriter {
public:
    /** A chunk of `id` holding the data. */
    ///@{
    void add(uint32_t id, const void *data, size_t length);
    void add(uint32_t id, const std::vector<uint8_t> &data) { add(id, data.data(), data.size()); }
    ///@}
    /** A chunk of `id` holding a 32-bit number. */
    void addInt(uint32_t id, int32_t v) { add(id, &v, 4); }
    /**
     * Text in a block of `size` bytes, zero-filled and cut to leave a NUL
     * (size 0: the text and its NUL).
     */
    void addText(uint32_t id, std::string_view text, size_t size = 0);
    /** The file so far. */
    const std::vector<uint8_t> &bytes() const { return _bytes; }

private:
    std::vector<uint8_t> _bytes;
};

/** Reads a saved game's chunks back, in the order they were written. */
class SaveReader {
public:
    /** Over a saved game's bytes (none: at its end already). */
    ///@{
    SaveReader() = default;
    explicit SaveReader(std::vector<uint8_t> bytes) : _bytes(std::move(bytes)) {}
    ///@}
    /**
     * The next chunk, which must be `id`; `length` (0: any) must match its
     * length. Fails naming what was found instead, or a damaged chunk.
     */
    Error next(uint32_t id, std::vector<uint8_t> &out, size_t length = 0);
    /** The next chunk as a 32-bit number: it must be 4 bytes. */
    Error nextInt(uint32_t id, int32_t &v);
    /** The next chunk's data up to the first NUL. */
    Error nextText(uint32_t id, std::string &out, size_t length = 0);
    /** Whether the next chunk is `id` (without reading it). */
    bool peek(uint32_t id) const;
    /** Whether every chunk has been read. */
    bool atEnd() const { return _position >= _bytes.size(); }

private:
    std::vector<uint8_t> _bytes;
    size_t _position = 0;
};

} // namespace reluva::idtech3::raven
