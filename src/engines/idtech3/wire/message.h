// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::wire {

// Elite Force's messages (protocol 24, docs/formats/idtech3/eliteforce-network.md):
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
    void string(const std::string& s, size_t maxLength = 1023);
    void bigString(const std::string& s) { string(s, 8191); }
    void data(const uint8_t* bytes, size_t count);
    void alignToByte();

    const std::vector<uint8_t>& bytes() const { return data_; }
    size_t bitCount() const { return bit_; }

private:
    std::vector<uint8_t> data_;
    size_t bit_ = 0;
};

class MessageReader {
public:
    MessageReader() = default;
    explicit MessageReader(std::vector<uint8_t> bytes) : data_(std::move(bytes)) {}
    MessageReader(const uint8_t* bytes, size_t count) : data_(bytes, bytes + count) {}

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

    bool atEnd() const { return bit_ >= data_.size() * 8; }
    bool overflowed() const { return overflowed_; }
    size_t bitPosition() const { return bit_; }
    // The bytes after the first `count`, for the netchan's header.
    void skipBytes(size_t count) { bit_ = count * 8; }
    const std::vector<uint8_t>& bytes() const { return data_; }

private:
    std::vector<uint8_t> data_;
    size_t bit_ = 0;
    bool overflowed_ = false;
};

} // namespace reluva::idtech3::wire
