// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::text {

/** UTF-16 code units as UTF-8; an unpaired surrogate becomes U+FFFD. */
std::string utf16leToUtf8(const uint16_t *units, size_t count);

/**
 * Windows-1252 text (the Western code page of Windows 95 games) as UTF-8;
 * its five unassigned bytes become U+FFFD.
 */
std::string windows1252ToUtf8(std::string_view s);

/** UTF-8 text as Windows-1252; a character it hasn't becomes '?'. */
std::string utf8ToWindows1252(std::string_view s);

/**
 * The code point at s[i] of UTF-8 text, advancing i past it (U+FFFD for a
 * bad sequence). i must be less than s.size().
 */
uint32_t nextUtf8(std::string_view s, size_t &i);

/**
 * A code point as Latin-1 (U+0000-U+00FF) for drawing in fonts limited to
 * it: typographic quotes and dashes become their ASCII forms, a bullet a
 * middle dot, a tab a space, anything else outside Latin-1 '?'.
 */
uint32_t toLatin1(uint32_t cp);

/**
 * Changing and ignoring case, of ASCII letters only: other bytes (UTF-8's
 * among them) are kept and compared as they are.
 */
///@{
std::string toUpper(std::string_view s);
std::string toLower(std::string_view s);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
bool hasSuffixIgnoreCase(std::string_view s, std::string_view suffix);
///@}
/**
 * Whether `s` matches a pattern where '*' is any run of characters and '?'
 * any one, ignoring ASCII case ("MUSIC.?", "*.ovr").
 */
bool matchGlob(std::string_view s, std::string_view pattern);
/** The parts between each `sep` (one more than there are separators). */
std::vector<std::string> split(std::string_view s, char sep);
/** Without the white space at either end. */
std::string trim(std::string_view s);
/** printf's formatting into a string. */
std::string format(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace reluva::text
