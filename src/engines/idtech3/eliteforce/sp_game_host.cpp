// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "common/md4.h"
#include "emulation/x86/format.h"
#include "engines/idtech3/eliteforce/game_layout.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/formats/entities.h"

#include "common/detection.h"
#include "common/md5.h"
#include "common/archive.h"
#include "common/fs.h"
#include "engines/idtech3/vm/vm_services.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <istream>
#include <sstream>

namespace reluva::idtech3::eliteforce {

namespace {

// The engine's functions in the table GetGameAPI is handed
// (docs/engines/idtech3/games/stvef/systems/sp-interface.md).
enum Import : int {
    kPrintf = 0,
    kWriteCam = 1,
    kError = 2,
    kMilliseconds = 3,
    kCvar = 4,
    kCvarSet = 5,
    kCvarVariableIntegerValue = 6,
    kCvarVariableStringBuffer = 7,
    kArgc = 8,
    kArgv = 9,
    kFsFOpenFile = 10,
    kFsRead = 11,
    kFsWrite = 12,
    kFsFCloseFile = 13,
    kFsReadFile = 14,
    kFsFreeFile = 15,
    kAppendToSaveGame = 16,
    kReadFromSaveGame = 17,
    kSendConsoleCommand = 18,
    kDropClient = 19,
    kSendServerCommand = 20,
    kSetConfigstring = 21,
    kGetConfigstring = 22,
    kGetUserinfo = 23,
    kSetUserinfo = 24,
    kGetServerinfo = 25,
    kSetBrushModel = 26,
    kTrace = 27,
    kPointContents = 28,
    kInPvs = 29,
    kInPvsIgnorePortals = 30,
    kAdjustAreaPortalState = 31,
    kAreasConnected = 32,
    kLinkEntity = 33,
    kUnlinkEntity = 34,
    kEntitiesInBox = 35,
    kEntityContact = 36,
    kVoiceVolume = 37,
    kMalloc = 38,
    kFree = 39,
    kImportCount = 40,
    // The Expansion Pack's engine's two more, by their places in its table
    // (the rest move along: 16 and 17 by one, 18 on by two).
    kXpFsGetFileList = 16,
    kXpReadFromSaveGameOptional = 19,
    kXpImportCount = 42,
};

constexpr int32_t kMaxEntities = 1024;
constexpr uint32_t kCvarStructureSize = 44;
constexpr uint32_t kArgvSize = 1024;

uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

SpGameHost::SpGameHost(const SearchSet &files, CvarStore &cvars)
    : _files(files), _cvars(cvars), _serverWorld([this](int32_t n, EntityShape &out) { return entityShape(n, out); }),
      _openFiles(files) {
}

bool SpGameHost::entityShape(int32_t n, EntityShape &out) const {
    const uint32_t a = entityAddress(n);
    if (!a || !_cpu->valid(a, uint32_t(kEntity.owner + 4)))
        return false;
    out.origin = readVec(a + kEntity.currentOrigin);
    out.angles = readVec(a + kEntity.currentAngles);
    out.mins = readVec(a + kEntity.mins);
    out.maxs = readVec(a + kEntity.maxs);
    out.contents = int32_t(_cpu->read32(a + kEntity.contents));
    const uint32_t owner = _cpu->read32(a + kEntity.owner);
    out.owner = owner ? entityNumber(owner) : -1;
    out.bmodel = _cpu->read32(a + kEntity.bmodel) != 0;
    out.modelIndex = int32_t(_cpu->read32(a + kEntity.modelIndex));
    return true;
}

SpGameHost::~SpGameHost() = default;

int SpGameHost::importPosition(int slot) const {
    if (!_expansion || slot < kAppendToSaveGame)
        return slot;
    return slot < kSendConsoleCommand ? slot + 1 : slot + 2;
}

void SpGameHost::import(int slot, const std::function<uint32_t(x86::Cpu &)> &fn) {
    // cdecl: the caller pops the arguments.
    const uint32_t address = _cpu->addNative("engine[" + std::to_string(slot) + "]", [this, slot, fn](x86::Cpu &c) {
        _callCounts.add(slot);
        if (trace)
            trace(slot, c.read32(c.reg[x86::Cpu::ESP]));
        c.reg[x86::Cpu::EAX] = fn(c);
        return 0u;
    });
    _cpu->write32(_imports + 4 * uint32_t(importPosition(slot)), address);
}

uint32_t SpGameHost::text(const std::string &s) {
    const uint32_t a = _process->memory().alloc(uint32_t(s.size()) + 1);
    if (a)
        _cpu->writeString(a, s, uint32_t(s.size()) + 1);
    _levelText.push_back(a);
    return a;
}

Error SpGameHost::load(const std::vector<uint8_t> &dll) {
    _cpu = std::make_unique<x86::Cpu>(kMemory);
    _process = std::make_unique<x86::Win32Process>(*_cpu, kExeName);
    _process->output = [this](const std::string &s) {
        if (print)
            print(s);
    };
    if (clock)
        _process->milliseconds = clock;
    _startMs = uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    // The Expansion Pack's (its own efgamex86.dll, 999,500 bytes) expects
    // its engine's longer table.
    {
        Md5 md5;
        md5.update(dll.data(), std::min<size_t>(dll.size(), kDetectionHashBytes));
        _expansion = dll.size() == 999500 && md5.hexDigest() == "f8f997ef96a09755f0d1c920c15ca773";
    }
    if (Error e = _process->load("efgamex86.dll", dll, _module))
        return e;
    if (Error e = _process->attach(*_module))
        return e;
    _imports = _process->memory().alloc(4 * (_expansion ? kXpImportCount : kImportCount));
    _voiceVolume = _process->memory().alloc(4 * kMaxEntities);
    _argvBuffer = _process->memory().alloc(kArgvSize);
    addImports();
    // The client game's half: dllEntry hands it the system call function.
    // The server game calls into it too, so it's set up from the start.
    if (const uint32_t dllEntry = _module->exportAddress("dllEntry")) {
        const uint32_t syscall = _cpu->addNative("cgame syscall", [this](x86::Cpu &c) {
            c.reg[x86::Cpu::EAX] = cgameCall(c);
            return 0u;
        });
        uint32_t ignored = 0;
        if (Error e = _cpu->call(dllEntry, {syscall}, ignored))
            return Error::fail("dllEntry: " + e.message);
    }
    const uint32_t getGameApi = _module->exportAddress("GetGameAPI");
    if (!getGameApi)
        return Error::fail("efgamex86.dll has no GetGameAPI");
    uint32_t result = 0;
    if (Error e = _cpu->call(getGameApi, {_imports}, result))
        return Error::fail("GetGameAPI: " + e.message);
    if (!_cpu->valid(result, 17 * 4))
        return Error::fail("GetGameAPI gave no table");
    _exports = result;
    const int32_t version = int32_t(_cpu->read32(_exports));
    if (version != kApiVersion)
        return Error::fail("The game is version " + std::to_string(version) + ", not " + std::to_string(kApiVersion));
    return Error::success();
}

Error SpGameHost::callGame(int exportIndex, const std::vector<uint32_t> &args, uint32_t *result) {
    syncCvars();
    uint32_t r = 0;
    const uint32_t fn = _cpu->read32(_exports + 4 * uint32_t(exportIndex));
    if (Error e = _cpu->call(fn, args, r))
        return e;
    if (result)
        *result = r;
    return Error::success();
}

int32_t SpGameHost::mapChecksum(const std::vector<uint8_t> &file) {
    const std::array<uint8_t, 16> digest = md4(file.data(), file.size());
    uint32_t sum = 0;
    for (int i = 0; i < 4; ++i)
        sum ^= uint32_t(digest[size_t(4 * i)]) | uint32_t(digest[size_t(4 * i + 1)]) << 8 |
               uint32_t(digest[size_t(4 * i + 2)]) << 16 | uint32_t(digest[size_t(4 * i + 3)]) << 24;
    return int32_t(sum);
}

Error SpGameHost::startLevel(const std::string &mapName, std::shared_ptr<const Bsp> map, int32_t checksum,
                             int32_t randomSeed, int32_t loadMode) {
    _cvars.applyLatched(); // as the original's server, before the level starts
    if (!map)
        return Error::fail("No map");
    if (!_exports)
        return Error::fail("The game isn't loaded");
    _map = std::move(map);
    _world = std::make_unique<CollisionWorld>(*_map);
    {
        // The light grid, for LightForPoint (the worldspawn's "gridsize").
        float cell[3] = {64, 64, 128};
        std::vector<Entity> entities;
        if (!parseEntities(_map->entities, entities) && !entities.empty())
            entities[0].vector("gridsize", cell);
        _lightGrid = LightGrid{};
        _lightGrid.build(*_map, {cell[0], cell[1], cell[2]});
    }
    _marks.reset();
    _areaPortals = std::make_unique<AreaPortals>(_world->areaCount());
    _serverWorld.setLevel(_map.get(), _world.get(), _areaPortals.get(),
                          [this] { return _cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas"); });
    for (uint32_t a : _levelText)
        _process->memory().free(a);
    _levelText.clear();
    _cvars.create("mapname", "", CvarStore::kServerInfo | CvarStore::kReadOnly);
    _cvars.set("mapname", mapName, true);
    _cvars.create("sv_mapChecksum", "", CvarStore::kReadOnly);
    _cvars.set("sv_mapChecksum", std::to_string(checksum), true);
    _cvars.create("sv_maxclients", "1", CvarStore::kServerInfo | CvarStore::kLatch);
    _cvars.create("g_gametype", "0", CvarStore::kServerInfo | CvarStore::kLatch);
    _cvars.create("sv_cheats", "1", CvarStore::kServerInfo | CvarStore::kReadOnly);
    // The original's server starts a level's clock at 1000 ms, not 0: what
    // the game schedules for "now plus a fraction" at spawn (a ref_tag
    // linking itself into the camera's tags) would otherwise be due at 0,
    // which is never.
    _levelTime = kStartTimeMs;
    const uint32_t now = clock ? clock()
                               : uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count() -
                                          _startMs);
    // Init(mapname, checksum, entity text, level time, random seed, global
    // time, a saved game just loaded: no).
    // The Expansion Pack's (as Jedi Outcast's): the spawn target after the
    // map's name, and whether this is a level change last.
    const std::vector<uint32_t> args =
        _expansion ? std::vector<uint32_t>{text(mapName),
                                           text(spawnTarget),
                                           uint32_t(checksum),
                                           text(_map->entities),
                                           uint32_t(_levelTime),
                                           uint32_t(randomSeed),
                                           now,
                                           uint32_t(loadMode),
                                           loadTransition ? 1u : 0u}
                   : std::vector<uint32_t>{text(mapName),        uint32_t(checksum),   text(_map->entities),
                                           uint32_t(_levelTime), uint32_t(randomSeed), now,
                                           uint32_t(loadMode)};
    if (Error e = callGame(kInit, args))
        return e;
    // Frames to let the level settle before anyone joins, as the original's
    // server runs them (three, then one more, 100 ms apart): the NPCs spawn
    // in them, so they're there when the player's arrival starts the
    // level's scripts, which find the entities they affect as they load.
    for (int i = 0; i < 4; ++i) {
        if (Error e = callGame(kRunFrame, {uint32_t(_levelTime)}))
            return e;
        _levelTime += kSettleFrameMs;
    }
    // The server's and the system's settings, for the client game (as the
    // original's server sets them once the game is loaded).
    _configstrings[0] = infoString(CvarStore::kServerInfo);
    _configstrings[1] = infoString(CvarStore::kSystemInfo);
    return Error::success();
}

Error SpGameHost::connectPlayer(const std::string &userinfo, std::string &refused, int32_t loadMode) {
    _userinfo[0] = userinfo;
    uint32_t reason = 0;
    // ClientConnect(client, first time, saved game loaded)
    if (Error e = callGame(kClientConnect, {0, 1, uint32_t(loadMode)}, &reason))
        return e;
    if (reason) {
        refused = _cpu->string(reason, 1024);
        return Error::success();
    }
    // ClientBegin(client, usercmd, saved game loaded): an empty command.
    const uint32_t cmd = _process->memory().alloc(64);
    _cpu->write32(cmd, uint32_t(_levelTime));
    const Error e = callGame(kClientBegin, {0, cmd, uint32_t(loadMode)});
    _process->memory().free(cmd);
    return e;
}

void SpGameHost::startVoice(int32_t entity, const std::string &name) {
    auto log = [&](const std::string &text) {
        if (voiceLog)
            voiceLog("[" + std::to_string(_levelTime) + " ms] " + text);
    };
    if (entity < 0 || entity >= kMaxEntities || name.empty()) {
        log("voice not counted: entity " + std::to_string(entity) + ", sound '" + name + "'");
        return;
    }
    auto sound = _soundLibrary.find(name);
    if (!sound || sound->rate <= 0 || sound->frames() == 0) {
        log("voice not counted: " + name + " couldn't be read (entity " + std::to_string(entity) + ")");
        return;
    }
    log("+" + std::to_string(entity) + " " + name + " " +
        std::to_string(int64_t(sound->frames()) * 1000 / sound->rate) + " ms");
    // Its peak, as the original's sound loading measures it: the largest
    // sample's high byte, the scale its thresholds are taken against.
    int peak = 0;
    for (int16_t v : sound->samples)
        peak = std::max(peak, std::abs(v >> 8));
    _voices[entity] = {sound, _levelTime, float(peak)};
    // Speaking from now, as the original marks it on starting (-1: started,
    // silent so far), so the game sees it in the same frame.
    if (_voiceVolume)
        _cpu->write32(_voiceVolume + 4 * uint32_t(entity), 0xffffffffu);
}

void SpGameHost::updateVoiceVolumes() {
    // As the original's mixer does each pass: every entity 0, then each one
    // speaking given how loud its voice is where it has got to (the mean
    // square of ten samples a hundred apart, their high bytes) against
    // s_threshold1-4 times the sound's peak: -1 quiet, then 5 to 8 (the
    // client game shows the head skin that many after its first, the
    // mouth's shapes). A line that has ended
    // leaves 0, which ends the scripts' wait for it.
    if (!_voiceVolume)
        return;
    std::memset(_cpu->memory().data() + _voiceVolume, 0, 4 * kMaxEntities);
    auto threshold = [&](const char *name, float fallback) {
        return _cvars.find(name) ? _cvars.value(name) : fallback;
    };
    const float t1 = threshold("s_threshold1", 0.5f);
    const float t2 = threshold("s_threshold2", 4.0f);
    const float t3 = threshold("s_threshold3", 7.0f);
    const float t4 = threshold("s_threshold4", 8.0f);
    for (auto it = _voices.begin(); it != _voices.end();) {
        const Voice &v = it->second;
        const size_t frames = v.sound->frames();
        const size_t at = size_t(int64_t(_levelTime - v.startMs) * v.sound->rate / 1000);
        if (at >= frames) {
            if (voiceLog)
                voiceLog("[" + std::to_string(_levelTime) + " ms] -" + std::to_string(it->first));
            it = _voices.erase(it);
            continue;
        }
        int sum = 0;
        int n = 0;
        for (size_t i = at; i < frames && n < 10; i += 100, ++n) {
            const int s = v.sound->samples[i * size_t(v.sound->channels)] >> 8;
            sum += s * s;
        }
        const float level = float(sum / std::max(n, 1));
        int32_t value = 8;
        if (t1 * v.average > level)
            value = -1;
        else if (t2 * v.average > level)
            value = 5;
        else if (t3 * v.average > level)
            value = 6;
        else if (t4 * v.average > level)
            value = 7;
        _cpu->write32(_voiceVolume + 4 * uint32_t(it->first), uint32_t(value));
        ++it;
    }
}

Error SpGameHost::runFrame(bool snapshot) {
    _levelTime += kFrameMs;
    updateVoiceVolumes();
    if (Error e = callGame(kRunFrame, {uint32_t(_levelTime)}))
        return e;
    if (snapshot)
        sendSnapshot();
    return Error::success();
}

Error SpGameHost::writeLevel(bool autosave) {
    return callGame(kWriteLevel, {autosave ? 1u : 0u});
}

Error SpGameHost::readLevel(bool autosave) {
    // (The Expansion Pack's: and whether it's a level change's.)
    if (_expansion)
        return callGame(kReadLevel, {autosave ? 1u : 0u, loadTransition ? 1u : 0u});
    return callGame(kReadLevel, {autosave ? 1u : 0u});
}

void SpGameHost::replaceConfigstrings(const std::map<int32_t, std::string> &strings) {
    _configstrings = strings;
}

uint32_t SpGameHost::playerState() const {
    // Client 0's: its entity's client pointer (the player state leads the
    // game's client structure).
    if (!entities())
        return 0;
    const uint32_t ps = _cpu->read32(entityAddress(0) + uint32_t(kEntity.client));
    return _cpu->valid(ps, kPlayerStateSize) ? ps : 0;
}

int32_t SpGameHost::playerHealth() const {
    const uint32_t ps = playerState();
    return ps ? int32_t(_cpu->read32(ps + kPsHealth)) : 0;
}

void SpGameHost::savePlayer() {
    // As the original's engine before a level change and an autosave: the
    // player's state into cvars the game reads back when the next level
    // starts (health, armour, weapons, the current weapon, its state, the
    // view angles; ammunition; the Borg's adaptations).
    _cvars.set("playersave", "");
    const uint32_t ps = playerState();
    if (!ps)
        return;
    auto i = [&](uint32_t at) { return int32_t(_cpu->read32(ps + at)); };
    auto f = [&](uint32_t at) { return double(readFloat(ps + at)); };
    char buf[256];
    std::snprintf(buf, sizeof buf, "%i %i %i %i %i %f %f %f", i(0xbc), i(0xc8), i(0xc4), i(0x94), i(0x98), f(0x9c),
                  f(0xa0), f(0xa4));
    _cvars.set("playersave", buf);
    for (uint32_t n = 0; n < 4; ++n)
        _cvars.set("playerammo" + std::to_string(n), std::to_string(i(0x17c + 4 * n)));
    for (uint32_t n = 0; n < 32; ++n)
        _cvars.set("borgadapt" + std::to_string(n), std::to_string(i(0x18c + 4 * n)));
}

uint32_t SpGameHost::allowedToSaveHere(uint32_t flag) {
    uint32_t result = 0;
    if (callGame(kGameAllowedToSaveHere, {flag}, &result))
        return 0;
    return result;
}

Error SpGameHost::consoleCommand(const std::string &text, bool *handled) {
    _args.clear();
    std::string word;
    bool quoted = false;
    for (char c : text) {
        if (c == '"')
            quoted = !quoted;
        else if (c == ' ' && !quoted) {
            if (!word.empty())
                _args.push_back(word);
            word.clear();
        } else
            word += c;
    }
    if (!word.empty())
        _args.push_back(word);
    uint32_t result = 0;
    const Error e = callGame(kConsoleCommand, {}, &result);
    if (handled)
        *handled = result != 0;
    return e;
}

// --- Entities ---------------------------------------------------------------------

uint32_t SpGameHost::entities() const {
    return _exports ? _cpu->read32(_exports + 14 * 4) : 0;
}
int32_t SpGameHost::entitySize() const {
    return _exports ? int32_t(_cpu->read32(_exports + 15 * 4)) : 0;
}
int32_t SpGameHost::entityCount() const {
    return _exports ? int32_t(_cpu->read32(_exports + 16 * 4)) : 0;
}

int32_t SpGameHost::entityNumber(uint32_t address) const {
    const uint32_t base = entities();
    const int32_t size = entitySize();
    if (!base || size <= 0 || address < base)
        return -1;
    const uint32_t offset = address - base;
    return offset % uint32_t(size) == 0 && offset / uint32_t(size) < uint32_t(kMaxEntities)
               ? int32_t(offset / uint32_t(size))
               : -1;
}

uint32_t SpGameHost::entityAddress(int32_t number) const {
    if (number < 0 || number >= kMaxEntities || !entities())
        return 0;
    return entities() + uint32_t(number) * uint32_t(entitySize());
}

float SpGameHost::readFloat(uint32_t address) const {
    const uint32_t u = _cpu->read32(address);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

Vec3f SpGameHost::readVec(uint32_t address) const {
    Vec3f v{};
    if (address)
        for (int i = 0; i < 3; ++i)
            v[size_t(i)] = readFloat(address + 4 * uint32_t(i));
    return v;
}

void SpGameHost::writeVec(uint32_t address, const Vec3f &v) {
    for (int i = 0; i < 3; ++i)
        _cpu->write32(address + 4 * uint32_t(i), floatBits(v[size_t(i)]));
}

int32_t SpGameHost::entityInt(int32_t number, int32_t offset) const {
    const uint32_t a = entityAddress(number);
    return a ? int32_t(_cpu->read32(a + uint32_t(offset))) : 0;
}

Vec3f SpGameHost::entityVec(int32_t number, int32_t offset) const {
    const uint32_t a = entityAddress(number);
    return a ? readVec(a + uint32_t(offset)) : Vec3f{};
}

void SpGameHost::linkEntity(uint32_t address) {
    const int32_t n = entityNumber(address);
    EntityShape shape;
    if (n < 0 || !entityShape(n, shape))
        return;
    Vec3f absMin{};
    Vec3f absMax{};
    _serverWorld.link(n, shape, absMin, absMax);
    writeVec(address + kEntity.absMin, absMin);
    writeVec(address + kEntity.absMax, absMax);
    _cpu->write32(address + kEntity.linked, 1);
    _cpu->write32(address + kEntity.solid, uint32_t(ServerWorld::packedSolid(shape, kSolidBrushModel)));
}

void SpGameHost::unlinkEntity(uint32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0)
        return;
    _serverWorld.unlink(n);
    _cpu->write32(address + kEntity.linked, 0);
}

ServerWorld::Placement SpGameHost::entityPlacement(int32_t n) const {
    // As linked; or, for one marked linked in a saved game but not linked
    // since, where its box says.
    if (const ServerWorld::Placement *p = _serverWorld.placement(n))
        return *p;
    return _serverWorld.place(entityVec(n, kEntity.absMin), entityVec(n, kEntity.absMax));
}

void SpGameHost::writeTrace(uint32_t r, const TraceResult &t, int32_t hit) {
    // Quake III's trace_t (56 bytes), as the original's engine fills it.
    if (!_cpu->valid(r, 56))
        return;
    _cpu->write32(r + 0, t.allSolid);
    _cpu->write32(r + 4, t.startSolid);
    _cpu->write32(r + 8, floatBits(t.fraction));
    writeVec(r + 12, t.endPos);
    writeVec(r + 24, t.normal);
    _cpu->write32(r + 36, floatBits(t.dist));
    int type = 3;
    int signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (t.normal[size_t(i)] == 1 || t.normal[size_t(i)] == -1)
            type = i;
        if (t.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    _cpu->write32(r + 40, uint32_t(type | signs << 8));
    _cpu->write32(r + 44, uint32_t(t.surfaceFlags));
    _cpu->write32(r + 48, uint32_t(t.contents));
    _cpu->write32(r + 52, uint32_t(hit));
}

std::string SpGameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar &cv : _cvars.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

// --- Cvars ------------------------------------------------------------------------

uint32_t SpGameHost::cvarStructure(const std::string &name) {
    const std::string key = lower(name);
    auto it = _cvarMirrors.find(key);
    if (it == _cvarMirrors.end()) {
        MirroredCvar m;
        m.address = _process->memory().alloc(kCvarStructureSize);
        const uint32_t nameText = _process->memory().alloc(uint32_t(name.size()) + 1);
        _cpu->writeString(nameText, name, uint32_t(name.size()) + 1);
        _cpu->write32(m.address, nameText);
        it = _cvarMirrors.emplace(key, m).first;
    }
    MirroredCvar &m = it->second;
    const CvarStore::Cvar *cv = _cvars.find(name);
    if (cv && cv->modificationCount != m.modificationCount) {
        if (m.text)
            _process->memory().free(m.text);
        m.text = _process->memory().alloc(uint32_t(cv->value.size()) + 1);
        _cpu->writeString(m.text, cv->value, uint32_t(cv->value.size()) + 1);
        _cpu->write32(m.address + 4, m.text);
        _cpu->write32(m.address + 16, uint32_t(cv->flags));
        if (m.modificationCount >= 0)
            _cpu->write32(m.address + 20, 1); // modified
        _cpu->write32(m.address + 24, uint32_t(cv->modificationCount));
        _cpu->write32(m.address + 28, floatBits(float(std::atof(cv->value.c_str()))));
        _cpu->write32(m.address + 32, uint32_t(std::atoi(cv->value.c_str())));
        m.modificationCount = cv->modificationCount;
    }
    return m.address;
}

void SpGameHost::syncCvars() {
    // (Done before every call into the game: only when something changed.)
    if (_cvars.changes() == _cvarChangesSynced)
        return;
    _cvarChangesSynced = _cvars.changes();
    for (auto &[key, m] : _cvarMirrors) {
        const CvarStore::Cvar *cv = _cvars.find(key);
        if (cv && cv->modificationCount != m.modificationCount)
            cvarStructure(cv->name);
    }
}

// --- Files (for both halves of the DLL) ---------------------------------------------

// FS_FOpenFile(path, &handle, mode): read from the game's search path; what
// it writes is kept only until closed, for now.
uint32_t SpGameHost::fsOpen(x86::Cpu &c, uint32_t pathAddress, uint32_t handleAddress, uint32_t mode) {
    uint32_t handle = 0;
    const int32_t length = _openFiles.open(c.string(pathAddress, 256), int32_t(mode), handleAddress != 0, true, handle);
    if (handleAddress)
        c.write32(handleAddress, handle);
    return uint32_t(length);
}

uint32_t SpGameHost::fsRead(x86::Cpu &c, uint32_t buffer, uint32_t length, uint32_t handle) {
    if (!c.valid(buffer, length))
        return 0;
    return uint32_t(_openFiles.read(handle, c.memory().data() + buffer, length));
}

uint32_t SpGameHost::fsWrite(x86::Cpu &c, uint32_t buffer, uint32_t length, uint32_t handle) {
    if (!c.valid(buffer, length))
        return 0;
    return uint32_t(_openFiles.write(handle, c.memory().data() + buffer, length));
}

// --- The client game's system calls -------------------------------------------------

void SpGameHost::writeVmCvar(uint32_t address, const CvarStore::Cvar &cv) {
    // vmCvar_t: handle, modification count, value, integer, text (256).
    if (!_cpu->valid(address, CvarStore::kVmCvarSize))
        return;
    _cpu->write32(address, uint32_t(cv.handle));
    _cpu->write32(address + 4, uint32_t(cv.modificationCount));
    _cpu->write32(address + 8, floatBits(std::strtof(cv.value.c_str(), nullptr)));
    _cpu->write32(address + 12, uint32_t(std::strtol(cv.value.c_str(), nullptr, 10)));
    _cpu->writeString(address + 16, cv.value, 256);
}

// NOLINTNEXTLINE(readability-make-member-function-const): starts sounds
void SpGameHost::playAmbient(const std::vector<raven::AmbientSets::Sound> &sounds) {
    if (!soundSystem)
        return;
    for (const raven::AmbientSets::Sound &s : sounds)
        if (s.loop)
            soundSystem->addAmbientLoop(s.entity, s.origin, s.volume, s.name);
        else
            soundSystem->startAmbientSound(s.entity, s.origin, s.volume, s.name);
}

int32_t SpGameHost::registerSkin(const std::string &name) {
    // As Elite Force's renderer: name.skin, then name-1.skin to name-8.skin
    // (a head's faces: blinking, the mouth's shapes) in the handles after
    // it; the handle negated when there are any. The client game shows a
    // face by adding to the first's handle.
    const int32_t handle = _skins.add(name);
    if (handle <= 0)
        return handle;
    auto [it, added] = _skinFaces.try_emplace(handle, 0);
    const std::string lowerName = lower(name);
    if (added && lowerName.size() > 5 && lowerName.compare(lowerName.size() - 5, 5, ".skin") == 0) {
        const std::string base = name.substr(0, name.size() - 5);
        for (int i = 1; i <= 8; ++i) {
            const std::string face = base + "-" + std::to_string(i) + ".skin";
            if (!_files.find(face))
                break;
            if (_skins.add(face) != handle + i)
                break; // not next to it (registered before): can't be shown by number
            ++it->second;
        }
    }
    return it->second ? -handle : handle;
}

uint32_t SpGameHost::cgameCall(x86::Cpu &c) {
    // syscall(number, args...): the arguments after the number.
    auto a = [&c](int n) { return c.arg(n + 1); };
    auto str = [&](int n, size_t max = 1024) { return c.string(a(n), max); };
    const int number = int(c.arg(0));
    _cgameCallCounts.add(number);
    switch (number) {
    case 0: // Print(text)
        if (print)
            print(str(0, 4096));
        return 0;
    case 1: // Error(text)
        c.abort("efgamex86.dll (client game): " + str(0, 4096));
        return 0;
    case 2:
        return _process->milliseconds();
    case 3: { // Cvar_Register(vmCvar, name, value, flags)
        const CvarStore::Cvar &cv = _cvars.create(str(1, 256), str(2, 256), int32_t(a(3)));
        if (a(0))
            writeVmCvar(a(0), cv);
        return 0;
    }
    case 4: // Cvar_Update(vmCvar)
        if (const CvarStore::Cvar *cv = _cvars.byHandle(int32_t(c.read32(a(0))));
            cv && int32_t(c.read32(a(0) + 4)) != cv->modificationCount)
            writeVmCvar(a(0), *cv);
        return 0;
    case 5: { // Cvar_Set(name, value)
        const std::string name = str(0, 256);
        _cvars.set(name, str(1, 1024), true); // as the original's: read-only ones too
        if (_cvarMirrors.count(lower(name)))
            cvarStructure(name);
        return 0;
    }
    case 6:
        return uint32_t(_args.size()); // Argc()
    case 7:                            // Argv(n, buffer, size)
        c.writeString(a(1), a(0) < _args.size() ? _args[a(0)] : "", a(2));
        return 0;
    case 8: { // Args(buffer, size)
        std::string all;
        for (size_t i = 1; i < _args.size(); ++i)
            all += (i > 1 ? " " : "") + _args[i];
        c.writeString(a(0), all, a(1));
        return 0;
    }
    case 9:
        return fsOpen(c, a(0), a(1), a(2));
    case 10:
        return fsRead(c, a(0), a(1), a(2));
    case 11:
        return fsWrite(c, a(0), a(1), a(2));
    case 12:
        _openFiles.close(a(0));
        return 0;
    case 13:
        _consoleCommands.push_back(str(0, 4096));
        return 0; // SendConsoleCommand
    case 14:
        _clientCommands.push_back(str(0, 256));
        return 0; // AddCommand(name)
    case 15: {    // SendClientCommand(text): to the server game, as ClientCommand
        tokenize(str(0, 4096));
        uint32_t ignored = 0;
        const uint32_t fn = c.read32(_exports + 4 * kClientCommand);
        if (Error e = c.call(fn, {0}, ignored))
            c.abort(e.message);
        return 0;
    }
    case 16: // UpdateScreen(): while loading, the client game's loading screen shown
        if (updateScreen && !_inUpdateScreen) {
            _inUpdateScreen = true;
            updateScreen();
            _inUpdateScreen = false;
        }
        return 0;
    // Collision, for the client game's prediction: the same map.
    case 17:
        return 0; // CM_LoadMap(name)
    case 18:
        return _world ? uint32_t(_world->modelCount()) : 0; // CM_NumInlineModels()
    case 19:
        return a(0); // CM_InlineModel(n)
    case 20:         // CM_TempBoxModel(mins, maxs)
        _tempBoxes.assign(1, {readVec(a(0)), readVec(a(1))});
        return kTempBoxModel;
    case 21:   // CM_PointContents(point, model)
    case 22: { // CM_TransformedPointContents(point, model, origin, angles)
        const Vec3f point = readVec(a(0));
        const Vec3f origin = number == 22 ? readVec(a(2)) : Vec3f{};
        const Vec3f angles = number == 22 ? readVec(a(3)) : Vec3f{};
        Vec3f p = point;
        for (int i = 0; i < 3; ++i)
            p[size_t(i)] -= origin[size_t(i)];
        if (a(1) == kTempBoxModel) {
            if (_tempBoxes.empty())
                return 0;
            for (int i = 0; i < 3; ++i)
                if (p[size_t(i)] < _tempBoxes[0].first[size_t(i)] || p[size_t(i)] > _tempBoxes[0].second[size_t(i)])
                    return 0;
            return 1;
        }
        return _world ? uint32_t(_world->transformedPointContents(point, int(a(1)), origin, angles)) : 0;
    }
    case 23:
        clientTrace(c, false);
        return 0; // CM_BoxTrace
    case 24:
        clientTrace(c, true);
        return 0; // CM_TransformedBoxTrace
    case 25: { // CM_MarkFragments(numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer)
        // -> fragments: the pieces of the level under a mark (scorches,
        // bullet holes), each (first point, point count) into the points.
        const uint32_t count = a(0);
        if (!_map || count < 3 || count > 64 || !c.valid(a(1), count * 12) || !c.valid(a(2), 12))
            return 0;
        if (!_marks)
            _marks = std::make_unique<MarkSurfaces>(*_map, 4.0f);
        std::vector<Vec3f> polygon;
        for (uint32_t i = 0; i < count; ++i)
            polygon.push_back(readVec(a(1) + 12 * i));
        std::vector<Vec3f> points;
        std::vector<MarkSurfaces::Fragment> fragments;
        _marks->mark(polygon, readVec(a(2)), std::min<uint32_t>(a(3), 4096), std::min<uint32_t>(a(5), 1024), points,
                     fragments);
        if (!c.valid(a(4), uint32_t(points.size()) * 12) || !c.valid(a(6), uint32_t(fragments.size()) * 8))
            return 0;
        for (size_t i = 0; i < points.size(); ++i)
            writeVec(a(4) + uint32_t(i) * 12, points[i]);
        for (size_t i = 0; i < fragments.size(); ++i) {
            c.write32(a(6) + uint32_t(i) * 8, uint32_t(fragments[i].firstPoint));
            c.write32(a(6) + uint32_t(i) * 8 + 4, uint32_t(fragments[i].numPoints));
        }
        return uint32_t(fragments.size());
    }
    // Sound, played if there's somewhere to play it.
    case 26: // S_StartSound(origin or 0, entity, channel, sfx)
        if (voiceLog && (a(2) == 3 || a(2) == 4 || _sounds.name(int32_t(a(3))).find("voice") != std::string::npos))
            voiceLog("[" + std::to_string(_levelTime) + " ms] sound on entity " + std::to_string(int32_t(a(1))) +
                     ", channel " + std::to_string(int32_t(a(2))) + ": " + _sounds.name(int32_t(a(3))));
        if (a(2) == 3 || a(2) == 4)
            startVoice(int32_t(a(1)), _sounds.name(int32_t(a(3))));
        if (soundSystem)
            soundSystem->startSound(a(0) ? std::optional<Vec3f>(readVec(a(0))) : std::nullopt, int(a(1)), int(a(2)),
                                    _sounds.name(int32_t(a(3))));
        return 0;
    case 27: // S_StartLocalSound(sfx, channel)
        if (voiceLog && (a(1) == 3 || a(1) == 4 || _sounds.name(int32_t(a(0))).find("voice") != std::string::npos))
            voiceLog("[" + std::to_string(_levelTime) + " ms] local sound, channel " + std::to_string(int32_t(a(1))) +
                     ": " + _sounds.name(int32_t(a(0))));
        if (soundSystem)
            soundSystem->startLocalSound(_sounds.name(int32_t(a(0))), int(a(1)));
        return 0;
    case 28: // S_ClearLoopingSounds()
        if (soundSystem)
            soundSystem->clearLoopingSounds();
        return 0;
    case 29: // S_AddLoopingSound(entity, origin, velocity, sfx)
        if (soundSystem)
            soundSystem->addLoopingSound(int(a(0)), readVec(a(1)), _sounds.name(int32_t(a(3))));
        return 0;
    case 30: // S_UpdateEntityPosition(entity, origin)
        if (soundSystem)
            soundSystem->updateEntityPosition(int(a(0)), readVec(a(1)));
        return 0;
    case 31: // S_Respatialize(entity, origin, axis[3], inWater)
        if (soundSystem)
            soundSystem->respatialize(int(a(0)), readVec(a(1)),
                                      {readVec(a(2)), readVec(a(2) + 12), readVec(a(2) + 24)});
        return 0;
    case 32:
        return uint32_t(_sounds.add(str(0, 256))); // S_RegisterSound(name)
    case 33:                                       // S_StartBackgroundTrack(intro, loop)
        if (soundSystem)
            soundSystem->startBackgroundTrack(str(0, 256), str(1, 256));
        return 0;
    case 34:
    case 35:
    case 36:
    case 37:
        return 0; // force feedback: none
    // The renderer: what's registered and drawn is recorded.
    case 38:
        return 0; // R_LoadWorldMap(name)
    case 39:
        return uint32_t(_models.add(str(0, 256)));
    case 40:
        return uint32_t(registerSkin(str(0, 256))); // R_RegisterSkin(name)
    case 41:
    case 42: { // R_RegisterShader, R_RegisterShaderNoMip: 0 if it can't be found
        const std::string name = str(0, 256);
        return shaderExists && !shaderExists(name) ? 0 : uint32_t(_shaders.add(name));
    }
    case 43:
        _scene.clear();
        return 0; // R_ClearScene
    case 44:      // R_AddRefEntityToScene(refEntity)
        if (c.valid(a(0), kRefEntitySize))
            _scene.addEntity(c.memory().data() + a(0), kRefEntitySize, 0);
        return 0;
    case 45: { // R_LightForPoint(point, ambient, directed, direction) -> whether the level has a light grid
        if (_lightGrid.empty())
            return 0;
        const PointLight light = _lightGrid.sample(readVec(a(0)), false);
        writeVec(a(1), {light.ambient[0], light.ambient[1], light.ambient[2]});
        writeVec(a(2), {light.directed[0], light.directed[1], light.directed[2]});
        writeVec(a(3), {light.direction[0], light.direction[1], light.direction[2]});
        return 1;
    }
    case 46: { // R_AddPolyToScene(shader, vertex count, vertices)
        const uint32_t bytes = std::min<uint32_t>(a(1), 256) * 24;
        _scene.addPoly(int32_t(a(0)), int32_t(a(1)),
                       bytes && c.valid(a(2), bytes) ? c.memory().data() + a(2) : nullptr);
        return 0;
    }
    case 47: // R_AddLightToScene(origin, intensity, r, g, b)
        _scene.addLight(readVec(a(0)), readFloat(c.reg[x86::Cpu::ESP] + 12), readFloat(c.reg[x86::Cpu::ESP] + 16),
                        readFloat(c.reg[x86::Cpu::ESP] + 20), readFloat(c.reg[x86::Cpu::ESP] + 24));
        return 0;
    case 48: // R_RenderScene(refdef)
        _scene.render(c.valid(a(0), kRefdefSize) ? c.memory().data() + a(0) : nullptr, _pictures.size());
        return 0;
    case 49: // R_SetColor(rgba, or 0 for white)
        if (!a(0))
            _color = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i)
                _color[size_t(i)] = readFloat(a(0) + 4 * uint32_t(i));
        return 0;
    case 50: { // R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader)
        auto f = [&](int n) { return readFloat(c.reg[x86::Cpu::ESP] + 8 + 4 * uint32_t(n)); };
        _pictures.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), int32_t(a(8)), _color});
        return 0;
    }
    case 51: { // R_DrawScreenShot(x, y, w, h): the screen capture, over the pictures so far
        auto f = [&](int n) { return readFloat(c.reg[x86::Cpu::ESP] + 8 + 4 * uint32_t(n)); };
        _shots.push_back({f(0), f(1), f(2), f(3), _pictures.size()});
        return 0;
    }
    case 54: { // R_DrawRotatePic(x, y, w, h, s1, t1, s2, t2, angle, shader)
        auto f = [&](int n) { return readFloat(c.reg[x86::Cpu::ESP] + 8 + 4 * uint32_t(n)); };
        _pictures.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), int32_t(a(9)), _color, f(8)});
        return 0;
    }
    case 52: { // R_ModelBounds(model, mins, maxs)
        Vec3f mins{};
        Vec3f maxs{};
        const std::string &name = _models.name(int32_t(a(0)));
        if (!name.empty() && name[0] == '*') {
            if (_world)
                _world->modelBounds(std::atoi(name.c_str() + 1), mins, maxs);
        } else if (const Md3Model *m = md3(int32_t(a(0))); m && !m->frames.empty()) {
            mins = m->frames[0].mins;
            maxs = m->frames[0].maxs;
        }
        writeVec(a(1), mins);
        writeVec(a(2), maxs);
        return 0;
    }
    case 53: { // R_LerpTag(orientation, model, startFrame, endFrame, frac, tag) -> found
        std::array<float, 12> o{};
        const bool found = lerpTag(md3(int32_t(a(1))), int32_t(a(2)), int32_t(a(3)),
                                   readFloat(c.reg[x86::Cpu::ESP] + 24), str(5, 64), o);
        if (c.valid(a(0), 48))
            for (int i = 0; i < 12; ++i)
                c.write32(a(0) + 4 * uint32_t(i), floatBits(o[size_t(i)]));
        return found ? 1 : 0;
    }
    case 55:
        return 0; // R_Scissor?
    case 56:
        writeGlconfig(a(0));
        return 0;
    case 57:
        writeGameState(a(0));
        return 0;
    case 58: // GetCurrentSnapshotNumber(&number, &serverTime)
        c.write32(a(0), uint32_t(_snapshotNumber));
        c.write32(a(1), _snapshots.empty() ? 0u : uint32_t(_snapshots.back().serverTime));
        return 0;
    case 59: { // GetSnapshot(number, out) -> whether it's still there
        const int32_t n = int32_t(a(0));
        if (n > _snapshotNumber) {
            c.abort("GetSnapshot: " + std::to_string(n) + " is ahead of the latest");
            return 0;
        }
        for (const Snapshot &snap : _snapshots)
            if (snap.number == n) {
                writeSnapshot(a(1), snap);
                return 1;
            }
        return 0;
    }
    case 60: { // GetServerCommand(sequence) -> whether it's there; its words for Argv
        const int32_t n = int32_t(a(0));
        if (n < 1 || size_t(n) > _toClient.size())
            return 0;
        tokenize(_serverCommands[_toClient[size_t(n - 1)]].second);
        return 1;
    }
    case 61:
        return uint32_t(_commandNumber); // GetCurrentCmdNumber()
    case 62: {                           // GetUserCmd(number, out) -> whether it's still kept
        const int32_t n = int32_t(a(0));
        if (n > _commandNumber || n <= _commandNumber - 64)
            return 0;
        writeUserCommand(a(1), _commands[size_t(n & 63)]);
        return 1;
    }
    case 63:
        _clientWeapon = uint8_t(a(0));
        return 0; // SetUserCmdValue(weapon, sensitivity)
    case 64:
        return 16u << 20; // MemoryRemaining()
    // Ambient sound sets (AmbientSets).
    case 65: // S_UpdateAmbientSet(name, origin): the level's general set, each frame
        playAmbient(_ambient.updateGeneral(str(0, 256), readVec(a(1)), _levelTime));
        return 0;
    case 66: { // S_AddLocalSet(name, listener origin, origin, entity, last time) -> the new time
        int64_t last = int32_t(a(4));
        playAmbient(_ambient.addLocal(str(0, 256), readVec(a(1)), readVec(a(2)), int(a(3)), last, _levelTime));
        return uint32_t(int32_t(last));
    }
    case 67: { // AS_ParseSets(): sound/sound.txt, for the sets precached
        std::vector<uint8_t> data;
        std::vector<std::string> warnings;
        if (Error e = _files.read("sound/sound.txt", data))
            warnings.push_back("couldn't load ambient sound sets from sound/sound.txt: " + e.message);
        else
            _ambient.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), warnings);
        if (print)
            for (const std::string &w : warnings)
                print("ambient sets: " + w + "\n");
        return 0;
    }
    case 68:
        _ambient.addPrecacheEntry(str(0, 256));
        return 0; // AS_AddPrecacheEntry(name)
    case 69: {    // AS_GetBModelSound(name, stage) -> sfx, or -1
        const std::string name = _ambient.bmodelSound(str(0, 256), int32_t(a(1)));
        return name.empty() ? 0xffffffffu : uint32_t(_sounds.add(name));
    }
    case 70: { // S_GetSampleLength(sfx): whole milliseconds in EAX (the cgame's wrapper loads it with fild)
        const std::string &name = _sounds.name(int32_t(a(0)));
        if (!name.empty())
            if (auto sound = _soundLibrary.find(name); sound && sound->rate > 0)
                return uint32_t(int64_t(sound->frames()) * 1000 / sound->rate);
        return 0;
    }
    default:
        ++_cgameUnhandled[number];
        return 0;
    }
}

