// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/sounds.h"

#include "common/audio/decode.h"

namespace reluva::idtech3 {

std::shared_ptr<const audio::Sound> SoundLibrary::get(std::string_view name) {
    const std::string key = normalizeArchiveName(name);
    if (key.empty())
        return nullptr;
    if (const auto it = sounds_.find(key); it != sounds_.end())
        return it->second;
    std::shared_ptr<const audio::Sound> result;
    std::vector<uint8_t> data;
    if (Error err = files_.read(key, data)) {
        problems_.push_back(std::string(name) + ": " + err.message);
    } else {
        auto sound = std::make_shared<audio::Sound>();
        if (Error derr = audio::decodeSound(data, *sound))
            problems_.push_back(std::string(name) + ": " + derr.message);
        else if (sound->frames() == 0)
            problems_.push_back(std::string(name) + ": no samples");
        else
            result = std::move(sound);
    }
    sounds_.emplace(key, result);
    return result;
}

std::shared_ptr<const audio::Sound> SoundLibrary::find(std::string_view name) {
    if (name.empty())
        return nullptr;
    if (auto sound = get(name))
        return sound;
    const std::string full(name);
    const size_t slash = full.rfind('/');
    const size_t dot = full.find('.', slash == std::string::npos ? 0 : slash);
    const std::string stem = dot == std::string::npos ? full : full.substr(0, dot);
    for (const char* ext : {".wav", ".mp3"})
        if (stem + ext != full)
            if (auto sound = get(stem + ext))
                return sound;
    return nullptr;
}

} // namespace reluva::idtech3
