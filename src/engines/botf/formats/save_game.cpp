// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/save_game.h"

#include "common/bytes.h"
#include "common/text.h"
#include "engines/botf/formats/alt_res.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::botf {

namespace {

// gameInfo's fields used here, as the Ultimate Editor's GameInfo.java
// gives them.
constexpr size_t kDescriptionAt = 0x0D;
constexpr size_t kDescriptionSize = 51;
constexpr size_t kTurnAt = 0x40;
constexpr size_t kMultiplayerAt = 0x4C;
constexpr size_t kUsed = kMultiplayerAt + 4;
constexpr size_t kEmpireAt = 0x252;

// The saved game's text files are compressed with the window full of
// spaces; gameInfo is one of them.
constexpr uint8_t kTextWindowFill = 0x20;

std::string field(const std::vector<uint8_t> &data, size_t at, size_t size) {
    size_t len = 0;
    while (len < size && data[at + len])
        ++len;
    return text::windows1252ToUtf8(std::string_view(reinterpret_cast<const char *>(&data[at]), len));
}

// One of the archive's files, decompressed; empty files are allowed.
// techinfo's window starts full of zeros, the others' of spaces.
Error readFile(const AltResArchive &save, const char *name, std::vector<uint8_t> &out) {
    const std::string wanted = normalizeArchiveName(name);
    for (const AltResArchive::Entry &e : save.entries())
        if (normalizeArchiveName(e.name) == wanted)
            return save.read(e, out, wanted == normalizeArchiveName("techinfo") ? 0 : kTextWindowFill);
    return Error::fail(std::string("no ") + name);
}

// Opens the save at `path` and reads its files `names` into `out`, in
// order; an error names the save.
Error readFiles(const std::filesystem::path &path, std::initializer_list<const char *> names,
                std::initializer_list<std::vector<uint8_t> *> out) {
    AltResArchive save;
    if (Error err = save.open(path))
        return err;
    auto file = out.begin();
    for (const char *name : names)
        if (Error err = readFile(save, name, **file++))
            return Error::fail(path.string() + ": " + err.message);
    return Error::success();
}

int16_t s16(const std::vector<uint8_t> &d, size_t at) {
    return int16_t(readLE16(&d[at]));
}

int32_t s32(const std::vector<uint8_t> &d, size_t at) {
    return int32_t(readLE32(&d[at]));
}

// galInfo: the galaxy's size (0x08), and how many systems (0x0C), stellar
// objects (0x0E) and sectors (0x14) the other files hold.
constexpr size_t kGalaxySizeAt = 0x08;
constexpr size_t kSystemCountAt = 0x0C;
constexpr size_t kStellarCountAt = 0x0E;
constexpr size_t kSectorCountAt = 0x14;
constexpr size_t kGalInfoSize = 48;

// sector.lst: a 104-byte entry a sector, row by row, each starting with its
// row and column.
constexpr size_t kSectorSize = 104;

// systInfo: a system's fixed part, then its planets (76 bytes each), a word
// for each planet, its specials (64 bytes each), a word for each trade
// route, and, for the player's, the production queue.
constexpr size_t kSystemSize = 0x328;
constexpr size_t kPlanetSize = 76;
constexpr size_t kSpecialSize = 64;
constexpr size_t kProductionQueueSize = 108;

// stellInfo: an 88-byte entry an object.
constexpr size_t kStellarSize = 88;

// GWTForce: a 108-byte entry a task force.
constexpr size_t kTaskForceSize = 108;

// GShipList: a 116-byte entry a ship.
constexpr size_t kShipSize = 116;

// The races techinfo has room for: the five empires and 30 minor races.
constexpr size_t kRaceCount = 35;

Error readSectors(const std::vector<uint8_t> &d, SavedGalaxy &g) {
    if (d.size() % kSectorSize || d.empty())
        return Error::fail("sector.lst isn't whole sectors");
    const size_t count = d.size() / kSectorSize;
    // The last sector's row and column give the grid's size.
    const size_t last = (count - 1) * kSectorSize;
    g.height = s32(d, last) + 1;
    g.width = s32(d, last + 4) + 1;
    if (g.width <= 0 || g.height <= 0 || size_t(g.width) * size_t(g.height) != count)
        return Error::fail("sector.lst isn't a grid");
    g.sectors.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t at = i * kSectorSize;
        if (s32(d, at) != int(i) / g.width || s32(d, at + 4) != int(i) % g.width)
            return Error::fail("sector.lst's sectors aren't in order");
        SavedSector &s = g.sectors[i];
        s.starbase = s16(d, at + 8);
        s.stellarObject = s16(d, at + 10);
        s.system = s16(d, at + 12);
        s.claims = readLE16(&d[at + 14]);
        s.explored = readLE64(&d[at + 40]);
    }
    return Error::success();
}