int32_t SpGameHost::entityModel(const SceneEntity &e) {
    if (e.bytes.size() < 12)
        return 0;
    int32_t v;
    std::memcpy(&v, e.bytes.data() + 8, 4);
    return v;
}

const Md3Model *SpGameHost::md3(int32_t handle) {
    auto [it, added] = _md3s.try_emplace(handle);
    if (added) {
        const std::string &name = _models.name(handle);
        std::vector<uint8_t> data;
        auto m = std::make_shared<Md3Model>();
        if (!name.empty() && name[0] != '*' && !_files.read(name, data) && !parseModel(data, *m))
            it->second = std::move(m);
    }
    return it->second.get();
}

void SpGameHost::clientTrace(x86::Cpu &c, bool transformed) {
    // (results, start, end, mins, maxs, model, brushmask[, origin, angles]):
    // against one model only (the client game adds the entities itself).
    auto a = [&c](int n) { return c.arg(n + 1); };
    Vec3f start = readVec(a(1));
    Vec3f end = readVec(a(2));
    const Vec3f mins = a(3) ? readVec(a(3)) : Vec3f{};
    const Vec3f maxs = a(4) ? readVec(a(4)) : Vec3f{};
    const uint32_t model = a(5);
    const int32_t mask = int32_t(a(6));
    const Vec3f origin = transformed ? readVec(a(7)) : Vec3f{};
    const Vec3f angles = transformed ? readVec(a(8)) : Vec3f{};
    TraceResult t;
    if (model == kTempBoxModel) {
        // A box isn't turned, as the original's.
        Vec3f s = start;
        Vec3f e = end;
        for (int i = 0; i < 3; ++i) {
            s[size_t(i)] -= origin[size_t(i)];
            e[size_t(i)] -= origin[size_t(i)];
        }
        if (!_tempBoxes.empty())
            t = CollisionWorld::traceBox(s, e, mins, maxs, _tempBoxes[0].first, _tempBoxes[0].second, 1);
    } else if (_world) {
        t = _world->transformedTrace(start, end, mins, maxs, int(model), mask, origin, angles);
    }
    for (int i = 0; i < 3; ++i)
        t.endPos[size_t(i)] = start[size_t(i)] + t.fraction * (end[size_t(i)] - start[size_t(i)]);
    writeTrace(a(0), t, t.fraction < 1 ? kEntityWorld : kEntityNone);
}

