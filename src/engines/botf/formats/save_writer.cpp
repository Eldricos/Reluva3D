// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/save_writer.h"

#include "common/archive.h"
#include "common/bytes.h"
#include "common/text.h"
#include "engines/botf/formats/save_layout.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace reluva::botf {

namespace {

using namespace saved;

void put16(std::vector<uint8_t> &d, size_t at, int value) {
    writeLE16(&d[at], uint16_t(value));
}

void put32(std::vector<uint8_t> &d, size_t at, int value) {
    writeLE32(&d[at], uint32_t(value));
}

void put64(std::vector<uint8_t> &d, size_t at, uint64_t value) {
    writeLE32(&d[at], uint32_t(value));
    writeLE32(&d[at + 4], uint32_t(value >> 32));
}

// Only where it reads differently, so what follows the text's end (left
// over in the game's own files) stays as it was.
void putText(std::vector<uint8_t> &d, size_t at, size_t size, const std::string &value) {
    if (field(d, at, size) == value)
        return;
    std::string bytes = text::utf8ToWindows1252(value);
    // Room for the zero that ends it.
    bytes.resize(std::min(bytes.size(), size - 1));
    std::fill(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + size), 0);
    std::copy(bytes.begin(), bytes.end(), d.begin() + std::ptrdiff_t(at));
}

// Flags the readers take as anything but 0, written as 1 only where they
// read differently.
void putFlag16(std::vector<uint8_t> &d, size_t at, bool value) {
    if ((s16(d, at) != 0) != value)
        put16(d, at, value ? 1 : 0);
}

void putFlag32(std::vector<uint8_t> &d, size_t at, bool value) {
    if ((s32(d, at) != 0) != value)
        put32(d, at, value ? 1 : 0);
}

Error missing(const char *name) {
    return Error::fail(std::string("the save has no ") + name);
}

Error writeSummary(const SaveSummary &summary, SaveFiles &save) {
    std::vector<uint8_t> *info = save.file("gameInfo");
    if (!info)
        return missing("gameInfo");
    if (info->size() <= kEmpireAt)
        return Error::fail("gameInfo is too small");
    save.setName(text::utf8ToWindows1252(summary.name));
    // gameInfo holds the file's name again, in 13 bytes.
    putText(*info, 0, kDescriptionAt, summary.name);
    putText(*info, kDescriptionAt, kDescriptionSize, summary.description);
    put32(*info, kTurnAt, summary.turn);
    putFlag32(*info, kMultiplayerAt, summary.multiplayer);
    if (summary.empire >= 0 && summary.empire < 5)
        (*info)[kEmpireAt] = uint8_t(summary.empire);
    if (info->size() >= kGalaxySizeInfoAt + 4) {
        if (summary.galaxyShape >= 0)
            put32(*info, kGalaxyShapeAt, summary.galaxyShape);
        if (summary.galaxySize >= 0)
            put32(*info, kGalaxySizeInfoAt, summary.galaxySize);
    }
    return Error::success();
}

void writeSector(const SavedSector &s, std::vector<uint8_t> &d, size_t at) {
    put16(d, at + 8, s.starbase);
    put16(d, at + 10, s.stellarObject);
    put16(d, at + 12, s.system);
    put16(d, at + 14, s.claims);
    put64(d, at + 40, s.explored);
    put64(d, at + 48, s.revealed);
    for (size_t e = 0; e < s.scanStrength.size(); ++e)
        put32(d, at + 0x40 + e * 4, s.scanStrength[e]);
}

