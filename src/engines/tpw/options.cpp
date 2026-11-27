// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/options.h"

#include "engines/tpw/formats/sam.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace reluva::tpw {

namespace {

constexpr const char *kOptionsFile = "options.txt";
constexpr std::array<const char *, 3> kVolumeKeys{"effects", "speech", "music"};
constexpr std::array<const char *, 3> kSoundDefaults{"DefaultVolume.SFX", "DefaultVolume.SPEECH",
                                                     "DefaultVolume.MUSIC"};

int clampPercent(int v, int least) {
    return std::clamp(v, least, 100);
}

} // namespace

GameOptions defaultOptions(const SearchSet &files) {
    GameOptions o;
    std::vector<uint8_t> data;
    SamFile config;
    if (!files.read("Config.sam", data) && !config.parse(data)) {
        const int which = int(config.number("Defaults.DefaultConfig", 1));
        o.gameSpeed = clampPercent(
            int(config.number("Config[" + std::to_string(which) + "].DefaultGameSpeed", GameOptions::kNormalSpeed)),
            10);
    }
    SamFile sound;
    if (!files.read("sound.sam", data) && !sound.parse(data))
        for (size_t i = 0; i < o.volume.size(); ++i)
            o.volume[i] = clampPercent(int(sound.number(kSoundDefaults[i], 100)), 0);
    return o;
}

void applyOptions(const GameOptions &options, audio::Mixer *mixer) {
    if (!mixer)
        return;
    constexpr std::array<audio::SoundType, 3> kTypes{audio::SoundType::kEffects, audio::SoundType::kSpeech,
                                                     audio::SoundType::kMusic};
    for (size_t i = 0; i < kTypes.size(); ++i)
        mixer->setTypeVolume(kTypes[i], options.volume[i] * audio::kMaxVolume / 100);
}

GameOptions readOptions(const std::filesystem::path &saveFolder, const GameOptions &defaults) {
    GameOptions o = defaults;
    std::ifstream in(saveFolder / kOptionsFile);
    for (std::string line; in && std::getline(in, line);) {
        std::istringstream words(line);
        std::string key;
        int value = 0;
        if (!(words >> key >> value))
            continue;
        if (key == "speed")
            o.gameSpeed = clampPercent(value, 10);
        else if (key == "advisor")
            o.advisor = value != 0;
        for (size_t i = 0; i < kVolumeKeys.size(); ++i)
            if (key == kVolumeKeys[i])
                o.volume[i] = clampPercent(value, 0);
    }
    return o;
}

Error writeOptions(const std::filesystem::path &saveFolder, const GameOptions &options) {
    if (saveFolder.empty())
        return Error::success();
    std::error_code ec;
    std::filesystem::create_directories(saveFolder, ec);
    std::ofstream out(saveFolder / kOptionsFile, std::ios::trunc);
    out << "speed " << options.gameSpeed << "\n";
    for (size_t i = 0; i < kVolumeKeys.size(); ++i)
        out << kVolumeKeys[i] << " " << options.volume[i] << "\n";
    out << "advisor " << (options.advisor ? 1 : 0) << "\n";
    if (!out)
        return Error::fail("can't write " + (saveFolder / kOptionsFile).string());
    return Error::success();
}

} // namespace reluva::tpw
