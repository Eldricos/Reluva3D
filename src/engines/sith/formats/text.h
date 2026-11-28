// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::sith {

/** A point or direction, in the game's units, z up. */
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

/**
 * The lines of one of the game's text files (.3do, .key, .jkl...) with
 * their comments (from #) and blank lines left out, each trimmed; Windows
 * line ends are taken as they come.
 */
class TextLines {
public:
    /** Splits `data` into lines; it must outlive this. */
    explicit TextLines(const std::vector<uint8_t> &data);

    /** Whether every line has been taken. */
    bool done() const { return _next >= _lines.size(); }
    /** The next line, without taking it; empty when done. */
    std::string_view peek() const { return done() ? std::string_view() : _lines[_next].text; }
    /** Takes the next line; empty when done. */
    std::string_view next() { return done() ? std::string_view() : _lines[_next++].text; }
    /** The number in the file of the line next() last gave, for messages. */
    int lineNumber() const { return _next == 0 ? 0 : _lines[_next - 1].number; }

private:
    struct Line {
        std::string_view text;
        int number;
    };
    std::vector<Line> _lines;
    size_t _next = 0;
};

/** The words of a line, split at spaces and tabs. */
std::vector<std::string_view> words(std::string_view line);

/** Whether `line` starts with `keyword`, ignoring case, then a space or its end. */
bool startsWith(std::string_view line, std::string_view keyword);

/** What follows `keyword` at the start of `line`, trimmed (startsWith first). */
std::string_view after(std::string_view line, std::string_view keyword);

/** A word as a number; false if it isn't wholly one. */
///@{
bool toInt(std::string_view word, int &out);
bool toFloat(std::string_view word, float &out);
/** In hexadecimal with 0x, or decimal. */
bool toFlags(std::string_view word, uint32_t &out);
///@}

/**
 * A line of the form "<n>: <fields...>": its number and its fields' words;
 * false if it doesn't start with a number and a colon. The colon may touch
 * the field after it ("0:file.mat").
 */
bool numbered(std::string_view line, int &number, std::vector<std::string_view> &fields);

} // namespace reluva::sith
