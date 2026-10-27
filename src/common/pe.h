// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva {

// A Windows executable or DLL (the PE/COFF format, from Microsoft's
// published specification): what's needed to see what it holds and
// needs, and to load it. 32-bit images (PE32) in full; 64-bit ones (PE32+)
// as far as their headers.
struct PeImage {
    uint16_t machine = 0; // 0x14c: x86
    uint16_t characteristics = 0;
    uint32_t timestamp = 0;
    bool pe32Plus = false;
    uint64_t imageBase = 0;
    uint32_t entryPoint = 0; // an RVA (relative to the image base); 0 if none
    uint32_t imageSize = 0, headersSize = 0;
    uint32_t sectionAlignment = 0, fileAlignment = 0;
    uint16_t subsystem = 0;
    std::string linkerVersion;

    struct Section {
        std::string name;
        uint32_t virtualAddress = 0, virtualSize = 0;
        uint32_t rawOffset = 0, rawSize = 0;
        uint32_t characteristics = 0; // 0x20 code, 0x40 data, 0x80 bss; 0x20000000 exec, 0x40000000 read, 0x80000000 write
    };
    std::vector<Section> sections;

    struct Export {
        std::string name; // empty: by ordinal only
        uint32_t ordinal = 0;
        uint32_t rva = 0;
        std::string forwarder; // "DLL.Function" if it's forwarded elsewhere
    };
    std::string exportName; // the DLL's own name, as its export directory gives it
    std::vector<Export> exports;

    struct Import {
        std::string name; // empty: by ordinal
        uint32_t ordinal = 0, hint = 0;
        uint32_t iatRva = 0; // where the loader writes its address
    };
    struct ImportedDll {
        std::string name;
        std::vector<Import> functions;
    };
    std::vector<ImportedDll> imports;

    uint32_t relocationCount = 0; // base relocations (to load it at another address)
    std::vector<uint32_t> relocations; // the RVAs of the 32-bit ones (HIGHLOW), which are all a PE32 has
    bool hasTls = false;          // thread-local storage callbacks
    uint32_t resourceRva = 0, resourceSize = 0; // the resource directory
    std::vector<std::string> otherDirectories; // data directories present besides those above

    // The file offset of an RVA, or -1 if it's in no section's file data.
    int64_t fileOffset(uint32_t rva) const;
};

Error parsePe(const std::vector<uint8_t>& file, PeImage& out);

// The data of the first resource of a type (16: the version information),
// whatever its name and language; empty if there's none.
std::vector<uint8_t> findResource(const PeImage& pe, const std::vector<uint8_t>& file, uint32_t type);

} // namespace reluva
