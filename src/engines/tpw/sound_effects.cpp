// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/sound_effects.h"

#include <algorithm>

namespace reluva::tpw {

std::string soundBankFile(const std::string &categoryFolder, const std::string &bank) {
    const size_t slash = bank.find_last_of("\\/");
    const std::string parent = categoryFolder.substr(0, categoryFolder.find_last_of('/') + 1);
    std::string under = bank.substr(0, slash == std::string::npos ? 0 : slash + 1);
    std::replace(under.begin(), under.end(), '\\', '/');
    return parent + under + bank.substr(slash == std::string::npos ? 0 : slash + 1) + "HD.sdt";
}

const std::vector<std::string> &SoundEffects::globalCategories() {
    static const std::vector<std::string> kNames{"rides", "ui", "kids", "staff", "ambient"};
    return kNames;
}

Error SoundEffects::load(const SearchSet &files, const std::string &folder,
                         const std::vector<std::string> &categories) {
    _files = &files;
    for (const std::string &name : categories) {
        std::vector<uint8_t> effects;
        std::vector<uint8_t> banks;
        const std::string base = folder + "/cat_" + name;
        if (Error err = files.read(base + "SFX.map", effects))
            return err;
        if (Error err = files.read(base + "BANK.map", banks))
            return err;
        Category c;
        c.folder = folder;
        if (Error err = c.map.parse(effects, banks))
            return Error::fail(base + ": " + err.message);
        _categories.push_back(std::move(c));
    }
    return Error::success();
}

uint16_t SoundEffects::roll() {
    _seed = _seed * 1103515245u + 12345u;
    return uint16_t(_seed >> 16);
}

bool SoundEffects::choose(uint32_t id, Choice &out) {
    for (const Category &c : _categories) {
        const SoundMap::Effect *effect = c.map.find(id);
        if (!effect)
            continue;
        const SoundMap::Group *group = nullptr;
        const uint16_t groupRoll = roll();
        const SoundMap::Sound *sound = SoundMap::pick(*effect, groupRoll, roll(), &group);
        if (!sound || sound->bank >= c.map.banks().size())
            return false;
        out.bank = soundBankFile(c.folder, c.map.banks()[sound->bank]);
        out.entry = sound->entry;
        const int span = std::max(0, group->volumeHigh - group->volumeLow);
        out.volume = group->volumeLow + (span ? roll() % (span + 1) : 0);
        return true;
    }
    return false;
}

audio::SoundHandle SoundEffects::play(audio::Mixer &mixer, uint32_t id, audio::SoundType type) {
    Choice choice;
    if (!_files || !choose(id, choice))
        return audio::kNoSound;
    auto &sound = _sounds[{choice.bank, choice.entry}];
    if (!sound) {
        auto &bank = _banks[choice.bank];
        if (!bank) {
            std::vector<uint8_t> data;
            bank = std::make_unique<SoundBank>();
            if (_files->read(choice.bank, data) || bank->parse(std::move(data)))
                return audio::kNoSound;
        }
        auto decoded = std::make_shared<audio::Sound>();
        if (bank->decode(choice.entry, *decoded))
            return audio::kNoSound;
        sound = std::move(decoded);
    }
    return mixer.play(sound, type, choice.volume * audio::kMaxVolume / 100);
}

} // namespace reluva::tpw