void writePlanet(const SavedPlanet &planet, std::vector<uint8_t> &d, size_t p) {
    // The names the galaxy's making gives ("Acamar I") end in a line break,
    // which the reader drops; Sol's ("Earth") don't. A new name ends as the
    // old one did.
    std::string name = field(d, p, 40);
    const bool lineBreak = !name.empty() && name.back() == '\n';
    if (lineBreak)
        name.pop_back();
    if (name != planet.name)
        putText(d, p, 40, lineBreak ? planet.name + "\n" : planet.name);
    putText(d, p + 0x28, 14, planet.animation);
    put16(d, p + 0x38, planet.size);
    put16(d, p + 0x3A, planet.terraformNeeded);
    put16(d, p + 0x3C, planet.terraformDone);
    put16(d, p + 0x3E, planet.foodBonus);
    put16(d, p + 0x40, planet.energyBonus);
    put16(d, p + 0x42, planet.growthBonus);
    d[p + 0x48] = uint8_t(planet.type);
    d[p + 0x49] = uint8_t(planet.atmosphere);
}

// The fixed part's fields; the record's size, which its planets, specials,
// trade routes and control set, must stay as it is.
void writeSystemFields(const SavedSystem &s, std::vector<uint8_t> &d, size_t at, size_t size) {
    put32(d, at + 4, s.control);
    putText(d, at + 0x08, 40, s.name);
    d[at + 0x31] = uint8_t(s.starType);
    putText(d, at + 0x32, 5, s.starAnimation);
    put32(d, at + 0x40, s.population);
    put16(d, at + 0x44, s.residentRace);
    // 0x23, a race past the last, is no one.
    const int held = s32(d, at + 0x4C);
    if ((held == 0x23 ? -1 : held) != s.controllingRace)
        put32(d, at + 0x4C, s.controllingRace < 0 ? 0x23 : s.controllingRace);
    for (size_t i = 0; i < s.workers.size(); ++i)
        put32(d, at + 0x54 + 4 * i, s.workers[i]);
    put16(d, at + 0x6E, s.dilithium);
    put32(d, at + 0x78, s.mapX);
    put32(d, at + 0x7C, s.mapY);
    put32(d, at + 0x80, s.buildOrder);
    put16(d, at + 0x88, s.building);
    put16(d, at + 0x90, s.ship);
    // Kept as ten times what the screen shows; the last digit isn't read.
    if (s32(d, at + 0xBC) / 10 != s.maxPopulation)
        put32(d, at + 0xBC, s.maxPopulation * 10);
    if (s.control == 2)
        putFlag32(d, at + size - 4, s.buying);
}

Error writeSystems(const std::vector<SavedSystem> &systems, std::vector<uint8_t> &d) {
    size_t i = 0;
    for (size_t at = 0; at < d.size(); ++i) {
        if (d.size() - at < kSystemSize || i >= systems.size())
            return Error::fail("systInfo holds more systems than the game");
        const SavedSystem &s = systems[i];
        const int planets = s16(d, at + 0x6A);
        const int specials = s16(d, at + 0x6C);
        const bool players = s32(d, at + 4) == 2;
        const int tradeRoutes = d[at + 0x308] + d[at + 0x309];
        if (planets < 0 || specials < 0 || size_t(planets) != s.planets.size())
            return Error::fail(s.name + "'s planets aren't systInfo's");
        // The player's systems have a production queue the others haven't.
        if (players != (s.control == 2))
            return Error::fail(s.name + " can't become or stop being the player's");
        const size_t size = kSystemSize + size_t(planets) * (kPlanetSize + 4) + size_t(specials) * kSpecialSize +
                            size_t(tradeRoutes) * 4 + (players ? kProductionQueueSize : 0);
        if (d.size() - at < size)
            return Error::fail("systInfo ends in a system");
        writeSystemFields(s, d, at, size);
        for (size_t p = 0; p < s.planets.size(); ++p)
            writePlanet(s.planets[p], d, at + kSystemSize + p * kPlanetSize);
        at += size;
    }
    if (i != systems.size())
        return Error::fail("systInfo holds fewer systems than the game");
    return Error::success();
}

