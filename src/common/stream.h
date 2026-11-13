// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

// Bytes read in order, with little-endian values: for decoders that walk a
// file (or a part of one) rather than index into it. Reading past the end
// gives zeros and sets err().
class ReadStream {
public:
    virtual ~ReadStream() = default;

    // Reads up to `size` bytes; returns how many were read.
    virtual uint32_t read(void *data, uint32_t size) = 0;
    virtual bool eos() const = 0;
    bool err() const { return _err; }

    uint8_t readByte();
    uint16_t readUint16LE();
    uint32_t readUint32LE();
    int16_t readSint16LE() { return int16_t(readUint16LE()); }
    int32_t readSint32LE() { return int32_t(readUint32LE()); }
    uint16_t readUint16BE();
    uint32_t readUint32BE();
    // `len` bytes, up to the first NUL among them.
    std::string readFixedString(uint32_t len);
    // Up to a NUL byte (not included) or the end.
    std::string readCString();

protected:
    bool _err = false;
};

class SeekableReadStream : public ReadStream {
public:
    virtual int64_t pos() const = 0;
    virtual int64_t size() const = 0;
    // `whence` is SEEK_SET, SEEK_CUR or SEEK_END.
    virtual bool seek(int64_t offset, int whence = SEEK_SET) = 0;
    bool skip(uint32_t n) { return seek(n, SEEK_CUR); }
    // The rest of the stream.
    std::vector<uint8_t> readAll();
    // Up to the end of the line (LF, CR LF or CR; not included) or of the
    // stream.
    std::string readLine();
};

// A stream over bytes in memory: its own, or a part of a buffer shared with
// other streams (an archive's members).
class MemoryReadStream : public SeekableReadStream {
public:
    explicit MemoryReadStream(std::vector<uint8_t> data);
    MemoryReadStream(std::shared_ptr<const std::vector<uint8_t>> buffer, uint32_t offset, uint32_t size);

    uint32_t read(void *data, uint32_t size) override;
    bool eos() const override { return _eos; }
    int64_t pos() const override { return _pos; }
    int64_t size() const override { return _size; }
    bool seek(int64_t offset, int whence = SEEK_SET) override;
    const uint8_t *data() const { return _buffer->data() + _offset; }

private:
    std::shared_ptr<const std::vector<uint8_t>> _buffer;
    uint32_t _offset;
    uint32_t _size;
    uint32_t _pos = 0;
    bool _eos = false;
};

// Bytes written in order, with little-endian values.
class MemoryWriteStream {
public:
    void write(const void *data, uint32_t size);
    void writeByte(uint8_t v) { _data.push_back(v); }
    void writeUint16LE(uint16_t v);
    void writeUint32LE(uint32_t v);
    void writeUint16BE(uint16_t v);
    void writeUint32BE(uint32_t v);
    // The string's bytes, without a NUL.
    void writeString(const std::string &s) { write(s.data(), uint32_t(s.size())); }
    // A string in a field of `size` bytes, padded with NULs (cut if longer).
    void writeFixedString(const std::string &s, uint32_t size);
    const std::vector<uint8_t> &data() const { return _data; }
    uint32_t size() const { return uint32_t(_data.size()); }

private:
    std::vector<uint8_t> _data;
};

// A whole file into memory, and back; false if it can't be read (written).
bool readFile(const std::filesystem::path &path, std::vector<uint8_t> &out);
bool writeFile(const std::filesystem::path &path, const std::vector<uint8_t> &data);

} // namespace reluva
