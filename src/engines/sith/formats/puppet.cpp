// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/puppet.h"

#include "common/fs.h"
#include "engines/sith/formats/text.h"

#include <string>

namespace reluva::sith {

namespace {

// The number after "<key>=" in a word like "Mode=1," or "basedon=0".
bool value(std::string_view word, std::string_view key, int &out) {
    if (word.size() <= key.size() + 1 || fs::toLower(word.substr(0, key.size())) != key || word[key.size()] != '=')
        return false;
    word.remove_prefix(key.size() + 1);
    if (!word.empty() && word.back() == ',')
        word.remove_suffix(1);
    return toInt(word, out);
}

} // namespace

const Puppet::Action *Puppet::find(int mode, std::string_view action) const {
    const std::string want = fs::toLower(action);
    // A mode's base, and its base's: no retail puppet goes more than two deep.
    for (int depth = 0; depth < 8 && mode >= 0; ++depth) {
        const Mode *m = nullptr;
        for (const Mode &candidate : modes)
            if (candidate.number == mode)
                m = &candidate;
        if (!m)
            return nullptr;
        for (const Action &a : m->actions)
            if (fs::toLower(a.name) == want)
                return &a;
        mode = m->basedOn;
    }
    return nullptr;
}

Error parsePuppet(const std::vector<uint8_t> &data, Puppet &out) {
    out = Puppet{};
    TextLines lines(data);
    auto bad = [&](const std::string &what) {
        return Error::fail("puppet line " + std::to_string(lines.lineNumber()) + ": can't read " + what);
    };
    bool inJoints = false;
    while (!lines.done()) {
        const std::string_view line = lines.next();
        const std::vector<std::string_view> w = words(line);
        int number = 0;
        if (value(w[0], "mode", number)) {
            Puppet::Mode &mode = out.modes.emplace_back();
            mode.number = number;
            if (w.size() > 1 && !value(w[1], "basedon", mode.basedOn))
                return bad("a mode's base");
            inJoints = false;
        } else if (startsWith(line, "joints")) {
            inJoints = true;
        } else if (startsWith(line, "end")) {
            break;
        } else if (inJoints) {
            const size_t eq = w[0].find('=');
            int joint = 0;
            int node = 0;
            if (eq == std::string_view::npos || !toInt(w[0].substr(0, eq), joint) || !toInt(w[0].substr(eq + 1), node))
                return bad("a joint");
            out.joints[joint] = node;
        } else {
            Puppet::Action a;
            if (out.modes.empty() || w.size() < 5 || !toFlags(w[2], a.flags) || !toInt(w[3], a.lowPriority) ||
                !toInt(w[4], a.highPriority))
                return bad("an action");
            a.name = w[0];
            a.keyframe = w[1];
            out.modes.back().actions.push_back(std::move(a));
        }
    }
    return Error::success();
}

} // namespace reluva::sith
