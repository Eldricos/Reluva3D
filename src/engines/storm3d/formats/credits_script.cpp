// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/credits_script.h"

#include "common/text.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::storm3d {

namespace {

// "(0.0,0.3,0.6)": three numbers; false if it isn't.
bool triple(const std::string &value, std::array<float, 3> &out) {
    std::array<float, 3> v{};
    if (std::sscanf(value.c_str(), " ( %f , %f , %f )", &v[0], &v[1], &v[2]) != 3)
        return false;
    out = v;
    return true;
}

} // namespace

CreditsScript CreditsScript::parse(std::string_view text) {
    CreditsScript script;
    Page page;
    auto endPage = [&]() {
        // Blank lines at a page's end make no room.
        while (!page.lines.empty() && page.lines.back().empty())
            page.lines.pop_back();
        if (!page.lines.empty())
            script.pages.push_back(page);
        page.lines.clear();
    };
    for (std::string line : text::split(text, '\n')) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] != '@') {
            page.lines.push_back(line);
            continue;
        }
        const size_t eq = line.find('=');
        const std::string name = text::toLower(line.substr(1, eq == std::string::npos ? std::string::npos : eq - 1));
        const std::string value = eq == std::string::npos ? "" : line.substr(eq + 1);
        if (name == "cls")
            endPage();
        else if (name == "colour")
            triple(value, page.colour);
        else if (name == "scale")
            triple(value, page.scale);
        else if (name == "hold")
            page.hold = float(std::atof(value.c_str()));
    }
    endPage();
    return script;
}

} // namespace reluva::storm3d
