// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/saved_game.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace reluva::storm3d {

namespace {

// The program's version for a game saved, as its head says it (0x803).
constexpr int kVersion = 2051;
// Reluva's mark after the description, where the program's own has its
// binarySave: the program reading a file of Reluva's stops there.
constexpr std::string_view kMark = "reluvaSave [1] =\r\n1\r\n";

// The text after "name = " (or "name [1] =" and a line) at `at`, and where the next line begins.
std::string_view valueAfter(std::string_view text, std::string_view name, size_t &at) {
    const size_t found = text.find(name, at);
    if (found == std::string_view::npos)
        return {};
    size_t start = text.find('=', found);
    if (start == std::string_view::npos)
        return {};
    ++start;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\r' || text[start] == '\n'))
        ++start;
    size_t end = start;
    while (end < text.size() && text[end] != '\r' && text[end] != '\n')
        ++end;
    at = end;
    return text.substr(start, end - start);
}

} // namespace

std::vector<uint8_t> SavedGame::bytes() const {
    std::string head = "version [1] =\r\n" + std::to_string(kVersion) + "\r\nsaveGameDesc = ";
    char field[kDescriptionBytes] = {};
    std::memcpy(field, description.data(), std::min(description.size(), kDescriptionBytes - 1));
    char hex[3];
    for (char c : field) {
        std::snprintf(hex, sizeof hex, "%02x", unsigned(uint8_t(c)));
        head += hex;
    }
    head += "\r\n";
    head += kMark;
    std::vector<uint8_t> out(head.begin(), head.end());
    for (int32_t value : {int32_t(campaign), int32_t(mission), int32_t(map.size())}) {
        const auto *b = reinterpret_cast<const uint8_t *>(&value);
        out.insert(out.end(), b, b + 4);
    }
    out.insert(out.end(), map.begin(), map.end());
    out.insert(out.end(), state.begin(), state.end());
    return out;
}

std::optional<std::string> SavedGame::descriptionOf(const std::vector<uint8_t> &file) {
    // The first 512 bytes, as text: the version, and from 0x7fa on the
    // description (0x543ab0).
    const std::string_view text(reinterpret_cast<const char *>(file.data()), std::min<size_t>(file.size(), 512));
    size_t at = 0;
    const std::string_view version = valueAfter(text, "version", at);
    if (version.empty() || std::atoi(std::string(version).c_str()) < 0x7fa)
        return std::nullopt;
    const std::string_view hex = valueAfter(text, "saveGameDesc", at);
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const char c = char(std::strtol(std::string(hex.substr(i, 2)).c_str(), nullptr, 16));
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

Error SavedGame::parse(const std::vector<uint8_t> &file) {
    const std::string_view text(reinterpret_cast<const char *>(file.data()), file.size());
    const std::optional<std::string> found = descriptionOf(file);
    const size_t mark = text.find(kMark);
    if (!found || mark == std::string_view::npos || mark > 1024)
        return Error::fail("not a game Reluva saved");
    const size_t body = mark + kMark.size();
    if (file.size() < body + 12)
        return Error::fail("the saved game is cut short");
    description = *found;
    int32_t values[3];
    std::memcpy(values, file.data() + body, 12);
    campaign = values[0];
    mission = values[1];
    if (values[2] < 0 || file.size() < body + 12 + size_t(values[2]))
        return Error::fail("the saved game is cut short");
    map.assign(reinterpret_cast<const char *>(file.data()) + body + 12, size_t(values[2]));
    state.assign(file.begin() + std::ptrdiff_t(body + 12 + size_t(values[2])), file.end());
    return Error::success();
}

std::string SavedGame::nextName(const std::vector<std::string> &names) {
    // One past the highest; a name that isn't s<n>.sav counts as 0.
    int highest = -1;
    for (const std::string &name : names) {
        int n = 0;
        if (std::sscanf(name.c_str(), "s%d.sav", &n) != 1)
            n = 0;
        highest = std::max(highest, n);
    }
    return "s" + std::to_string(highest + 1) + ".sav";
}

} // namespace reluva::storm3d
