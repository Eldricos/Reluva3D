// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once

#include "common/types.h"
#include <string>
#include <vector>

namespace reluva {

struct PeResourceName {
    uint32_t id = 0;
    bool named = false;
    std::u16string text;
    bool operator==(const PeResourceName&) const = default;
    bool isId(uint32_t value) const { return !named && id == value; }
};

struct PeResource {
    PeResourceName type, name;
    uint16_t language = 0;
    uint32_t codePage = 0;
    std::vector<uint8_t> data;
};

// Reads PE32/PE32+ resources as data, without loading executable code. Supports
// the type/name/language directory layout, including named types and names.
// RVAs must map wholly into raw section data; offsets inside the directory
// are bounded by its declared size. Results are empty on failure.
Error readPeResources(const std::vector<uint8_t>& file, std::vector<PeResource>& out);
const PeResource* findPeResource(const std::vector<PeResource>& resources,
                                 uint32_t type, uint32_t name, uint16_t language);

struct PeString {
    uint32_t id = 0;
    uint16_t language = 0;
    std::string text; // UTF-8; valid surrogate pairs decoded, invalid UTF-16 rejected
};

// Decodes all RT_STRING blocks (type 6). No implicit language fallback and no
// replacement of malformed text. Empty entries are omitted; IDs are preserved.
Error readPeStrings(const std::vector<PeResource>& resources, std::vector<PeString>& out);

} // namespace reluva
