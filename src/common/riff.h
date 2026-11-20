// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva {

/**
 * A RIFF file read as a tree: each chunk its four-character id and its
 * bytes; a RIFF or LIST chunk its form type and its children. Chunks are
 * padded to even sizes, as RIFF has them.
 */
struct RiffChunk {
    std::string id;                // "RIFF", "LIST", "fmt "...
    std::string type;              // a RIFF or LIST chunk's form type ("DLS ", "lins"...); else empty
    const uint8_t *data = nullptr; // the chunk's bytes (for RIFF and LIST, after the type)
    size_t size = 0;
    std::vector<RiffChunk> children;

    /** Whether it's a RIFF or LIST chunk: one with a form type and children. */
    bool isList() const { return id == "RIFF" || id == "LIST"; }
    /** The first child with this id, or null. */
    const RiffChunk *find(const std::string &id) const;
    /** The first child list (RIFF or LIST) of this form type, or null. */
    const RiffChunk *findList(const std::string &type) const;
    /** Every child list (RIFF or LIST) of this form type, in the file's order. */
    std::vector<const RiffChunk *> lists(const std::string &type) const;

    /** The byte at offset `at` in the chunk; 0 past its end. */
    uint8_t u8(size_t at) const { return at < size ? data[at] : 0; }
    /** A little-endian 16-bit value at offset `at`; bytes past the chunk's end read as 0. */
    uint16_t u16(size_t at) const { return uint16_t(u8(at) | u8(at + 1) << 8); }
    /** A little-endian 32-bit value at offset `at`; bytes past the chunk's end read as 0. */
    uint32_t u32(size_t at) const { return uint32_t(u16(at) | uint32_t(u16(at + 2)) << 16); }
    /** A little-endian signed 16-bit value at offset `at`; bytes past the chunk's end read as 0. */
    int16_t s16(size_t at) const { return int16_t(u16(at)); }
    /** A little-endian signed 32-bit value at offset `at`; bytes past the chunk's end read as 0. */
    int32_t s32(size_t at) const { return int32_t(u32(at)); }
    /** A UTF-16 string chunk (UNAM...) up to its first NUL, as ASCII (other characters as '?'). */
    std::string text16() const;
};

/**
 * Reads `data` (which must outlive the tree) as one chunk: a whole RIFF
 * file's top chunk. A chunk running past its parent is an error; bytes
 * after the RIFF chunk are left alone.
 * @return An error if it isn't a RIFF file or a chunk doesn't fit; `out` is then empty.
 */
Error readRiff(const std::vector<uint8_t> &data, RiffChunk &out);

} // namespace reluva
