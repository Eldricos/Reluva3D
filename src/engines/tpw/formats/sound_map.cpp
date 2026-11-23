// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 17 categories', worked out from them and
// checked against the banks' sounds:
// docs/engines/tpw/games/simcoaster/formats/sound-maps.md.

#include "engines/tpw/formats/sound_map.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr size_t kBankCount = 0x18;
constexpr size_t kBankRecord = 11; // not read: what they hold isn't known
constexpr size_t kEffectCount = 0x1c;
constexpr size_t kTable = 0x34;
constexpr size_t kTableRecord = 20;
constexpr size_t kGroupHeader = 42;
constexpr size_t kSoundRecord = 16;
constexpr size_t kLinkRecord = 8;
// More than any category holds (the speech's 1,136), against a bad count.
constexpr uint32_t kMostEffects = 0x10000;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

uint16_t le16(const std::vector<uint8_t> &d, size_t at) {
    return uint16_t(d[at] | d[at + 1] << 8);
}

} // namespace

Error SoundMap::parse(const std::vector<uint8_t> &effects, const std::vector<uint8_t> &banks) {
    _banks.clear();
    _effects.clear();
    if (banks.size() < kBankCount + 4)
        return Error::fail("not a sound category's banks");
    const uint32_t bankCount = le32(banks, kBankCount);
    size_t at = kBankCount + 4 + kBankRecord * size_t(std::min<uint32_t>(bankCount, kMostEffects));
    for (uint32_t i = 0; i < bankCount; ++i) {
        if (at + 4 > banks.size())
            return Error::fail("the banks' names run past the end");
        const uint32_t length = le32(banks, at);
        if (length > banks.size() - at - 4)
            return Error::fail("the banks' names run past the end");
        const char *name = reinterpret_cast<const char *>(&banks[at + 4]);
        _banks.emplace_back(name, std::find(name, name + length, '\0'));
        at += 4 + length;
    }

    if (effects.size() < kTable)
        return Error::fail("not a sound category's effects");
    const uint32_t count = le32(effects, kEffectCount);
    if (count > kMostEffects || kTable + size_t(count) * kTableRecord > effects.size())
        return Error::fail("the effects' table runs past the end");
    at = kTable + size_t(count) * kTableRecord;
    _effects.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        Effect &effect = _effects[i];
        const size_t row = kTable + size_t(i) * kTableRecord;
        effect.id = le32(effects, row);
        const uint32_t groups = le32(effects, row + 4);
        if (groups > kMostEffects || at + size_t(groups) * kGroupHeader > effects.size())
            return Error::fail("effect " + std::to_string(effect.id) + " runs past the end");
        // The groups' headers, then each group's sounds and its links to
        // others (which chain an ambient's sounds; not read).
        std::vector<uint32_t> links(groups);
        effect.groups.resize(groups);
        for (uint32_t g = 0; g < groups; ++g, at += kGroupHeader) {
            Group &group = effect.groups[g];
            group.sounds.resize(le32(effects, at));
            links[g] = le32(effects, at + 4);
            group.volumeLow = effects[at + 12];
            group.volumeHigh = effects[at + 13];
            group.weight = le16(effects, at + 30);
        }
        for (uint32_t g = 0; g < groups; ++g) {
            std::vector<Sound> &sounds = effect.groups[g].sounds;
            if (sounds.size() > (effects.size() - at) / kSoundRecord)
                return Error::fail("effect " + std::to_string(effect.id) + " runs past the end");
            for (Sound &s : sounds) {
                // Both counted from 1 in the file.
                s.entry = le32(effects, at) - 1;
                s.upTo = le16(effects, at + 4);
                s.millis = le32(effects, at + 8);
                s.bank = le32(effects, at + 12) - 1;
                at += kSoundRecord;
            }
            if (links[g] > (effects.size() - at) / kLinkRecord)
                return Error::fail("effect " + std::to_string(effect.id) + " runs past the end");
            at += size_t(links[g]) * kLinkRecord;
        }
    }
    return Error::success();
}

const SoundMap::Effect *SoundMap::find(uint32_t id) const {
    for (const Effect &e : _effects)
        if (e.id == id)
            return &e;
    return nullptr;
}

const SoundMap::Sound *SoundMap::pick(const Effect &effect, uint16_t groupRoll, uint16_t soundRoll,
                                      const Group **group) {
    // The weights add up to 0xffff, or a little under; a roll past them
    // takes the last group.
    const Group *chosen = nullptr;
    uint32_t sum = 0;
    for (const Group &g : effect.groups) {
        if (g.sounds.empty())
            continue;
        chosen = &g;
        sum += g.weight;
        if (groupRoll < sum)
            break;
    }
    if (group)
        *group = chosen;
    return chosen ? pick(*chosen, soundRoll) : nullptr;
}

const SoundMap::Sound *SoundMap::pick(const Group &group, uint16_t roll) {
    for (const Sound &s : group.sounds)
        if (roll <= s.upTo)
            return &s;
    return group.sounds.empty() ? nullptr : &group.sounds.back();
}

} // namespace reluva::tpw
