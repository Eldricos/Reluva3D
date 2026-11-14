// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3::raven {

// Raven's string packages (Jedi Outcast's strip/*.sp, made with their
// StripEd tool; see docs/engines/idtech3/formats/strings.md): the game's
// text, each line by a reference name, in each language.
struct StringPackage {
    std::string reference; // MENUS0, INGAME...
    int id = 0;
    std::string description;
    struct Entry {
        int index = 0;
        std::string reference;
        std::array<std::string, 3> text; // English, French, German; "\n" made a line break
        std::string notes;
        std::string flags;
    };
    std::vector<Entry> entries;
};

// One package's text. Fails on a line it doesn't know or an entry left
// open.
Error parseStringPackage(std::string_view text, StringPackage &out);

// Every package in the game's files (strip/*.sp), looked up as the game's
// code asks: by "PACKAGE_ENTRY" (MENUS0_ABOUT), whatever its case.
class StringTable {
public:
    enum class Language { kEnglish, kFrench, kGerman };

    // Packages that can't be read are left out, each with why in `warnings`.
    void load(const SearchSet &files, std::vector<std::string> *warnings = nullptr);
    // nullptr if there's no such entry. An entry without text in
    // `language` gives its English.
    const std::string *find(std::string_view key, Language language = Language::kEnglish) const;
    const std::vector<StringPackage> &packages() const { return _packages; }
    size_t size() const { return _byKey.size(); }

private:
    std::vector<StringPackage> _packages;
    std::unordered_map<std::string, std::pair<size_t, size_t>> _byKey; // upper case: package, entry
};

} // namespace reluva::idtech3::raven
