// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The special rides: activated with golden tickets and staff trained in
// abilities. docs/engines/tpw/games/simcoaster/systems/special-rides.md.

#include "engines/tpw/park/park.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

// Who learns each ability (ABILITIES.str): as the missions' ObstacleStaff
// pair them (mechanics construction, rock blasting, demolition,
// tunnelling and stonemasonry; handymen rubble removal; gardeners water,
// landfill, landscaping, excavation and logging); engineers ride
// upgrading; janitors firefighting ("train a janitor so he can
// extinguish", TAG_SYSTEM 116) and guards first aid ("Call guard for
// first aid", UITEXT 659).
constexpr std::array<int, 15> kAbilityKinds{-1, 0, 0, 0, 0, 0, 0, 1, 1, 3, 2, 2, 2, 2, 2}; // by Park::StaffKind
// Abilities known from the start (guessed: no mission's research needs them).
constexpr std::array<int, 3> kFirstAbilities{1, 7, 9};

} // namespace

std::optional<Park::StaffKind> Park::abilityKind(int ability) {
    if (ability <= 0 || ability >= int(kAbilityKinds.size()))
        return std::nullopt;
    return StaffKind(kAbilityKinds[size_t(ability)]);
}

bool Park::abilityDiscovered(int ability) const {
    if (std::find(kFirstAbilities.begin(), kFirstAbilities.end(), ability) != kFirstAbilities.end())
        return true;
    const auto needs = [&](const Mission &m) {
        return std::any_of(m.staffNeeded.begin(), m.staffNeeded.end(),
                           [&](const Mission::StaffNeed &n) { return n.ability == ability; });
    };
    for (int n = 0; n < _progress && n < int(_missions.size()); ++n)
        if (needs(_missions[size_t(n)]))
            return true;
    return _obstacleResearched && needs(_mission);
}

bool Park::awaitsActivation(size_t object) const {
    if (object >= _objects.size() || _objects[object].active)
        return false;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    return a && a->special;
}

// As an obstacle's: the first trained of each, each staff member once.
std::vector<std::vector<int>> Park::activationStaff(size_t object) const {
    std::vector<std::vector<int>> out;
    const Attractions::Attraction *a = object < _objects.size() ? _attractions.find(_objects[object].id) : nullptr;
    if (!a)
        return out;
    std::vector<int> taken;
    for (const auto &[ability, count] : a->skills) {
        std::vector<int> &ids = out.emplace_back();
        const std::optional<StaffKind> kind = abilityKind(ability);
        for (const StaffMember &m : _staff)
            if (kind && m.kind == *kind && m.ability == ability && int(ids.size()) < count &&
                std::find(taken.begin(), taken.end(), m.id) == taken.end()) {
                ids.push_back(m.id);
                taken.push_back(m.id);
            }
    }
    return out;
}

// Its abilities discovered, its staff there, its golden tickets given
// up: it's open, its hoarding gone.
Park::Activation Park::activate(size_t object) {
    if (!awaitsActivation(object))
        return Activation::kNotSpecial;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    for (const auto &[ability, count] : a->skills)
        if (!abilityDiscovered(ability))
            return Activation::kUndiscovered;
    const std::vector<std::vector<int>> staff = activationStaff(object);
    for (size_t i = 0; i < a->skills.size(); ++i)
        if (int(staff[i].size()) < a->skills[i].second)
            return Activation::kNeedsStaff;
    if (_goldenTickets < a->ticketCost)
        return Activation::kNeedsTickets;
    _goldenTickets -= a->ticketCost;
    _objects[object].active = true;
    _objects[object].open = true;
    return Activation::kActivated;
}

std::vector<int> Park::ridesActivated() const {
    std::vector<int> ids;
    for (const PlacedObject &o : _objects)
        if (const Attractions::Attraction *a = _attractions.find(o.id); a && a->special && o.active)
            ids.push_back(o.id);
    return ids;
}

void Park::setRidesActivated(const std::vector<int> &ids) {
    for (PlacedObject &o : _objects)
        if (std::find(ids.begin(), ids.end(), o.id) != ids.end())
            o.active = o.open = true;
}

} // namespace reluva::tpw
