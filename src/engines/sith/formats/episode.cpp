// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/episode.h"

#include "common/fs.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace reluva::sith {

namespace {

// The line up to its comment, trimmed.
std::string_view content(std::string_view line) {
    line = line.substr(0, line.find('#'));
    while (!line.empty() && std::isspace(uint8_t(line.front())))
        line.remove_prefix(1);
    while (!line.empty() && std::isspace(uint8_t(line.back())))
        line.remove_suffix(1);
    return line;
}

} // namespace

const Episode::Step *Episode::find(int line) const {
    for (const Step &s : steps)
        if (s.line == line)
            return &s;
    return nullptr;
}

Error parseEpisode(const std::vector<uint8_t> &data, Episode &out) {
    out = Episode{};
    const std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
    bool titled = false;
    int lineNumber = 0;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view raw = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNumber;

        // The title is the first line, in quotes, and may hold a #.
        if (!titled) {
            const size_t open = raw.find('"');
            const size_t close = open == std::string_view::npos ? open : raw.find('"', open + 1);
            if (close == std::string_view::npos)
                return Error::fail("episode without its title in quotes on its first line");
            out.title.assign(raw.substr(open + 1, close - open - 1));
            titled = true;
            continue;
        }
        const std::string_view line = content(raw);
        if (line.empty())
            continue;
        std::istringstream in{std::string(line)};
        std::string word;
        in >> word;
        const std::string lower = fs::toLower(word);
        if (lower == "end")
            break;
        if (lower == "type") {
            in >> out.type;
            continue;
        }
        if (lower == "seq") {
            in >> out.declaredSteps;
            continue;
        }
        Episode::Step step;
        if (word.empty() || word.back() != ':')
            return Error::fail("episode line " + std::to_string(lineNumber) + " isn't a step");
        step.line = std::atoi(word.c_str());
        if (!(in >> step.cd >> step.level >> step.kind >> step.file >> step.lightPower >> step.darkPower >>
              step.gotoA >> step.gotoB))
            return Error::fail("episode line " + std::to_string(lineNumber) + " has too few fields");
        out.steps.push_back(std::move(step));
    }
    if (!titled)
        return Error::fail("episode file is empty");
    return Error::success();
}

} // namespace reluva::sith
