// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/archive.h"

#include "common/bytes.h"
#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"

namespace reluva::stharbinger {

std::shared_ptr<LstArchive> LstArchive::open(const Archive& source, const std::string& base) {
    return open(
        &source,
        [](const void* src, const std::string& name, std::vector<uint8_t>& out) {
            const Archive& a = *static_cast<const Archive*>(src);
            return a.has(name) && !a.read(name, out);
        },
        base);
}

std::shared_ptr<LstArchive> LstArchive::open(const SearchSet& source, const std::string& base) {
    return open(
        &source,
        [](const void* src, const std::string& name, std::vector<uint8_t>& out) {
            const SearchSet& s = *static_cast<const SearchSet*>(src);
            return s.has(name) && !s.read(name, out);
        },
        base);
}

std::shared_ptr<LstArchive> LstArchive::open(const void* source, Reader reader, const std::string& base) {
    std::vector<uint8_t> all, lst, cat;
    if (!reader(source, base + ".LST", lst) || !reader(source, base + ".ALL", all))
        return nullptr;
    reader(source, base + ".CAT", cat);
    auto archive = create(std::move(all), lst, cat);
    if (!archive)
        log::warning("%s.LST does not match %s.ALL", base.c_str(), base.c_str());
    else
        archive->description_ = base + ".ALL";
    return archive;
}

std::shared_ptr<LstArchive> LstArchive::create(std::vector<uint8_t> all, const std::vector<uint8_t>& lst,
                                               const std::vector<uint8_t>& cat) {
    std::shared_ptr<LstArchive> archive(new LstArchive());
    uint32_t total = uint32_t(all.size());
    for (size_t i = 0; i + 4 <= lst.size(); i += 4)
        archive->offsets_.push_back(readLE32(&lst[i]));
    // The last offset is normally the size of the .ALL; add it if it is not.
    if (archive->offsets_.empty() || archive->offsets_.back() != total)
        archive->offsets_.push_back(total);
    for (size_t i = 1; i < archive->offsets_.size(); i++)
        if (archive->offsets_[i] < archive->offsets_[i - 1] || archive->offsets_[i] > total)
            return nullptr;
    archive->data_ = std::make_shared<const std::vector<uint8_t>>(std::move(all));

    if (!cat.empty()) {
        // Text: the entry count, then one name per line (CRLF, maybe a ^Z at the end).
        std::string text(cat.begin(), cat.end());
        std::vector<std::string> lines;
        for (std::string line : text::split(text, '\n')) {
            line = text::trim(line);
            if (!line.empty() && line != "\x1a")
                lines.push_back(line);
        }
        if (!lines.empty() && lines[0].find_first_not_of("0123456789") == std::string::npos)
            lines.erase(lines.begin());
        if (lines.size() != archive->size())
            log::warning("CAT lists %u names for %u entries", unsigned(lines.size()), archive->size());
        archive->names_ = lines;
        for (uint32_t i = 0; i < lines.size() && i < archive->size(); i++)
            archive->byName_.emplace(fs::normalizeDosPath(lines[i]), i);
    }
    return archive;
}

uint32_t LstArchive::entrySize(uint32_t index) const {
    return index < size() ? offsets_[index + 1] - offsets_[index] : 0;
}

std::unique_ptr<MemoryReadStream> LstArchive::getEntry(uint32_t index) const {
    if (index >= size())
        return nullptr;
    return std::make_unique<MemoryReadStream>(data_, offsets_[index], entrySize(index));
}

std::string LstArchive::getName(uint32_t index) const { return index < names_.size() ? names_[index] : std::string(); }

void LstArchive::setPrefix(const std::string& prefix) {
    byName_.clear();
    for (uint32_t i = 0; i < names_.size() && i < size(); i++)
        byName_.emplace(fs::normalizeDosPath(prefix + names_[i]), i);
}

bool LstArchive::has(std::string_view name) const { return byName_.count(fs::normalizeDosPath(name)) != 0; }

std::unique_ptr<MemoryReadStream> LstArchive::openMember(std::string_view name) const {
    auto it = byName_.find(fs::normalizeDosPath(name));
    if (it == byName_.end())
        return nullptr;
    return getEntry(it->second);
}

Error LstArchive::read(std::string_view name, std::vector<uint8_t>& out) const {
    auto entry = openMember(name);
    if (!entry)
        return Error::fail(std::string(name) + " not found in " + description_);
    out.assign(entry->data(), entry->data() + entry->size());
    return Error::success();
}

std::vector<std::string> LstArchive::list() const {
    std::vector<std::string> out;
    for (const auto& entry : byName_)
        out.push_back(entry.first);
    return out;
}

std::unique_ptr<SeekableReadStream> openFile(const SearchSet& search, std::string_view name) {
    std::vector<uint8_t> data;
    if (!search.has(name) || search.read(name, data))
        return nullptr;
    return std::make_unique<MemoryReadStream>(std::move(data));
}

} // namespace reluva::stharbinger
