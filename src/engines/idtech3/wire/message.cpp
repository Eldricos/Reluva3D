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
    if (!_code) {
        rawBits(value, count);
        return;
    }
    const int odd = count & 7;
    rawBits(value, odd);
    value = odd < 32 ? value >> odd : 0;
    for (int done = odd; done < count; done += 8, value >>= 8)
        _code->writeCode(uint8_t(value), _data, _bit);
    // A byte past the last bit's, even on a boundary, as jk2mp.exe sizes
    // them (0x431010): its readers take a last read ending there as past
    // the end.
    _data.resize((_bit >> 3) + 1, 0);
}

void MessageWriter::rawBits(uint32_t value, int count) {
    while (count > 0) {
        const size_t index = _bit >> 3;
        const int used = int(_bit & 7);
        const int room = 8 - used;
        const int take = count < room ? count : room;
        if (index >= _data.size())
            _data.push_back(0);
        _data[index] = uint8_t(_data[index] | ((value & ((1u << take) - 1)) << used));
        value >>= take;
        count -= take;
        _bit += size_t(take);
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
    if (!_code && (_bit & 7)) {
        _bit = ((_bit >> 3) + 2) * 8;
        _data.resize(_bit >> 3, 0);
    }
}

void MessageWriter::string(const std::string &s, size_t maxLength) {
    const size_t n = s.size() < maxLength ? s.size() : maxLength;
    if (_code) {
        for (size_t i = 0; i < n; ++i)
            byte(uint8_t(s[i]) > 127 ? '.' : uint8_t(s[i]));
        byte(0);
        return;
    }
    alignToByte();
    for (size_t i = 0; i < n; ++i)
        _data.push_back(uint8_t(s[i]));
    _data.push_back(0);
    _bit = _data.size() * 8;
}

void MessageWriter::data(const uint8_t *bytes, size_t count) {
    if (_code) {
        for (size_t i = 0; i < count; ++i)
            byte(bytes[i]);
        return;
    }
    alignToByte();
    _data.insert(_data.end(), bytes, bytes + count);
    _bit = _data.size() * 8;
}

uint32_t MessageReader::bits(int count) {
    if (!_code)
        return rawBits(count);
    const int odd = count & 7;
    uint32_t value = rawBits(odd);
    for (int done = odd; done < count; done += 8) {
        const int c = _code->readCode(_data, _bit);
        if (c < 0) {
            _overflowed = true;
            return value;
        }
        value |= uint32_t(c) << done;
    }
    return value;
}

uint32_t MessageReader::rawBits(int count) {
    uint32_t value = 0;
    int got = 0;
    while (got < count) {
        const size_t index = _bit >> 3;
        const int used = int(_bit & 7);
        const int room = 8 - used;
        const int take = count - got < room ? count - got : room;
        if (index >= _data.size()) {
            _overflowed = true;
            _bit += size_t(count - got);
            return value;
        }
        value |= uint32_t((_data[index] >> used) & ((1u << take) - 1)) << got;
        got += take;
        _bit += size_t(take);
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
        _overflowed = true;
        return -1;
    }
    if (_code) {
        const int c = _code->readCode(_data, _bit);
        if (c < 0)
            _overflowed = true;
        return c;
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
    if (!_code && (_bit & 7))
        _bit = ((_bit >> 3) + 2) * 8;
}

std::string MessageReader::string(size_t maxLength) {
    alignToByte();
    std::string out;
    while (true) {
        const int32_t c = byte();
        if (c <= 0)
            break;
        if (out.size() < maxLength)
            out.push_back(c == '%' || (_code && c > 127) ? '.' : char(c));
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
