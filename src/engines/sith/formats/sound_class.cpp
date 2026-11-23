// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/sound_class.h"

#include "common/fs.h"

#include <cstdlib>
#include <sstream>

namespace reluva::sith {

const SoundClass::Entry *SoundClass::find(std::string_view event) const {
    const std::string want = fs::toLower(event);
    for (const Entry &e : entries)
        if (fs::toLower(e.event) == want)
            return &e;
    return nullptr;
}

Error parseSoundClass(const std::vector<uint8_t> &data, SoundClass &out) {
    out = SoundClass{};
    const std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
    int lineNumber = 0;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNumber;

        std::istringstream in{std::string(line.substr(0, line.find('#')))};
        SoundClass::Entry e;
        if (!(in >> e.event))
            continue;
        if (!(in >> e.wav))
            return Error::fail("sound class line " + std::to_string(lineNumber) + " names no sound");
        auto bad = [&] { return Error::fail("sound class line " + std::to_string(lineNumber) + " has a bad number"); };
        std::string word;
        if (in >> word) {
            char *after = nullptr;
            e.flags = uint32_t(std::strtoul(word.c_str(), &after, 16)); // always hexadecimal (0x4e68ac)
            if (*after)
                return bad();
        }
        for (std::optional<float> *field : {&e.minRadius, &e.maxRadius, &e.maxVolume}) {
            if (!(in >> word))
                break;
            char *after = nullptr;
            *field = std::strtof(word.c_str(), &after);
            if (*after)
                return bad();
        }
        out.entries.push_back(std::move(e));
    }
    return Error::success();
}

} // namespace reluva::sith
