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
 * docs/engines/age/games/mm2/formats/ar.md. Folder entries are left out of
 * list().
 */
class ArArchive : public Archive {
public:
    /** A file's entry in the archive's directory. */
    struct Entry {
        std::string name;    // as stored, '/' separators
        uint32_t offset = 0; // of the data, from the start of the file
        uint32_t size = 0;
        uint32_t storedSize = 0; // differs from size when deflated
        /** Whether it's stored deflated (its stored size differs from its size). */
        bool deflated() const { return storedSize != size; }
    };

    /**
     * Reads the archive's directory; files are read from it when asked for.
     * Fails without "DAVE", or on a directory or file past the file's end.
     * Of two files with one name, the first is kept.
     */
    Error open(const std::filesystem::path &path);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _path.string(); }

    /** Every file in the archive's order, folders left out. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** The file of that name (matched as normalizeArchiveName does), or nullptr. */
    const Entry *entry(std::string_view name) const;
    /**
     * A file's contents, inflated if need be. Opens the archive for each read,
     * so a const archive can be read from several threads at once.
     */
    Error read(const Entry &entry, std::vector<uint8_t> &out) const;

private:
    std::filesystem::path _path;
    std::vector<Entry> _entries;
    std::unordered_map<std::string, size_t> _byName; // normalized name -> _entries index
};

} // namespace reluva::age
