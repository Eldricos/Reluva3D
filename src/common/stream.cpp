// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/stream.h"

#include <algorithm>

#include <cstring>
#include <fstream>

namespace reluva {

uint8_t ReadStream::readByte() {
    uint8_t b = 0;
    if (read(&b, 1) != 1)
        _err = true;
    return b;
}

uint16_t ReadStream::readUint16LE() {
    uint8_t b[2] = {0, 0};
    if (read(b, 2) != 2)
        _err = true;
    return uint16_t(b[0] | (b[1] << 8));
}

uint32_t ReadStream::readUint32LE() {
    uint8_t b[4] = {0, 0, 0, 0};
    if (read(b, 4) != 4)
        _err = true;
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

uint16_t ReadStream::readUint16BE() {
    uint8_t b[2] = {0, 0};
    if (read(b, 2) != 2)
        _err = true;
    return uint16_t(b[0] << 8 | b[1]);
}

uint32_t ReadStream::readUint32BE() {
    uint8_t b[4] = {0, 0, 0, 0};
    if (read(b, 4) != 4)
        _err = true;
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | uint32_t(b[3]);
}

std::string ReadStream::readFixedString(uint32_t len) {
    std::string s(len, '\0');
    if (len && read(s.data(), len) != len)
        _err = true;
    s.resize(std::min<size_t>(s.find('\0'), len));
    return s;
}

std::string SeekableReadStream::readLine() {
    std::string line;
    uint8_t c = 0;
    while (read(&c, 1) == 1) {
        if (c == '\n')
            break;
        if (c == '\r') {
            // The LF of a CR LF; any other byte is put back.
            uint8_t next = 0;
            if (read(&next, 1) == 1 && next != '\n')
                seek(-1, SEEK_CUR);
            break;
        }
        line += char(c);
    }
    return line;
}

void MemoryWriteStream::writeUint16BE(uint16_t v) {
    _data.push_back(uint8_t(v >> 8));
    _data.push_back(uint8_t(v));
}

void MemoryWriteStream::writeUint32BE(uint32_t v) {
    writeUint16BE(uint16_t(v >> 16));
    writeUint16BE(uint16_t(v));
}

std::string ReadStream::readCString() {
    std::string s;
    uint8_t c;
    while (read(&c, 1) == 1 && c != 0)
        s += char(c);
    return s;
}

std::vector<uint8_t> SeekableReadStream::readAll() {
    const int64_t remaining = size() - pos();
    std::vector<uint8_t> out(remaining > 0 ? size_t(remaining) : 0);
    if (!out.empty())
        out.resize(read(out.data(), uint32_t(out.size())));
    return out;
}

MemoryReadStream::MemoryReadStream(std::vector<uint8_t> data)
    : _buffer(std::make_shared<const std::vector<uint8_t>>(std::move(data))), _offset(0) {
    _size = uint32_t(_buffer->size());
}

MemoryReadStream::MemoryReadStream(std::shared_ptr<const std::vector<uint8_t>> buffer, uint32_t offset, uint32_t size)
    : _buffer(std::move(buffer)), _offset(offset), _size(size) {
    if (uint64_t(_offset) + _size > _buffer->size())
        _size = _offset < _buffer->size() ? uint32_t(_buffer->size() - _offset) : 0;
}

uint32_t MemoryReadStream::read(void *data, uint32_t size) {
    const uint32_t avail = _size - _pos;
    if (size > avail) {
        size = avail;
        _eos = true;
    }
    if (size)
        std::memcpy(data, this->data() + _pos, size);
    _pos += size;
    return size;
}

bool MemoryReadStream::seek(int64_t offset, int whence) {
    const int64_t target = whence == SEEK_SET ? offset : whence == SEEK_CUR ? _pos + offset : _size + offset;
    if (target < 0 || target > _size)
        return false;
    _pos = uint32_t(target);
    _eos = false;
    return true;
}

void MemoryWriteStream::write(const void *data, uint32_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    _data.insert(_data.end(), p, p + size);
}

void MemoryWriteStream::writeUint16LE(uint16_t v) {
    writeByte(uint8_t(v));
    writeByte(uint8_t(v >> 8));
}

void MemoryWriteStream::writeUint32LE(uint32_t v) {
    writeUint16LE(uint16_t(v));
    writeUint16LE(uint16_t(v >> 16));
}

void MemoryWriteStream::writeFixedString(const std::string &s, uint32_t size) {
    for (uint32_t i = 0; i < size; i++)
        writeByte(i < s.size() ? uint8_t(s[i]) : 0);
}

bool readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
        return false;
    in.seekg(0, std::ios::beg);
    out.resize(size_t(size));
    if (size > 0 && !in.read(reinterpret_cast<char *>(out.data()), size))
        return false;
    return true;
}

bool writeFile(const std::filesystem::path &path, const std::vector<uint8_t> &data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    if (!data.empty())
        out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    out.close();
    return bool(out);
}

} // namespace reluva
