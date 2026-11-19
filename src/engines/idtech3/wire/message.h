// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::wire {

// Elite Force's messages (protocol 24, docs/engines/idtech3/games/stvef/systems/network.md):
// a plain bit stream, each byte filled from its lowest bit up and each
// value written lowest bit first, so values needn't start on a byte.
// Strings and raw bytes do: one that would start inside a byte starts
// at the byte after the next (as the original's code moves on a byte and
// then to the next boundary).
class MessageWriter {
public:
    // `bits`: 1 to 32; a value's low `bits` bits are written.
    void bits(uint32_t value, int bits);
    void byte(uint32_t v) { bits(v, 8); }
    void shortValue(int32_t v) { bits(uint32_t(v), 16); }
    void longValue(int32_t v) { bits(uint32_t(v), 32); }
    void floatValue(float v);
    // Floats in entity and player states: whole numbers from -4096 to
    // 4095 in 13 bits, anything else in 32.
    void smallFloat(float v);
    void string(const std::string &s, size_t maxLength = 1023);
    void bigString(const std::string &s) { string(s, 8191); }
    void data(const uint8_t *bytes, size_t count);
    void alignToByte();

    const std::vector<uint8_t> &bytes() const { return _data; }
    size_t bitCount() const { return _bit; }

private:
    std::vector<uint8_t> _data;
    size_t _bit = 0;
};

class MessageReader {
public:
    MessageReader() = default;
    explicit MessageReader(std::vector<uint8_t> bytes) : _data(std::move(bytes)) {}
    MessageReader(const uint8_t *bytes, size_t count) : _data(bytes, bytes + count) {}

    // A value of `bits` bits (1 to 32); past the end, bits read as 0 and
    // overflowed() says so. `sign` extends the top bit.
    uint32_t bits(int bits);
    int32_t signedBits(int bits);
    // -1 past the end, as the original's readers expect.
    int32_t byte();
    int32_t shortValue() { return signedBits(16); }
    int32_t longValue() { return int32_t(bits(32)); }
    float floatValue();
    float smallFloat();
    std::string string(size_t maxLength = 1023);
    std::string bigString() { return string(8191); }
    std::vector<uint8_t> data(size_t count);
    void alignToByte();

    bool atEnd() const { return _bit >= _data.size() * 8; }
    bool overflowed() const { return _overflowed; }
    size_t bitPosition() const { return _bit; }
    // The bytes after the first `count`, for the netchan's header.
    void skipBytes(size_t count) { _bit = count * 8; }
    const std::vector<uint8_t> &bytes() const { return _data; }

private:
    std::vector<uint8_t> _data;
    size_t _bit = 0;
    bool _overflowed = false;
};

} // namespace reluva::idtech3::wire
