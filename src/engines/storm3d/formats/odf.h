// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * An object definition file (`.odf`): a unit's, a weapon's or one of the
 * game's settings, as `key = value` lines, with the files it `#include`s
 * read in their place so that its own lines override theirs. See
 * docs/engines/storm3d/games/armada/formats/odf.md.
 */
class Odf {
public:
    /** One `key = value`, with the lines of quoted strings after it that continue its value. */
    struct Entry {
        std::string file;    // the file it's in, as named (an included file's name for its lines)
        std::string section; // the last `[...]` before it in its file, without the brackets; "" if none
        std::string key;     // as written
        std::string value;   // after the `=`, trimmed; continuation lines joined with '\n'
    };

    /** Gives a file's text by name, or fails if there's none. */
    using Loader = std::function<Error(std::string_view name, std::string &text)>;

    /**
     * Reads a file and, through the loader, the files it includes, in the
     * order the lines come.
     * @return a failure if a file can't be loaded or includes nest past 16
     *         levels (which a loop of includes would)
     */
    Error load(std::string_view name, const Loader &loader);

    /** Every entry, included files' first, in the order read. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** Lines that are none of the forms above, with their file and line number. */
    const std::vector<std::string> &problems() const { return _problems; }

    /** The last entry with that key, matched ignoring case, in any section; nullptr if none. */
    const Entry *find(std::string_view key) const;
    /** The value's first string (the text between its first two quotes), or the whole value if it has none. */
    std::string string(std::string_view key, std::string_view fallback = {}) const;
    /** Every quoted string in the value, in order. */
    std::vector<std::string> strings(std::string_view key) const;
    /** The value read as a number the way C's atof() reads one: `0.75f` and `10;` are numbers. */
    double number(std::string_view key, double fallback = 0) const;

    /** A value's quoted strings; an unclosed quote runs to the value's end. */
    static std::vector<std::string> splitStrings(std::string_view value);
    /**
     * The number at the start of a value, as atof() reads it.
     * @param ok set to whether the value starts with one
     */
    static double parseNumber(std::string_view value, bool &ok);

private:
    Error parse(std::string_view name, const Loader &loader, int depth);

    std::vector<Entry> _entries;
    std::vector<std::string> _problems;
};

} // namespace reluva::storm3d