Error writeGalaxy(const SavedGalaxy &g, SaveFiles &save) {
    std::vector<uint8_t> *info = save.file("galInfo");
    std::vector<uint8_t> *sectors = save.file("sector.lst");
    std::vector<uint8_t> *systems = save.file("systInfo");
    std::vector<uint8_t> *stellar = save.file("stellInfo");
    if (!info || !sectors || !systems || !stellar)
        return missing("galaxy");
    if (info->size() < kGalInfoSize)
        return Error::fail("galInfo is too small");
    put32(*info, kGalaxySizeAt, g.size);
    if (sectors->size() != g.sectors.size() * kSectorSize)
        return Error::fail("sector.lst's sectors aren't the galaxy's");
    for (size_t i = 0; i < g.sectors.size(); ++i)
        writeSector(g.sectors[i], *sectors, i * kSectorSize);
    if (Error err = writeSystems(g.systems, *systems))
        return err;
    if (stellar->size() != g.stellarObjects.size() * kStellarSize)
        return Error::fail("stellInfo's objects aren't the galaxy's");
    for (size_t i = 0; i < g.stellarObjects.size(); ++i) {
        const SavedStellarObject &o = g.stellarObjects[i];
        const size_t at = i * kStellarSize;
        put32(*stellar, at, o.type);
        putText(*stellar, at + 4, 40, o.name);
        put32(*stellar, at + 68, o.mapX);
        put32(*stellar, at + 72, o.mapY);
    }
    return Error::success();
}

Error writeOutposts(const std::vector<SavedOutpost> &outposts, SaveFiles &save) {
    std::vector<uint8_t> *d = save.file("starBaseInfo");
    if (!d && outposts.empty())
        return Error::success();
    if (!d || d->size() / kOutpostSize != outposts.size())
        return Error::fail("starBaseInfo's outposts aren't the game's");
    for (size_t i = 0; i < outposts.size(); ++i) {
        const size_t at = i * kOutpostSize;
        putText(*d, at, 40, outposts[i].name);
        put16(*d, at + 0x28, outposts[i].owner);
        put16(*d, at + 0x2A, outposts[i].sector);
    }
    return Error::success();
}

// strcInfo, whole: every system in order, those without buildings too,
// its number and how many kinds, then each kind's count, building, powered
// and 0. Only where they changed, to keep the game's own file.
Error writeBuildings(const std::vector<SavedSystem> &systems, SaveFiles &save) {
    std::vector<std::vector<SavedBuildings>> was(systems.size());
    const std::vector<uint8_t> *d = save.file("strcInfo");
    for (size_t at = 0; d && at + 4 <= d->size();) {
        const int system = s16(*d, at);
        const int kinds = s16(*d, at + 2);
        at += 4;
        if (kinds < 0 || size_t(kinds) * kBuildingSize > d->size() - at)
            return Error::fail("strcInfo ends in a system's buildings");
        for (int k = 0; k < kinds; ++k, at += kBuildingSize)
            if (system >= 0 && size_t(system) < systems.size())
                was[size_t(system)].push_back({s16(*d, at + 2), s16(*d, at), s16(*d, at + 4)});
    }
    auto same = [](const SavedBuildings &a, const SavedBuildings &b) {
        return a.building == b.building && a.count == b.count && a.powered == b.powered;
    };
    bool changed = false;
    for (size_t i = 0; i < systems.size(); ++i)
        changed |=
            !std::equal(was[i].begin(), was[i].end(), systems[i].buildings.begin(), systems[i].buildings.end(), same);
    if (!changed)
        return Error::success();
    std::vector<uint8_t> out;
    auto add16 = [&](int v) {
        out.push_back(uint8_t(v));
        out.push_back(uint8_t(v >> 8));
    };
    for (size_t i = 0; i < systems.size(); ++i) {
        add16(int(i));
        add16(int(systems[i].buildings.size()));
        for (const SavedBuildings &b : systems[i].buildings) {
            add16(b.count);
            add16(b.building);
            add16(b.powered);
            add16(0);
        }
    }
    save.add("strcInfo") = std::move(out);
    return Error::success();
}

