// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace reluva {

// Minimal MD5 (RFC 1321), used only for identifying game data files.
class Md5 {
public:
    Md5();
    void update(const void *data, size_t len);
    std::string hexDigest(); // finalizes; call once

private:
    void block(const uint8_t *p);

    uint32_t _state[4];
    uint64_t _length = 0; // bytes
    uint8_t _buffer[64];
    size_t _bufferLen = 0;
};

} // namespace reluva
