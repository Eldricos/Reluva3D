// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Armada's maps. The layout was worked out from the 49 maps of
// the English release: see
// docs/engines/storm3d/games/armada/formats/bzn.md.

#include "engines/storm3d/formats/bzn.h"

#include <cstring>
#include <string_view>

namespace reluva::storm3d {

namespace {

constexpr size_t kHeaderFields = 17; // the settings before the first object

// The fields every object starts with, in order.
constexpr uint8_t kObjectStart[] = {BznMap::kClass, BznMap::kInt,     BznMap::kVector, BznMap::kInt,
                                    BznMap::kText,  BznMap::kPointer, BznMap::kMatrix};
// A path's: a word, its name's length, its name, its points' count, its
// points, its kind, and a float (0.01 in every map).
constexpr uint8_t kPathFields[] = {BznMap::kRaw,    BznMap::kInt, BznMap::kText, BznMap::kInt,
                                   BznMap::kVector, BznMap::kRaw, BznMap::kFloat};

uint32_t word(const BznMap::Field &f) {
    uint32_t v = 0;
    if (f.data.size() >= 4)
        std::memcpy(&v, f.data.data(), 4);
    return v;
}
float real(const BznMap::Field &f) {
    float v = 0;
    if (f.data.size() >= 4)
        std::memcpy(&v, f.data.data(), 4);
    return v;
}
// Text to its first nul.
std::string text(const BznMap::Field &f) {
    const auto *p = reinterpret_cast<const char *>(f.data.data());
    return {p, strnlen(p, f.data.size())};
}
BznMap::Vec3 point(const BznMap::Field &f, size_t at = 0) {
    BznMap::Vec3 v{};
    if (f.data.size() >= (at + 1) * 12)
        std::memcpy(v.data(), f.data.data() + at * 12, 12);
    return v;
}

bool startsWith(const std::vector<BznMap::Field> &fields, size_t at, const uint8_t *types, size_t count) {
    if (at + count > fields.size())
        return false;
    for (size_t i = 0; i < count; ++i)
        if (fields[at + i].type != types[i])
            return false;
    return true;
}

// The header's lines: `name [1] =` then the value on the next line, or
// `name = value`; "binarySave" true before the fields.
Error readHeader(std::string_view text, BznMap &map, size_t &end) {
    size_t at = 0;
    auto line = [&]() -> std::string_view {
        const size_t to = text.find('\n', at);
        if (to == std::string_view::npos)
            return {};
        std::string_view l = text.substr(at, to - at);
        at = to + 1;
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        return l;
    };
    bool binary = false;
    while (at < text.size() && !binary) {
        const std::string_view l = line();
        if (l.empty())
            return Error::fail("the header ends without binarySave");
        if (l.starts_with("version")) {
            map.version = std::atoi(std::string(line()).c_str());
        } else if (l.starts_with("saveGameDesc = ")) {
            const std::string_view hex = l.substr(15);
            for (size_t i = 0; i + 1 < hex.size(); i += 2) {
                const int c = std::stoi(std::string(hex.substr(i, 2)), nullptr, 16);
                if (!c)
                    break;
                map.description += char(c);
            }
        } else if (l.starts_with("binarySave")) {
            if (line() != "true")
                return Error::fail("the map isn't saved in binary");
            binary = true;
        } else {
            return Error::fail("the header has an unknown line: " + std::string(l));
        }
    }
    end = at;
    return Error::success();
}

} // namespace

Error BznMap::parse(const std::vector<uint8_t> &file) {
    *this = {};
    // The header is short text; the fields start after its last line.
    const std::string_view all(reinterpret_cast<const char *>(file.data()), file.size());
    size_t at = 0;
    if (Error err = readHeader(all.substr(0, std::min<size_t>(all.size(), 4096)), *this, at))
        return err;

    // Each field: a word whose low byte is its type (the other three are
    // whatever was in the program's memory), its size, its bytes.
    while (at + 8 <= file.size()) {
        uint32_t size = 0;
        std::memcpy(&size, file.data() + at + 4, 4);
        if (size > file.size() - at - 8)
            return Error::fail("a field runs past the end, at " + std::to_string(at));
        fields.push_back({file[at], {file.begin() + long(at + 8), file.begin() + long(at + 8 + size)}});
        at += 8 + size;
    }
    if (at != file.size())
        return Error::fail("the file ends inside a field");

    static constexpr uint8_t kHeaderTypes[kHeaderFields] = {kText,  kBool,  kText,  kFloat, kFloat, kFloat,
                                                            kFloat, kFloat, kFloat, kBool,  kFloat, kFloat,
                                                            kFloat, kText,  kText,  kText,  kInt};
    if (!startsWith(fields, 0, kHeaderTypes, kHeaderFields))
        return Error::fail("the map's settings aren't as expected");
    name = text(fields[0]);
    for (size_t i = 0; i < 6; ++i)
        area[i] = real(fields[3 + i]);
    for (size_t i = 0; i < 3; ++i)
        light[i] = real(fields[10 + i]);
    background = text(fields[13]);
    techTree = text(fields[14]);
    race = text(fields[15]);
    const uint32_t count = word(fields[16]);

    // The objects: each found by the fields every object starts with.
    for (size_t i = kHeaderFields; i < fields.size(); ++i) {
        if (!startsWith(fields, i, kObjectStart, std::size(kObjectStart)))
            continue;
        Object o;
        o.odf = text(fields[i]);
        o.handle = word(fields[i + 1]);
        o.position = point(fields[i + 2]);
        o.team = int32_t(word(fields[i + 3]));
        o.label = text(fields[i + 4]);
        if (fields[i + 6].data.size() == 48)
            std::memcpy(o.transform.data(), fields[i + 6].data.data(), 48);
        o.firstField = i;
        if (!objects.empty())
            objects.back().fieldCount = i - objects.back().firstField;
        objects.push_back(std::move(o));
        i += std::size(kObjectStart) - 1;
    }
    if (objects.size() != count)
        return Error::fail("the map says " + std::to_string(count) + " objects; " + std::to_string(objects.size()) +
                           " were found");

    // A mission's scripts, named in 64-character fields after the objects.
    const size_t afterObjects = objects.empty() ? kHeaderFields : objects.back().firstField + std::size(kObjectStart);

    // The players' counters: nine runs of float, float, integer, float,
    // float, integer, integer, the last nine such runs after the objects.
    static constexpr uint8_t kPlayerFields[] = {kFloat, kFloat, kInt, kFloat, kFloat, kInt, kInt};
    constexpr size_t kPlayers = 9;
    constexpr size_t kRun = std::size(kPlayerFields) * kPlayers;
    for (size_t i = fields.size(); i-- > afterObjects;) {
        if (i + kRun > fields.size())
            continue;
        bool all = true;
        for (size_t p = 0; p < kPlayers && all; ++p)
            all = startsWith(fields, i + p * std::size(kPlayerFields), kPlayerFields, std::size(kPlayerFields));
        if (!all)
            continue;
        for (size_t p = 0; p < kPlayers; ++p) {
            const size_t f = i + p * std::size(kPlayerFields);
            PlayerStart s;
            s.dilithium = real(fields[f]);
            s.maxDilithium = real(fields[f + 1]);
            s.officerLimit = int32_t(word(fields[f + 2]));
            s.crew = real(fields[f + 3]);
            s.maxCrew = real(fields[f + 4]);
            s.officersUsed = int32_t(word(fields[f + 5]));
            s.other = int32_t(word(fields[f + 6]));
            players.push_back(s);
        }
        break;
    }
    for (size_t i = afterObjects; i < fields.size(); ++i)
        if (fields[i].type == kText && fields[i].data.size() == 64) {
            const std::string s = text(fields[i]);
            if (s.size() > 4 &&
                (s.ends_with(".dsl") || s.ends_with(".drl") || s.ends_with(".DSL") || s.ends_with(".DRL")))
                scripts.push_back(s);
        }

    // The paths end the file, after their count.
    size_t first = fields.size();
    for (size_t i = fields.size(); i >= afterObjects + std::size(kPathFields); i -= std::size(kPathFields)) {
        const size_t p = i - std::size(kPathFields);
        if (!startsWith(fields, p, kPathFields, std::size(kPathFields)))
            break;
        Path path;
        path.name = text(fields[p + 2]);
        const uint32_t points = word(fields[p + 3]);
        if (fields[p + 4].data.size() != size_t(points) * 12 || fields[p + 2].data.size() != word(fields[p + 1]))
            return Error::fail("path " + path.name + " isn't as its sizes say");
        for (uint32_t k = 0; k < points; ++k)
            path.points.push_back(point(fields[p + 4], k));
        path.kind = word(fields[p + 5]);
        paths.insert(paths.begin(), std::move(path));
        first = p;
    }
    if (!paths.empty() && (first == 0 || fields[first - 1].type != kInt || word(fields[first - 1]) != paths.size()))
        return Error::fail("the map's paths aren't as their count says");
    if (!objects.empty())
        objects.back().fieldCount = (paths.empty() ? fields.size() : first - 1) - objects.back().firstField;
    return Error::success();
}

} // namespace reluva::storm3d