void writeEmpire(const SavedEmpire &m, std::vector<uint8_t> &d, size_t at) {
    putText(d, at, 40, m.name);
    putText(d, at + 40, 40, m.leader);
    // The credits as the turn began (0x98), which loading the game seems to
    // start from, change with the credits now; what's been bought since,
    // the difference between them, stays.
    if (const int was = s32(d, at + 0x54); was != m.credits) {
        put32(d, at + 0x54, m.credits);
        put32(d, at + 0x98, s32(d, at + 0x98) + m.credits - was);
    }
    put32(d, at + 0x58, m.creditsPerTurn);
    put32(d, at + 0x5C, m.supportCost);
    put32(d, at + 0x60, m.dilithiumPerTurn);
    put32(d, at + 0x64, m.shipsBuilding);
    put32(d, at + 0x68, m.researchPoints);
    put32(d, at + 0x6C, m.population);
    put16(d, at + 0x74, m.morale);
    put32(d, at + 0x78, m.intelligence);
    for (size_t o = 0; o < 5; ++o) {
        put32(d, at + 0x110 + o * 4, m.treatyStatus[o]);
        putFlag32(d, at + 0x124 + o * 4, m.territoryAccess[o]);
        putFlag32(d, at + 0x138 + o * 4, m.tradeAllowed[o]);
    }
    put32(d, at + 0x14C, m.tradeRoutes);
    put64(d, at + 0x158, m.knownRaces);
}

Error writeEmpires(const std::array<SavedEmpire, 5> &empires, SaveFiles &save) {
    std::vector<uint8_t> *info = save.file("empsInfo");
    constexpr size_t kEmpireSize = 444;
    if (!info)
        return missing("empsInfo");
    if (info->size() < empires.size() * kEmpireSize)
        return Error::fail("empsInfo is too small");
    for (size_t e = 0; e < empires.size(); ++e) {
        writeEmpire(empires[e], *info, e * kEmpireSize);
        // provInfo#, whole where it changed: a count, then the systems.
        const std::string name = "provInfo" + std::to_string(e);
        const std::vector<uint8_t> *d = save.file(name);
        std::vector<int> was;
        const int count = d && d->size() >= 4 ? s32(*d, 0) : 0;
        for (int i = 0; i < count && 4 + size_t(i) * 2 + 2 <= d->size(); ++i)
            was.push_back(s16(*d, 4 + size_t(i) * 2));
        if (was == empires[e].systems)
            continue;
        std::vector<uint8_t> out(4 + empires[e].systems.size() * 2);
        put32(out, 0, int(empires[e].systems.size()));
        for (size_t i = 0; i < empires[e].systems.size(); ++i)
            put16(out, 4 + i * 2, empires[e].systems[i]);
        save.add(name) = std::move(out);
    }
    return Error::success();
}

void putDouble(std::vector<uint8_t> &d, size_t at, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put64(d, at, bits);
}

// A list as GShipList and GWTForce are: a head giving the entry's size
// (0x10), the room the list's file has (0x14, in entries) and how many are
// in use (0x18), and the list, its entries first. `ids` are the entries to
// write, in order, by their numbers, which `idOf` reads from an entry. An
// entry that was in the list keeps its bytes, so what isn't known of it
// stays; a new one starts from zeros, or from `fresh` where given. Room
// grows by half when it runs out, as the game's lists do (32, 48, 72,
// 108); room an entry no longer uses is cleared.
struct ListFiles {
    const char *head;
    const char *list;
    size_t entrySize;
    int (*idOf)(const std::vector<uint8_t> &, size_t);
};

