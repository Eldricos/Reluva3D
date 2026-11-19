// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>

#include "common/text.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// A Windows-style INI file, looked up as GetPrivateProfileString does:
// section and key names ignore case, and a section that appears twice
// (UNIVERSE.INI repeats most of them at the end, with the scan texts)
// holds the keys of both.
class IniFile {
public:
    void load(const std::string &text);

    bool hasSection(const std::string &section) const;
    bool has(const std::string &section, const std::string &key) const;
    std::string get(const std::string &section, const std::string &key,
                    const std::string &fallback = std::string()) const;
    int getInt(const std::string &section, const std::string &key, int fallback = 0) const;
    double getDouble(const std::string &section, const std::string &key, double fallback = 0) const;
    // TRUE/FALSE (or a number); fallback when missing.
    bool getBool(const std::string &section, const std::string &key, bool fallback = false) const;
    void set(const std::string &section, const std::string &key, const std::string &value);

    // Changes made with set(), which saved games keep.
    void syncChanges(Serializer &s);

private:
    typedef std::map<std::string, std::string> Section;
    std::map<std::string, Section> _sections;
    std::map<std::pair<std::string, std::string>, std::string> _changes;
};

} // namespace reluva::stgenerations