Error readSystems(const std::vector<uint8_t> &d, SavedGalaxy &g) {
    for (size_t at = 0; at < d.size();) {
        if (d.size() - at < kSystemSize)
            return Error::fail("systInfo ends in a system");
        SavedSystem s;
        s.control = s32(d, at + 4);
        s.name = field(d, at + 0x08, 40);
        s.starType = d[at + 0x31];
        s.starAnimation = field(d, at + 0x32, 5);
        s.population = s32(d, at + 0x40);
        s.residentRace = s16(d, at + 0x44);
        s.controllingRace = s32(d, at + 0x4C);
        // 0x23, a race past the last, is no one.
        if (s.controllingRace == 0x23)
            s.controllingRace = -1;
        const int planets = s16(d, at + 0x6A);
        const int specials = s16(d, at + 0x6C);
        s.dilithium = s16(d, at + 0x6E);
        for (size_t i = 0; i < s.workers.size(); ++i)
            s.workers[i] = s32(d, at + 0x54 + 4 * i);
        s.buildOrder = s32(d, at + 0x80);
        s.building = s16(d, at + 0x88);
        s.ship = s16(d, at + 0x90);
        // Kept as ten times what the screen shows.
        s.maxPopulation = s32(d, at + 0xBC) / 10;
        s.mapX = s32(d, at + 0x78);
        s.mapY = s32(d, at + 0x7C);
        const int tradeRoutes = d[at + 0x308] + d[at + 0x309];
        if (s32(d, at) != int(g.systems.size()) || planets < 0 || specials < 0)
            return Error::fail("systInfo's system " + std::to_string(g.systems.size()) + " isn't one");
        const size_t size = kSystemSize + size_t(planets) * (kPlanetSize + 4) + size_t(specials) * kSpecialSize +
                            size_t(tradeRoutes) * 4 + (s.control == 2 ? kProductionQueueSize : 0);
        if (d.size() - at < size)
            return Error::fail("systInfo ends in a system");
        // The production queue is the record's last part; its last word
        // says the first item is to be bought.
        if (s.control == 2)
            s.buying = s32(d, at + size - 4) != 0;
        for (int i = 0; i < planets; ++i) {
            const size_t p = at + kSystemSize + size_t(i) * kPlanetSize;
            SavedPlanet planet;
            planet.name = field(d, p, 40);
            // The game ends each planet's name with a line break.
            if (!planet.name.empty() && planet.name.back() == '\n')
                planet.name.pop_back();
            planet.animation = field(d, p + 0x28, 14);
            planet.size = s16(d, p + 0x38);
            planet.type = d[p + 0x48];
            planet.atmosphere = d[p + 0x49];
            s.planets.push_back(std::move(planet));
        }
        g.systems.push_back(std::move(s));
        at += size;
    }
    return Error::success();
}

Error readStellarObjects(const std::vector<uint8_t> &d, SavedGalaxy &g) {
    if (d.size() % kStellarSize)
        return Error::fail("stellInfo isn't whole objects");
    for (size_t at = 0; at < d.size(); at += kStellarSize) {
        SavedStellarObject o;
        o.type = s32(d, at);
        o.name = field(d, at + 4, 40);
        o.mapX = s32(d, at + 68);
        o.mapY = s32(d, at + 72);
        g.stellarObjects.push_back(std::move(o));
    }
    return Error::success();
}

} // namespace

Error readSaveSummary(const std::filesystem::path &path, SaveSummary &summary) {
    AltResArchive save;
    if (Error err = save.open(path))
        return err;
    summary = SaveSummary{};
    summary.name = text::windows1252ToUtf8(save.name());
    for (const AltResArchive::Entry &e : save.entries()) {
        if (normalizeArchiveName(e.name) != normalizeArchiveName("gameInfo"))
            continue;
        std::vector<uint8_t> info;
        if (Error err = save.read(e, info, kTextWindowFill))
            return err;
        if (info.size() < kUsed)
            return Error::fail(path.string() + ": gameInfo is too small");
        summary.description = field(info, kDescriptionAt, kDescriptionSize);
        const uint8_t *t = &info[kTurnAt];
        summary.turn = int(uint32_t(t[0] | t[1] << 8 | t[2] << 16) | uint32_t(t[3]) << 24);
        summary.multiplayer = info[kMultiplayerAt] != 0;
        if (info.size() > kEmpireAt && info[kEmpireAt] < 5)
            summary.empire = info[kEmpireAt];
        return Error::success();
    }
    return Error::fail(path.string() + " has no gameInfo");
}