Error writeList(const ListFiles &files, const std::vector<int> &ids, SaveFiles &save,
                const std::vector<uint8_t> *fresh = nullptr) {
    std::vector<uint8_t> *head = save.file(files.head);
    std::vector<uint8_t> *list = save.file(files.list);
    if (!head || !list)
        return missing(files.list);
    const size_t size = files.entrySize;
    if (head->size() < 0x1C || s32(*head, 0x10) != int(size) || s32(*head, 0x18) < 0 ||
        size_t(s32(*head, 0x18)) * size > list->size())
        return Error::fail(std::string(files.head) + " doesn't fit " + files.list);
    const std::vector<uint8_t> old = *list;
    const size_t oldCount = size_t(s32(*head, 0x18));
    std::unordered_map<int, size_t> was;
    for (size_t i = 0; i < oldCount; ++i)
        was.emplace(files.idOf(old, i * size), i);
    size_t room = std::max<size_t>(size_t(std::max(s32(*head, 0x14), 0)), old.size() / size);
    while (room < ids.size())
        room = room < 2 ? 32 : room + room / 2;
    list->resize(room * size, 0);
    for (size_t i = 0; i < ids.size(); ++i) {
        uint8_t *to = &(*list)[i * size];
        if (const auto it = was.find(ids[i]); it != was.end())
            std::copy_n(&old[it->second * size], size, to);
        else if (fresh && fresh->size() >= (i + 1) * size)
            std::copy_n(&(*fresh)[i * size], size, to);
        else
            std::fill_n(to, size, 0);
    }
    for (size_t i = ids.size(); i < oldCount; ++i)
        std::fill_n(&(*list)[i * size], size, 0);
    if (s32(*head, 0x14) != int(room))
        put32(*head, 0x14, int(room));
    put32(*head, 0x18, int(ids.size()));
    return Error::success();
}

int shipIdOf(const std::vector<uint8_t> &d, size_t at) {
    return s16(d, at + 0x2A);
}

int taskForceIdOf(const std::vector<uint8_t> &d, size_t at) {
    return s16(d, at);
}

void writeShip(const SavedShip &s, std::vector<uint8_t> &d, size_t at) {
    putText(d, at, 40, s.name);
    put16(d, at + 0x28, s.race);
    put16(d, at + 0x2A, s.id);
    put32(d, at + 0x2C, s.beamAccuracyBonus);
    put32(d, at + 0x30, s.torpedoAccuracyBonus);
    put32(d, at + 0x34, s.defenseBonus);
    put32(d, at + 0x38, s.shields);
    put32(d, at + 0x3C, s.hull);
    put32(d, at + 0x40, s.experience);
    put32(d, at + 0x44, s.experienceLevel);
    put16(d, at + 0x50, s.taskForce);
    put32(d, at + 0x52, s.shipClass);
    put32(d, at + 0x58, s.cloak);
    put32(d, at + 0x5C, s.agility);
    put32(d, at + 0x60, s.interception);
    put32(d, at + 0x64, s.bombing);
    put32(d, at + 0x6C, s.construction);
    put32(d, at + 0x70, s.groundCombat);
}

// The computer players' lists of ships (AgtSh#, each agent's, and TskSh#,
// each task's: 16-bit ship numbers), which aren't written yet: a ship
// one of them lists can't be taken out.
Error checkPlansKeep(const std::vector<int> &ids, const SaveFiles &save) {
    const std::vector<uint8_t> *list = save.file("GShipList");
    const std::vector<uint8_t> *head = save.file("GShipHead");
    if (!list || !head || head->size() < 0x1C)
        return Error::success();
    std::vector<int> kept = ids;
    std::sort(kept.begin(), kept.end());
    for (int i = 0; i < s32(*head, 0x18) && size_t(i + 1) * kShipSize <= list->size(); ++i) {
        const int id = shipIdOf(*list, size_t(i) * kShipSize);
        if (std::binary_search(kept.begin(), kept.end(), id))
            continue;
        for (const std::string &name : save.names()) {
            const bool plan = name.rfind("AgtSh", 0) == 0 || name.rfind("TskSh", 0) == 0;
            if (!plan || name.find('.') != std::string::npos)
                continue;
            const std::vector<uint8_t> &d = *save.file(name);
            for (size_t at = 0; at + 2 <= d.size(); at += 2)
                if (s16(d, at) == id)
                    return Error::fail("ship " + std::to_string(id) + " can't be taken out: the computer's " + name +
                                       " still lists it");
        }
    }
    return Error::success();
}