void SpGameHost::tokenize(const std::string &text) {
    // Words split at spaces, with quoted runs kept whole.
    _args.clear();
    std::string word;
    bool quoted = false;
    bool any = false;
    for (char ch : text) {
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if ((ch == ' ' || ch == '\n') && !quoted) {
            if (any || !word.empty())
                _args.push_back(word);
            word.clear();
            any = false;
        } else {
            word += ch;
        }
    }
    if (any || !word.empty())
        _args.push_back(word);
}

void SpGameHost::writeGlconfig(uint32_t address) {
    writeGlconfigTo(*_cpu, address);
}

void SpGameHost::writeGlconfigTo(x86::Cpu &cpu, uint32_t address) {
    // glconfig_t as the original engine fills it (5196 bytes): the
    // renderer's, vendor's and version text (1024 each) and the extensions
    // (2048), then Quake III's numbers from 5120 with two more of Elite
    // Force's before the screen size (at 5168 and 5172).
    constexpr uint32_t kSize = 5196;
    constexpr uint32_t kNumbers = 5120;
    if (!cpu.valid(address, kSize))
        return;
    std::memset(cpu.memory().data() + address, 0, kSize);
    cpu.writeString(address, "Reluva", 1024);
    cpu.writeString(address + 1024, "Reluva", 1024);
    cpu.writeString(address + 2048, "1.0", 1024);
    const uint32_t w = address + kNumbers;
    cpu.write32(w + 0, 2048); // maxTextureSize
    cpu.write32(w + 4, 2);    // texture units
    cpu.write32(w + 8, 32);   // colour bits
    cpu.write32(w + 12, 24);  // depth bits
    cpu.write32(w + 16, 8);   // stencil bits
    cpu.write32(w + 28, 1);   // deviceSupportsGamma
    cpu.write32(w + 36, 1);   // textureEnvAddAvailable
    cpu.write32(w + 48, 640); // vidWidth
    cpu.write32(w + 52, 480); // vidHeight
    cpu.write32(w + 56, floatBits(640.0f / 480.0f));
    cpu.write32(w + 60, 60); // displayFrequency
}

