// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/settings.h"

#include "common/bytes.h"
#include "common/fs.h"
#include "common/text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::sfc {

namespace {

std::string trim(const std::string &s) {
    const size_t first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// A section's names as an array: "0", "1"... in order, as the map's header
// names the indexes of Regions, Terrain, Planets and Bases.
std::vector<std::string> arrayOf(const SettingsFile &file, const std::string &section) {
    std::vector<std::string> out;
    for (size_t i = 0;; ++i) {
        const auto v = file.value(section, std::to_string(i));
        if (!v)
            return out;
        out.push_back(*v);
    }
}

} // namespace

Error SettingsFile::read(const std::vector<uint8_t> &data) {
    sections.assign(1, Section{});
    const std::string text = text::windows1252ToUtf8(std::string(data.begin(), data.end()));
    size_t number = 0;
    for (size_t start = 0; start < text.size(); ++number) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string::npos)
                return Error::fail("line " + std::to_string(number + 1) + ": a section without its ]");
            sections.push_back(Section{trim(line.substr(1, close - 1)), {}});
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return Error::fail("line " + std::to_string(number + 1) + ": neither a section nor key = value");
        Entry e;
        e.key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (!value.empty() && value[0] == '"') {
            // A quote not closed (two of the retail files' names) runs to
            // the line's end.
            const size_t close = value.find('"', 1);
            e.value = value.substr(1, close == std::string::npos ? std::string::npos : close - 1);
            e.quoted = true;
        } else {
            if (const size_t comment = value.find("//"); comment != std::string::npos)
                value = trim(value.substr(0, comment));
            e.value = value;
        }
        sections.back().entries.push_back(std::move(e));
    }
    return Error::success();
}

const SettingsFile::Section *SettingsFile::section(const std::string &name) const {
    const std::string want = fs::toLower(name);
    for (const Section &s : sections)
        if (fs::toLower(s.name) == want)
            return &s;
    return nullptr;
}

std::optional<std::string> SettingsFile::value(const std::string &sectionName, const std::string &key) const {
    const Section *s = section(sectionName);
    if (!s)
        return std::nullopt;
    const std::string want = fs::toLower(key);
    for (const Entry &e : s->entries)
        if (fs::toLower(e.key) == want)
            return e.value;
    return std::nullopt;
}

Error MetaverseMap::read(const std::vector<uint8_t> &data) {
    // The header is settings text, up to where it says the data starts: its
    // "Data=" line, found first.
    static const char kData[] = "\nData=";
    const auto it = std::search(data.begin(), data.end(), kData, kData + sizeof kData - 1);
    if (it == data.end())
        return Error::fail("a map without its data's offset");
    const size_t at =
        std::strtoul(std::string(it + 6, it + std::min<long>(16, data.end() - it - 6)).c_str(), nullptr, 10);
    if (at > data.size())
        return Error::fail("a map cut short");
    // Zeros pad it to there.
    const auto headerEnd = std::find(data.begin(), data.begin() + long(at), uint8_t(0));
    SettingsFile header;
    if (Error e = header.read(std::vector<uint8_t>(data.begin(), headerEnd)))
        return e;
    if (header.value("Version", "FileType").value_or("") != "StarfleetCommand.Map.Metaverse")
        return Error::fail("not a campaign map");
    // The data: the Galaxy, a Map: its width and height, then its cells, an
    // array (its length, then each Cell's seven 4-byte fields).
    if (at + 12 > data.size())
        return Error::fail("a map cut short");
    const uint8_t *d = data.data() + at;
    width = readLE32(d);
    height = readLE32(d + 4);
    const uint32_t count = readLE32(d + 8);
    if (count != uint64_t(width) * height || at + 12 + uint64_t(count) * 28 > data.size())
        return Error::fail("a map whose cells don't fit its size");
    cells.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *c = d + 12 + 28 * i;
        Cell &cell = cells[i];
        cell.economic = int32_t(readLE32(c));
        const uint32_t bits = readLE32(c + 4);
        std::memcpy(&cell.impedence, &bits, 4);
        cell.strength = int32_t(readLE32(c + 8));
        cell.region = int32_t(readLE32(c + 12));
        cell.terrain = int32_t(readLE32(c + 16));
        cell.planet = int32_t(readLE32(c + 20));
        cell.base = int32_t(readLE32(c + 24));
    }
    regions = arrayOf(header, "Classes/Regions");
    terrains = arrayOf(header, "Classes/Terrain");
    planets = arrayOf(header, "Classes/Planets");
    bases = arrayOf(header, "Classes/Bases");
    return Error::success();
}

} // namespace reluva::sfc
