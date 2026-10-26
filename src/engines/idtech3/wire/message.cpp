// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/wire/message.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3::wire {

namespace {
constexpr int kSmallFloatBits = 13;
constexpr int32_t kSmallFloatBias = 1 << (kSmallFloatBits - 1); // 4096
} // namespace

void MessageWriter::bits(uint32_t value, int count) {
    if (count < 32)
        value &= (1u << count) - 1;
    while (count > 0) {
        const size_t index = bit_ >> 3;
        const int used = int(bit_ & 7), room = 8 - used, take = count < room ? count : room;
        if (index >= data_.size())
            data_.push_back(0);
        data_[index] = uint8_t(data_[index] | ((value & ((1u << take) - 1)) << used));
        value >>= take;
        count -= take;
        bit_ += size_t(take);
    }
}

void MessageWriter::floatValue(float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    bits(u, 32);
}

void MessageWriter::smallFloat(float v) {
    const auto whole = int32_t(v);
    if (float(whole) == v && whole + kSmallFloatBias >= 0 && whole + kSmallFloatBias < (1 << kSmallFloatBits)) {
        bits(0, 1);
        bits(uint32_t(whole + kSmallFloatBias), kSmallFloatBits);
    } else {
        bits(1, 1);
        floatValue(v);
    }
}

void MessageWriter::alignToByte() {
    // As the original: inside a byte, on past the next boundary too.
    if (bit_ & 7) {
        bit_ = ((bit_ >> 3) + 2) * 8;
        data_.resize(bit_ >> 3, 0);
    }
}

void MessageWriter::string(const std::string& s, size_t maxLength) {
    alignToByte();
    const size_t n = s.size() < maxLength ? s.size() : maxLength;
    for (size_t i = 0; i < n; ++i)
        data_.push_back(uint8_t(s[i]));
    data_.push_back(0);
    bit_ = data_.size() * 8;
}

void MessageWriter::data(const uint8_t* bytes, size_t count) {
    alignToByte();
    data_.insert(data_.end(), bytes, bytes + count);
    bit_ = data_.size() * 8;
}

uint32_t MessageReader::bits(int count) {
    uint32_t value = 0;
    int got = 0;
    while (got < count) {
        const size_t index = bit_ >> 3;
        const int used = int(bit_ & 7), room = 8 - used, take = count - got < room ? count - got : room;
        if (index >= data_.size()) {
            overflowed_ = true;
            bit_ += size_t(count - got);
            return value;
        }
        value |= uint32_t((data_[index] >> used) & ((1u << take) - 1)) << got;
        got += take;
        bit_ += size_t(take);
    }
    return value;
}

int32_t MessageReader::signedBits(int count) {
    uint32_t v = bits(count);
    if (count < 32 && (v & (1u << (count - 1))))
        v |= ~((1u << count) - 1);
    return int32_t(v);
}

int32_t MessageReader::byte() {
    if (atEnd()) {
        overflowed_ = true;
        return -1;
    }
    return int32_t(bits(8));
}

float MessageReader::floatValue() {
    const uint32_t u = bits(32);
    float v;
    std::memcpy(&v, &u, 4);
    return v;
}

float MessageReader::smallFloat() {
    if (bits(1) == 0)
        return float(int32_t(bits(kSmallFloatBits)) - kSmallFloatBias);
    return floatValue();
}

void MessageReader::alignToByte() {
    if (bit_ & 7)
        bit_ = ((bit_ >> 3) + 2) * 8;
}

std::string MessageReader::string(size_t maxLength) {
    alignToByte();
    std::string out;
    while (true) {
        const int32_t c = byte();
        if (c <= 0)
            break;
        if (out.size() < maxLength)
            out.push_back(c == '%' ? '.' : char(c));
    }
    return out;
}

std::vector<uint8_t> MessageReader::data(size_t count) {
    alignToByte();
    std::vector<uint8_t> out;
    for (size_t i = 0; i < count; ++i) {
        const int32_t c = byte();
        if (c < 0)
            break;
        out.push_back(uint8_t(c));
    }
    return out;
}

} // namespace reluva::idtech3::wire
