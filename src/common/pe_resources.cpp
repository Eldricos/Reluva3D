// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/pe_resources.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <tuple>

namespace reluva {
namespace {

struct Bytes {
    const std::vector<uint8_t>& data;
    bool has(size_t at, size_t n) const { return at <= data.size() && n <= data.size() - at; }
    uint16_t u16(size_t at) const { return uint16_t(data[at] | uint16_t(data[at + 1]) << 8); }
    uint32_t u32(size_t at) const { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; }
};

bool utf16(const Bytes& bytes, size_t at, size_t count, std::string& out) {
    if (count > (bytes.data.size() - std::min(at, bytes.data.size())) / 2 || !bytes.has(at, count * 2))
        return false;
    out.clear();
    for (size_t i = 0; i < count; ++i) {
        uint32_t c = bytes.u16(at + i * 2);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == count)
                return false;
            const uint32_t low = bytes.u16(at + i * 2);
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            return false;
        }
        if (c < 0x80) {
            out.push_back(char(c));
        } else if (c < 0x800) {
            out.push_back(char(0xc0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out.push_back(char(0xe0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(char(0x80 | (c & 0x3f)));
        } else {
            out.push_back(char(0xf0 | (c >> 18)));
            out.push_back(char(0x80 | ((c >> 12) & 0x3f)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(char(0x80 | (c & 0x3f)));
        }
    }
    return true;
}

struct Section { uint32_t rva, raw, size; };

class ResourceReader {
public:
    explicit ResourceReader(const std::vector<uint8_t>& data) : b_{data} {}

    Error read(std::vector<PeResource>& out) {
        if (b_.data.size() > 128 * 1024 * 1024 || !b_.has(0, 64) || b_.u16(0) != 0x5a4d)
            return fail("invalid DOS header or file too large");
        const size_t pe = b_.u32(0x3c);
        if (pe < 64 || !b_.has(pe, 24) || b_.u32(pe) != 0x4550)
            return fail("invalid PE header");
        const size_t opt = pe + 24, optSize = b_.u16(pe + 20);
        if (!b_.has(opt, optSize) || optSize < 2)
            return fail("truncated optional header");
        const auto magic = b_.u16(opt);
        if (magic != 0x10b && magic != 0x20b)
            return fail("unsupported optional header");
        const size_t dirs = magic == 0x10b ? 96 : 112;
        if (optSize < dirs || b_.u32(opt + dirs - 4) > (optSize - dirs) / 8)
            return fail("truncated data directories");
        const size_t count = b_.u16(pe + 6), table = opt + optSize;
        if (!b_.has(table, count * 40))
            return fail("truncated section table");
        for (size_t i = 0; i < count; ++i) {
            const size_t s = table + i * 40;
            Section section{b_.u32(s + 12), b_.u32(s + 20), b_.u32(s + 16)};
            if (!b_.has(section.raw, section.size) || uint64_t(section.rva) + section.size > uint64_t(UINT32_MAX) + 1)
                return fail("invalid section range");
            sections_.push_back(section);
        }
        if (b_.u32(opt + dirs - 4) <= 2)
            return Error::success();
        const uint32_t rva = b_.u32(opt + dirs + 16);
        size_ = b_.u32(opt + dirs + 20);
        if (!rva && !size_)
            return Error::success();
        if (!rva || size_ < 16 || !map(rva, size_, base_))
            return fail("resource directory outside file-backed sections");
        std::array<PeResourceName, 3> path;
        if (!directory(0, 0, path))
            return fail(reason_);
        out = std::move(resources_);
        return Error::success();
    }

private:
    static Error fail(const std::string& why) { return Error::fail("PE resources: " + why); }
    bool bad(const char* why) { reason_ = why; return false; }
    bool inside(size_t rel, size_t n) const { return rel <= size_ && n <= size_ - rel; }
    bool map(uint32_t rva, size_t n, size_t& at) const {
        bool found = false;
        for (const auto& s : sections_) {
            if (rva < s.rva || uint64_t(rva) - s.rva > s.size)
                continue;
            const auto delta = size_t(rva - s.rva);
            if (n > s.size - delta)
                continue;
            if (found) // Ambiguous overlapping raw mappings are not accepted.
                return false;
            at = size_t(s.raw) + delta;
            found = b_.has(at, n);
        }
        return found;
    }
    bool name(uint32_t key, PeResourceName& out) {
        out = {};
        if (!(key & 0x80000000)) {
            out.id = key;
            return true;
        }
        const size_t rel = key & 0x7fffffff;
        if (!inside(rel, 2))
            return bad("name offset outside directory");
        const size_t n = b_.u16(base_ + rel);
        if (n * 2 > 64 * 1024 * 1024 - nameBytes_ || !inside(rel + 2, n * 2))
            return bad("truncated resource name");
        nameBytes_ += n * 2;
        std::string checked;
        if (!utf16(b_, base_ + rel + 2, n, checked))
            return bad("invalid UTF-16 resource name");
        out.named = true;
        for (size_t i = 0; i < n; ++i)
            out.text.push_back(char16_t(b_.u16(base_ + rel + 2 + i * 2)));
        return true;
    }
    bool directory(size_t rel, size_t depth, std::array<PeResourceName, 3>& path) {
        if (!inside(rel, 16))
            return bad("truncated directory header");
        const size_t at = base_ + rel;
        const size_t named = b_.u16(at + 12), count = named + b_.u16(at + 14);
        if (count > 65536 - entries_ || !inside(rel + 16, count * 8))
            return bad("directory entry limit or truncated entries");
        entries_ += count;
        std::set<std::tuple<bool, uint32_t, std::u16string>> keys;
        for (size_t i = 0; i < count; ++i) {
            const size_t entry = at + 16 + i * 8;
            if (!name(b_.u32(entry), path[depth]))
                return false;
            const auto& key = path[depth];
            if (key.named != (i < named) || !keys.emplace(key.named, key.id, key.text).second)
                return bad("invalid or duplicate resource key");
            const uint32_t target = b_.u32(entry + 4);
            if (depth < 2) {
                if (!(target & 0x80000000))
                    return bad("expected type/name/language directories");
                if (!directory(target & 0x7fffffff, depth + 1, path))
                    return false;
            } else {
                if (key.named || key.id > 65535 || (target & 0x80000000) || !inside(target, 16))
                    return bad("invalid language or resource data entry");
                const size_t dataEntry = base_ + target;
                const size_t n = b_.u32(dataEntry + 4);
                size_t payload = 0;
                const size_t copiedNames = (path[0].text.size() + path[1].text.size()) * 2;
                if (copiedNames > 64 * 1024 * 1024 - payloadBytes_ ||
                    n > 64 * 1024 * 1024 - payloadBytes_ - copiedNames || !map(b_.u32(dataEntry), n, payload))
                    return bad("invalid resource payload range or size limit");
                payloadBytes_ += n + copiedNames;
                PeResource resource{path[0], path[1], uint16_t(key.id), b_.u32(dataEntry + 8), {}};
                resource.data.assign(b_.data.begin() + payload, b_.data.begin() + payload + n);
                resources_.push_back(std::move(resource));
            }
        }
        return true;
    }
    Bytes b_;
    std::vector<Section> sections_;
    std::vector<PeResource> resources_;
    size_t base_ = 0, size_ = 0, entries_ = 0, payloadBytes_ = 0, nameBytes_ = 0;
    std::string reason_;
};
} // namespace

Error readPeResources(const std::vector<uint8_t>& file, std::vector<PeResource>& out) {
    out.clear();
    return ResourceReader(file).read(out);
}

const PeResource* findPeResource(const std::vector<PeResource>& resources,
                                 uint32_t type, uint32_t name, uint16_t language) {
    for (const auto& resource : resources)
        if (resource.type.isId(type) && resource.name.isId(name) && resource.language == language)
            return &resource;
    return nullptr;
}

Error readPeStrings(const std::vector<PeResource>& resources, std::vector<PeString>& out) {
    out.clear();
    std::vector<PeString> strings;
    std::set<std::pair<uint32_t, uint16_t>> keys;
    size_t textBytes = 0;
    for (const auto& resource : resources) {
        if (!resource.type.isId(6))
            continue;
        if (resource.name.named || !resource.name.id || resource.name.id > 4096)
            return Error::fail("PE strings: invalid block ID");
        Bytes b{resource.data};
        size_t at = 0;
        for (uint32_t slot = 0; slot < 16; ++slot) {
            if (!b.has(at, 2))
                return Error::fail("PE strings: truncated block");
            const size_t n = b.u16(at);
            at += 2;
            PeString s{(resource.name.id - 1) * 16 + slot, resource.language, {}};
            if (!keys.emplace(s.id, s.language).second || !utf16(b, at, n, s.text))
                return Error::fail("PE strings: duplicate ID/language or invalid UTF-16");
            at += n * 2;
            if (s.text.size() > 64 * 1024 * 1024 - textBytes)
                return Error::fail("PE strings: text size limit");
            textBytes += s.text.size();
            if (n)
                strings.push_back(std::move(s));
        }
        if (resource.data.size() - at > 3 ||
            !std::all_of(resource.data.begin() + at, resource.data.end(), [](uint8_t v) { return v == 0; }))
            return Error::fail("PE strings: unexpected trailing data");
    }
    std::sort(strings.begin(), strings.end(), [](const PeString& a, const PeString& b) {
        return std::tie(a.id, a.language) < std::tie(b.id, b.language);
    });
    out = std::move(strings);
    return Error::success();
}
} // namespace reluva
