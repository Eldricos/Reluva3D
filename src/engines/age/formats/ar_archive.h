// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::age {

/**
 * An Angel Game Engine .ar archive ("DAVE"), as Midtown Madness 2's
 * mm2core.ar, mm2tex.ar, mm2aud.ar and mm2audex.ar. See
 * docs/engines/age/games/mm2/formats/ar.md. Folder entries are left out of list().
 */
class ArArchive : public Archive {
public:
    /** A file in the archive's directory. */
    struct Entry {
        std::string name;    // as stored, '/' separators
        uint32_t offset = 0; // of the data, from the start of the file
        uint32_t size = 0;
        uint32_t storedSize = 0; // differs from size when deflated
        /** Whether it is stored raw-deflated rather than as is. */
        bool deflated() const { return storedSize != size; }
    };

    /**
     * Reads the archive's directory; of two entries with the same name
     * (case and separators aside), the first is kept.
     * @return A failure if it isn't a DAVE archive or its directory, names
     *         or files run past the end of the file.
     */
    Error open(const std::filesystem::path &path);

    /** The files' names as stored, in the archive's order. */
    std::vector<std::string> list() const override;
    /** Whether the archive has the file, case and separators aside. */
    bool has(std::string_view name) const override;
    /** Reads a file, inflated; safe from several threads, as each read opens the archive afresh. */
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    /** The archive's path. */
    std::string describe() const override { return _path.string(); }

    /** Every file in the archive's order, folders left out. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** The file's entry, case and separators aside; nullptr if there is none. */
    const Entry *entry(std::string_view name) const;
    /** Reads one of entries()' files, inflated. */
    Error read(const Entry &entry, std::vector<uint8_t> &out) const;

private:
    std::filesystem::path _path;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> entries_ index
};

} // namespace reluva::age
