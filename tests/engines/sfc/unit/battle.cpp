// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command's unit tests: the battle as read from the running
// program's memory, and the pilot's first choices from it.

#include "common/events.h"
#include "engines/sfc/host/battle.h"
#include "engines/sfc/host/pilot.h"

#include "tests/support/test.h"

#include <bit>
#include <cmath>

using namespace reluva;

namespace {

// Where the game keeps a battle (docs/engines/sfc/games/sfc2/systems/battle.md),
// and room for the objects the test makes.
constexpr uint32_t kWorld = 0x83fea0;
constexpr uint32_t kTeamsPointer = 0x8872d4;
constexpr uint32_t kRelationsPointer = 0x86a758;
constexpr uint32_t kHeap = 0x1000000;
constexpr uint32_t kShipKind = 0x71bd3c;

void writeFloat(x86::Cpu &cpu, uint32_t a, float v) {
    cpu.write32(a, std::bit_cast<uint32_t>(v));
}

// A battle of three ships: the player's (1024, slot 0, at 0,0 facing +x),
// a neutral (1025, slot 1, 30 ahead) and an enemy (1026, slot 2, a
// little abaft the right beam at range 5, which the player has targeted), with the teams' table:
// slot 1 neutral (5) and slot 2 an enemy (6) to slot 0.
struct Fixture {
    x86::Cpu cpu{32u << 20};
    uint32_t next = kHeap;

    uint32_t allocate(uint32_t size) {
        const uint32_t a = next;
        next += (size + 15) & ~15u;
        return a;
    }
    void ship(int id, int slot, float x, float y, int target) {
        const uint32_t a = allocate(0x200);
        cpu.write32(a, kShipKind);
        cpu.write32(a + 0x18, 3);
        cpu.write32(a + 0x28, uint32_t(target));
        writeFloat(cpu, a + 0x34, x);
        writeFloat(cpu, a + 0x38, y);
        cpu.write32(a + 0x60, uint32_t(slot));
        writeFloat(cpu, a + 0x98, 1.0f);
        writeFloat(cpu, a + 0xa0, 0.0f);
        cpu.write32(kWorld + 0x30 + 4 * uint32_t(id), a);
    }
    Fixture() {
        cpu.write32(kWorld + 0x24, 1024);
        const uint32_t teams = allocate(0x6204);
        cpu.write32(teams + 0x6200, 0);
        cpu.write32(kTeamsPointer, teams);
        const uint32_t relations = allocate(0x100);
        const uint32_t row = allocate(0x100);
        cpu.write32(relations + 0x54, row);
        cpu.write32(row + 0x10 + 4 * 1, 5);
        cpu.write32(row + 0x10 + 4 * 2, 6);
        cpu.write32(kRelationsPointer, relations);
        ship(1024, 0, 0.0f, 0.0f, 1026);
        ship(1025, 1, 300.0f, 0.0f, -1);
        ship(1026, 2, -10.0f, -50.0f, 1024);
    }
};

void testObjects() {
    Fixture f;
    const sfc::Battle battle(f.cpu);
    CHECK(battle.running());
    CHECK(battle.playerId() == 1024);
    const auto objects = battle.objects();
    CHECK(objects.size() == 3);
    const sfc::Battle::Object me = battle.object(1024);
    const sfc::Battle::Object delegate = battle.object(1025);
    const sfc::Battle::Object enemy = battle.object(1026);
    CHECK(me.ship && me.relation == 1 && me.friendly());
    CHECK(delegate.relation == 5 && !delegate.friendly() && !delegate.hostile());
    CHECK(enemy.relation == 6 && enemy.hostile());
    CHECK(me.target == 1026);
    CHECK(std::fabs(sfc::Battle::range(me, delegate) - 30.0f) < 0.01f);
    CHECK(battle.object(1027).address == 0);
}

// The pilot's first inputs: red alert, the speed raised, the nearest
// enemy chosen; then, with the enemy abaft the right beam, a click right
// of the view's middle to turn toward it, and the right-aft shield
// reinforced. It doesn't fire at what's off its bow.
void testPilot() {
    Fixture f;
    sfc::Pilot pilot(f.cpu);
    std::vector<sfc::Pilot::Action> got;
    for (uint64_t frame = 1; frame <= 20; ++frame)
        if (auto a = pilot.step(frame))
            got.push_back(*a);
    CHECK(got.size() >= 10);
    if (got.size() < 10)
        return;
    CHECK(got[0].key == 'r');
    CHECK(got[1].key == 's');
    CHECK(got[7].key == '`');
    bool turnedRight = false;
    bool reinforced = false;
    bool fired = false;
    for (const auto &a : got) {
        turnedRight |= a.key == 0 && a.x > 500;
        reinforced |= a.key == key::kKp0 + 3;
        fired |= a.key == 'z';
    }
    CHECK(turnedRight);
    CHECK(reinforced);
    CHECK(!fired);
}

// Nothing to fly: no player's object, no input.
void testNoBattle() {
    x86::Cpu cpu(32u << 20);
    sfc::Pilot pilot(cpu);
    CHECK(!sfc::Battle(cpu).running());
    CHECK(!pilot.step(1));
}

} // namespace

static const reluva::test::Suite kSuite("sfc.unit.battle",
                                        {
                                            {"objects", [](const std::filesystem::path &) { testObjects(); }},
                                            {"pilot", [](const std::filesystem::path &) { testPilot(); }},
                                            {"no battle", [](const std::filesystem::path &) { testNoBattle(); }},
                                        });
