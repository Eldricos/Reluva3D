// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A ride's speed, capacity and duration, as its window's sliders set
// them: docs/engines/tpw/games/simcoaster/systems/building.md#its-window-and-menu.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::tpw {

namespace {

// Of a ride's riders, the percent who buy their photo (guessed).
constexpr uint32_t kPhotoBuyers = 33;
// What a track add-on adds to its ride's excitement (guessed).
constexpr int kAddOnExcitement = 10;

} // namespace

std::optional<Park::SettingRange> Park::rideSetting(size_t object, RideSetting setting) const {
    if (object >= _objects.size())
        return std::nullopt;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    const Attractions::Attraction::Level *level = levelOf(object);
    if (!a || !level || a->kind != Attractions::Kind::kRide || level->capacity < 0)
        return std::nullopt;
    SettingRange r;
    int own = 0;
    switch (setting) {
    case RideSetting::kSpeed:
        r = {0, a->minSpeed, a->maxSpeed, level->redLineSpeed};
        own = level->speed;
        break;
    case RideSetting::kCapacity:
        r = {0, a->minCapacity, std::max(a->minCapacity, a->maxCapacity), level->redLineCapacity};
        own = level->capacity;
        break;
    case RideSetting::kDuration:
        r = {0, a->minDuration, std::max(a->minDuration, a->maxDuration), level->redLineDuration};
        own = level->duration;
        break;
    }
    const int set = _objects[object].settings[size_t(setting)];
    r.value = std::clamp(set >= 0 ? set : own, r.least, r.most);
    return r;
}

void Park::setRideSetting(size_t object, RideSetting setting, int value) {
    if (const std::optional<SettingRange> r = rideSetting(object, setting))
        _objects[object].settings[size_t(setting)] = std::clamp(value, r->least, r->most);
}

// A red line of 0 is none: the settings' upgrades have them only for
// what they raise.
bool Park::pastRedLine(size_t object) const {
    for (size_t s = 0; s < kRideSettings; ++s)
        if (const std::optional<SettingRange> r = rideSetting(object, RideSetting(s)); r && r->redLine > 0)
            if (r->value > r->redLine)
                return true;
    return false;
}

int Park::excitementOf(size_t object) const {
    if (object >= _objects.size())
        return -1;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    if (!a || a->excitement < 0)
        return -1;
    // Its track's add-ons each add to it (guessed).
    const int added = kAddOnExcitement * int(_objects[object].addOns.size());
    const std::optional<SettingRange> speed = rideSetting(object, RideSetting::kSpeed);
    if (!speed || a->levels[0].speed <= 0)
        return std::min(100, a->excitement + added);
    return std::clamp(int(std::lround(float(a->excitement) * float(speed->value) / float(a->levels[0].speed))) + added,
                      0, 100);
}

int Park::photoRideNear(const Footprint &f) const {
    int best = -1;
    int nearest = kPhotoReach + 1;
    for (size_t i = 0; i < _objects.size(); ++i) {
        const Attractions::Attraction *a = _attractions.find(_objects[i].id);
        if (!a || a->kind != Attractions::Kind::kRide)
            continue;
        for (const Cell &r : footprint(a->shape, _objects[i].at, _objects[i].angle).cells)
            for (const Cell &c : f.cells) {
                const int d = std::max(std::abs(r.x - c.x), std::abs(r.y - c.y));
                if (d < nearest) {
                    nearest = d;
                    best = int(i);
                }
            }
    }
    return best;
}

int Park::photoRideOf(size_t kiosk) const {
    if (kiosk >= _objects.size())
        return -1;
    const Attractions::Attraction *a = _attractions.find(_objects[kiosk].id);
    if (!a || !a->photo)
        return -1;
    return photoRideNear(footprint(a->shape, _objects[kiosk].at, _objects[kiosk].angle));
}

// A rider coming off buys their photo from an open kiosk of the ride's,
// a third of them, who have the money (guessed); its happiness effect
// theirs, its takings and cost of goods the kiosk's.
void Park::sellPhoto(Visitor &v, size_t ride) {
    for (size_t k = 0; k < _objects.size(); ++k) {
        PlacedObject &o = _objects[k];
        const Attractions::Attraction *a = _attractions.find(o.id);
        const int queue = queueOf(k);
        if (!a || !a->photo || !o.open || queue < 0 || photoRideOf(k) != int(ride))
            continue;
        const int price = _queues[size_t(queue)].fare;
        const uint32_t roll = (uint32_t(v.id) * 2654435761u + uint32_t(v.rides) * 40503u) >> 8;
        if (roll % 100 >= kPhotoBuyers || v.cash < price)
            return;
        v.cash -= price;
        v.spent += price;
        ++v.purchases;
        v.happiness = std::clamp(v.happiness + float(a->happinessEffect), 0.0f, 100.0f);
        notePrice(k, priceOpinion(price, a->expectedPrice));
        book(Ledger::kShops, price);
        book(Ledger::kRunning, a->costOfGoods);
        ++o.users;
        o.takings += price;
        o.goods += a->costOfGoods;
        ++_usesById[o.id];
        ++_photosById[_objects[ride].id];
        return;
    }
}

} // namespace reluva::tpw
