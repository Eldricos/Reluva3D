// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::text {

// Convert UTF-16 little-endian code units to UTF-8. Unpaired surrogates
// become U+FFFD.
std::string utf16leToUtf8(const uint16_t* units, size_t count);

// The code point at s[i] of UTF-8 text, advancing i past it (U+FFFD for a
// bad sequence). i must be less than s.size().
uint32_t nextUtf8(std::string_view s, size_t& i);

// A code point as Latin-1 (U+0000-U+00FF) for drawing in fonts limited to
// it: typographic quotes and dashes become their ASCII forms, a tab a space,
// anything else outside Latin-1 '?'.
uint32_t toLatin1(uint32_t cp);

// ASCII case: the rest of the bytes are kept as they are.
std::string toUpper(std::string_view s);
std::string toLower(std::string_view s);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
bool hasSuffixIgnoreCase(std::string_view s, std::string_view suffix);
// Whether `s` matches a pattern where '*' is any run of characters and '?'
// any one, ignoring ASCII case ("MUSIC.?", "*.ovr").
bool matchGlob(std::string_view s, std::string_view pattern);
// The parts between each `sep` (one more than there are separators).
std::vector<std::string> split(std::string_view s, char sep);
// Without the white space at either end.
std::string trim(std::string_view s);
// printf's formatting into a string.
std::string format(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace reluva::text