void SpGameHost::writeGameState(uint32_t address) {
    // gameState_t (20100 bytes): each configstring's offset into the text
    // (1024), the text (16000), how much of it is used. Offset 0 is an
    // empty string.
    constexpr uint32_t kStrings = 1024;
    constexpr uint32_t kChars = 16000;
    constexpr uint32_t kSize = 4 * kStrings + kChars + 4;
    if (!_cpu->valid(address, kSize))
        return;
    std::memset(_cpu->memory().data() + address, 0, kSize);
    uint32_t used = 1;
    for (const auto &[index, value] : _configstrings) {
        if (index < 0 || uint32_t(index) >= kStrings || value.empty() || used + value.size() + 1 > kChars)
            continue;
        _cpu->write32(address + 4 * uint32_t(index), used);
        _cpu->writeString(address + 4 * kStrings + used, value, uint32_t(value.size()) + 1);
        used += uint32_t(value.size()) + 1;
    }
    _cpu->write32(address + 4 * kStrings + kChars, used);
}

void SpGameHost::writeUserCommand(uint32_t address, const UserCommand &cmd) {
    if (!_cpu->valid(address, kUserCmdSize))
        return;
    std::memset(_cpu->memory().data() + address, 0, kUserCmdSize);
    _cpu->write32(address, uint32_t(cmd.serverTime));
    _cpu->write32(address + 4, uint32_t(cmd.buttons));
    _cpu->write8(address + 8, cmd.weapon);
    for (int i = 0; i < 3; ++i)
        _cpu->write32(address + 12 + 4 * uint32_t(i), uint32_t(cmd.angles[size_t(i)]));
    _cpu->write8(address + 24, uint8_t(cmd.forward));
    _cpu->write8(address + 25, uint8_t(cmd.right));
    _cpu->write8(address + 26, uint8_t(cmd.up));
}

