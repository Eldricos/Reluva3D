// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The in-game interface's layout (`misc/gui_fed.cfg` and the other
 * races', which include `gui_glob.cfg`): lines of `name = values`, the
 * values numbers or quoted names, `//` comments, `#include "file"`.
 * See docs/engines/storm3d/games/armada/formats/gui-config.md.
 */
class GuiConfig {
public:
    /** Reads a file by its name (in the same folder as the one including it). */
    using Loader = std::function<Error(std::string_view name, std::string &text)>;
    /**
     * Reads `name` and what it includes; a name set again keeps its last value.
     * @return a failure if a file can't be read
     */
    Error load(std::string_view name, const Loader &loader);
    /** Reads text, without includes. */
    void parse(std::string_view text);

    /** A name's values as written, quotes taken off; none if it isn't set. */
    const std::vector<std::string> *find(std::string_view name) const;
    /** A name's `i`th value as a number; `fallback` if there's none. */
    double number(std::string_view name, size_t i = 0, double fallback = 0) const;
    /** A name's first value as text; empty if there's none. */
    std::string text(std::string_view name) const;
    /** Four numbers: an area's x, y, width and height; zeros if it isn't set. */
    std::array<int, 4> area(std::string_view name) const;

private:
    Error load(std::string_view name, const Loader &loader, int depth);
    std::map<std::string, std::vector<std::string>, std::less<>> _values;
};

} // namespace reluva::storm3d
