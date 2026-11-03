// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/text_resources.h"
#include <charconv>

namespace reluva::boggle {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string_view trim(std::string_view s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}
std::string_view line(std::string_view& s) {
    const auto end = s.find('\n');
    const auto result = s.substr(0, end);
    s = end == s.npos ? std::string_view{} : s.substr(end + 1);
    return trim(result);
}
bool number(std::string_view s, unsigned& value) {
    if (s.empty()) return false;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}
}
Error readPalette(std::string_view text, Palette& out) {
    out = {};
    auto fail = [] { return Error::fail("JASC palette: expected 0100, 256 RGB entries in range 0..255"); };
    if (text.size() > 65536 || line(text) != "JASC-PAL" || line(text) != "0100" || line(text) != "256")
        return fail();
    Palette result{};
    for (auto& color : result) {
        auto row = line(text);
        unsigned channels[3]{};
        for (auto& channel : channels) {
            row = trim(row);
            const auto end = row.find_first_of(" \t\r\n");
            if (!number(row.substr(0, end), channel) || channel > 255) return fail();
            row = end == row.npos ? std::string_view{} : row.substr(end);
        }
        if (!trim(row).empty()) return fail();
        color = {uint8_t(channels[0]), uint8_t(channels[1]), uint8_t(channels[2])};
    }
    if (!trim(text).empty()) return fail();
    out = result;
    return Error::success();
}
Error readDiceSet(std::string_view text, DiceSet& out) {
    out = {};
    auto fail = [] { return Error::fail("Dice set: expected 16 or 25 rows of six nonempty letter faces"); };
    if (text.size() > 65536) return fail();
    DiceSet result;
    while (!text.empty()) {
        auto row = line(text);
        if (row.empty() || result.dice.size() == 25) return fail();
        std::array<std::string, 6> die;
        for (size_t face = 0; face < die.size(); ++face) {
            const auto end = row.find(',');
            const auto token = trim(row.substr(0, end));
            if (token.empty() || token.size() > 8 || (face < 5) != (end != row.npos)) return fail();
            for (unsigned char c : token)
                if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && c < 0x80) return fail();
            die[face] = std::string(token);
            row = end == row.npos ? std::string_view{} : row.substr(end + 1);
        }
        result.dice.push_back(std::move(die));
    }
    if (result.dice.size() != 16 && result.dice.size() != 25) return fail();
    result.side = result.dice.size() == 16 ? 4 : 5;
    out = std::move(result);
    return Error::success();
}
} // namespace reluva::boggle
