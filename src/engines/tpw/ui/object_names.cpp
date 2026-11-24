// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/object_names.h"

#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cctype>

namespace reluva::tpw {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string joined(const std::array<std::string, 2> &pair) {
    return pair[1].empty() ? pair[0] : pair[0].empty() ? pair[1] : pair[0] + " " + pair[1];
}

} // namespace

Error ObjectNames::load(const SearchSet &files, const std::string &folder, const CharacterSet &characters) {
    if (Error err = readStringTable(files, folder, "OBJECT_NAMES", characters, _names))
        return err;
    return readStringTable(files, folder, "OLD_OBJECT_NAMES", characters, _oldNames);
}

// The pairs of OLD_OBJECT_NAMES, at the same places as OBJECT_NAMES',
// hold older names, mostly those the objects' settings have. An object's
// is the pair whose old name is its own, whole or by its first word.
// Not all are right: Land of Invention's TV sim has a Polar Zone's old
// name, and its log flume its own.
std::array<std::string, 2> ObjectNames::lines(const Attractions::Attraction &a) const {
    const std::string own = lower(a.name);
    auto pair = [](const StringTable &t, size_t k) {
        return std::array<std::string, 2>{t.text(2 * k), t.text(2 * k + 1)};
    };
    const size_t pairs = _oldNames.size() / 2;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < pairs && 2 * k + 1 < _names.size(); ++k) {
            const std::string old = lower(pass == 0 ? joined(pair(_oldNames, k)) : _oldNames.text(2 * k));
            if (!old.empty() && old == own) {
                const std::array<std::string, 2> shown = pair(_names, k);
                if (!joined(shown).empty())
                    return shown;
            }
        }
    const size_t space = a.name.find(' ');
    if (space == std::string::npos)
        return {a.name, {}};
    return {a.name.substr(0, space), a.name.substr(space + 1)};
}

std::string ObjectNames::shown(const Attractions::Attraction &a) const {
    return joined(lines(a));
}

} // namespace reluva::tpw
