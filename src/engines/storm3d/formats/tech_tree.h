// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * What a player must have before building each unit (a map's tech tree,
 * `*.tt`): a line a unit, its definition, how many it needs, and those,
 * as
 *
 *     fyard2.odf   2 fyard.odf fresear.odf
 *
 * See docs/engines/storm3d/games/armada/formats/tech-tree.md.
 */
class TechTree {
public:
    /** Reads the file's text; lines not of that form are passed over. */
    void parse(std::string_view file);
    /** Whether it says anything (a map without one, or one not read, doesn't). */
    bool empty() const { return _needs.empty(); }
    /** What a unit needs, by its definition's name without ".odf", lower case; null if it's not listed. */
    const std::vector<std::string> *needsOf(const std::string &unit) const;
    /**
     * Whether a unit can be built with `owned` (definitions' names, lower
     * case): all it needs among them. One the tree doesn't list can.
     */
    bool allows(const std::string &unit, const std::set<std::string> &owned) const;

private:
    std::map<std::string, std::vector<std::string>> _needs;
};

} // namespace reluva::storm3d