Error writeShips(const std::vector<SavedShip> &ships, SaveFiles &save) {
    std::vector<int> ids;
    for (const SavedShip &s : ships)
        ids.push_back(s.id);
    if (Error err = checkPlansKeep(ids, save))
        return err;
    if (Error err = writeList({"GShipHead", "GShipList", kShipSize, shipIdOf}, ids, save))
        return err;
    std::vector<uint8_t> &list = *save.file("GShipList");
    for (size_t i = 0; i < ships.size(); ++i)
        writeShip(ships[i], list, i * kShipSize);
    return Error::success();
}

// Room for nine ships' numbers, those not used -1.
constexpr size_t kMaxShips = 9;

void writeTaskForce(const SavedTaskForce &t, int width, std::vector<uint8_t> &d, size_t at) {
    // The game leaves the numbers of ships gone from a task force after its
    // count; they're cleared only where the count changes, or in a new one.
    const bool recount = s32(d, at + 0x04) != t.ships;
    put16(d, at, t.id);
    put32(d, at + 0x04, t.ships);
    for (size_t k = 0; k < kMaxShips; ++k) {
        const size_t slot = at + 0x08 + 4 * k;
        if (k < t.shipIds.size())
            put32(d, slot, t.shipIds[k]);
        else if (recount || s32(d, slot) == 0)
            put32(d, slot, -1);
    }
    put32(d, at + 0x2C, t.mission);
    // A sector's number, row by row; any number below 0 is none.
    const int target = t.destinationX < 0 || t.destinationY < 0 ? -1 : t.destinationY * width + t.destinationX;
    if (std::max(s32(d, at + 0x30), -1) != target)
        put32(d, at + 0x30, target);
    put32(d, at + 0x34, t.owner);
    put32(d, at + 0x38, t.y);
    put32(d, at + 0x3C, t.x);
    put16(d, at + 0x40, t.abilities);
    put32(d, at + 0x44, t.firstColonyShip);
    put32(d, at + 0x48, t.unknown48);
    put32(d, at + 0x4C, t.control);
    put16(d, at + 0x50, t.production);
    put16(d, at + 0x52, t.scanRange);
    put32(d, at + 0x54, t.orderId);
    putDouble(d, at + 0x58, t.range);
    putDouble(d, at + 0x60, t.speed);
    putFlag32(d, at + 0x68, t.cloaked);
}

// alienTFInfo#, each race's task forces, the major empires' too: a count,
// then their numbers (16 bits each), in their order. The game has one for
// each race in the game, a minor race's even with none, and none for the
// monsters'; only those there are written, where a race's differ.
Error writeRacesTaskForces(const std::vector<SavedTaskForce> &taskForces, SaveFiles &save) {
    std::map<int, std::vector<int>> byRace;
    for (const std::string &name : save.names())
        if (name.rfind("alienTFInfo", 0) == 0)
            byRace.try_emplace(std::atoi(name.c_str() + 11));
    for (const SavedTaskForce &t : taskForces)
        if (const auto it = byRace.find(t.owner); it != byRace.end())
            it->second.push_back(t.id);
    for (auto &[race, ids] : byRace) {
        std::sort(ids.begin(), ids.end());
        const std::string name = "alienTFInfo" + std::to_string(race);
        const std::vector<uint8_t> *d = save.file(name);
        std::vector<int> was;
        const int count = d && d->size() >= 4 ? s32(*d, 0) : 0;
        for (int i = 0; i < count && 4 + size_t(i) * 2 + 2 <= d->size(); ++i)
            was.push_back(s16(*d, 4 + size_t(i) * 2));
        if (was == ids)
            continue;
        std::vector<uint8_t> out(4 + ids.size() * 2);
        put32(out, 0, int(ids.size()));
        for (size_t i = 0; i < ids.size(); ++i)
            put16(out, 4 + i * 2, ids[i]);
        save.add(name) = std::move(out);
    }
    return Error::success();
}

