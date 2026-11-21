// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/save_game.h"

#include "common/bytes.h"
#include "common/text.h"
#include "engines/botf/formats/alt_res.h"

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
Error readFile(const AltResArchive &save, const char *name, std::vector<uint8_t> &out) {
    for (const AltResArchive::Entry &e : save.entries())
        if (normalizeArchiveName(e.name) == normalizeArchiveName(name))
            return save.read(e, out, kTextWindowFill);
    return Error::fail(std::string("no ") + name);
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
        s.mapX = s32(d, at + 0x78);
        s.mapY = s32(d, at + 0x7C);
        const int tradeRoutes = d[at + 0x308] + d[at + 0x309];
        if (s32(d, at) != int(g.systems.size()) || planets < 0 || specials < 0)
            return Error::fail("systInfo's system " + std::to_string(g.systems.size()) + " isn't one");
        const size_t size = kSystemSize + size_t(planets) * (kPlanetSize + 4) + size_t(specials) * kSpecialSize +
                            size_t(tradeRoutes) * 4 + (s.control == 2 ? kProductionQueueSize : 0);
        if (d.size() - at < size)
            return Error::fail("systInfo ends in a system");
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
    AltResArchive save;
    if (Error err = save.open(path))
        return err;
    galaxy = SavedGalaxy{};
    std::vector<uint8_t> info;
    std::vector<uint8_t> sectors;
    std::vector<uint8_t> systems;
    std::vector<uint8_t> stellar;
    Error err = readFile(save, "galInfo", info);
    if (!err)
        err = readFile(save, "sector.lst", sectors);
    if (!err)
        err = readFile(save, "systInfo", systems);
    if (!err)
        err = readFile(save, "stellInfo", stellar);
    if (!err && info.size() < kGalInfoSize)
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

} // namespace reluva::botf
