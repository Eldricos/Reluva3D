// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' weapons: the weapon descriptions (weapons.c) and choosing the
// one worth most by each bot's weapon weights. Behaviour read from the
// GPL ioquake3 bot library (be_ai_weap.c); new code.

#include "engines/idtech3/botlib/bot_library.h"

namespace reluva::idtech3::botlib {

int BotLibrary::allocWeaponState() {
    return alloc(_weaponStates);
}

void BotLibrary::freeWeaponState(int handle) {
    if (state(_weaponStates, handle))
        _weaponStates[size_t(handle) - 1].reset();
}

int BotLibrary::loadWeaponWeights(int handle, const std::string &file) {
    WeaponState *ws = state(_weaponStates, handle);
    if (!ws)
        return blerr::kCannotLoadWeaponWeights;
    loadConfigs();
    ws->weights = weights(file);
    if (!ws->weights)
        return blerr::kCannotLoadWeaponWeights;
    if (_weapons.empty())
        return blerr::kCannotLoadWeaponConfig;
    ws->weightIndex.clear();
    for (const WeaponInfo &w : _weapons)
        ws->weightIndex.push_back(w.valid ? ws->weights->find(w.name) : -1);
    return blerr::kNone;
}

int BotLibrary::chooseBestFightWeapon(int handle, const std::vector<int> &inventory) const {
    const WeaponState *ws = state(_weaponStates, handle);
    if (!ws || !ws->weights)
        return 0;
    int best = 0;
    float bestWeight = 0;
    for (size_t i = 0; i < _weapons.size() && i < ws->weightIndex.size(); ++i) {
        if (!_weapons[i].valid || ws->weightIndex[i] < 0)
            continue;
        const float w = ws->weights->evaluate(ws->weightIndex[i], inventory);
        if (w > bestWeight) {
            bestWeight = w;
            best = int(i);
        }
    }
    return best;
}

const WeaponInfo *BotLibrary::weaponInfo(int weapon) const {
    if (weapon <= 0 || size_t(weapon) >= _weapons.size())
        return nullptr;
    return &_weapons[size_t(weapon)];
}

} // namespace reluva::idtech3::botlib
