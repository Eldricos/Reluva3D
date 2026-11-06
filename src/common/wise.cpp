// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/wise.h"

#include "common/detection.h"
#include "common/inflate.h"
#include "common/md5.h"
#include "common/zip.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace reluva {

namespace {

uint32_t le32(const std::vector<uint8_t>& d, size_t at) {
    return at + 4 <= d.size() ? uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 |
                                    uint32_t(d[at + 3]) << 24
                              : 0;
}

bool readAll(const std::filesystem::path& file, std::vector<uint8_t>& data) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

Error readWiseInstaller(const std::filesystem::path& file, std::vector<WiseStream>& out) {
    out.clear();
    std::vector<uint8_t> d;
    if (!readAll(file, d))
        return Error::fail("Couldn't read " + file.string());
    // A Windows program (MZ, then PE), its sections' end the start of
    // what's appended; the program Wise's (it names its WiseMain).
    const uint32_t pe = le32(d, 0x3c);
    if (d.size() < 64 || d[0] != 'M' || d[1] != 'Z' || pe + 24 > d.size() || std::memcmp(&d[pe], "PE\0\0", 4) != 0)
        return Error::fail(file.filename().string() + " isn't a Windows program");
    const size_t sections = size_t(d[pe + 6] | d[pe + 7] << 8), optional = size_t(d[pe + 20] | d[pe + 21] << 8);
    size_t end = 0;
    for (size_t i = 0; i < sections; ++i) {
        const size_t s = pe + 24 + optional + i * 40;
        end = std::max<size_t>(end, size_t(le32(d, s + 20)) + le32(d, s + 16));
    }
    static const char kWise[] = "WiseMain";
    if (end == 0 || end >= d.size() ||
        std::search(d.begin(), d.begin() + long(end), kWise, kWise + 8) == d.begin() + long(end))
        return Error::fail(file.filename().string() + " isn't a Wise installer");
    // Its header (settings, the title) is followed by the first stream:
    // the first place a deflate stream starts whose CRC follows it.
    auto streamAt = [&](size_t at, WiseStream& s) {
        std::vector<uint8_t> bytes;
        size_t used = 0;
        if (inflateStream(d.data() + at, d.size() - at, bytes, used) || at + used + 4 > d.size())
            return false;
        if (crc32(bytes.data(), bytes.size()) != le32(d, at + used))
            return false;
        s.offset = at;
        s.packedSize = used;
        s.size = bytes.size();
        s.crc = le32(d, at + used);
        Md5 md5;
        md5.update(bytes.data(), size_t(std::min<uint64_t>(bytes.size(), kDetectionHashBytes)));
        s.headMd5 = md5.hexDigest();
        return true;
    };
    size_t at = end;
    WiseStream s;
    for (const size_t limit = std::min(d.size(), end + 65536); at < limit; ++at)
        if (streamAt(at, s))
            break;
    if (at >= d.size() || s.packedSize == 0)
        return Error::fail("No files found in " + file.filename().string());
    while (true) {
        out.push_back(s);
        at = size_t(s.offset + s.packedSize + 4);
        if (at >= d.size() || !streamAt(at, s))
            break;
    }
    return Error::success();
}

Error extractWiseStream(const std::filesystem::path& file, const WiseStream& stream, std::vector<uint8_t>& out) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> packed(size_t(stream.packedSize));
    if (!in || !in.seekg(std::streamoff(stream.offset)) ||
        !in.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed.size())))
        return Error::fail("Couldn't read " + file.string());
    if (Error err = inflateRaw(packed.data(), packed.size(), size_t(stream.size), out))
        return Error::fail(file.filename().string() + ": " + err.message);
    if (crc32(out.data(), out.size()) != stream.crc)
        return Error::fail(file.filename().string() + " is damaged (a file's CRC doesn't match)");
    return Error::success();
}

} // namespace reluva
