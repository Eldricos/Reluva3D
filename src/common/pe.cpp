// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/pe.h"

#include <algorithm>
#include <cstdio>

namespace reluva {

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    bool ok(size_t at, size_t n) const { return at <= d.size() && n <= d.size() - at; }
    uint16_t u16(size_t at) const { return ok(at, 2) ? uint16_t(d[at] | d[at + 1] << 8) : 0; }
    uint32_t u32(size_t at) const {
        return ok(at, 4) ? uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 |
                               uint32_t(d[at + 3]) << 24
                         : 0;
    }
    uint64_t u64(size_t at) const { return uint64_t(u32(at)) | uint64_t(u32(at + 4)) << 32; }
    std::string str(size_t at, size_t max = 512) const {
        std::string s;
        while (at < d.size() && d[at] && s.size() < max)
            s.push_back(char(d[at++]));
        return s;
    }
};

// The data directories' names, in the order the optional header lists them.
const char* const kDirectories[16] = {"export",   "import",       "resource", "exception", "security",
                                      "basereloc", "debug",       "architecture", "globalptr", "tls",
                                      "load config", "bound import", "iat",      "delay import", "clr", "reserved"};

} // namespace

int64_t PeImage::fileOffset(uint32_t rva) const {
    if (rva < headersSize)
        return rva;
    for (const Section& s : sections)
        if (rva >= s.virtualAddress && rva < s.virtualAddress + std::max(s.virtualSize, s.rawSize))
            return rva - s.virtualAddress < s.rawSize ? int64_t(s.rawOffset) + (rva - s.virtualAddress) : -1;
    return -1;
}