void SpGameHost::writeSnapshot(uint32_t address, const Snapshot &snap) {
    // snapshot_t as the original engine fills it: flags 0, ping 4, server
    // time 8, the area mask 12 (32 bytes), the command number 44, the
    // player state 48 (544), the entity count 592, the entities 596 (256
    // of 232 bytes), the server command sequence 60000.
    constexpr uint32_t kSize = 0xea64;
    if (!_cpu->valid(address, kSize))
        return;
    _cpu->write32(address, 0);
    _cpu->write32(address + 4, 0);
    _cpu->write32(address + 8, uint32_t(snap.serverTime));
    std::memcpy(_cpu->memory().data() + address + 12, snap.areaMask.data(), 32);
    _cpu->write32(address + 44, uint32_t(snap.commandNumber));
    std::memcpy(_cpu->memory().data() + address + 48, snap.playerState.data(), kPlayerStateSize);
    _cpu->write32(address + 0x250, uint32_t(snap.entities.size()));
    for (size_t i = 0; i < snap.entities.size(); ++i)
        std::memcpy(_cpu->memory().data() + address + 0x254 + kEntityStateSize * i, snap.entities[i].data(),
                    kEntityStateSize);
    _cpu->write32(address + 0xea60, uint32_t(snap.serverCommandSequence));
}

