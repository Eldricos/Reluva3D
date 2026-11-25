// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Microsoft cabinets, as Microsoft's published Cabinet File Format describes
// them: a header (CFHEADER), its folders (CFFOLDER: where their data blocks
// start, how many, how compressed) and files (CFFILE: size, offset in their
// folder's uncompressed data, name), and each folder's data blocks (CFDATA).
// An MSZIP block is "CK" and a deflate stream that may refer back into the
// previous block's 32 KB.

#include "common/mscab.h"

#include "common/bytes.h"
#include "common/inflate.h"

#include <algorithm>
#include <cstring>

namespace reluva {

namespace {

constexpr uint16_t kHasReserve = 0x0004;
constexpr uint16_t kStored = 0;
constexpr uint16_t kMszip = 1;
constexpr size_t kMszipWindow = 32768;

// One cabinet starting at `at` in `file`: its files appended to `out`.
Error readCabinet(const std::vector<uint8_t> &file, size_t at, std::vector<CabinetFile> &out) {
    const uint8_t *cab = file.data() + at;
    const size_t left = file.size() - at;
    if (left < 36)
        return Error::fail("a cabinet cut short");
    const uint32_t size = readLE32(cab + 8);
    const uint32_t filesAt = readLE32(cab + 16);
    const uint16_t folderCount = readLE16(cab + 26);
    const uint16_t fileCount = readLE16(cab + 28);
    const uint16_t flags = readLE16(cab + 30);
    if (size > left || filesAt >= size)
        return Error::fail("a cabinet cut short");
    size_t p = 36;
    uint8_t folderReserve = 0;
    uint8_t dataReserve = 0;
    if (flags & kHasReserve) {
        if (p + 4 > size)
            return Error::fail("a damaged cabinet");
        const uint16_t headerReserve = readLE16(cab + p);
        folderReserve = cab[p + 2];
        dataReserve = cab[p + 3];
        p += 4 + headerReserve;
    }
    // Each folder's data, uncompressed.
    std::vector<std::vector<uint8_t>> folders;
    for (uint16_t f = 0; f < folderCount; ++f) {
        if (p + 8 > size)
            return Error::fail("a damaged cabinet");
        size_t block = readLE32(cab + p);
        const uint16_t blocks = readLE16(cab + p + 4);
        const uint16_t compression = readLE16(cab + p + 6) & 0x0f;
        p += 8 + folderReserve;
        if (compression != kStored && compression != kMszip)
            return Error::fail("a cabinet compressed with Quantum or LZX, which Reluva doesn't read");
        std::vector<uint8_t> data;
        std::vector<uint8_t> window;
        for (uint16_t b = 0; b < blocks; ++b) {
            if (block + 8 + dataReserve > size)
                return Error::fail("a cabinet cut short");
            const uint16_t packed = readLE16(cab + block + 4);
            const uint16_t unpacked = readLE16(cab + block + 6);
            const size_t from = block + 8 + dataReserve;
            if (from + packed > size)
                return Error::fail("a cabinet cut short");
            if (compression == kStored) {
                data.insert(data.end(), cab + from, cab + from + packed);
            } else {
                if (packed < 2 || cab[from] != 'C' || cab[from + 1] != 'K')
                    return Error::fail("a damaged MSZIP block");
                // The last 32 KB out, as the stream's history.
                const size_t history = window.size();
                size_t consumed = 0;
                if (Error e = inflateStream(cab + from + 2, packed - 2u, window, consumed, history + unpacked, history))
                    return Error::fail("a damaged MSZIP block: " + e.message);
                if (window.size() - history != unpacked)
                    return Error::fail("an MSZIP block of the wrong size");
                data.insert(data.end(), window.begin() + long(history), window.end());
                if (window.size() > kMszipWindow)
                    window.erase(window.begin(), window.end() - long(kMszipWindow));
            }
            block = from + packed;
        }
        folders.push_back(std::move(data));
    }
    p = filesAt;
    for (uint16_t i = 0; i < fileCount; ++i) {
        if (p + 16 > size)
            return Error::fail("a damaged cabinet");
        const uint32_t fileSize = readLE32(cab + p);
        const uint32_t offset = readLE32(cab + p + 4);
        const uint16_t folder = readLE16(cab + p + 8);
        p += 16;
        const auto end = std::find(cab + p, cab + size, uint8_t(0));
        if (end == cab + size)
            return Error::fail("a damaged cabinet");
        CabinetFile f;
        f.name.assign(reinterpret_cast<const char *>(cab + p), size_t(end - (cab + p)));
        p = size_t(end - cab) + 1;
        // Continued from or into another cabinet (0xFFFD and up): not read.
        if (folder >= folders.size())
            continue;
        const std::vector<uint8_t> &data = folders[folder];
        if (uint64_t(offset) + fileSize > data.size())
            return Error::fail("a cabinet's file runs past its folder: " + f.name);
        f.data.assign(data.begin() + long(offset), data.begin() + long(offset + fileSize));
        out.push_back(std::move(f));
    }
    return Error::success();
}

} // namespace

Error readMicrosoftCabinets(const std::vector<uint8_t> &file, std::vector<CabinetFile> &out) {
    out.clear();
    static const uint8_t kSignature[8] = {'M', 'S', 'C', 'F', 0, 0, 0, 0};
    auto it = file.begin();
    while ((it = std::search(it, file.end(), std::begin(kSignature), std::end(kSignature))) != file.end()) {
        const size_t at = size_t(it - file.begin());
        std::vector<CabinetFile> files;
        // Bytes that only look like a cabinet's start (inside other data)
        // are passed over; a cabinet that's really there and can't be read
        // is an error.
        if (Error e = readCabinet(file, at, files); e) {
            if (at + 36 <= file.size() && readLE32(file.data() + at + 8) <= file.size() - at &&
                readLE16(file.data() + at + 24) == 0x0103)
                return e;
            ++it;
            continue;
        }
        const uint32_t size = readLE32(file.data() + at + 8);
        for (CabinetFile &f : files)
            out.push_back(std::move(f));
        it += long(std::max<uint32_t>(size, 8));
    }
    return Error::success();
}

} // namespace reluva
