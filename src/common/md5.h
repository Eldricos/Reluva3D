// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace reluva {

/**
 * An MD5 digest (RFC 1321) worked out a piece at a time, for telling game
 * data files apart.
 */
class Md5 {
public:
    Md5();
    /** Adds the next `len` bytes. */
    void update(const void *data, size_t len);
    /**
     * The digest of everything added, in lower-case hex or as bytes. Either
     * finishes it, once: nothing may be added or asked for after.
     */
    ///@{
    std::string hexDigest();
    std::array<uint8_t, 16> digest();
    ///@}

private:
    void block(const uint8_t *p);

    uint32_t _state[4];
    uint64_t _length = 0; // bytes
    uint8_t _buffer[64];
    size_t _bufferLen = 0;
};

/** HMAC-MD5 (RFC 2104) of `message` with `key`: proves a password without sending it. */
std::array<uint8_t, 16> hmacMd5(const void *key, size_t keyLen, const void *message, size_t len);

} // namespace reluva
