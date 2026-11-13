// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/stream.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stharbinger {

// NAME.ALL with its NAME.LST offsets and, if present, NAME.CAT names.
//
// Entries are always available by index. With a .CAT they are also members by
// name, so an LstArchive over SSE.ALL can sit in a SearchSet (through a
// SharedArchive) and serve CC.ALL, CC.LST ... exactly as if they had been
// installed to disk.
class LstArchive : public Archive {
public:
    // Reads `base`.LST, `base`.ALL and the optional `base`.CAT from `source`
    // (e.g. base = "SSE/SSE"). Returns nullptr if the .LST or .ALL is missing
    // or the offsets are invalid.
    static std::shared_ptr<LstArchive> open(const Archive &source, const std::string &base);
    static std::shared_ptr<LstArchive> open(const SearchSet &source, const std::string &base);
    // The same from data already in memory; `cat` may be empty.
    static std::shared_ptr<LstArchive> create(std::vector<uint8_t> all, const std::vector<uint8_t> &lst,
                                              const std::vector<uint8_t> &cat = std::vector<uint8_t>());

    uint32_t size() const { return uint32_t(_offsets.size()) - 1; }
    uint32_t entrySize(uint32_t index) const;
    // A stream over entry `index` (sharing the archive buffer), or nullptr.
    std::unique_ptr<MemoryReadStream> getEntry(uint32_t index) const;
    // The name from the .CAT, or an empty string.
    std::string getName(uint32_t index) const;
    // Serves the members under a directory, e.g. "ART/" + name.
    void setPrefix(const std::string &prefix);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _description; }
    // A stream over a member, or nullptr.
    std::unique_ptr<MemoryReadStream> openMember(std::string_view name) const;

private:
    LstArchive() {}
    using Reader = bool (*)(const void *source, const std::string &name, std::vector<uint8_t> &out);
    static std::shared_ptr<LstArchive> open(const void *source, Reader reader, const std::string &base);

    std::string _description;

    std::shared_ptr<const std::vector<uint8_t>> _data;
    std::vector<uint32_t> _offsets;          // size() + 1 bounds
    std::vector<std::string> _names;         // from the .CAT, may be empty
    std::map<std::string, uint32_t> _byName; // normalized name -> index
};

// An archive kept elsewhere too (a cache of LstArchives), in a SearchSet.
class SharedArchive : public Archive {
public:
    explicit SharedArchive(std::shared_ptr<const Archive> archive) : _archive(std::move(archive)) {}
    std::vector<std::string> list() const override { return _archive->list(); }
    bool has(std::string_view name) const override { return _archive->has(name); }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override { return _archive->read(name, out); }
    std::string describe() const override { return _archive->describe(); }

private:
    std::shared_ptr<const Archive> _archive;
};

// A file of a search set as a stream, or nullptr if it isn't there.
std::unique_ptr<SeekableReadStream> openFile(const SearchSet &search, std::string_view name);

} // namespace reluva::stharbinger