Error parsePe(const std::vector<uint8_t>& file, PeImage& out) {
    out = PeImage{};
    const Reader r{file};
    if (r.u16(0) != 0x5a4d) // "MZ"
        return Error::fail("Not an executable (no MZ header)");
    const uint32_t pe = r.u32(0x3c);
    if (r.u32(pe) != 0x00004550) // "PE\0\0"
        return Error::fail("Not a PE executable");
    const size_t coff = pe + 4;
    out.machine = r.u16(coff);
    const uint16_t sectionCount = r.u16(coff + 2);
    out.timestamp = r.u32(coff + 4);
    const uint16_t optionalSize = r.u16(coff + 16);
    out.characteristics = r.u16(coff + 18);
    const size_t opt = coff + 20;
    const uint16_t magic = r.u16(opt);
    if (magic != 0x10b && magic != 0x20b)
        return Error::fail("Unknown optional header");
    out.pe32Plus = magic == 0x20b;
    out.linkerVersion = std::to_string(file.size() > opt + 3 ? file[opt + 2] : 0) + "." +
                        std::to_string(file.size() > opt + 3 ? file[opt + 3] : 0);
    out.entryPoint = r.u32(opt + 16);
    out.imageBase = out.pe32Plus ? r.u64(opt + 24) : r.u32(opt + 28);
    out.sectionAlignment = r.u32(opt + 32);
    out.fileAlignment = r.u32(opt + 36);
    out.imageSize = r.u32(opt + 56);
    out.headersSize = r.u32(opt + 60);
    out.subsystem = r.u16(opt + 68);
    const size_t dirs = opt + (out.pe32Plus ? 112 : 96);
    const uint32_t dirCount = std::min<uint32_t>(r.u32(dirs - 4), 16);
    auto dir = [&](int i) -> std::pair<uint32_t, uint32_t> {
        if (uint32_t(i) >= dirCount)
            return {0, 0};
        return {r.u32(dirs + size_t(8 * i)), r.u32(dirs + size_t(8 * i) + 4)};
    };

    const size_t sections = opt + optionalSize;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const size_t s = sections + size_t(40) * i;
        if (!r.ok(s, 40))
            return Error::fail("Section table cut short");
        PeImage::Section sec;
        for (size_t k = 0; k < 8 && file[s + k]; ++k)
            sec.name.push_back(char(file[s + k]));
        sec.virtualSize = r.u32(s + 8);
        sec.virtualAddress = r.u32(s + 12);
        sec.rawSize = r.u32(s + 16);
        sec.rawOffset = r.u32(s + 20);
        sec.characteristics = r.u32(s + 36);
        out.sections.push_back(sec);
    }
    auto at = [&](uint32_t rva) { return out.fileOffset(rva); };

    // Exports: the DLL's name, then its functions by ordinal, with names
    // for those that have them; an address inside the export directory is
    // a forwarder string.
    if (const auto [rva, size] = dir(0); rva && at(rva) >= 0) {
        const auto e = size_t(at(rva));
        if (const int64_t n = at(r.u32(e + 12)); n >= 0)
            out.exportName = r.str(size_t(n));
        const uint32_t base = r.u32(e + 16), count = r.u32(e + 20), named = r.u32(e + 24);
        const int64_t functions = at(r.u32(e + 28)), names = at(r.u32(e + 32)), ordinals = at(r.u32(e + 36));
        if (functions >= 0 && count < 65536) {
            for (uint32_t i = 0; i < count; ++i) {
                PeImage::Export x;
                x.ordinal = base + i;
                x.rva = r.u32(size_t(functions) + 4 * i);
                if (!x.rva)
                    continue;
                if (x.rva >= rva && x.rva < rva + size && at(x.rva) >= 0)
                    x.forwarder = r.str(size_t(at(x.rva)));
                out.exports.push_back(x);
            }
            if (names >= 0 && ordinals >= 0)
                for (uint32_t i = 0; i < named && i < 65536; ++i) {
                    const uint16_t index = r.u16(size_t(ordinals) + 2 * i);
                    const int64_t name = at(r.u32(size_t(names) + 4 * i));
                    for (auto& x : out.exports)
                        if (x.ordinal == base + index && name >= 0)
                            x.name = r.str(size_t(name));
                }
        }
    }

    // Imports: each DLL's descriptor (20 bytes) until an empty one; its
    // lookup table (or, without one, its address table) lists the
    // functions, by name (with a hint) or by ordinal.
    if (const auto [rva, size] = dir(1); rva && at(rva) >= 0) {
        (void)size;
        for (size_t d = size_t(at(rva)); r.ok(d, 20) && out.imports.size() < 256; d += 20) {
            const uint32_t lookup = r.u32(d), nameRva = r.u32(d + 12), iat = r.u32(d + 16);
            if (!lookup && !nameRva && !iat)
                break;
            PeImage::ImportedDll dll;
            if (const int64_t n = at(nameRva); n >= 0)
                dll.name = r.str(size_t(n));
            const uint32_t table = lookup ? lookup : iat;
            const size_t entry = out.pe32Plus ? 8 : 4;
            if (const int64_t t = at(table); t >= 0)
                for (size_t k = 0; k < 4096; ++k) {
                    const uint64_t v = out.pe32Plus ? r.u64(size_t(t) + k * entry) : r.u32(size_t(t) + k * entry);
                    if (!v)
                        break;
                    PeImage::Import imp;
                    imp.iatRva = iat + uint32_t(k * entry);
                    const bool byOrdinal = out.pe32Plus ? (v >> 63) != 0 : (v >> 31) != 0;
                    if (byOrdinal) {
                        imp.ordinal = uint32_t(v & 0xffff);
                    } else if (const int64_t h = at(uint32_t(v)); h >= 0) {
                        imp.hint = r.u16(size_t(h));
                        imp.name = r.str(size_t(h) + 2);
                    }
                    dll.functions.push_back(imp);
                }
            out.imports.push_back(std::move(dll));
        }
    }

    // Base relocations: blocks of a page's RVA, the block's size, then
    // 16-bit entries (type in the top 4 bits; type 0 pads).
    if (const auto [rva, size] = dir(5); rva && at(rva) >= 0) {
        size_t b = size_t(at(rva)), end = b + size;
        while (b + 8 <= end && r.ok(b, 8)) {
            const uint32_t blockSize = r.u32(b + 4);
            if (blockSize < 8)
                break;
            for (size_t e = b + 8; e + 2 <= b + blockSize; e += 2) {
                const uint16_t entry = r.u16(e);
                out.relocationCount += (entry >> 12) != 0;
                if (entry >> 12 == 3)
                    out.relocations.push_back(r.u32(b) + (entry & 0xfff));
            }
            b += blockSize;
        }
    }
    out.hasTls = dir(9).first != 0;
    for (int i = 0; i < int(dirCount); ++i)
        if (dir(i).first && i != 0 && i != 1 && i != 5 && i != 9)
            out.otherDirectories.emplace_back(kDirectories[i]);
    return Error::success();
}

} // namespace reluva
