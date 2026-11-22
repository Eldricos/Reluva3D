// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/alt_res.h"

#include "common/bytes.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace reluva::botf {

namespace {

constexpr size_t kHeaderSize = 24;
constexpr size_t kEntrySize = 40;
constexpr size_t kWindowSize = 4096;
// Where writing into the window starts. Found by decoding with each start:
// only this one gives the copies even (2-byte pixel) distances and the
// pictures clean colours (docs/engines/botf/formats/alt-res.md).
constexpr size_t kWindowStart = 1;
// A reference copies 2 to 17 bytes: its length less 2 in 4 bits.
constexpr size_t kMinMatch = 2;
constexpr size_t kMaxMatch = 17;
// The saved games' files are padded to whole blocks after the header.
constexpr size_t kBlockSize = 16384;

uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
}

Error readAt(const std::filesystem::path &path, uint64_t offset, size_t size, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + path.string());
    out.resize(size);
    in.seekg(std::streamoff(offset));
    if (!in.read(reinterpret_cast<char *>(out.data()), std::streamsize(size)))
        return Error::fail(path.string() + ": can't read " + std::to_string(size) + " bytes at " +
                           std::to_string(offset));
    return Error::success();
}

} // namespace

Error decompressLzss(const uint8_t *data, size_t dataSize, size_t size, std::vector<uint8_t> &out, size_t *used,
                     uint8_t windowFill) {
    out.clear();
    out.reserve(size);
    uint8_t window[kWindowSize];
    std::fill(std::begin(window), std::end(window), windowFill);
    size_t r = kWindowStart;
    size_t pos = 0;
    unsigned flags = 0;
    auto put = [&](uint8_t c) {
        out.push_back(c);
        window[r] = c;
        r = (r + 1) % kWindowSize;
    };
    while (out.size() < size) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (pos >= dataSize)
                break;
            flags = data[pos++] | 0xFF00u;
        }
        if (flags & 1) {
            if (pos >= dataSize)
                break;
            put(data[pos++]);
        } else {
            if (pos + 1 >= dataSize)
                break;
            const unsigned a = data[pos];
            const unsigned b = data[pos + 1];
            pos += 2;
            const size_t from = (a & 0x0F) << 8 | b;
            // A reference may overshoot the file's end; the encoder never
            // makes one that does, but damaged data could.
            for (size_t k = 0, n = (a >> 4) + 2; k < n && out.size() < size; ++k)
                put(window[(from + k) % kWindowSize]);
        }
    }
    if (used)
        *used = pos;
    if (out.size() < size)
        return Error::fail("compressed data ends after " + std::to_string(out.size()) + " of " + std::to_string(size) +
                           " bytes");
    return Error::success();
}

std::vector<uint8_t> compressLzss(const uint8_t *data, size_t size) {
    std::vector<uint8_t> out;
    out.reserve(size + size / 8 + 1);
    // The last place each pair of bytes was seen, and before it, as a chain.
    constexpr size_t kNone = SIZE_MAX;
    constexpr size_t kChainLimit = 256;
    std::vector<size_t> head(65536, kNone);
    std::vector<size_t> previous(size, kNone);
    auto key = [&](size_t i) { return size_t(data[i]) << 8 | data[i + 1]; };
    auto remember = [&](size_t i) {
        if (i + 1 < size) {
            previous[i] = head[key(i)];
            head[key(i)] = i;
        }
    };
    size_t flagAt = 0;
    int items = 8;
    for (size_t i = 0; i < size;) {
        if (items == 8) {
            flagAt = out.size();
            out.push_back(0);
            items = 0;
        }
        size_t best = 0;
        size_t from = 0;
        if (i + kMinMatch <= size) {
            const size_t longest = std::min(kMaxMatch, size - i);
            size_t tries = 0;
            // A source as far back as the window holds: written no more
            // than 4,095 bytes ago, so not yet overwritten.
            for (size_t j = head[key(i)]; j != kNone && i - j < kWindowSize && tries < kChainLimit;
                 j = previous[j], ++tries) {
                size_t n = 0;
                // The copy may run into what it has just written, as the
                // decoder's does.
                while (n < longest && data[j + n] == data[i + n])
                    ++n;
                if (n > best) {
                    best = n;
                    from = (j + kWindowStart) % kWindowSize;
                    if (n == longest)
                        break;
                }
            }
        }
        if (best >= kMinMatch) {
            out.push_back(uint8_t((best - kMinMatch) << 4 | from >> 8));
            out.push_back(uint8_t(from & 0xFF));
        } else {
            best = 1;
            out[flagAt] |= uint8_t(1 << items);
            out.push_back(data[i]);
        }
        ++items;
        for (size_t k = 0; k < best; ++k)
            remember(i + k);
        i += best;
    }
    return out;
}

uint16_t altResCheck(const uint8_t *data, size_t size) {
    static const std::array<uint16_t, 256> kTable = [] {
        std::array<uint16_t, 256> t{};
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i << 8;
            for (int k = 0; k < 8; ++k)
                c = (c & 0x8000 ? c << 1 ^ 0x1021 : c << 1) & 0xFFFF;
            t[i] = uint16_t(c);
        }
        return t;
    }();
    unsigned r = 0;
    for (size_t i = 0; i < size; ++i)
        r = ((r << 8 | data[i]) ^ kTable[r >> 8]) & 0xFFFF;
    return uint16_t(r);
}

