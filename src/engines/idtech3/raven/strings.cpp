// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// String packages, as Jedi Outcast's 30 files lay them out (checked on
// every one); docs/engines/idtech3/formats/strings.md describes them.

#include "engines/idtech3/raven/strings.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace reluva::idtech3::raven {

namespace {

std::string upper(std::string s) {
    for (char &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A quoted value: from the first quote to the last on the line, as text
// may hold quotes ("I hate it when you say "troubling.""); "\n" a line
// break.
std::string quoted(std::string_view rest) {
    const size_t open = rest.find('"');
    const size_t close = rest.rfind('"');
    if (open == std::string_view::npos || close <= open)
        return std::string(rest);
    std::string out;
    const std::string_view inside = rest.substr(open + 1, close - open - 1);
    for (size_t i = 0; i < inside.size(); ++i) {
        if (inside[i] == '\\' && i + 1 < inside.size() && inside[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += inside[i];
        }
    }
    return out;
}

} // namespace

Error parseStringPackage(std::string_view text, StringPackage &out) {
    out = StringPackage{};
    std::istringstream lines{std::string(text)};
    std::string line;
    int number = 0;
    bool inEntry = false;
    int pendingIndex = -1;
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        const size_t end = line.find_first_of(" \t", start);
        const std::string key = line.substr(start, end - start);
        const std::string_view rest =
            end == std::string::npos ? std::string_view() : std::string_view(line).substr(end + 1);
        auto bad = [&](const std::string &why) {
            return Error::fail("string package line " + std::to_string(number) + ": " + why);
        };
        if (key == "{") {
            if (inEntry || pendingIndex < 0)
                return bad("an entry opens without its INDEX");
            inEntry = true;
            out.entries.push_back({});
            out.entries.back().index = pendingIndex;
            pendingIndex = -1;
        } else if (key == "}") {
            if (!inEntry)
                return bad("an entry closes that isn't open");
            inEntry = false;
        } else if (inEntry) {
            StringPackage::Entry &e = out.entries.back();
            if (key == "REFERENCE")
                e.reference = std::string(rest.substr(std::min(rest.find_first_not_of(" \t"), rest.size())));
            else if (key.rfind("TEXT_LANGUAGE", 0) == 0 && key.size() == 14 && key[13] >= '1' && key[13] <= '3')
                e.text[size_t(key[13] - '1')] = quoted(rest);
            else if (key == "NOTES")
                e.notes = quoted(rest);
            else if (key == "FLAGS")
                e.flags = std::string(rest.substr(std::min(rest.find_first_not_of(" \t"), rest.size())));
            else
                return bad("unknown key " + key);
        } else if (key == "INDEX") {
            pendingIndex = std::max(0, std::atoi(std::string(rest).c_str()));
        } else if (key == "REFERENCE") {
            out.reference = std::string(rest.substr(std::min(rest.find_first_not_of(" \t"), rest.size())));
        } else if (key == "ID") {
            out.id = std::atoi(std::string(rest).c_str());
        } else if (key == "DESCRIPTION") {
            out.description = quoted(rest);
        } else if (key != "VERSION" && key != "CONFIG" && key != "COUNT") {
            return bad("unknown key " + key);
        }
    }
    if (inEntry)
        return Error::fail("string package ends inside an entry");
    return Error::success();
}

void StringTable::load(const SearchSet &files, std::vector<std::string> *warnings) {
    _packages.clear();
    _byKey.clear();
    std::vector<uint8_t> data;
    for (const std::string &name : files.list()) {
        const std::string key = normalizeArchiveName(name);
        if (key.rfind("strip/", 0) != 0 || key.size() < 3 || key.compare(key.size() - 3, 3, ".sp") != 0)
            continue;
        StringPackage p;
        Error err = files.read(name, data);
        if (!err)
            err = parseStringPackage(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), p);
        if (err) {
            if (warnings)
                warnings->push_back(name + ": " + err.message);
            continue;
        }
        _packages.push_back(std::move(p));
    }
    for (size_t pi = 0; pi < _packages.size(); ++pi)
        for (size_t ei = 0; ei < _packages[pi].entries.size(); ++ei)
            _byKey.emplace(upper(_packages[pi].reference + "_" + _packages[pi].entries[ei].reference),
                           std::make_pair(pi, ei));
}

const std::string *StringTable::find(std::string_view key, Language language) const {
    const auto it = _byKey.find(upper(std::string(key)));
    if (it == _byKey.end())
        return nullptr;
    const StringPackage::Entry &e = _packages[it->second.first].entries[it->second.second];
    const std::string &text = e.text[size_t(language)];
    return text.empty() ? &e.text[0] : &text;
}

} // namespace reluva::idtech3::raven
