// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/stream.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace reluva {

// Saving and loading with the same code: each sync call writes a value
// when saving and reads it back into the same variable when loading, so a
// saved game's layout is written once. Values are little-endian, in the
// number of bytes asked for (signed ones sign-extended when read); strings
// and vectors are preceded by their length (32 bits). Once a read runs out
// or a length is implausible, err() is set and the rest reads as zeros.
class Serializer {
public:
    // Loading from `in`, or saving to `out` (the other nullptr).
    Serializer(ReadStream *in, MemoryWriteStream *out) : _in(in), _out(out) {}

    // The layout's version, for data saved before a field was added.
    void setVersion(uint32_t version) { _version = version; }
    uint32_t version() const { return _version; }

    bool isLoading() const { return _in != nullptr; }
    bool isSaving() const { return _out != nullptr; }
    bool err() const { return _failed || (_in && _in->err()); }
    void fail() { _failed = true; }

    void syncBytes(void *data, uint32_t size) {
        if (_in) {
            if (_in->read(data, size) != size)
                _failed = true;
        } else if (_out) {
            _out->write(data, size);
        }
    }

    template<typename T>
    void syncAsInt(T &value, int bytes) {
        uint8_t buffer[8] = {};
        if (_out) {
            const uint64_t v = uint64_t(int64_t(value));
            for (int i = 0; i < bytes; ++i)
                buffer[i] = uint8_t(v >> (8 * i));
        }
        syncBytes(buffer, uint32_t(bytes));
        if (_in) {
            uint64_t v = 0;
            for (int i = 0; i < bytes; ++i)
                v |= uint64_t(buffer[i]) << (8 * i);
            // Sign-extended from the stored width.
            if (bytes < 8 && ((v >> (8 * bytes - 1)) & 1) && T(-1) < T(0))
                v |= ~0ull << (8 * bytes);
            value = T(v);
        }
    }

    void syncAsByte(uint8_t &value) { syncAsInt(value, 1); }
    void syncAsBool(bool &value) {
        uint8_t b = value ? 1 : 0;
        syncAsInt(b, 1);
        value = b != 0;
    }
    void syncAsUint16(uint16_t &value) { syncAsInt(value, 2); }
    void syncAsSint16(int16_t &value) { syncAsInt(value, 2); }
    void syncAsUint32(uint32_t &value) { syncAsInt(value, 4); }
    void syncAsSint32(int32_t &value) { syncAsInt(value, 4); }
    void syncAsUint64(uint64_t &value) { syncAsInt(value, 8); }
    // An int in 32 bits.
    void syncAsInt(int &value) { syncAsInt(value, 4); }
    // A double's bits, as they are.
    void syncAsDouble(double &value) {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        syncAsInt(bits, 8);
        std::memcpy(&value, &bits, sizeof bits);
    }
    void syncString(std::string &value) {
        uint32_t size = uint32_t(value.size());
        syncAsUint32(size);
        if (_in) {
            if (size > (1u << 20) || err()) {
                _failed = true;
                return;
            }
            value.resize(size);
        }
        if (size)
            syncBytes(value.data(), size);
    }
    // A vector, each element with sync(serializer, element).
    template<typename T, typename F>
    void syncVector(std::vector<T> &values, F sync) {
        uint32_t size = uint32_t(values.size());
        syncAsUint32(size);
        if (_in) {
            if (size > (1u << 24) || err()) {
                _failed = true;
                return;
            }
            values.resize(size);
        }
        for (T &value : values) {
            if (err())
                return;
            sync(*this, value);
        }
    }
    // A count the layout fixes, stored to check it: a load whose count
    // differs fails.
    void syncCount(uint32_t count) {
        uint32_t stored = count;
        syncAsUint32(stored);
        if (stored != count)
            _failed = true;
    }

private:
    ReadStream *_in;
    MemoryWriteStream *_out;
    bool _failed = false;
    uint32_t _version = 0;
};

} // namespace reluva