Error readSavedGalaxy(const std::filesystem::path &path, SavedGalaxy &galaxy) {
    galaxy = SavedGalaxy{};
    std::vector<uint8_t> info;
    std::vector<uint8_t> sectors;
    std::vector<uint8_t> systems;
    std::vector<uint8_t> stellar;
    if (Error err =
            readFiles(path, {"galInfo", "sector.lst", "systInfo", "stellInfo"}, {&info, &sectors, &systems, &stellar}))
        return err;
    Error err = Error::success();
    if (info.size() < kGalInfoSize)
        err = Error::fail("galInfo is too small");
    if (!err)
        err = readSectors(sectors, galaxy);
    if (!err)
        err = readSystems(systems, galaxy);
    if (!err)
        err = readStellarObjects(stellar, galaxy);
    if (err)
        return Error::fail(path.string() + ": " + err.message);
    galaxy.size = s32(info, kGalaxySizeAt);
    // galInfo's counts and the sectors' indices must agree with the lists.
    if (s16(info, kSectorCountAt) != int(galaxy.sectors.size()) ||
        s16(info, kSystemCountAt) != int(galaxy.systems.size()) ||
        s16(info, kStellarCountAt) != int(galaxy.stellarObjects.size()))
        return Error::fail(path.string() + ": galInfo's counts don't match the galaxy's files");
    for (const SavedSector &s : galaxy.sectors)
        if (s.system >= int(galaxy.systems.size()) || s.stellarObject >= int(galaxy.stellarObjects.size()))
            return Error::fail(path.string() + ": a sector has a system or object that isn't there");
    return Error::success();
}

Error readSavedEmpires(const std::filesystem::path &path, std::array<SavedEmpire, 5> &empires) {
    empires = {};
    std::vector<uint8_t> info;
    if (Error err = readFiles(path, {"empsInfo"}, {&info}))
        return err;
    // A 444-byte entry an empire: its name and its leader's, then the
    // turn's figures as they are now (0x54) and as the turn began (0x98).
    constexpr size_t kEmpireSize = 444;
    if (info.size() < empires.size() * kEmpireSize)
        return Error::fail(path.string() + ": empsInfo is too small");
    for (size_t e = 0; e < empires.size(); ++e) {
        const size_t at = e * kEmpireSize;
        SavedEmpire &m = empires[e];
        m.name = field(info, at, 40);
        m.leader = field(info, at + 40, 40);
        m.credits = s32(info, at + 0x54);
        m.creditsPerTurn = s32(info, at + 0x58);
        m.dilithiumPerTurn = s32(info, at + 0x60);
        m.researchPoints = s32(info, at + 0x68);
        m.population = s32(info, at + 0x6C);
    }
    return Error::success();
}

Error readSavedShips(const std::filesystem::path &path, std::vector<SavedShip> &ships) {
    ships.clear();
    std::vector<uint8_t> head;
    std::vector<uint8_t> list;
    if (Error err = readFiles(path, {"GShipHead", "GShipList"}, {&head, &list}))
        return err;
    // The head as the task forces' (GWTForceHd).
    if (head.size() < 0x1C || s32(head, 0x10) != int(kShipSize) || s32(head, 0x18) < 0 ||
        size_t(s32(head, 0x18)) * kShipSize > list.size())
        return Error::fail(path.string() + ": GShipHead doesn't fit GShipList");
    for (int i = 0; i < s32(head, 0x18); ++i) {
        const size_t at = size_t(i) * kShipSize;
        SavedShip s;
        s.name = field(list, at, 40);
        s.race = s16(list, at + 0x28);
        s.id = s16(list, at + 0x2A);
        s.shields = s32(list, at + 0x38);
        s.hull = s32(list, at + 0x3C);
        s.experience = s32(list, at + 0x40);
        s.taskForce = s16(list, at + 0x50);
        s.shipClass = s32(list, at + 0x52);
        ships.push_back(std::move(s));
    }
    return Error::success();
}

