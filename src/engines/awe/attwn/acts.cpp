// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/acts.h"

#include "common/fs.h"
#include "engines/awe/formats/tables.h"
#include "engines/awe/game/story.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

// Surrounding quotes off (0x402d63); the cells have no doubled ones.
std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

} // namespace

Error readChapters(const Archive &files, std::vector<ActRow> &out) {
    out.clear();
    // The game opens the act's chapter's file each time (0x402ca4); every
    // chapter's rows read once comes to the same.
    for (int chapter = 1; chapter < 100; chapter++) {
        char name[32];
        std::snprintf(name, sizeof name, "text/chapter%02d.txt", chapter);
        std::vector<uint8_t> data;
        if (files.read(name, data))
            break;
        const Table t = parseTable(decodeText(data));
        for (size_t i = 0; i < t.rows.size(); i++) {
            auto cell = [&](int column) { return unquote(t.cell(i, column)); };
            int major;
            int minor;
            if (!parseActName(cell(0), major, minor))
                continue;
            ActRow r;
            r.act = actName(major, minor);
            r.preLoad = cell(1);
            r.time = std::atoi(cell(2).c_str());
            r.weather = std::atoi(cell(3).c_str());
            for (const Command &c : parseCommands(cell(4)))
                for (const std::string &w : c.words)
                    r.characters.push_back(fs::toLower(w));
            r.completion = cell(5);
            r.event = cell(6);
            r.music = cell(7);
            out.push_back(std::move(r));
        }
    }
    return out.empty() ? Error::fail("No chapter tables (Text/Chapter01.txt)") : Error::success();
}

std::string placeOf(const std::string &scene) {
    std::string place = fs::toLower(scene.substr(0, scene.find('_')));
    if (place.size() >= 2)
        place.resize(place.size() - 2);
    return place;
}

} // namespace reluva::awe::attwn
