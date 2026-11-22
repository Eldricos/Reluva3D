// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/save_writer.h"

#include "common/archive.h"
#include "common/bytes.h"
#include "common/text.h"
#include "engines/botf/formats/save_layout.h"

#include <algorithm>
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
    // The game ends each name with a line break, which the reader drops.
    std::string name = field(d, p, 40);
    if (!name.empty() && name.back() == '\n')
        name.pop_back();
    if (name != planet.name)
        putText(d, p, 40, planet.name + "\n");
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
    put32(d, at + 0x54, m.credits);
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

// A list's head, as GShipHead and GWTForceHd are: the entry's size at
// 0x10, how many are in use at 0x18.
Error listInUse(const std::vector<uint8_t> *head, const std::vector<uint8_t> *list, size_t entrySize, size_t count,
                const char *name) {
    if (!head || !list)
        return missing(name);
    if (head->size() < 0x1C || s32(*head, 0x10) != int(entrySize) || s32(*head, 0x18) != int(count) ||
        list->size() < count * entrySize)
        return Error::fail(std::string(name) + "'s entries aren't the game's");
    return Error::success();
}

Error writeShips(const std::vector<SavedShip> &ships, SaveFiles &save) {
    std::vector<uint8_t> *list = save.file("GShipList");
    if (Error err = listInUse(save.file("GShipHead"), list, kShipSize, ships.size(), "GShipList"))
        return err;
    for (size_t i = 0; i < ships.size(); ++i) {
        const SavedShip &s = ships[i];
        const size_t at = i * kShipSize;
        putText(*list, at, 40, s.name);
        put16(*list, at + 0x28, s.race);
        put16(*list, at + 0x2A, s.id);
        put32(*list, at + 0x38, s.shields);
        put32(*list, at + 0x3C, s.hull);
        put32(*list, at + 0x40, s.experience);
        put16(*list, at + 0x50, s.taskForce);
        put32(*list, at + 0x52, s.shipClass);
    }
    return Error::success();
}

Error writeTaskForces(const std::vector<SavedTaskForce> &taskForces, int width, SaveFiles &save) {
    std::vector<uint8_t> *list = save.file("GWTForce");
    if (Error err = listInUse(save.file("GWTForceHd"), list, kTaskForceSize, taskForces.size(), "GWTForce"))
        return err;
    // Room for nine ships' numbers.
    constexpr size_t kMaxShips = 9;
    for (size_t i = 0; i < taskForces.size(); ++i) {
        const SavedTaskForce &t = taskForces[i];
        const size_t at = i * kTaskForceSize;
        if (t.shipIds.size() > kMaxShips)
            return Error::fail("a task force has more ships than GWTForce has room for");
        put16(*list, at, t.id);
        put32(*list, at + 0x04, t.ships);
        for (size_t k = 0; k < t.shipIds.size(); ++k)
            put32(*list, at + 0x08 + 4 * k, t.shipIds[k]);
        put32(*list, at + 0x2C, t.mission);
        // A sector's number, row by row; any number below 0 is none.
        const int target = t.destinationX < 0 || t.destinationY < 0 ? -1 : t.destinationY * width + t.destinationX;
        if (std::max(s32(*list, at + 0x30), -1) != target)
            put32(*list, at + 0x30, target);
        put32(*list, at + 0x34, t.owner);
        put32(*list, at + 0x38, t.y);
        put32(*list, at + 0x3C, t.x);
    }
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
        err = writeTreaties(game.treaties, save);
    if (!err)
        err = writeRaces(game.races, save);
    return err;
}

} // namespace reluva::botf