Error writeAltRes(const std::filesystem::path &path, std::string_view name, const std::vector<AltResFile> &files) {
    constexpr size_t kNameSize = 16;
    if (name.size() >= kNameSize)
        return Error::fail("the archive's name \"" + std::string(name) + "\" is too long");
    std::vector<uint8_t> out(kHeaderSize, 0);
    std::copy(name.begin(), name.end(), out.begin());
    std::vector<uint8_t> dir(files.size() * kEntrySize, 0);
    for (size_t i = 0; i < files.size(); ++i) {
        const AltResFile &f = files[i];
        if (f.name.size() >= kNameSize)
            return Error::fail("the file name \"" + f.name + "\" is too long");
        uint8_t *e = &dir[i * kEntrySize];
        std::copy(f.name.begin(), f.name.end(), e);
        writeLE32(e + 16, f.stored.empty() && f.emptyAtZero ? 0 : uint32_t(out.size()));
        writeLE32(e + 20, uint32_t(i));
        writeLE32(e + 24, f.size);
        writeLE32(e + 28, uint32_t(f.stored.size()));
        writeLE32(e + 32, f.flag);
        writeLE32(e + 36, f.check);
        out.insert(out.end(), f.stored.begin(), f.stored.end());
    }
    writeLE32(&out[16], uint32_t(out.size()));
    writeLE32(&out[20], uint32_t(files.size()));
    out.insert(out.end(), dir.begin(), dir.end());
    const size_t blocks = (out.size() - kHeaderSize + kBlockSize - 1) / kBlockSize;
    out.resize(kHeaderSize + blocks * kBlockSize, 0);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size())))
            return Error::fail("can't write " + temporary.string());
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec)
        return Error::fail("can't replace " + path.string() + ": " + ec.message());
    return Error::success();
}

Error AltResArchive::open(const std::filesystem::path &path) {
    _path = path;
    _name.clear();
    _entries.clear();
    _byName.clear();
    std::error_code ec;
    _fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        return Error::fail("can't open " + path.string());
    std::vector<uint8_t> header;
    if (_fileSize < kHeaderSize)
        return Error::fail(path.string() + " is too small for alt.res");
    if (Error err = readAt(path, 0, kHeaderSize, header))
        return err;
    for (size_t i = 0; i < 16 && header[i]; ++i)
        _name.push_back(char(header[i]));
    const uint32_t dirOffset = u32(&header[16]);
    const uint32_t count = u32(&header[20]);
    if (dirOffset < kHeaderSize || dirOffset > _fileSize || count > (_fileSize - dirOffset) / kEntrySize)
        return Error::fail(path.string() + ": directory of " + std::to_string(count) + " entries at " +
                           std::to_string(dirOffset) + " doesn't fit in the file");
    std::vector<uint8_t> dir;
    if (Error err = readAt(path, dirOffset, size_t(count) * kEntrySize, dir))
        return err;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *e = &dir[size_t(i) * kEntrySize];
        Entry entry;
        size_t len = 0;
        while (len < 16 && e[len])
            ++len;
        entry.name.assign(reinterpret_cast<const char *>(e), len);
        entry.offset = u32(e + 16);
        entry.index = u32(e + 20);
        entry.size = u32(e + 24);
        entry.compressedSize = u32(e + 28);
        entry.flag = u32(e + 32);
        entry.check = u32(e + 36);
        if (uint64_t(entry.offset) + entry.compressedSize > _fileSize)
            return Error::fail(path.string() + ": " + entry.name + " runs past the end of the file");
        _byName.emplace(normalizeArchiveName(entry.name), _entries.size());
        _entries.push_back(std::move(entry));
    }
    return Error::success();
}

std::vector<std::string> AltResArchive::list() const {
    std::vector<std::string> names;
    for (const Entry &e : _entries)
        names.push_back(e.name);
    return names;
}

bool AltResArchive::has(std::string_view name) const {
    return _byName.count(normalizeArchiveName(name)) != 0;
}

Error AltResArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    auto it = _byName.find(normalizeArchiveName(name));
    if (it == _byName.end())
        return Error::fail(std::string(name) + " isn't in " + _path.string());
    return read(_entries[it->second], out);
}

Error AltResArchive::read(const Entry &entry, std::vector<uint8_t> &out, uint8_t windowFill) const {
    std::vector<uint8_t> packed;
    if (Error err = readAt(_path, entry.offset, entry.compressedSize, packed))
        return err;
    if (Error err = decompressLzss(packed.data(), packed.size(), entry.size, out, nullptr, windowFill))
        return Error::fail(_path.string() + ": " + entry.name + ": " + err.message);
    return Error::success();
}

Error AltResArchive::readStored(const Entry &entry, std::vector<uint8_t> &out) const {
    return readAt(_path, entry.offset, entry.compressedSize, out);
}

} // namespace reluva::botf