// GWTForce, the task forces as they are now, in their order; and
// GTForceList, as they were when the turn began, in their numbers' order:
// those it had keep its entries, new ones are as GWTForce has them.
Error writeTaskForces(const std::vector<SavedTaskForce> &taskForces, int width, SaveFiles &save) {
    std::vector<int> ids;
    for (const SavedTaskForce &t : taskForces) {
        if (t.shipIds.size() > kMaxShips)
            return Error::fail("a task force has more ships than GWTForce has room for");
        ids.push_back(t.id);
    }
    if (Error err = writeList({"GWTForceHd", "GWTForce", kTaskForceSize, taskForceIdOf}, ids, save))
        return err;
    std::vector<uint8_t> &list = *save.file("GWTForce");
    for (size_t i = 0; i < taskForces.size(); ++i)
        writeTaskForce(taskForces[i], width, list, i * kTaskForceSize);
    if (!save.has("GTForceList"))
        return Error::success();
    std::vector<size_t> byNumber(taskForces.size());
    for (size_t i = 0; i < byNumber.size(); ++i)
        byNumber[i] = i;
    std::stable_sort(byNumber.begin(), byNumber.end(),
                     [&](size_t a, size_t b) { return taskForces[a].id < taskForces[b].id; });
    std::vector<int> sorted;
    std::vector<uint8_t> fresh(taskForces.size() * kTaskForceSize);
    for (size_t i = 0; i < byNumber.size(); ++i) {
        sorted.push_back(taskForces[byNumber[i]].id);
        std::copy_n(&list[byNumber[i] * kTaskForceSize], kTaskForceSize, &fresh[i * kTaskForceSize]);
    }
    if (Error err = writeList({"GTForceHd", "GTForceList", kTaskForceSize, taskForceIdOf}, sorted, save, &fresh))
        return err;
    return writeRacesTaskForces(taskForces, save);
}

Error writeCounters(const SavedGame &game, SaveFiles &save) {
    std::vector<uint8_t> *ships = save.file("ShipCntr");
    std::vector<uint8_t> *forces = save.file("TForceCntr");
    if (!ships || !forces || ships->size() < 4 || forces->size() < 4)
        return missing("ShipCntr or TForceCntr");
    put32(*ships, 0, game.nextShipId);
    put32(*forces, 0, game.nextTaskForceId);
    return Error::success();
}

Error writeTreaties(const std::vector<SavedTreaty> &treaties, SaveFiles &save) {
    std::vector<uint8_t> *d = save.file("treaty");
    if (!d && treaties.empty())
        return Error::success();
    if (!d || d->size() < kTreatyHeaderSize || s32(*d, 0) != int(treaties.size()) ||
        d->size() < kTreatyHeaderSize + treaties.size() * kTreatySize)
        return Error::fail("treaty's treaties aren't the game's");
    for (size_t i = 0; i < treaties.size(); ++i) {
        const SavedTreaty &t = treaties[i];
        const size_t at = kTreatyHeaderSize + i * kTreatySize;
        put32(*d, at + 0x30, t.type);
        put16(*d, at + 0x34, t.from);
        put16(*d, at + 0x36, t.to);
        put32(*d, at + 0x3C, t.startTurn);
        put32(*d, at + 0x40, t.endTurn);
    }
    return Error::success();
}

