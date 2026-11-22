// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Building the Theme Park World engine's files for its tests, laid out as
// SimCoaster's are (docs/engines/tpw/games/simcoaster/formats/).

#pragma once

#include "tests/support/zip_writer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw::testing {

using Bytes = std::vector<uint8_t>;
using test::toBytes;

inline void put32(Bytes &b, size_t at, uint32_t v) {
    if (b.size() < at + 4)
        b.resize(at + 4);
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// RefPack of only literals: runs of 4 (0xe0), then the rest with the end
// code. `size` is the header's, the real one unless a test says otherwise.
inline Bytes refPack(const Bytes &data, size_t size) {
    Bytes out = {0x10, 0xfb, uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size)};
    size_t at = 0;
    for (; data.size() - at >= 4; at += 4) {
        out.push_back(0xe0);
        out.insert(out.end(), data.begin() + long(at), data.begin() + long(at) + 4);
    }
    out.push_back(uint8_t(0xfc + (data.size() - at)));
    out.insert(out.end(), data.begin() + long(at), data.end());
    return out;
}

struct WadFile {
    std::string storedName; // as the archive has it: "folder\\name" or just "name"
    Bytes data;
    bool packed = false;
    size_t packedSizeField = 0; // RefPack's header size, if not the data's
};

// An archive: the header, a record per file, the data, then the names.
inline Bytes makeWad(const std::vector<WadFile> &files) {
    Bytes b = {'D', 'W', 'F', 'B'};
    put32(b, 4, 2);
    const size_t records = 0x58;
    put32(b, 0x48, uint32_t(files.size()));
    b.resize(records + files.size() * 0x28);
    std::vector<size_t> nameAt;
    for (size_t i = 0; i < files.size(); ++i) {
        const WadFile &f = files[i];
        const Bytes stored = f.packed ? refPack(f.data, f.packedSizeField ? f.packedSizeField : f.data.size()) : f.data;
        const size_t r = records + i * 0x28;
        put32(b, r, uint32_t(0x100 + i));
        put32(b, r + 12, uint32_t(b.size()));
        put32(b, r + 16, uint32_t(stored.size()));
        put32(b, r + 20, f.packed ? 4 : 0);
        put32(b, r + 24, f.packed ? uint32_t(f.data.size()) : 0);
        b.insert(b.end(), stored.begin(), stored.end());
    }
    const size_t names = b.size();
    for (size_t i = 0; i < files.size(); ++i) {
        put32(b, records + i * 0x28 + 4, uint32_t(b.size()));
        put32(b, records + i * 0x28 + 8, uint32_t(files[i].storedName.size() + 1));
        b.insert(b.end(), files[i].storedName.begin(), files[i].storedName.end());
        b.push_back(0);
    }
    put32(b, 0x4c, uint32_t(names));
    put32(b, 0x50, uint32_t(b.size() - names));
    return b;
}

} // namespace reluva::tpw::testing