std::vector<int32_t> SpGameHost::visibleEntities(const Vec3f &eye) {
    // As the original's server picks them: entities in use and linked, not
    // flagged NOCLIENT (1); BROADCAST (0x20) ones always, the rest in an
    // area joined to the viewer's and a cluster it can see; a PORTAL
    // (0x40) adds what's seen from its other end (origin2, at 0x68).
    std::vector<int32_t> out;
    std::set<int32_t> added;
    const bool noAreas = _cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas");
    const int32_t count = std::min(entityCount(), kMaxEntities);
    auto look = [&](const Vec3f &from, auto &self, int depth) -> void {
        const int leaf = _world ? _world->leafAt(from) : -1;
        const int area = leaf >= 0 ? _world->leafArea(leaf) : -1;
        const int cluster = leaf >= 0 && size_t(leaf) < _map->leafs.size() ? _map->leafs[size_t(leaf)].cluster : -1;
        for (int32_t n = 0; n < count; ++n) {
            if (added.count(n) || !entityInt(n, kEntity.inUse) || !entityInt(n, kEntity.linked))
                continue;
            const int32_t flags = entityInt(n, kEntity.svFlags);
            if (flags & 1)
                continue;
            if (!(flags & 0x20) && _world) {
                const ServerWorld::Placement place = entityPlacement(n);
                const auto &areas = place.areas;
                if (!noAreas && area >= 0 && areas.first >= 0 && !_serverWorld.areasConnected(area, areas.first) &&
                    (areas.second < 0 || !_serverWorld.areasConnected(area, areas.second)))
                    continue;
                bool seen = place.leafCount == 0 || cluster < 0;
                for (const int c : place.clusters)
                    seen = seen || _map->clusterVisible(cluster, c);
                if (!seen)
                    continue;
            }
            added.insert(n);
            out.push_back(n);
            if ((flags & 0x40) && depth < 2)
                self(entityVec(n, 0x68), self, depth + 1);
        }
    };
    look(eye, look, 0);
    std::sort(out.begin(), out.end());
    return out;
}

