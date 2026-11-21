// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::stawayteam {

struct TextObject;

/**
 * A `Key=Value` line of a TextObject. When the value names a class and a
 * block follows it, `object` holds that block and `value` its class name.
 */
struct TextField {
    std::string key;
    std::string value; // as written: trailing spaces are part of it
    std::unique_ptr<TextObject> object;
};

/**
 * An object as the game writes it in text: its class name, then its
 * fields in braces, one `Key=Value` a line, each a value or another object.
 * Rules.txt, the maps (.zax), the missions (.MDS) and most definitions are
 * one each (docs/engines/stawayteam/formats/text-object.md).
 *
 * An array is an object of class "Array": its items are fields under one
 * key, after an optional count (`Item Count` or `Array Count`). Fields
 * keep their order, and a key may come more than once.
 */
struct TextObject {
    std::string className;
    std::vector<TextField> fields;

    /** The first field of a key, or nullptr. */
    const TextField *field(std::string_view key) const;
    /** The first field of a key's value, or nullptr if there's none. */
    const std::string *value(std::string_view key) const;
    /** The object the first field of a key holds, or nullptr. */
    const TextObject *object(std::string_view key) const;
    /** A field's value as a whole number, if it is one. */
    std::optional<long> integer(std::string_view key) const;
    /** A field's value as a number, if it is one ("0.25", "-1"). */
    std::optional<double> number(std::string_view key) const;
    /** An array's items: its fields without the count, if it has one. */
    std::vector<const TextField *> items() const;
};

/**
 * Reads an object from a file's text. Lines may end in CRLF or LF; tabs
 * and spaces before a line are skipped, as the game does, but not after.
 * @return An error naming the line, if the text isn't an object.
 */
Error readTextObject(std::string_view text, TextObject &out);

/**
 * The object as the game writes it: a tab for each level, CRLF after each
 * line. The game's own files come back as they were, but for 17 that
 * have no line end after their last brace.
 */
std::string writeTextObject(const TextObject &object);

} // namespace reluva::stawayteam
