// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * One of the game's settings files written in a part of C: `RTS_CFG.h`,
 * `CCA.cfg`, the computer player's `.aip` personalities and others.
 * Variables are declared with their values (`float OVERVIEW_MAX_ZOOM =
 * 4.0;`), arrays of structures with brace lists, and `#define`s name
 * values too. See docs/engines/storm3d/games/armada/formats/config.md.
 */
class ConfigFile {
public:
    /** A value: a number, a string, or a list (an array's or a structure's). */
    struct Value {
        enum class Kind { kNumber, kString, kList };
        Kind kind = Kind::kNumber;
        double number = 0;
        std::string text;         // a string's, without its quotes; a line break in it is '\n'
        std::vector<Value> items; // a list's
    };

    /** A structure's definition: its fields' names, in order. */
    struct Struct {
        std::vector<std::string> fields;
    };

    /** Gives a file's text by name, or fails if there's none. */
    using Loader = std::function<Error(std::string_view name, std::string &text)>;

    /**
     * Reads a file and, through the loader, the files it includes.
     * @return a failure if a file can't be loaded, includes nest past 16
     *         levels, or a declaration can't be read (the message gives
     *         its file and line)
     */
    Error load(std::string_view name, const Loader &loader);

    /** The variables, by name as written; a later declaration of a name replaces an earlier one. */
    const std::map<std::string, Value> &variables() const { return _variables; }
    /** The `#define`s, by name, each its text after the name. */
    const std::map<std::string, std::string> &defines() const { return _defines; }
    /** The structures defined, by name. */
    const std::map<std::string, Struct> &structs() const { return _structs; }
    /**
     * What was skipped, with its file and line: `#DATA` blocks, unknown
     * directives, and variables whose value uses a name not set yet.
     */
    const std::vector<std::string> &problems() const { return _problems; }

    /**
     * A setting by name, matched exactly: a variable, or else a `#define`
     * read as a number, a string or an expression of other settings.
     * @return nullptr if there's none or a define can't be read as a value
     */
    const Value *find(std::string_view name) const;
    /** find()'s number, or `fallback` if it isn't one. */
    double number(std::string_view name, double fallback = 0) const;
    /** find()'s string, or `fallback` if it isn't one. */
    std::string string(std::string_view name, std::string_view fallback = {}) const;

private:
    struct Token;
    class Parser;

    Error preprocess(std::string_view name, const Loader &loader, int depth, std::vector<Token> &tokens);

    std::map<std::string, Value> _variables;
    std::map<std::string, std::string> _defines;
    std::map<std::string, Struct> _structs;
    std::vector<std::string> _problems;
    mutable std::map<std::string, Value> _defineValues; // defines read as values, on first find()
    mutable std::set<std::string> _evaluating;          // defines being read, against loops
};

} // namespace reluva::storm3d