void SpGameHost::makeSnapshot() {
    Snapshot snap;
    snap.number = ++_snapshotNumber;
    snap.serverTime = _levelTime;
    snap.commandNumber = _commandNumber;
    snap.playerState.assign(kPlayerStateSize, 0);
    // The player's state: where the player's entity's client points.
    const uint32_t ps = uint32_t(entityInt(0, kEntity.client));
    if (ps && _cpu->valid(ps, kPlayerStateSize))
        std::memcpy(snap.playerState.data(), _cpu->memory().data() + ps, kPlayerStateSize);
    // From the client game's camera if one's running, else the player's
    // eye: origin (20) raised by the view height (168).
    Vec3f eye{};
    const uint32_t camera = _process->memory().alloc(16);
    uint32_t cameraOn = 0;
    if (_cgameRunning && camera) {
        if (Error e = _cpu->call(_cgameVmMain, {kCgCameraPos, camera}, cameraOn))
            cameraOn = 0;
    }
    if (cameraOn) {
        eye = readVec(camera);
    } else if (ps) {
        eye = readVec(ps + 20);
        eye[2] += float(int32_t(_cpu->read32(ps + 168)));
    }
    _process->memory().free(camera);
    for (const int32_t n : visibleEntities(eye)) {
        if (snap.entities.size() >= 256)
            break;
        const uint32_t a = entityAddress(n);
        // An entity whose number isn't its own put right, as the original's
        // server does (see GameHost::snapshot).
        if (_cpu->read32(a + uint32_t(kEntity.number)) != uint32_t(n))
            _cpu->write32(a + uint32_t(kEntity.number), uint32_t(n));
        snap.entities.emplace_back(_cpu->memory().data() + a, _cpu->memory().data() + a + kEntityStateSize);
    }
    snap.serverCommandSequence = int32_t(_toClient.size());
    if (_world && _areaPortals && !(_cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas"))) {
        const int area = _world->leafArea(_world->leafAt(eye));
        if (area >= 0)
            for (int a = 0; a < std::min(_world->areaCount(), 256); ++a)
                if (!_areaPortals->connected(area, a))
                    snap.areaMask[size_t(a >> 3)] |= uint8_t(1 << (a & 7));
    }
    _snapshots.push_back(std::move(snap));
    if (_snapshots.size() > 32)
        _snapshots.erase(_snapshots.begin());
}

Error SpGameHost::startClientGame() {
    _cgameVmMain = _module->exportAddress("vmMain");
    if (!_cgameVmMain)
        return Error::fail("efgamex86.dll has no vmMain");
    _cgameRunning = true;
    makeSnapshot();
    syncCvars();
    uint32_t ignored = 0;
    // CG_INIT's argument is the server commands received so far (the
    // original engine passes its client's command sequence), which the
    // client game counts on from.
    return _cpu->call(_cgameVmMain, {kCgInit, uint32_t(_toClient.size())}, ignored);
}

void SpGameHost::addServerCommand(int32_t to, std::string text) {
    if (to == 0 || to == -1)
        _toClient.push_back(_serverCommands.size());
    _serverCommands.emplace_back(to, std::move(text));
}

Error SpGameHost::clientThink(const UserCommand &input) {
    UserCommand cmd = input;
    cmd.weapon = _clientWeapon;
    _commands[size_t(++_commandNumber & 63)] = cmd;
    const uint32_t address = _process->memory().alloc(kUserCmdSize);
    writeUserCommand(address, cmd);
    const Error e = callGame(kClientThink, {0, address});
    _process->memory().free(address);
    return e;
}

Error SpGameHost::drawClientFrame(int32_t time) {
    _scene = {};
    _pictures.clear();
    _shots.clear();
    syncCvars();
    uint32_t ignored = 0;
    const Error e = _cpu->call(_cgameVmMain, {kCgDrawActiveFrame, uint32_t(time), 0}, ignored);
    _lastScene = std::move(_scene);
    _lastPictures = std::move(_pictures);
    _lastShots = std::move(_shots);
    _scene = {};
    _pictures.clear();
    _shots.clear();
    return e;
}

Error SpGameHost::drawLoadingFrame() {
    // CG_DRAW_ACTIVE_FRAME from inside the client game's start (as the
    // original's screen update does): not ready yet, it draws its loading
    // screen. What it was drawing meanwhile is kept aside.
    Scene sceneBefore = std::move(_scene);
    std::vector<Picture> picturesBefore = std::move(_pictures);
    std::vector<ScreenShotDraw> shotsBefore = std::move(_shots);
    const std::array<float, 4> colorBefore = _color;
    _scene = {};
    _pictures.clear();
    _shots.clear();
    _color = {1, 1, 1, 1};
    uint32_t ignored = 0;
    const Error e = _cpu->call(_cgameVmMain, {kCgDrawActiveFrame, uint32_t(_levelTime), 0}, ignored);
    _lastScene = std::move(_scene);
    _lastPictures = std::move(_pictures);
    _lastShots = std::move(_shots);
    _scene = std::move(sceneBefore);
    _pictures = std::move(picturesBefore);
    _shots = std::move(shotsBefore);
    _color = colorBefore;
    return e;
}

bool SpGameHost::cameraPosition(Vec3f &at) {
    if (!_cgameRunning)
        return false;
    const uint32_t v = _process->memory().alloc(16);
    uint32_t on = 0;
    if (Error e = _cpu->call(_cgameVmMain, {kCgCameraPos, v}, on))
        on = 0;
    at = readVec(v);
    _process->memory().free(v);
    return on != 0;
}

Error SpGameHost::clientConsoleCommand(const std::string &text) {
    tokenize(text);
    if (_args.empty())
        return Error::success();
    uint32_t handled = 0;
    if (_cgameRunning) {
        syncCvars();
        if (Error e = _cpu->call(_cgameVmMain, {kCgConsoleCommand}, handled))
            return e;
    }
    if (handled)
        return Error::success();
    tokenize(text);
    return callGame(kClientCommand, {0});
}

// --- The engine's functions ---------------------------------------------------------

void SpGameHost::addImports() {
    auto str = [](x86::Cpu &c, int n, size_t max = 1024) { return c.string(c.arg(n), max); };
    auto say = [this](const std::string &s) {
        if (print)
            print(s);
    };

    import(kPrintf, [say](x86::Cpu &c) {
        say(x86::formatPrintf(c, c.arg(0), c.reg[x86::Cpu::ESP] + 8));
        return 0u;
    });
    import(kWriteCam, [](x86::Cpu &) { return 0u; });
    import(kError, [](x86::Cpu &c) {
        c.abort("efgamex86.dll: " + x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });
    import(kMilliseconds, [this](x86::Cpu &) { return _process->milliseconds(); });
    import(kCvar, [this, str](x86::Cpu &c) {
        const std::string name = str(c, 0, 256);
        _cvars.create(name, str(c, 1, 256), int32_t(c.arg(2)));
        return cvarStructure(name);
    });
    import(kCvarSet, [this, str](x86::Cpu &c) {
        // As the original engine's, forced: the game sets its read-only
        // ones (skippingCinematic) this way.
        const std::string name = str(c, 0, 256);
        _cvars.set(name, str(c, 1, 1024), true);
        if (_cvarMirrors.count(lower(name)))
            cvarStructure(name);
        return 0u;
    });
    import(kCvarVariableIntegerValue, [this, str](x86::Cpu &c) { return uint32_t(_cvars.integer(str(c, 0, 256))); });
    import(kCvarVariableStringBuffer, [this, str](x86::Cpu &c) {
        c.writeString(c.arg(1), _cvars.string(str(c, 0, 256)), c.arg(2));
        return 0u;
    });
    import(kArgc, [this](x86::Cpu &) { return uint32_t(_args.size()); });
    import(kArgv, [this](x86::Cpu &c) {
        const uint32_t n = c.arg(0);
        c.writeString(_argvBuffer, n < _args.size() ? _args[n] : "", kArgvSize);
        return _argvBuffer;
    });

    // Files: read from the game's search path; what it writes is kept
    // only until closed, for now.
    // Files.
    import(kFsFOpenFile, [this](x86::Cpu &c) { return fsOpen(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(kFsRead, [this](x86::Cpu &c) { return fsRead(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(kFsWrite, [this](x86::Cpu &c) { return fsWrite(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(kFsFCloseFile, [this](x86::Cpu &c) {
        _openFiles.close(c.arg(0));
        return 0u;
    });
    import(kFsReadFile, [this, str](x86::Cpu &c) -> uint32_t {
        std::vector<uint8_t> data;
        if (_files.read(str(c, 0, 256), data)) {
            if (c.arg(1))
                c.write32(c.arg(1), 0);
            return 0xffffffffu;
        }
        if (c.arg(1)) {
            // A NUL after it, as the original's.
            const uint32_t buffer = _process->memory().alloc(uint32_t(data.size()) + 1);
            if (!buffer)
                return 0xffffffffu;
            std::memcpy(c.memory().data() + buffer, data.data(), data.size());
            c.write8(buffer + uint32_t(data.size()), 0);
            c.write32(c.arg(1), buffer);
        }
        return uint32_t(data.size());
    });
    import(kFsFreeFile, [this](x86::Cpu &c) {
        _process->memory().free(c.arg(0));
        return 0u;
    });

    // Saved games: kept in memory for now.
    import(kAppendToSaveGame, [this](x86::Cpu &c) -> uint32_t {
        // (id, data, length): into the saved game being written.
        const uint32_t data = c.arg(1);
        const uint32_t length = c.arg(2);
        if (!_saveWriter || !c.valid(data, length))
            return 0;
        _saveWriter->add(c.arg(0), c.memory().data() + data, length);
        return 1;
    });
    // (id, buffer, length; and in the Expansion Pack's, a pointer for a
    // buffer the engine makes when none is given): the saved game's next
    // chunk, which must be this one and (unless the length is 0) this
    // long. Optional (the Expansion Pack's other): a different chunk next
    // is left for later, and 0 returned.
    auto readChunk = [this](x86::Cpu &c, bool optional) -> uint32_t {
        const uint32_t id = c.arg(0);
        const uint32_t length = c.arg(2);
        uint32_t buffer = c.arg(1);
        if (optional && (!_saveReader || !_saveReader->peek(id)))
            return 0;
        std::vector<uint8_t> data;
        Error e = _saveReader ? _saveReader->next(id, data, length) : Error::fail("no saved game is being read");
        if (!e && _expansion && !buffer) {
            buffer = _process->memory().alloc(std::max<uint32_t>(uint32_t(data.size()), 1));
            if (!buffer)
                e = Error::fail("no memory for it");
            else if (const uint32_t out = c.arg(3); out && c.valid(out, 4))
                c.write32(out, buffer);
        }
        if (!e && !c.valid(buffer, uint32_t(data.size())))
            e = Error::fail("its buffer isn't in memory");
        if (e) {
            c.abort("ReadFromSaveGame(" + chunkName(id) + "): " + e.message);
            return 0;
        }
        std::memcpy(c.memory().data() + buffer, data.data(), data.size());
        return _expansion ? uint32_t(data.size()) : 1u;
    };
    import(kReadFromSaveGame, [readChunk](x86::Cpu &c) { return readChunk(c, false); });
    if (_expansion) {
        // Its slots of their own (not through import(), which places the
        // 1.x slots).
        auto own = [this](int position, const char *name, const std::function<uint32_t(x86::Cpu &)> &fn) {
            const uint32_t address = _cpu->addNative(name, [fn](x86::Cpu &c) {
                c.reg[x86::Cpu::EAX] = fn(c);
                return 0u;
            });
            _cpu->write32(_imports + 4 * uint32_t(position), address);
        };
        own(kXpReadFromSaveGameOptional, "engine[ReadFromSaveGameOptional]",
            [readChunk](x86::Cpu &c) { return readChunk(c, true); });
        own(kXpFsGetFileList, "engine[FS_GetFileList]", [this, str](x86::Cpu &c) -> uint32_t {
            // (path, extension, buffer, size) -> how many, as that
            // engine's (expansionFileList, packFileList); "$modlist": the
            // mods' folders and descriptions (listMods).
            const uint32_t path = c.arg(0);
            const uint32_t buffer = c.arg(2);
            const uint32_t size = c.arg(3);
            if (!path || int32_t(size) <= 0 || !c.valid(buffer, size))
                return 0;
            const std::string folder = str(c, 0, 256);
            std::vector<uint8_t> list;
            const uint32_t count =
                folder != "$modlist"
                    ? packFileList(expansionFileList(_files, folder, c.arg(1) ? str(c, 1, 64) : ""), size, list)
                : modInstall.empty() ? packModList({}, size, list)
                                     : packModList(listMods(modInstall, modBaseDir), size, list);
            std::memcpy(c.memory().data() + buffer, list.data(), list.size());
            return count;
        });
    }

    import(kSendConsoleCommand, [this, str](x86::Cpu &c) {
        _consoleCommands.push_back(str(c, 0, 4096));
        return 0u;
    });
    import(kDropClient, [say, str](x86::Cpu &c) {
        say("DropClient: " + str(c, 1) + "\n");
        return 0u;
    });
    import(kSendServerCommand, [this](x86::Cpu &c) {
        addServerCommand(int32_t(c.arg(0)), x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });
    import(kSetConfigstring, [this, str](x86::Cpu &c) {
        const int32_t index = int32_t(c.arg(0));
        const std::string value = str(c, 1, 8192);
        _configstrings[index] = value;
        // Once the client game runs, it hears of changes as the original
        // engine tells it.
        if (_cgameRunning)
            addServerCommand(0, "cs " + std::to_string(index) + " \"" + value + "\"");
        return 0u;
    });
    import(kGetConfigstring, [this](x86::Cpu &c) {
        auto it = _configstrings.find(int32_t(c.arg(0)));
        c.writeString(c.arg(1), it == _configstrings.end() ? std::string() : it->second, c.arg(2));
        return 0u;
    });
    import(kGetUserinfo, [this](x86::Cpu &c) {
        auto it = _userinfo.find(int32_t(c.arg(0)));
        c.writeString(c.arg(1), it == _userinfo.end() ? std::string() : it->second, c.arg(2));
        return 0u;
    });
    import(kSetUserinfo, [this, str](x86::Cpu &c) {
        _userinfo[int32_t(c.arg(0))] = str(c, 1);
        return 0u;
    });
    import(kGetServerinfo, [this](x86::Cpu &c) {
        c.writeString(c.arg(0), infoString(CvarStore::kServerInfo), c.arg(1));
        return 0u;
    });

    // The world.
    import(kSetBrushModel, [this, str](x86::Cpu &c) {
        const uint32_t ent = c.arg(0);
        const std::string name = str(c, 1, 64);
        if (name.size() < 2 || name[0] != '*') {
            c.abort("SetBrushModel: '" + name + "' isn't a brush model");
            return 0u;
        }
        const int model = std::atoi(name.c_str() + 1);
        Vec3f mins{};
        Vec3f maxs{};
        if (!_world || model <= 0 || !_world->modelBounds(model, mins, maxs)) {
            c.abort("SetBrushModel: the map has no model " + name);
            return 0u;
        }
        c.write32(ent + kEntity.modelIndex, uint32_t(model));
        writeVec(ent + kEntity.mins, mins);
        writeVec(ent + kEntity.maxs, maxs);
        c.write32(ent + kEntity.bmodel, 1);
        c.write32(ent + kEntity.contents, 0xffffffffu); // whatever its brushes hold
        linkEntity(ent);
        return 0u;
    });
    import(kTrace, [this](x86::Cpu &c) {
        const Vec3f zero{};
        int32_t hit = kEntityNone;
        const TraceResult t = _serverWorld.trace(readVec(c.arg(1)), c.arg(2) ? readVec(c.arg(2)) : zero,
                                                 c.arg(3) ? readVec(c.arg(3)) : zero, readVec(c.arg(4)),
                                                 int32_t(c.arg(5)), int32_t(c.arg(6)), hit);
        writeTrace(c.arg(0), t, hit);
        return 0u;
    });
    import(kPointContents,
           [this](x86::Cpu &c) { return uint32_t(_serverWorld.pointContents(readVec(c.arg(0)), int32_t(c.arg(1)))); });
    auto inPvs = [this](x86::Cpu &c, bool ignorePortals) -> uint32_t {
        if (!_world)
            return 1;
        const int l1 = _world->leafAt(readVec(c.arg(0)));
        const int l2 = _world->leafAt(readVec(c.arg(1)));
        if (l1 < 0 || l2 < 0)
            return 0;
        if (!_map->clusterVisible(_map->leafs[size_t(l1)].cluster, _map->leafs[size_t(l2)].cluster))
            return 0;
        return ignorePortals || _serverWorld.areasConnected(_world->leafArea(l1), _world->leafArea(l2)) ? 1 : 0;
    };
    import(kInPvs, [inPvs](x86::Cpu &c) { return inPvs(c, false); });
    import(kInPvsIgnorePortals, [inPvs](x86::Cpu &c) { return inPvs(c, true); });
    import(kAdjustAreaPortalState, [this](x86::Cpu &c) {
        if (const int32_t n = entityNumber(c.arg(0)); n >= 0 && _areaPortals) {
            const auto [area1, area2] = entityPlacement(n).areas;
            if (area2 != -1)
                _areaPortals->adjust(area1, area2, c.arg(1) != 0);
        }
        return 0u;
    });
    import(kAreasConnected,
           [this](x86::Cpu &c) { return _serverWorld.areasConnected(int(c.arg(0)), int(c.arg(1))) ? 1u : 0u; });
    import(kLinkEntity, [this](x86::Cpu &c) {
        linkEntity(c.arg(0));
        return 0u;
    });
    import(kUnlinkEntity, [this](x86::Cpu &c) {
        unlinkEntity(c.arg(0));
        return 0u;
    });
    import(kEntitiesInBox, [this](x86::Cpu &c) {
        // (mins, maxs, list of entity pointers, max) -> how many
        const Vec3f mins = readVec(c.arg(0));
        const Vec3f maxs = readVec(c.arg(1));
        std::vector<int32_t> found;
        _serverWorld.entitiesInBox(mins, maxs, found);
        uint32_t count = 0;
        for (const int32_t n : found) {
            if (count >= c.arg(3))
                break;
            c.write32(c.arg(2) + 4 * count++, entityAddress(n));
        }
        return count;
    });
    import(kEntityContact, [this](x86::Cpu &c) -> uint32_t {
        const Vec3f mins = readVec(c.arg(0));
        const Vec3f maxs = readVec(c.arg(1));
        const int32_t n = entityNumber(c.arg(2));
        if (n < 0)
            return 0;
        const Vec3f origin = entityVec(n, kEntity.currentOrigin);
        if (entityInt(n, kEntity.bmodel) && _world) {
            // The box against the model where it stands, turned as it is.
            const Vec3f zero{};
            return _world->transformedTrace(zero, zero, mins, maxs, entityInt(n, kEntity.modelIndex), -1, origin,
                                            entityVec(n, kEntity.currentAngles))
                           .startSolid
                       ? 1
                       : 0;
        }
        const Vec3f absMin = entityVec(n, kEntity.absMin);
        const Vec3f absMax = entityVec(n, kEntity.absMax);
        for (int i = 0; i < 3; ++i)
            if (absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)])
                return 0;
        return 1;
    });
    // Not a function: the voices' volumes, one int an entity.
    _cpu->write32(_imports + 4 * uint32_t(importPosition(kVoiceVolume)), _voiceVolume);
    import(kMalloc, [this](x86::Cpu &c) -> uint32_t {
        const uint32_t p = _process->memory().alloc(std::max(c.arg(0), 1u));
        if (!p)
            c.abort("Malloc: out of memory (" + std::to_string(c.arg(0)) + " bytes)");
        return p;
    });
    import(kFree, [this](x86::Cpu &c) {
        _process->memory().free(c.arg(0));
        return 0u;
    });
}

void writeSaveHeader(SaveWriter &w, const CvarStore &cvars, const SaveHeader &header) {
    w.addText(chunkId("COMM"), header.comment, 128);
    w.add(chunkId("SHOT"), header.shot);
    w.addText(chunkId("MPCM"), header.map, 1024);
    std::vector<const CvarStore::Cvar *> kept;
    for (const CvarStore::Cvar &cv : cvars.all())
        if (cv.flags & 2)
            kept.push_back(&cv);
    w.addInt(chunkId("CVCN"), int32_t(kept.size()));
    for (const CvarStore::Cvar *cv : kept) {
        w.addText(chunkId("CVAR"), cv->name);
        w.addText(chunkId("VALU"), cv->value);
    }
    w.addInt(chunkId("GAME"), header.autosave ? 1 : 0);
    if (header.autosave) {
        w.addText(chunkId("CVSV"), cvars.string("playersave"), 1024);
        for (int i = 0; i < 4; ++i)
            w.addText(chunkId("AMMO"), cvars.string("playerammo" + std::to_string(i)), 1024);
        for (int i = 0; i < 32; ++i)
            w.addText(chunkId("APTD"), cvars.string("borgadapt" + std::to_string(i)), 1024);
    }
}

Error writeSaveLevel(SaveWriter &w, SpGameHost &game, bool autosave) {
    if (!autosave) {
        w.addInt(chunkId("TIME"), game.levelTime());
        w.addInt(chunkId("TIMR"), 0);
        w.add(chunkId("STRP"), nullptr, 0);
        std::vector<std::pair<int32_t, const std::string *>> strings;
        for (const auto &[index, text] : game.configstrings())
            if (index != 1 && !text.empty())
                strings.emplace_back(index, &text);
        w.addInt(chunkId("CSCN"), int32_t(strings.size()));
        for (const auto &[index, text] : strings) {
            w.addInt(chunkId("CSIN"), index);
            w.addText(chunkId("CSDA"), *text);
        }
    }
    game.setSaveStream(&w, nullptr);
    const Error err = game.writeLevel(autosave);
    game.setSaveStream(nullptr, nullptr);
    return err;
}

Error readSaveHeader(SaveReader &r, CvarStore &cvars, SaveHeader &out) {
    int32_t count = 0;
    int32_t autosave = 0;
    if (Error e = r.nextText(chunkId("COMM"), out.comment))
        return e;
    if (Error e = r.next(chunkId("SHOT"), out.shot))
        return e;
    if (Error e = r.nextText(chunkId("MPCM"), out.map))
        return e;
    if (Error e = r.nextInt(chunkId("CVCN"), count))
        return e;
    for (int32_t i = 0; i < count; ++i) {
        std::string cvar;
        std::string value;
        if (Error e = r.nextText(chunkId("CVAR"), cvar))
            return e;
        if (Error e = r.nextText(chunkId("VALU"), value))
            return e;
        cvars.set(cvar, value, true);
    }
    if (Error e = r.nextInt(chunkId("GAME"), autosave))
        return e;
    out.autosave = autosave != 0;
    if (out.autosave) {
        std::string text;
        if (Error e = r.nextText(chunkId("CVSV"), text))
            return e;
        cvars.set("playersave", text);
        for (int i = 0; i < 4; ++i) {
            if (Error e = r.nextText(chunkId("AMMO"), text))
                return e;
            cvars.set("playerammo" + std::to_string(i), text);
        }
        for (int i = 0; i < 32; ++i) {
            if (Error e = r.nextText(chunkId("APTD"), text))
                return e;
            cvars.set("borgadapt" + std::to_string(i), text);
        }
    }
    return Error::success();
}

Error readSaveLevel(SaveReader &r, SpGameHost &game, bool autosave) {
    if (!autosave) {
        int32_t time = 0;
        int32_t residual = 0;
        int32_t count = 0;
        std::vector<uint8_t> ignored;
        if (Error e = r.nextInt(chunkId("TIME"), time))
            return e;
        if (Error e = r.nextInt(chunkId("TIMR"), residual))
            return e;
        if (Error e = r.next(chunkId("STRP"), ignored))
            return e;
        if (Error e = r.nextInt(chunkId("CSCN"), count))
            return e;
        std::map<int32_t, std::string> strings;
        for (int32_t i = 0; i < count; ++i) {
            int32_t at = 0;
            std::string text;
            if (Error e = r.nextInt(chunkId("CSIN"), at))
                return e;
            if (Error e = r.nextText(chunkId("CSDA"), text))
                return e;
            strings[at] = text;
        }
        game.setLevelTime(time);
        game.replaceConfigstrings(strings);
    }
    return game.readLevel(autosave);
}

std::vector<std::string> expansionFileList(const SearchSet &files, std::string_view path, std::string_view extension) {
    // A name's folder depth: its slashes (either kind).
    auto depth = [](std::string_view name) {
        return std::count_if(name.begin(), name.end(), [](char c) { return c == '/' || c == '\\'; });
    };
    auto lower = [](std::string_view text) {
        std::string out(text);
        for (char &c : out)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string base = lower(path);
    const std::string ext = lower(extension);
    const auto baseDepth = depth(path);
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](std::string_view name) {
        if (!name.empty() && (name[0] == '/' || name[0] == '\\'))
            name.remove_prefix(1);
        if (seen.insert(lower(name)).second)
            out.emplace_back(name);
    };
    for (const Archive *archive : files.archives()) {
        if (dynamic_cast<const DirectoryArchive *>(archive)) {
            // Loose files: the folder's own, in Windows' order.
            std::vector<std::string> names;
            for (const std::string &n : archive->list()) {
                const std::string key = lower(n);
                if (key.size() > base.size() + 1 && key.compare(0, base.size(), base) == 0 && key[base.size()] == '/' &&
                    key.find('/', base.size() + 1) == std::string::npos && key.size() - base.size() - 1 >= ext.size() &&
                    key.compare(key.size() - ext.size(), ext.size(), ext) == 0)
                    names.push_back(n.substr(base.size() + 1));
            }
            std::sort(names.begin(), names.end(),
                      [](const std::string &a, const std::string &b) { return reluva::fs::windowsNameLess(a, b); });
            for (const std::string &n : names)
                add(n);
            continue;
        }
        for (const std::string &n : archive->list()) {
            const std::string key = lower(n);
            const size_t slash = key.find_last_of("/\\");
            // Its folder at least as long as the path and starting with it,
            // at most two folders deeper; ending in the extension and more
            // than the path and it.
            if (depth(n) - baseDepth > 2 || slash == std::string::npos || slash < base.size() ||
                key.compare(0, base.size(), base) != 0 || key.size() < ext.size() ||
                key.size() == base.size() + ext.size() || key.compare(key.size() - ext.size(), ext.size(), ext) != 0)
                continue;
            add(std::string_view(n).substr(base.size()));
        }
    }
    return out;
}

uint32_t packFileList(const std::vector<std::string> &names, uint32_t size, std::vector<uint8_t> &out) {
    out.assign(size ? 1 : 0, 0);
    uint32_t count = 0;
    for (const std::string &n : names) {
        if (count == 0xfff)
            break;
        const size_t used = out.size() - 1;
        if (!size || n.size() + used + 2 > size)
            continue;
        out.insert(out.end() - 1, n.begin(), n.end());
        out.insert(out.end() - 1, 0);
        ++count;
    }
    return count;
}

std::optional<SaveKind> saveKind(std::istream &in) {
    // The chunks' headers only (id, length, checksum; then the data and
    // the magic word), stepped over to the first GCLI.
    const uint32_t clientId = chunkId("GCLI");
    for (int n = 0; n < 100000; ++n) {
        uint32_t header[3];
        if (!in.read(reinterpret_cast<char *>(header), sizeof header))
            return std::nullopt;
        if (header[0] == clientId) {
            if (header[1] == 2720)
                return SaveKind::kOriginal;
            if (header[1] == 3364)
                return SaveKind::kExpansion;
            return std::nullopt;
        }
        if (!in.seekg(std::streamoff(header[1]) + 4, std::ios::cur))
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<SaveKind> saveKind(const std::vector<uint8_t> &file) {
    std::istringstream in(std::string(file.begin(), file.end()), std::ios::binary);
    return saveKind(in);
}

} // namespace reluva::idtech3::eliteforce
