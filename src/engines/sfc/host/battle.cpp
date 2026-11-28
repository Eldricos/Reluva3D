// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/battle.h"

#include <bit>
#include <cmath>

namespace reluva::sfc {

namespace {

// The world object the battle's code works on (0x83fea0): the player's
// object's number at +0x24, objects by number from +0x30 (0x422404).
constexpr uint32_t kWorld = 0x83fea0;
constexpr uint32_t kPlayerId = kWorld + 0x24;
constexpr uint32_t kObjects = kWorld + 0x30;
// The numbers it accepts (0x422172).
constexpr int kMaxObjects = 0x4000;
// The ships' class: the table of methods every ship's object starts with
// (seen in battles; numbered from 1024 there).
constexpr uint32_t kShipKind = 0x71bd3c;
// The mission's teams (0x8872d4, made at 0x4be5b5): the player's slot at
// +0x6200 (0x4bf781). The table of how teams stand to each other
// (0x86a758): a row for each of 20 slots from +0x54, each row's entries
// from +0x10 (0x424fbf).
constexpr uint32_t kTeams = 0x8872d4;
constexpr uint32_t kPlayerSlot = 0x6200;
constexpr uint32_t kRelations = 0x86a758;
constexpr int kSlots = 20;

} // namespace

float Battle::readFloat(uint32_t address) const {
    return std::bit_cast<float>(_cpu.read32(address));
}

int Battle::playerId() const {
    const int id = static_cast<int>(_cpu.read32(kPlayerId));
    return id >= 0 && id < kMaxObjects ? id : -1;
}

bool Battle::running() const {
    const int id = playerId();
    return id >= 0 && _cpu.read32(kObjects + 4 * uint32_t(id)) != 0;
}

Battle::Object Battle::object(int id) const {
    Object o;
    if (id < 0 || id >= kMaxObjects)
        return o;
    const uint32_t a = _cpu.read32(kObjects + 4 * uint32_t(id));
    if (a == 0 || !_cpu.valid(a, 0x40))
        return o;
    o.id = id;
    o.address = a;
    o.kind = _cpu.read32(a);
    o.flags = _cpu.read32(a + 0x18);
    // Where it is, as the range between two objects is taken (0x4221ab).
    o.x = readFloat(a + 0x34);
    o.y = readFloat(a + 0x38);
    o.z = readFloat(a + 0x3c);
    o.ship = o.kind == kShipKind;
    if (o.ship) {
        o.target = static_cast<int>(_cpu.read32(a + 0x28));
        o.slot = static_cast<int>(_cpu.read32(a + 0x60));
        o.relation = relation(o.slot);
        o.headingX = readFloat(a + 0x98);
        o.headingY = readFloat(a + 0xa0);
    }
    return o;
}

int Battle::relation(int slot) const {
    const uint32_t teams = _cpu.read32(kTeams);
    const uint32_t table = _cpu.read32(kRelations);
    if (!teams || !table || !_cpu.valid(teams + kPlayerSlot, 4))
        return 0;
    const int mine = static_cast<int>(_cpu.read32(teams + kPlayerSlot));
    if (mine < 0 || mine >= kSlots || slot < 0 || slot >= kSlots)
        return 0;
    if (slot == mine)
        return 1;
    const uint32_t row = _cpu.read32(table + 0x54 + 4 * uint32_t(mine));
    return row ? static_cast<int>(_cpu.read32(row + 0x10 + 4 * uint32_t(slot))) : 0;
}

float Battle::range(const Object &a, const Object &b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) / 10.0f;
}

std::vector<Battle::Object> Battle::objects() const {
    std::vector<Object> out;
    for (int id = 0; id < kMaxObjects; ++id)
        if (Object o = object(id); o.address)
            out.push_back(o);
    return out;
}

} // namespace reluva::sfc