Error readSavedTaskForces(const std::filesystem::path &path, int width, std::vector<SavedTaskForce> &taskForces) {
    taskForces.clear();
    std::vector<uint8_t> head;
    std::vector<uint8_t> list;
    if (Error err = readFiles(path, {"GWTForceHd", "GWTForce"}, {&head, &list}))
        return err;
    // The head gives each entry's size (0x10) and how many are in use
    // (0x18), at the start of the list's room for more.
    if (head.size() < 0x1C || s32(head, 0x10) != int(kTaskForceSize) || s32(head, 0x18) < 0 ||
        size_t(s32(head, 0x18)) * kTaskForceSize > list.size())
        return Error::fail(path.string() + ": GWTForceHd doesn't fit GWTForce");
    for (int i = 0; i < s32(head, 0x18); ++i) {
        const size_t at = size_t(i) * kTaskForceSize;
        SavedTaskForce t;
        t.id = s16(list, at);
        t.ships = s32(list, at + 0x04);
        // Room for nine ships' numbers.
        for (int k = 0; k < std::min(t.ships, 9); ++k)
            t.shipIds.push_back(s32(list, at + 0x08 + 4 * size_t(k)));
        t.mission = s32(list, at + 0x2C);
        // The destination is a sector's number, row by row.
        if (const int target = s32(list, at + 0x30); target >= 0 && width > 0) {
            t.destinationX = target % width;
            t.destinationY = target / width;
        }
        t.owner = s32(list, at + 0x34);
        t.y = s32(list, at + 0x38);
        t.x = s32(list, at + 0x3C);
        taskForces.push_back(t);
    }
    return Error::success();
}

Error readSavedResearch(const std::filesystem::path &path, SavedResearch &research) {
    research = {};
    std::vector<uint8_t> info;
    if (Error err = readFiles(path, {"techinfo"}, {&info}))
        return err;
    // A count and an index of every race, then the five empires' 20-byte
    // entries, their shares 6 bytes in.
    constexpr size_t kFirst = 2 + 2 * kRaceCount;
    constexpr size_t kEntrySize = 20;
    if (info.size() < kFirst + research.size() * kEntrySize)
        return Error::fail(path.string() + ": techinfo is too small");
    for (size_t e = 0; e < research.size(); ++e)
        for (size_t f = 0; f < research[e].size(); ++f)
            research[e][f] = s16(info, kFirst + e * kEntrySize + 6 + 2 * f);
    return Error::success();
}

Error readSavedOrders(const std::filesystem::path &path, std::vector<SavedOrder> &orders) {
    orders.clear();
    std::vector<uint8_t> d;
    if (Error err = readFiles(path, {"ordInfo"}, {&d}))
        return err;
    // Each order: a 32-byte head (its id, turn, race, type and how long
    // the rest is), then the rest, as its type lays it out.
    constexpr size_t kHeadSize = 32;
    for (size_t at = 0; at < d.size();) {
        const int32_t size = d.size() - at >= kHeadSize ? s32(d, at + 0x1C) : -1;
        if (size < 0 || d.size() - at - kHeadSize < size_t(size))
            return Error::fail(path.string() + ": ordInfo ends in an order");
        SavedOrder o;
        o.id = s32(d, at);
        o.turn = s32(d, at + 4);
        o.race = s16(d, at + 8);
        o.type = s32(d, at + 0x10);
        const size_t c = at + kHeadSize;
        auto has = [&](size_t bytes) { return size_t(size) >= bytes; };
        switch (SavedOrder::Type(o.type)) {
        case SavedOrder::Type::kEconomy:
            if (has(24)) {
                o.buy = s32(d, c);
                o.system = s16(d, c + 4);
                // 0x7FFF for none.
                o.building = s16(d, c + 8) == 0x7FFF ? -1 : s16(d, c + 8);
                o.buildOrder = s32(d, c + 16);
                o.ship = s16(d, c + 20);
            }
            break;
        case SavedOrder::Type::kResearch:
            for (size_t i = 0; i < o.values.size() && has(2 * i + 2); ++i)
                o.values[i] = s16(d, c + 2 * i);
            break;
        case SavedOrder::Type::kMilitary:
            if (has(30)) {
                o.destinationY = s32(d, c + 4);
                o.destinationX = s32(d, c + 8);
                o.mission = s32(d, c + 20);
                o.taskForce = s16(d, c + 28);
            }
            break;
        case SavedOrder::Type::kLabor:
            if (has(24)) {
                o.system = s32(d, c);
                for (size_t i = 0; i < 5; ++i)
                    o.values[i] = s32(d, c + 4 + 4 * i);
            }
            break;
        default:
            break;
        }
        orders.push_back(o);
        at = c + size_t(size);
    }
    return Error::success();
}

} // namespace reluva::botf