Error writeRaces(const std::vector<SavedRace> &races, SaveFiles &save) {
    std::vector<uint8_t> *d = save.file("alienInfo");
    if (!d && races.empty())
        return Error::success();
    if (!d || d->size() / kRaceSize != races.size())
        return Error::fail("alienInfo's races aren't the game's");
    for (size_t i = 0; i < races.size(); ++i) {
        const SavedRace &r = races[i];
        const size_t at = i * kRaceSize;
        putFlag16(*d, at, r.alive);
        put16(*d, at + 0x18, r.homeSystem);
        put64(*d, at + 0x20, r.knownRaces);
        for (size_t e = 0; e < r.treaty.size(); ++e)
            put32(*d, at + 0x28 + e * 4, r.treaty[e]);
        put32(*d, at + 0x3C, r.status);
    }
    return Error::success();
}

} // namespace

Error SaveFiles::open(const std::filesystem::path &path) {
    _files.clear();
    AltResArchive archive;
    if (Error err = archive.open(path))
        return err;
    _name = archive.name();
    for (const AltResArchive::Entry &e : archive.entries()) {
        File f;
        f.stored = {e.name, {}, e.size, e.flag, e.check, e.offset == 0};
        if (Error err = archive.readStored(e, f.stored.stored))
            return err;
        f.chunked = normalizeArchiveName(e.name) == normalizeArchiveName("result.lst");
        // techinfo's window starts full of zeros, the others' of spaces.
        const uint8_t fill = normalizeArchiveName(e.name) == normalizeArchiveName("techinfo") ? 0 : kTextWindowFill;
        if (!f.chunked)
            if (Error err = archive.read(e, f.data, fill))
                return err;
        f.read = f.data;
        _files.push_back(std::move(f));
    }
    return Error::success();
}

Error SaveFiles::write(const std::filesystem::path &path) const {
    std::vector<AltResFile> out;
    out.reserve(_files.size());
    for (const File &f : _files) {
        if (f.chunked || f.data == f.read) {
            out.push_back(f.stored);
            continue;
        }
        AltResFile changed{
            f.stored.name,       compressLzss(f.data.data(), f.data.size()), uint32_t(f.data.size()), 1, 0,
            f.stored.emptyAtZero};
        changed.check = altResCheck(changed.stored.data(), changed.stored.size());
        out.push_back(std::move(changed));
    }
    return writeAltRes(path, _name, out);
}

size_t SaveFiles::find(std::string_view name) const {
    const std::string wanted = normalizeArchiveName(name);
    for (size_t i = 0; i < _files.size(); ++i)
        if (normalizeArchiveName(_files[i].stored.name) == wanted)
            return i;
    return _files.size();
}

std::vector<uint8_t> *SaveFiles::file(std::string_view name) {
    const size_t i = find(name);
    return i < _files.size() && !_files[i].chunked ? &_files[i].data : nullptr;
}

const std::vector<uint8_t> *SaveFiles::file(std::string_view name) const {
    const size_t i = find(name);
    return i < _files.size() && !_files[i].chunked ? &_files[i].data : nullptr;
}

std::vector<std::string> SaveFiles::names() const {
    std::vector<std::string> out;
    for (const File &f : _files)
        out.push_back(f.stored.name);
    return out;
}

std::vector<uint8_t> &SaveFiles::add(std::string_view name) {
    if (std::vector<uint8_t> *d = file(name))
        return *d;
    File f;
    f.stored.name = std::string(name);
    _files.push_back(std::move(f));
    return _files.back().data;
}

Error writeSavedGame(const SavedGame &game, SaveFiles &save) {
    Error err = writeSummary(game.summary, save);
    if (!err)
        err = writeGalaxy(game.galaxy, save);
    if (!err)
        err = writeOutposts(game.galaxy.outposts, save);
    if (!err)
        err = writeBuildings(game.galaxy.systems, save);
    if (!err)
        err = writeEmpires(game.empires, save);
    if (!err)
        err = writeShips(game.ships, save);
    if (!err)
        err = writeTaskForces(game.taskForces, game.galaxy.width, save);
    if (!err)
        err = writeCounters(game, save);
    if (!err)
        err = writeTreaties(game.treaties, save);
    if (!err)
        err = writeRaces(game.races, save);
    return err;
}

} // namespace reluva::botf
