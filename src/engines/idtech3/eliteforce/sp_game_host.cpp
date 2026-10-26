// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_game_host.h"

#include "common/md4.h"
#include "common/x86/format.h"
#include "engines/idtech3/eliteforce/game_layout.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// The engine's functions in the table GetGameAPI is handed
// (eliteforce-sp-interface.md).
enum Import : int {
    Printf = 0,
    WriteCam = 1,
    Error_ = 2,
    Milliseconds = 3,
    Cvar = 4,
    CvarSet = 5,
    CvarVariableIntegerValue = 6,
    CvarVariableStringBuffer = 7,
    Argc = 8,
    Argv = 9,
    FsFOpenFile = 10,
    FsRead = 11,
    FsWrite = 12,
    FsFCloseFile = 13,
    FsReadFile = 14,
    FsFreeFile = 15,
    AppendToSaveGame = 16,
    ReadFromSaveGame = 17,
    SendConsoleCommand = 18,
    DropClient = 19,
    SendServerCommand = 20,
    SetConfigstring = 21,
    GetConfigstring = 22,
    GetUserinfo = 23,
    SetUserinfo = 24,
    GetServerinfo = 25,
    SetBrushModel = 26,
    Trace = 27,
    PointContents = 28,
    InPvs = 29,
    InPvsIgnorePortals = 30,
    AdjustAreaPortalState = 31,
    AreasConnected = 32,
    LinkEntity = 33,
    UnlinkEntity = 34,
    EntitiesInBox = 35,
    EntityContact = 36,
    VoiceVolume = 37,
    Malloc = 38,
    Free = 39,
    ImportCount = 40,
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
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string chunkName(uint32_t id) {
    // Four characters, as the engine's own ('TIME' is 0x54494d45).
    std::string s;
    for (int i = 3; i >= 0; --i)
        s += char((id >> (8 * i)) & 0xff);
    return s;
}

} // namespace

SpGameHost::SpGameHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars) {}

SpGameHost::~SpGameHost() = default;

void SpGameHost::import(int slot, std::function<uint32_t(x86::Cpu&)> fn) {
    // cdecl: the caller pops the arguments.
    const uint32_t address = cpu_->addNative("engine[" + std::to_string(slot) + "]", [this, slot, fn](x86::Cpu& c) {
        ++callCounts_[slot];
        if (trace)
            trace(slot, c.read32(c.reg[x86::Cpu::ESP]));
        c.reg[x86::Cpu::EAX] = fn(c);
        return 0u;
    });
    cpu_->write32(imports_ + 4 * uint32_t(slot), address);
}

uint32_t SpGameHost::text(const std::string& s) {
    const uint32_t a = process_->memory().alloc(uint32_t(s.size()) + 1);
    if (a)
        cpu_->writeString(a, s, uint32_t(s.size()) + 1);
    levelText_.push_back(a);
    return a;
}

Error SpGameHost::load(const std::vector<uint8_t>& dll) {
    cpu_ = std::make_unique<x86::Cpu>(kMemory);
    process_ = std::make_unique<x86::Win32Process>(*cpu_);
    process_->output = [this](const std::string& s) {
        if (print)
            print(s);
    };
    startMs_ = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
    if (Error e = process_->load("efgamex86.dll", dll, module_))
        return e;
    if (Error e = process_->attach(*module_))
        return e;
    imports_ = process_->memory().alloc(4 * ImportCount);
    voiceVolume_ = process_->memory().alloc(4 * kMaxEntities);
    argvBuffer_ = process_->memory().alloc(kArgvSize);
    addImports();
    // The client game's half: dllEntry hands it the system call function.
    // The server game calls into it too, so it's set up from the start.
    if (const uint32_t dllEntry = module_->exportAddress("dllEntry")) {
        const uint32_t syscall = cpu_->addNative("cgame syscall", [this](x86::Cpu& c) {
            c.reg[x86::Cpu::EAX] = cgameCall(c);
            return 0u;
        });
        uint32_t ignored = 0;
        if (Error e = cpu_->call(dllEntry, {syscall}, ignored))
            return Error::fail("dllEntry: " + e.message);
    }
    const uint32_t getGameApi = module_->exportAddress("GetGameAPI");
    if (!getGameApi)
        return Error::fail("efgamex86.dll has no GetGameAPI");
    uint32_t result = 0;
    if (Error e = cpu_->call(getGameApi, {imports_}, result))
        return Error::fail("GetGameAPI: " + e.message);
    if (!cpu_->valid(result, 17 * 4))
        return Error::fail("GetGameAPI gave no table");
    exports_ = result;
    const int32_t version = int32_t(cpu_->read32(exports_));
    if (version != kApiVersion)
        return Error::fail("The game is version " + std::to_string(version) + ", not " + std::to_string(kApiVersion));
    return Error::success();
}

Error SpGameHost::callGame(int exportIndex, const std::vector<uint32_t>& args, uint32_t* result) {
    syncCvars();
    uint32_t r = 0;
    const uint32_t fn = cpu_->read32(exports_ + 4 * uint32_t(exportIndex));
    if (Error e = cpu_->call(fn, args, r))
        return e;
    if (result)
        *result = r;
    return Error::success();
}

int32_t SpGameHost::mapChecksum(const std::vector<uint8_t>& file) {
    const std::array<uint8_t, 16> digest = md4(file.data(), file.size());
    uint32_t sum = 0;
    for (int i = 0; i < 4; ++i)
        sum ^= uint32_t(digest[size_t(4 * i)]) | uint32_t(digest[size_t(4 * i + 1)]) << 8 |
               uint32_t(digest[size_t(4 * i + 2)]) << 16 | uint32_t(digest[size_t(4 * i + 3)]) << 24;
    return int32_t(sum);
}

Error SpGameHost::startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t checksum,
                             int32_t randomSeed) {
    if (!map)
        return Error::fail("No map");
    if (!exports_)
        return Error::fail("The game isn't loaded");
    map_ = std::move(map);
    world_ = std::make_unique<CollisionWorld>(*map_);
    areaPortals_ = std::make_unique<AreaPortals>(world_->areaCount());
    linked_.clear();
    for (uint32_t a : levelText_)
        process_->memory().free(a);
    levelText_.clear();
    cvars_.create("mapname", "", CvarStore::ServerInfo | CvarStore::ReadOnly);
    cvars_.set("mapname", mapName, true);
    cvars_.create("sv_mapChecksum", "", CvarStore::ReadOnly);
    cvars_.set("sv_mapChecksum", std::to_string(checksum), true);
    cvars_.create("sv_maxclients", "1", CvarStore::ServerInfo | CvarStore::Latch);
    cvars_.create("g_gametype", "0", CvarStore::ServerInfo | CvarStore::Latch);
    cvars_.create("sv_cheats", "1", CvarStore::ServerInfo | CvarStore::ReadOnly);
    levelTime_ = 0;
    const uint32_t now = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count() -
                                  startMs_);
    // Init(mapname, checksum, entity text, level time, random seed, global
    // time, a saved game just loaded: no).
    if (Error e = callGame(Init, {text(mapName), uint32_t(checksum), text(map_->entities), uint32_t(levelTime_),
                                  uint32_t(randomSeed), now, 0}))
        return e;
    // Frames to let the level settle before anyone joins, as the original's
    // server runs them (three, then one more, 100 ms apart): the NPCs spawn
    // in them, so they're there when the player's arrival starts the
    // level's scripts, which find the entities they affect as they load.
    for (int i = 0; i < 4; ++i) {
        if (Error e = callGame(RunFrame, {uint32_t(levelTime_)}))
            return e;
        levelTime_ += kSettleFrameMs;
    }
    // The server's and the system's settings, for the client game (as the
    // original's server sets them once the game is loaded).
    configstrings_[0] = infoString(CvarStore::ServerInfo);
    configstrings_[1] = infoString(CvarStore::SystemInfo);
    return Error::success();
}

Error SpGameHost::connectPlayer(const std::string& userinfo, std::string& refused) {
    userinfo_[0] = userinfo;
    uint32_t reason = 0;
    // ClientConnect(client, first time, saved game loaded)
    if (Error e = callGame(ClientConnect, {0, 1, 0}, &reason))
        return e;
    if (reason) {
        refused = cpu_->string(reason, 1024);
        return Error::success();
    }
    // ClientBegin(client, usercmd, saved game loaded): an empty command.
    const uint32_t cmd = process_->memory().alloc(64);
    cpu_->write32(cmd, uint32_t(levelTime_));
    const Error e = callGame(ClientBegin, {0, cmd, 0});
    process_->memory().free(cmd);
    return e;
}

void SpGameHost::startVoice(int32_t entity, const std::string& name) {
    auto log = [&](const std::string& text) {
        if (voiceLog)
            voiceLog("[" + std::to_string(levelTime_) + " ms] " + text);
    };
    if (entity < 0 || entity >= kMaxEntities || name.empty()) {
        log("voice not counted: entity " + std::to_string(entity) + ", sound '" + name + "'");
        return;
    }
    auto sound = soundLibrary_.find(name);
    if (!sound || sound->rate <= 0 || sound->frames() == 0) {
        log("voice not counted: " + name + " couldn't be read (entity " + std::to_string(entity) + ")");
        return;
    }
    log("+" + std::to_string(entity) + " " + name + " " +
        std::to_string(int64_t(sound->frames()) * 1000 / sound->rate) + " ms");
    // Its overall level, as the mean square of its samples' high bytes
    // (the measure the original compares a moment of it against).
    double sum = 0;
    const size_t frames = sound->frames(), step = std::max<size_t>(1, frames / 4096);
    size_t n = 0;
    for (size_t i = 0; i < frames; i += step, ++n) {
        const int v = sound->samples[i * size_t(sound->channels)] >> 8;
        sum += double(v * v);
    }
    voices_[entity] = {sound, levelTime_, float(n ? sum / double(n) : 0)};
    // Speaking from now, as the original marks it on starting (-1: started,
    // silent so far), so the game sees it in the same frame.
    if (voiceVolume_)
        cpu_->write32(voiceVolume_ + 4 * uint32_t(entity), 0xffffffffu);
}

void SpGameHost::updateVoiceVolumes() {
    // As the original's mixer does each pass: every entity 0, then each one
    // speaking given how loud its voice is where it has got to (the mean
    // square of ten samples a hundred apart) against s_threshold1-4 times
    // the sound's own level: -1 quiet, then 5 to 8. A line that has ended
    // leaves 0, which ends the scripts' wait for it.
    if (!voiceVolume_)
        return;
    std::memset(cpu_->memory().data() + voiceVolume_, 0, 4 * kMaxEntities);
    auto threshold = [&](const char* name, float fallback) {
        return cvars_.find(name) ? cvars_.value(name) : fallback;
    };
    const float t1 = threshold("s_threshold1", 0.5f), t2 = threshold("s_threshold2", 4.0f),
                t3 = threshold("s_threshold3", 7.0f), t4 = threshold("s_threshold4", 8.0f);
    for (auto it = voices_.begin(); it != voices_.end();) {
        const Voice& v = it->second;
        const size_t frames = v.sound->frames();
        const size_t at = size_t(int64_t(levelTime_ - v.startMs) * v.sound->rate / 1000);
        if (at >= frames) {
            if (voiceLog)
                voiceLog("[" + std::to_string(levelTime_) + " ms] -" + std::to_string(it->first));
            it = voices_.erase(it);
            continue;
        }
        double sum = 0;
        int n = 0;
        for (size_t i = at; i < frames && n < 10; i += 100, ++n) {
            const int s = v.sound->samples[i * size_t(v.sound->channels)] >> 8;
            sum += double(s * s);
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
        cpu_->write32(voiceVolume_ + 4 * uint32_t(it->first), uint32_t(value));
        ++it;
    }
}

Error SpGameHost::runFrame() {
    levelTime_ += kFrameMs;
    updateVoiceVolumes();
    if (Error e = callGame(RunFrame, {uint32_t(levelTime_)}))
        return e;
    if (cgameRunning_)
        makeSnapshot();
    return Error::success();
}

Error SpGameHost::consoleCommand(const std::string& text, bool* handled) {
    args_.clear();
    std::string word;
    bool quoted = false;
    for (char c : text) {
        if (c == '"')
            quoted = !quoted;
        else if (c == ' ' && !quoted) {
            if (!word.empty())
                args_.push_back(word);
            word.clear();
        } else
            word += c;
    }
    if (!word.empty())
        args_.push_back(word);
    uint32_t result = 0;
    const Error e = callGame(ConsoleCommand, {}, &result);
    if (handled)
        *handled = result != 0;
    return e;
}

// --- Entities ---------------------------------------------------------------------

uint32_t SpGameHost::entities() const { return exports_ ? cpu_->read32(exports_ + 14 * 4) : 0; }
int32_t SpGameHost::entitySize() const { return exports_ ? int32_t(cpu_->read32(exports_ + 15 * 4)) : 0; }
int32_t SpGameHost::entityCount() const { return exports_ ? int32_t(cpu_->read32(exports_ + 16 * 4)) : 0; }

int32_t SpGameHost::entityNumber(uint32_t address) const {
    const uint32_t base = entities();
    const int32_t size = entitySize();
    if (!base || size <= 0 || address < base)
        return -1;
    const uint32_t offset = address - base;
    return offset % uint32_t(size) == 0 && offset / uint32_t(size) < uint32_t(kMaxEntities) ? int32_t(offset / uint32_t(size))
                                                                                            : -1;
}

uint32_t SpGameHost::entityAddress(int32_t number) const {
    if (number < 0 || number >= kMaxEntities || !entities())
        return 0;
    return entities() + uint32_t(number) * uint32_t(entitySize());
}

float SpGameHost::readFloat(uint32_t address) const {
    const uint32_t u = cpu_->read32(address);
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

void SpGameHost::writeVec(uint32_t address, const Vec3f& v) {
    for (int i = 0; i < 3; ++i)
        cpu_->write32(address + 4 * uint32_t(i), floatBits(v[size_t(i)]));
}

int32_t SpGameHost::entityInt(int32_t number, int32_t offset) const {
    const uint32_t a = entityAddress(number);
    return a ? int32_t(cpu_->read32(a + uint32_t(offset))) : 0;
}

Vec3f SpGameHost::entityVec(int32_t number, int32_t offset) const {
    const uint32_t a = entityAddress(number);
    return a ? readVec(a + uint32_t(offset)) : Vec3f{};
}

void SpGameHost::linkEntity(uint32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0 || !cpu_->valid(address, uint32_t(kEntity.owner + 4)))
        return;
    linked_.insert(n);
    // Its box in the world, a unit larger all round; a turned brush model
    // by its farthest corner.
    const Vec3f origin = readVec(address + kEntity.currentOrigin), angles = readVec(address + kEntity.currentAngles);
    const Vec3f mins = readVec(address + kEntity.mins), maxs = readVec(address + kEntity.maxs);
    const bool bmodel = cpu_->read32(address + kEntity.bmodel) != 0;
    Vec3f absMin{}, absMax{};
    if (bmodel && (angles[0] || angles[1] || angles[2])) {
        float radius = 0;
        for (int i = 0; i < 3; ++i) {
            const float m = std::max(std::abs(mins[size_t(i)]), std::abs(maxs[size_t(i)]));
            radius += m * m;
        }
        radius = std::sqrt(radius);
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = origin[size_t(i)] - radius;
            absMax[size_t(i)] = origin[size_t(i)] + radius;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = origin[size_t(i)] + mins[size_t(i)];
            absMax[size_t(i)] = origin[size_t(i)] + maxs[size_t(i)];
        }
    }
    for (int i = 0; i < 3; ++i) {
        absMin[size_t(i)] -= 1;
        absMax[size_t(i)] += 1;
    }
    writeVec(address + kEntity.absMin, absMin);
    writeVec(address + kEntity.absMax, absMax);
    cpu_->write32(address + kEntity.linked, 1);
    // Its size for the client game: a brush model's marker, or a solid
    // box's half width, depth and height (+32), a byte each.
    int32_t solid = 0;
    if (bmodel) {
        solid = kSolidBrushModel;
    } else if (cpu_->read32(address + kEntity.contents) & (1 | 0x2000000)) {
        const auto clampByte = [](float v) { return std::clamp(int32_t(v), 1, 255); };
        solid = (clampByte(maxs[2] + 32) << 16) | (clampByte(-mins[2]) << 8) | clampByte(maxs[0]);
    }
    cpu_->write32(address + kEntity.solid, uint32_t(solid));
}

void SpGameHost::unlinkEntity(uint32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0)
        return;
    linked_.erase(n);
    cpu_->write32(address + kEntity.linked, 0);
}

std::pair<int, int> SpGameHost::entityAreas(int32_t n) const {
    std::pair<int, int> areas{-1, -1};
    if (!world_)
        return areas;
    std::vector<int> leafs;
    world_->boxLeafs(entityVec(n, kEntity.absMin), entityVec(n, kEntity.absMax), leafs);
    for (int leaf : leafs) {
        const int area = world_->leafArea(leaf);
        if (area < 0)
            continue;
        if (areas.first != -1 && areas.first != area)
            areas.second = area;
        else
            areas.first = area;
    }
    return areas;
}

bool SpGameHost::areasConnected(int area1, int area2) const {
    if (cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"))
        return true;
    return areaPortals_ && areaPortals_->connected(area1, area2);
}

TraceResult SpGameHost::traceMove(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                                  int32_t passEntity, int32_t mask, int32_t& hit) {
    TraceResult best;
    hit = kEntityNone;
    if (world_) {
        best = world_->trace(start, end, mins, maxs, 0, mask);
        hit = best.fraction < 1 ? kEntityWorld : kEntityNone;
    }
    Vec3f moveMin{}, moveMax{};
    for (int i = 0; i < 3; ++i) {
        moveMin[size_t(i)] = std::min(start[size_t(i)], end[size_t(i)]) + mins[size_t(i)] - 1;
        moveMax[size_t(i)] = std::max(start[size_t(i)], end[size_t(i)]) + maxs[size_t(i)] + 1;
    }
    auto ownerOf = [&](int32_t n) {
        const uint32_t owner = uint32_t(entityInt(n, kEntity.owner));
        return owner ? entityNumber(owner) : -1;
    };
    const int32_t passOwner = passEntity >= 0 && passEntity < kMaxEntities ? ownerOf(passEntity) : -1;
    for (int32_t n : linked_) {
        if (best.allSolid)
            break;
        // Not the mover, nor what it owns or what owns it.
        if (n == passEntity || (passEntity >= 0 && (ownerOf(n) == passEntity || n == passOwner)))
            continue;
        const int32_t contents = entityInt(n, kEntity.contents);
        if (!(contents & mask))
            continue;
        const Vec3f absMin = entityVec(n, kEntity.absMin), absMax = entityVec(n, kEntity.absMax);
        bool apart = false;
        for (int i = 0; i < 3; ++i)
            apart |= absMin[size_t(i)] > moveMax[size_t(i)] || absMax[size_t(i)] < moveMin[size_t(i)];
        if (apart)
            continue;
        const Vec3f origin = entityVec(n, kEntity.currentOrigin);
        TraceResult t;
        if (entityInt(n, kEntity.bmodel) && world_) {
            Vec3f s = start, e = end;
            for (int i = 0; i < 3; ++i) {
                s[size_t(i)] -= origin[size_t(i)];
                e[size_t(i)] -= origin[size_t(i)];
            }
            t = world_->trace(s, e, mins, maxs, entityInt(n, kEntity.modelIndex), mask);
            for (int i = 0; i < 3; ++i)
                t.endPos[size_t(i)] += origin[size_t(i)];
        } else {
            Vec3f boxMin = entityVec(n, kEntity.mins), boxMax = entityVec(n, kEntity.maxs);
            for (int i = 0; i < 3; ++i) {
                boxMin[size_t(i)] += origin[size_t(i)];
                boxMax[size_t(i)] += origin[size_t(i)];
            }
            t = CollisionWorld::traceBox(start, end, mins, maxs, boxMin, boxMax, contents);
        }
        if (t.allSolid || t.startSolid) {
            best.allSolid |= t.allSolid;
            best.startSolid = true;
            hit = n;
        }
        if (t.fraction < best.fraction) {
            const bool startSolid = best.startSolid;
            best = t;
            best.startSolid |= startSolid;
            hit = n;
        }
    }
    for (int i = 0; i < 3; ++i)
        best.endPos[size_t(i)] = start[size_t(i)] + best.fraction * (end[size_t(i)] - start[size_t(i)]);
    return best;
}

void SpGameHost::writeTrace(uint32_t r, const TraceResult& t, int32_t hit) {
    // Quake III's trace_t (56 bytes), as the original's engine fills it.
    if (!cpu_->valid(r, 56))
        return;
    cpu_->write32(r + 0, t.allSolid);
    cpu_->write32(r + 4, t.startSolid);
    cpu_->write32(r + 8, floatBits(t.fraction));
    writeVec(r + 12, t.endPos);
    writeVec(r + 24, t.normal);
    cpu_->write32(r + 36, floatBits(t.dist));
    int type = 3, signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (t.normal[size_t(i)] == 1 || t.normal[size_t(i)] == -1)
            type = i;
        if (t.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    cpu_->write32(r + 40, uint32_t(type | signs << 8));
    cpu_->write32(r + 44, uint32_t(t.surfaceFlags));
    cpu_->write32(r + 48, uint32_t(t.contents));
    cpu_->write32(r + 52, uint32_t(hit));
}

int32_t SpGameHost::pointContents(const Vec3f& point, int32_t passEntity) {
    int32_t result = world_ ? world_->pointContents(point, 0) : 0;
    for (int32_t n : linked_) {
        if (n == passEntity)
            continue;
        const Vec3f origin = entityVec(n, kEntity.currentOrigin);
        if (entityInt(n, kEntity.bmodel) && world_) {
            const Vec3f local{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
            if (world_->pointContents(local, entityInt(n, kEntity.modelIndex)))
                result |= entityInt(n, kEntity.contents);
        } else {
            const Vec3f mins = entityVec(n, kEntity.mins), maxs = entityVec(n, kEntity.maxs);
            bool inside = true;
            for (int i = 0; i < 3; ++i)
                inside &= point[size_t(i)] >= origin[size_t(i)] + mins[size_t(i)] &&
                          point[size_t(i)] <= origin[size_t(i)] + maxs[size_t(i)];
            if (inside)
                result |= entityInt(n, kEntity.contents);
        }
    }
    return result;
}

std::string SpGameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

// --- Cvars ------------------------------------------------------------------------

uint32_t SpGameHost::cvarStructure(const std::string& name) {
    const std::string key = lower(name);
    auto it = cvarMirrors_.find(key);
    if (it == cvarMirrors_.end()) {
        MirroredCvar m;
        m.address = process_->memory().alloc(kCvarStructureSize);
        const uint32_t nameText = process_->memory().alloc(uint32_t(name.size()) + 1);
        cpu_->writeString(nameText, name, uint32_t(name.size()) + 1);
        cpu_->write32(m.address, nameText);
        it = cvarMirrors_.emplace(key, m).first;
    }
    MirroredCvar& m = it->second;
    const CvarStore::Cvar* cv = cvars_.find(name);
    if (cv && cv->modificationCount != m.modificationCount) {
        if (m.text)
            process_->memory().free(m.text);
        m.text = process_->memory().alloc(uint32_t(cv->value.size()) + 1);
        cpu_->writeString(m.text, cv->value, uint32_t(cv->value.size()) + 1);
        cpu_->write32(m.address + 4, m.text);
        cpu_->write32(m.address + 16, uint32_t(cv->flags));
        if (m.modificationCount >= 0)
            cpu_->write32(m.address + 20, 1); // modified
        cpu_->write32(m.address + 24, uint32_t(cv->modificationCount));
        cpu_->write32(m.address + 28, floatBits(float(std::atof(cv->value.c_str()))));
        cpu_->write32(m.address + 32, uint32_t(std::atoi(cv->value.c_str())));
        m.modificationCount = cv->modificationCount;
    }
    return m.address;
}

void SpGameHost::syncCvars() {
    for (auto& [key, m] : cvarMirrors_) {
        const CvarStore::Cvar* cv = cvars_.find(key);
        if (cv && cv->modificationCount != m.modificationCount)
            cvarStructure(cv->name);
    }
}

// --- Files (for both halves of the DLL) ---------------------------------------------

// FS_FOpenFile(path, &handle, mode): read from the game's search path; what
// it writes is kept only until closed, for now.
uint32_t SpGameHost::fsOpen(x86::Cpu& c, uint32_t pathAddress, uint32_t handleAddress, uint32_t mode) {
    const std::string path = c.string(pathAddress, 256);
    if (mode != 0) {
        const uint32_t h = nextFile_++;
        openFiles_[h] = OpenFile{{}, 0, true};
        if (handleAddress)
            c.write32(handleAddress, h);
        return 0;
    }
    std::vector<uint8_t> data;
    if (files_.read(path, data)) {
        if (handleAddress)
            c.write32(handleAddress, 0);
        return 0xffffffffu;
    }
    const uint32_t length = uint32_t(data.size());
    if (handleAddress) {
        const uint32_t h = nextFile_++;
        openFiles_[h] = OpenFile{std::move(data), 0, false};
        c.write32(handleAddress, h);
    }
    return length;
}

uint32_t SpGameHost::fsRead(x86::Cpu& c, uint32_t buffer, uint32_t length, uint32_t handle) {
    auto it = openFiles_.find(handle);
    if (it == openFiles_.end() || !c.valid(buffer, length))
        return 0;
    OpenFile& f = it->second;
    const size_t n = std::min<size_t>(length, f.data.size() - f.position);
    std::memcpy(c.memory().data() + buffer, f.data.data() + f.position, n);
    f.position += n;
    return uint32_t(n);
}

uint32_t SpGameHost::fsWrite(x86::Cpu& c, uint32_t buffer, uint32_t length, uint32_t handle) {
    auto it = openFiles_.find(handle);
    if (it == openFiles_.end() || !c.valid(buffer, length))
        return 0;
    const uint8_t* p = c.memory().data() + buffer;
    it->second.data.insert(it->second.data.end(), p, p + length);
    return length;
}

// --- The client game's system calls -------------------------------------------------

void SpGameHost::writeVmCvar(uint32_t address, const CvarStore::Cvar& cv) {
    // vmCvar_t: handle, modification count, value, integer, text (256).
    if (!cpu_->valid(address, CvarStore::kVmCvarSize))
        return;
    cpu_->write32(address, uint32_t(cv.handle));
    cpu_->write32(address + 4, uint32_t(cv.modificationCount));
    cpu_->write32(address + 8, floatBits(std::strtof(cv.value.c_str(), nullptr)));
    cpu_->write32(address + 12, uint32_t(std::strtol(cv.value.c_str(), nullptr, 10)));
    cpu_->writeString(address + 16, cv.value, 256);
}

uint32_t SpGameHost::cgameCall(x86::Cpu& c) {
    // syscall(number, args...): the arguments after the number.
    auto a = [&c](int n) { return c.arg(n + 1); };
    auto str = [&](int n, size_t max = 1024) { return c.string(a(n), max); };
    const int number = int(c.arg(0));
    ++cgameCallCounts_[number];
    switch (number) {
    case 0: // Print(text)
        if (print)
            print(str(0, 4096));
        return 0;
    case 1: // Error(text)
        c.abort("efgamex86.dll (client game): " + str(0, 4096));
        return 0;
    case 2: return process_->milliseconds();
    case 3: { // Cvar_Register(vmCvar, name, value, flags)
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), int32_t(a(3)));
        if (a(0))
            writeVmCvar(a(0), cv);
        return 0;
    }
    case 4: // Cvar_Update(vmCvar)
        if (const CvarStore::Cvar* cv = cvars_.byHandle(int32_t(c.read32(a(0))));
            cv && int32_t(c.read32(a(0) + 4)) != cv->modificationCount)
            writeVmCvar(a(0), *cv);
        return 0;
    case 5: { // Cvar_Set(name, value)
        const std::string name = str(0, 256);
        cvars_.set(name, str(1, 1024));
        if (cvarMirrors_.count(lower(name)))
            cvarStructure(name);
        return 0;
    }
    case 6: return uint32_t(args_.size()); // Argc()
    case 7:                                 // Argv(n, buffer, size)
        c.writeString(a(1), a(0) < args_.size() ? args_[a(0)] : "", a(2));
        return 0;
    case 8: { // Args(buffer, size)
        std::string all;
        for (size_t i = 1; i < args_.size(); ++i)
            all += (i > 1 ? " " : "") + args_[i];
        c.writeString(a(0), all, a(1));
        return 0;
    }
    case 9: return fsOpen(c, a(0), a(1), a(2));
    case 10: return fsRead(c, a(0), a(1), a(2));
    case 11: return fsWrite(c, a(0), a(1), a(2));
    case 12: openFiles_.erase(a(0)); return 0;
    case 13: consoleCommands_.push_back(str(0, 4096)); return 0; // SendConsoleCommand
    case 14: clientCommands_.push_back(str(0, 256)); return 0;    // AddCommand(name)
    case 15: { // SendClientCommand(text): to the server game, as ClientCommand
        tokenize(str(0, 4096));
        uint32_t ignored = 0;
        const uint32_t fn = c.read32(exports_ + 4 * ClientCommand);
        if (Error e = c.call(fn, {0}, ignored))
            c.abort(e.message);
        return 0;
    }
    case 16: return 0; // UpdateScreen()
    // Collision, for the client game's prediction: the same map.
    case 17: return 0;                                                  // CM_LoadMap(name)
    case 18: return world_ ? uint32_t(world_->modelCount()) : 0;        // CM_NumInlineModels()
    case 19: return a(0);                                               // CM_InlineModel(n)
    case 20:                                                            // CM_TempBoxModel(mins, maxs)
        tempBoxes_.assign(1, {readVec(a(0)), readVec(a(1))});
        return kTempBoxModel;
    case 21:   // CM_PointContents(point, model)
    case 22: { // CM_TransformedPointContents(point, model, origin, angles)
        Vec3f p = readVec(a(0));
        if (number == 22) {
            const Vec3f origin = readVec(a(2));
            for (int i = 0; i < 3; ++i)
                p[size_t(i)] -= origin[size_t(i)];
        }
        if (a(1) == kTempBoxModel) {
            if (tempBoxes_.empty())
                return 0;
            for (int i = 0; i < 3; ++i)
                if (p[size_t(i)] < tempBoxes_[0].first[size_t(i)] || p[size_t(i)] > tempBoxes_[0].second[size_t(i)])
                    return 0;
            return 1;
        }
        return world_ ? uint32_t(world_->pointContents(p, int(a(1)))) : 0;
    }
    case 23: clientTrace(c, false); return 0; // CM_BoxTrace
    case 24: clientTrace(c, true); return 0;  // CM_TransformedBoxTrace
    case 25: return 0;                        // CM_MarkFragments: no wall marks yet
    // Sound, played if there's somewhere to play it.
    case 26: // S_StartSound(origin or 0, entity, channel, sfx)
        if (voiceLog && (a(2) == 3 || a(2) == 4 || sounds_.name(int32_t(a(3))).find("voice") != std::string::npos))
            voiceLog("[" + std::to_string(levelTime_) + " ms] sound on entity " + std::to_string(int32_t(a(1))) +
                     ", channel " + std::to_string(int32_t(a(2))) + ": " + sounds_.name(int32_t(a(3))));
        if (a(2) == 3 || a(2) == 4)
            startVoice(int32_t(a(1)), sounds_.name(int32_t(a(3))));
        if (soundSystem)
            soundSystem->startSound(a(0) ? std::optional<Vec3f>(readVec(a(0))) : std::nullopt, int(a(1)), int(a(2)),
                                    sounds_.name(int32_t(a(3))));
        return 0;
    case 27: // S_StartLocalSound(sfx, channel)
        if (voiceLog && (a(1) == 3 || a(1) == 4 || sounds_.name(int32_t(a(0))).find("voice") != std::string::npos))
            voiceLog("[" + std::to_string(levelTime_) + " ms] local sound, channel " + std::to_string(int32_t(a(1))) +
                     ": " + sounds_.name(int32_t(a(0))));
        if (soundSystem)
            soundSystem->startLocalSound(sounds_.name(int32_t(a(0))), int(a(1)));
        return 0;
    case 28: // S_ClearLoopingSounds()
        if (soundSystem)
            soundSystem->clearLoopingSounds();
        return 0;
    case 29: // S_AddLoopingSound(entity, origin, velocity, sfx)
        if (soundSystem)
            soundSystem->addLoopingSound(int(a(0)), readVec(a(1)), sounds_.name(int32_t(a(3))));
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
    case 32: return uint32_t(sounds_.add(str(0, 256))); // S_RegisterSound(name)
    case 33: // S_StartBackgroundTrack(intro, loop)
        if (soundSystem)
            soundSystem->startBackgroundTrack(str(0, 256), str(1, 256));
        return 0;
    case 34: case 35: case 36: case 37: return 0;       // force feedback: none
    // The renderer: what's registered and drawn is recorded.
    case 38: return 0; // R_LoadWorldMap(name)
    case 39: return uint32_t(models_.add(str(0, 256)));
    case 40: return uint32_t(skins_.add(str(0, 256)));
    case 41:
    case 42: return uint32_t(shaders_.add(str(0, 256)));
    case 43: scene_.clear(); return 0; // R_ClearScene
    case 44:                           // R_AddRefEntityToScene(refEntity)
        if (c.valid(a(0), kRefEntitySize))
            scene_.addEntity(c.memory().data() + a(0), kRefEntitySize, 0);
        return 0;
    case 45: { // R_LightForPoint(point, ambient, directed, direction): a plain light for now
        writeVec(a(1), {128, 128, 128});
        writeVec(a(2), {128, 128, 128});
        writeVec(a(3), {0, 0, 1});
        return 1;
    }
    case 46: { // R_AddPolyToScene(shader, vertex count, vertices)
        const uint32_t bytes = std::min<uint32_t>(a(1), 256) * 24;
        scene_.addPoly(int32_t(a(0)), int32_t(a(1)), bytes && c.valid(a(2), bytes) ? c.memory().data() + a(2) : nullptr);
        return 0;
    }
    case 47: // R_AddLightToScene(origin, intensity, r, g, b)
        scene_.addLight(readVec(a(0)), readFloat(c.reg[x86::Cpu::ESP] + 12), readFloat(c.reg[x86::Cpu::ESP] + 16),
                        readFloat(c.reg[x86::Cpu::ESP] + 20), readFloat(c.reg[x86::Cpu::ESP] + 24));
        return 0;
    case 48: // R_RenderScene(refdef)
        scene_.render(c.valid(a(0), kRefdefSize) ? c.memory().data() + a(0) : nullptr, pictures_.size());
        return 0;
    case 49: // R_SetColor(rgba, or 0 for white)
        if (!a(0))
            color_ = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i)
                color_[size_t(i)] = readFloat(a(0) + 4 * uint32_t(i));
        return 0;
    case 50: { // R_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader)
        auto f = [&](int n) { return readFloat(c.reg[x86::Cpu::ESP] + 8 + 4 * uint32_t(n)); };
        pictures_.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), int32_t(a(8)), color_});
        return 0;
    }
    case 51: case 54: return 0; // the saved game's picture; rotated pictures: not yet
    case 52: { // R_ModelBounds(model, mins, maxs)
        Vec3f mins{}, maxs{};
        const std::string& name = models_.name(int32_t(a(0)));
        if (!name.empty() && name[0] == '*') {
            if (world_)
                world_->modelBounds(std::atoi(name.c_str() + 1), mins, maxs);
        } else if (const Md3Model* m = md3(int32_t(a(0))); m && !m->frames.empty()) {
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
    case 55: return 0; // R_Scissor?
    case 56: writeGlconfig(a(0)); return 0;
    case 57: writeGameState(a(0)); return 0;
    case 58: // GetCurrentSnapshotNumber(&number, &serverTime)
        c.write32(a(0), uint32_t(snapshotNumber_));
        c.write32(a(1), snapshots_.empty() ? 0u : uint32_t(snapshots_.back().serverTime));
        return 0;
    case 59: { // GetSnapshot(number, out) -> whether it's still there
        const int32_t n = int32_t(a(0));
        if (n > snapshotNumber_) {
            c.abort("GetSnapshot: " + std::to_string(n) + " is ahead of the latest");
            return 0;
        }
        for (const Snapshot& snap : snapshots_)
            if (snap.number == n) {
                writeSnapshot(a(1), snap);
                return 1;
            }
        return 0;
    }
    case 60: { // GetServerCommand(sequence) -> whether it's there; its words for Argv
        std::vector<std::string> mine;
        for (const auto& [to, text] : serverCommands_)
            if (to == 0 || to == -1)
                mine.push_back(text);
        const int32_t n = int32_t(a(0));
        if (n < 1 || size_t(n) > mine.size())
            return 0;
        tokenize(mine[size_t(n - 1)]);
        return 1;
    }
    case 61: return uint32_t(commandNumber_); // GetCurrentCmdNumber()
    case 62: { // GetUserCmd(number, out) -> whether it's still kept
        const int32_t n = int32_t(a(0));
        if (n > commandNumber_ || n <= commandNumber_ - 64)
            return 0;
        writeUserCommand(a(1), commands_[size_t(n & 63)]);
        return 1;
    }
    case 63: clientWeapon_ = uint8_t(a(0)); return 0; // SetUserCmdValue(weapon, sensitivity)
    case 64: return 16u << 20;                        // MemoryRemaining()
    case 65: case 66: case 67: case 68: case 69: return 0; // ambient sound sets: not yet
    case 70: { // S_GetSampleLength(sfx): milliseconds, a float in ST(0), as the original's
        double ms = 0;
        const std::string& name = sounds_.name(int32_t(a(0)));
        if (!name.empty())
            if (auto sound = soundLibrary_.find(name); sound && sound->rate > 0)
                ms = double(sound->frames()) / double(sound->rate) * 1000.0;
        c.fpuPush(ms);
        return 0;
    }
    default: ++cgameUnhandled_[number]; return 0;
    }
}

int32_t SpGameHost::entityModel(const SceneEntity& e) {
    if (e.bytes.size() < 12)
        return 0;
    int32_t v;
    std::memcpy(&v, e.bytes.data() + 8, 4);
    return v;
}

const Md3Model* SpGameHost::md3(int32_t handle) {
    auto [it, added] = md3s_.try_emplace(handle);
    if (added) {
        const std::string& name = models_.name(handle);
        std::vector<uint8_t> data;
        auto m = std::make_shared<Md3Model>();
        if (!name.empty() && name[0] != '*' && !files_.read(name, data) && !parseModel(data, *m))
            it->second = std::move(m);
    }
    return it->second.get();
}

void SpGameHost::clientTrace(x86::Cpu& c, bool transformed) {
    // (results, start, end, mins, maxs, model, brushmask[, origin, angles]):
    // against one model only (the client game adds the entities itself).
    auto a = [&c](int n) { return c.arg(n + 1); };
    Vec3f start = readVec(a(1)), end = readVec(a(2));
    const Vec3f mins = a(3) ? readVec(a(3)) : Vec3f{}, maxs = a(4) ? readVec(a(4)) : Vec3f{};
    const uint32_t model = a(5);
    const int32_t mask = int32_t(a(6));
    Vec3f origin{};
    if (transformed) {
        origin = readVec(a(7));
        for (int i = 0; i < 3; ++i) {
            start[size_t(i)] -= origin[size_t(i)];
            end[size_t(i)] -= origin[size_t(i)];
        }
    }
    TraceResult t;
    if (model == kTempBoxModel) {
        if (!tempBoxes_.empty())
            t = CollisionWorld::traceBox(start, end, mins, maxs, tempBoxes_[0].first, tempBoxes_[0].second, 1);
    } else if (world_) {
        t = world_->trace(start, end, mins, maxs, int(model), mask);
    }
    for (int i = 0; i < 3; ++i)
        t.endPos[size_t(i)] = start[size_t(i)] + t.fraction * (end[size_t(i)] - start[size_t(i)]) + origin[size_t(i)];
    writeTrace(a(0), t, t.fraction < 1 ? kEntityWorld : kEntityNone);
}

void SpGameHost::tokenize(const std::string& text) {
    // Words split at spaces, with quoted runs kept whole.
    args_.clear();
    std::string word;
    bool quoted = false, any = false;
    for (char ch : text) {
        if (ch == '"') {
            quoted = !quoted;
            any = true;
        } else if ((ch == ' ' || ch == '\n') && !quoted) {
            if (any || !word.empty())
                args_.push_back(word);
            word.clear();
            any = false;
        } else {
            word += ch;
        }
    }
    if (any || !word.empty())
        args_.push_back(word);
}

void SpGameHost::writeGlconfig(uint32_t address) {
    // glconfig_t as the original engine fills it (5196 bytes): the
    // renderer's, vendor's and version text (1024 each) and the extensions
    // (2048), then Quake III's numbers from 5120 with two more of Elite
    // Force's before the screen size (at 5168 and 5172).
    constexpr uint32_t kSize = 5196, kNumbers = 5120;
    if (!cpu_->valid(address, kSize))
        return;
    std::memset(cpu_->memory().data() + address, 0, kSize);
    cpu_->writeString(address, "Reluva", 1024);
    cpu_->writeString(address + 1024, "Reluva", 1024);
    cpu_->writeString(address + 2048, "1.0", 1024);
    const uint32_t w = address + kNumbers;
    cpu_->write32(w + 0, 2048); // maxTextureSize
    cpu_->write32(w + 4, 2);    // texture units
    cpu_->write32(w + 8, 32);   // colour bits
    cpu_->write32(w + 12, 24);  // depth bits
    cpu_->write32(w + 16, 8);   // stencil bits
    cpu_->write32(w + 28, 1);   // deviceSupportsGamma
    cpu_->write32(w + 36, 1);   // textureEnvAddAvailable
    cpu_->write32(w + 48, 640); // vidWidth
    cpu_->write32(w + 52, 480); // vidHeight
    cpu_->write32(w + 56, floatBits(640.0f / 480.0f));
    cpu_->write32(w + 60, 60); // displayFrequency
}

void SpGameHost::writeGameState(uint32_t address) {
    // gameState_t (20100 bytes): each configstring's offset into the text
    // (1024), the text (16000), how much of it is used. Offset 0 is an
    // empty string.
    constexpr uint32_t kStrings = 1024, kChars = 16000, kSize = 4 * kStrings + kChars + 4;
    if (!cpu_->valid(address, kSize))
        return;
    std::memset(cpu_->memory().data() + address, 0, kSize);
    uint32_t used = 1;
    for (const auto& [index, value] : configstrings_) {
        if (index < 0 || uint32_t(index) >= kStrings || value.empty() || used + value.size() + 1 > kChars)
            continue;
        cpu_->write32(address + 4 * uint32_t(index), used);
        cpu_->writeString(address + 4 * kStrings + used, value, uint32_t(value.size()) + 1);
        used += uint32_t(value.size()) + 1;
    }
    cpu_->write32(address + 4 * kStrings + kChars, used);
}

void SpGameHost::writeUserCommand(uint32_t address, const UserCommand& cmd) {
    if (!cpu_->valid(address, kUserCmdSize))
        return;
    std::memset(cpu_->memory().data() + address, 0, kUserCmdSize);
    cpu_->write32(address, uint32_t(cmd.serverTime));
    cpu_->write32(address + 4, uint32_t(cmd.buttons));
    cpu_->write8(address + 8, cmd.weapon);
    for (int i = 0; i < 3; ++i)
        cpu_->write32(address + 12 + 4 * uint32_t(i), uint32_t(cmd.angles[size_t(i)]));
    cpu_->write8(address + 24, uint8_t(cmd.forward));
    cpu_->write8(address + 25, uint8_t(cmd.right));
    cpu_->write8(address + 26, uint8_t(cmd.up));
}

void SpGameHost::writeSnapshot(uint32_t address, const Snapshot& snap) {
    // snapshot_t as the original engine fills it: flags 0, ping 4, server
    // time 8, the area mask 12 (32 bytes), the command number 44, the
    // player state 48 (544), the entity count 592, the entities 596 (256
    // of 232 bytes), the server command sequence 60000.
    constexpr uint32_t kSize = 0xea64;
    if (!cpu_->valid(address, kSize))
        return;
    cpu_->write32(address, 0);
    cpu_->write32(address + 4, 0);
    cpu_->write32(address + 8, uint32_t(snap.serverTime));
    std::memcpy(cpu_->memory().data() + address + 12, snap.areaMask.data(), 32);
    cpu_->write32(address + 44, uint32_t(snap.commandNumber));
    std::memcpy(cpu_->memory().data() + address + 48, snap.playerState.data(), kPlayerStateSize);
    cpu_->write32(address + 0x250, uint32_t(snap.entities.size()));
    for (size_t i = 0; i < snap.entities.size(); ++i)
        std::memcpy(cpu_->memory().data() + address + 0x254 + kEntityStateSize * i, snap.entities[i].data(),
                    kEntityStateSize);
    cpu_->write32(address + 0xea60, uint32_t(snap.serverCommandSequence));
}

std::vector<int32_t> SpGameHost::visibleEntities(const Vec3f& eye) {
    // As the original's server picks them: entities in use and linked, not
    // flagged NOCLIENT (1); BROADCAST (0x20) ones always, the rest in an
    // area joined to the viewer's and a cluster it can see; a PORTAL
    // (0x40) adds what's seen from its other end (origin2, at 0x68).
    std::vector<int32_t> out;
    std::set<int32_t> added;
    const bool noAreas = cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas");
    const int32_t count = std::min(entityCount(), kMaxEntities);
    auto look = [&](const Vec3f& from, auto& self, int depth) -> void {
        const int leaf = world_ ? world_->leafAt(from) : -1;
        const int area = leaf >= 0 ? world_->leafArea(leaf) : -1;
        const int cluster = leaf >= 0 && size_t(leaf) < map_->leafs.size() ? map_->leafs[size_t(leaf)].cluster : -1;
        for (int32_t n = 0; n < count; ++n) {
            if (added.count(n) || !entityInt(n, kEntity.inUse) || !entityInt(n, kEntity.linked))
                continue;
            const int32_t flags = entityInt(n, kEntity.svFlags);
            if (flags & 1)
                continue;
            if (!(flags & 0x20) && world_) {
                const auto areas = entityAreas(n);
                if (!noAreas && area >= 0 && areas.first >= 0 && !areasConnected(area, areas.first) &&
                    (areas.second < 0 || !areasConnected(area, areas.second)))
                    continue;
                std::vector<int> leafs;
                world_->boxLeafs(entityVec(n, kEntity.absMin), entityVec(n, kEntity.absMax), leafs);
                bool seen = leafs.empty() || cluster < 0;
                for (const int l : leafs) {
                    const int c = map_->leafs[size_t(l)].cluster;
                    seen = seen || (c >= 0 && map_->clusterVisible(cluster, c));
                }
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
    snap.number = ++snapshotNumber_;
    snap.serverTime = levelTime_;
    snap.commandNumber = commandNumber_;
    snap.playerState.assign(kPlayerStateSize, 0);
    // The player's state: where the player's entity's client points.
    const uint32_t ps = uint32_t(entityInt(0, kEntity.client));
    if (ps && cpu_->valid(ps, kPlayerStateSize))
        std::memcpy(snap.playerState.data(), cpu_->memory().data() + ps, kPlayerStateSize);
    // From the client game's camera if one's running, else the player's
    // eye: origin (20) raised by the view height (168).
    Vec3f eye{};
    const uint32_t camera = process_->memory().alloc(16);
    uint32_t cameraOn = 0;
    if (cgameRunning_ && camera) {
        if (Error e = cpu_->call(cgameVmMain_, {CgCameraPos, camera}, cameraOn))
            cameraOn = 0;
    }
    if (cameraOn) {
        eye = readVec(camera);
    } else if (ps) {
        eye = readVec(ps + 20);
        eye[2] += float(int32_t(cpu_->read32(ps + 168)));
    }
    process_->memory().free(camera);
    for (const int32_t n : visibleEntities(eye)) {
        if (snap.entities.size() >= 256)
            break;
        const uint32_t a = entityAddress(n);
        snap.entities.emplace_back(cpu_->memory().data() + a, cpu_->memory().data() + a + kEntityStateSize);
    }
    for (const auto& [to, text] : serverCommands_)
        snap.serverCommandSequence += to == 0 || to == -1;
    if (world_ && areaPortals_ && !(cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"))) {
        const int area = world_->leafArea(world_->leafAt(eye));
        if (area >= 0)
            for (int a = 0; a < std::min(world_->areaCount(), 256); ++a)
                if (!areaPortals_->connected(area, a))
                    snap.areaMask[size_t(a >> 3)] |= uint8_t(1 << (a & 7));
    }
    snapshots_.push_back(std::move(snap));
    if (snapshots_.size() > 32)
        snapshots_.erase(snapshots_.begin());
}

Error SpGameHost::startClientGame() {
    cgameVmMain_ = module_->exportAddress("vmMain");
    if (!cgameVmMain_)
        return Error::fail("efgamex86.dll has no vmMain");
    cgameRunning_ = true;
    makeSnapshot();
    syncCvars();
    uint32_t ignored = 0;
    // CG_INIT's argument is the server commands received so far (the
    // original engine passes its client's command sequence), which the
    // client game counts on from.
    int32_t received = 0;
    for (const auto& [to, text] : serverCommands_)
        received += to == 0 || to == -1;
    return cpu_->call(cgameVmMain_, {CgInit, uint32_t(received)}, ignored);
}

Error SpGameHost::clientThink(const UserCommand& input) {
    UserCommand cmd = input;
    cmd.weapon = clientWeapon_;
    commands_[size_t(++commandNumber_ & 63)] = cmd;
    const uint32_t address = process_->memory().alloc(kUserCmdSize);
    writeUserCommand(address, cmd);
    const Error e = callGame(ClientThink, {0, address});
    process_->memory().free(address);
    return e;
}

Error SpGameHost::drawClientFrame(int32_t time) {
    scene_ = {};
    pictures_.clear();
    syncCvars();
    uint32_t ignored = 0;
    const Error e = cpu_->call(cgameVmMain_, {CgDrawActiveFrame, uint32_t(time), 0}, ignored);
    lastScene_ = std::move(scene_);
    lastPictures_ = std::move(pictures_);
    scene_ = {};
    pictures_.clear();
    return e;
}

bool SpGameHost::cameraPosition(Vec3f& at) {
    if (!cgameRunning_)
        return false;
    const uint32_t v = process_->memory().alloc(16);
    uint32_t on = 0;
    if (Error e = cpu_->call(cgameVmMain_, {CgCameraPos, v}, on))
        on = 0;
    at = readVec(v);
    process_->memory().free(v);
    return on != 0;
}

Error SpGameHost::clientConsoleCommand(const std::string& text) {
    tokenize(text);
    if (args_.empty())
        return Error::success();
    uint32_t handled = 0;
    if (cgameRunning_) {
        syncCvars();
        if (Error e = cpu_->call(cgameVmMain_, {CgConsoleCommand}, handled))
            return e;
    }
    if (handled)
        return Error::success();
    tokenize(text);
    return callGame(ClientCommand, {0});
}

// --- The engine's functions ---------------------------------------------------------

void SpGameHost::addImports() {
    auto str = [](x86::Cpu& c, int n, size_t max = 1024) { return c.string(c.arg(n), max); };
    auto say = [this](const std::string& s) {
        if (print)
            print(s);
    };

    import(Printf, [say](x86::Cpu& c) {
        say(x86::formatPrintf(c, c.arg(0), c.reg[x86::Cpu::ESP] + 8));
        return 0u;
    });
    import(WriteCam, [](x86::Cpu&) { return 0u; });
    import(Error_, [](x86::Cpu& c) {
        c.abort("efgamex86.dll: " + x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });
    import(Milliseconds, [this](x86::Cpu&) { return process_->milliseconds(); });
    import(Cvar, [this, str](x86::Cpu& c) {
        const std::string name = str(c, 0, 256);
        cvars_.create(name, str(c, 1, 256), int32_t(c.arg(2)));
        return cvarStructure(name);
    });
    import(CvarSet, [this, str](x86::Cpu& c) {
        const std::string name = str(c, 0, 256);
        cvars_.set(name, str(c, 1, 1024));
        if (cvarMirrors_.count(lower(name)))
            cvarStructure(name);
        return 0u;
    });
    import(CvarVariableIntegerValue, [this, str](x86::Cpu& c) { return uint32_t(cvars_.integer(str(c, 0, 256))); });
    import(CvarVariableStringBuffer, [this, str](x86::Cpu& c) {
        c.writeString(c.arg(1), cvars_.string(str(c, 0, 256)), c.arg(2));
        return 0u;
    });
    import(Argc, [this](x86::Cpu&) { return uint32_t(args_.size()); });
    import(Argv, [this](x86::Cpu& c) {
        const uint32_t n = c.arg(0);
        c.writeString(argvBuffer_, n < args_.size() ? args_[n] : "", kArgvSize);
        return argvBuffer_;
    });

    // Files: read from the game's search path; what it writes is kept
    // only until closed, for now.
    // Files.
    import(FsFOpenFile, [this](x86::Cpu& c) { return fsOpen(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(FsRead, [this](x86::Cpu& c) { return fsRead(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(FsWrite, [this](x86::Cpu& c) { return fsWrite(c, c.arg(0), c.arg(1), c.arg(2)); });
    import(FsFCloseFile, [this](x86::Cpu& c) {
        openFiles_.erase(c.arg(0));
        return 0u;
    });
    import(FsReadFile, [this, str](x86::Cpu& c) -> uint32_t {
        std::vector<uint8_t> data;
        if (files_.read(str(c, 0, 256), data)) {
            if (c.arg(1))
                c.write32(c.arg(1), 0);
            return 0xffffffffu;
        }
        if (c.arg(1)) {
            // A NUL after it, as the original's.
            const uint32_t buffer = process_->memory().alloc(uint32_t(data.size()) + 1);
            if (!buffer)
                return 0xffffffffu;
            std::memcpy(c.memory().data() + buffer, data.data(), data.size());
            c.write8(buffer + uint32_t(data.size()), 0);
            c.write32(c.arg(1), buffer);
        }
        return uint32_t(data.size());
    });
    import(FsFreeFile, [this](x86::Cpu& c) {
        process_->memory().free(c.arg(0));
        return 0u;
    });

    // Saved games: kept in memory for now.
    import(AppendToSaveGame, [this](x86::Cpu& c) -> uint32_t {
        const uint32_t data = c.arg(1), length = c.arg(2);
        if (!c.valid(data, length))
            return 0;
        const uint8_t* p = c.memory().data() + data;
        saveChunks_[chunkName(c.arg(0))].assign(p, p + length);
        return 1;
    });
    import(ReadFromSaveGame, [this](x86::Cpu& c) -> uint32_t {
        auto it = saveChunks_.find(chunkName(c.arg(0)));
        const uint32_t buffer = c.arg(1), length = c.arg(2);
        if (it == saveChunks_.end() || !c.valid(buffer, length)) {
            c.abort("ReadFromSaveGame: no chunk " + chunkName(c.arg(0)));
            return 0;
        }
        std::memcpy(c.memory().data() + buffer, it->second.data(), std::min<size_t>(length, it->second.size()));
        return 1;
    });

    import(SendConsoleCommand, [this, str](x86::Cpu& c) {
        consoleCommands_.push_back(str(c, 0, 4096));
        return 0u;
    });
    import(DropClient, [say, str](x86::Cpu& c) {
        say("DropClient: " + str(c, 1) + "\n");
        return 0u;
    });
    import(SendServerCommand, [this](x86::Cpu& c) {
        serverCommands_.emplace_back(int32_t(c.arg(0)), x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });
    import(SetConfigstring, [this, str](x86::Cpu& c) {
        const int32_t index = int32_t(c.arg(0));
        const std::string value = str(c, 1, 8192);
        configstrings_[index] = value;
        // Once the client game runs, it hears of changes as the original
        // engine tells it.
        if (cgameRunning_)
            serverCommands_.emplace_back(0, "cs " + std::to_string(index) + " \"" + value + "\"");
        return 0u;
    });
    import(GetConfigstring, [this](x86::Cpu& c) {
        auto it = configstrings_.find(int32_t(c.arg(0)));
        c.writeString(c.arg(1), it == configstrings_.end() ? std::string() : it->second, c.arg(2));
        return 0u;
    });
    import(GetUserinfo, [this](x86::Cpu& c) {
        auto it = userinfo_.find(int32_t(c.arg(0)));
        c.writeString(c.arg(1), it == userinfo_.end() ? std::string() : it->second, c.arg(2));
        return 0u;
    });
    import(SetUserinfo, [this, str](x86::Cpu& c) {
        userinfo_[int32_t(c.arg(0))] = str(c, 1);
        return 0u;
    });
    import(GetServerinfo, [this](x86::Cpu& c) {
        c.writeString(c.arg(0), infoString(CvarStore::ServerInfo), c.arg(1));
        return 0u;
    });

    // The world.
    import(SetBrushModel, [this, str](x86::Cpu& c) {
        const uint32_t ent = c.arg(0);
        const std::string name = str(c, 1, 64);
        if (name.size() < 2 || name[0] != '*') {
            c.abort("SetBrushModel: '" + name + "' isn't a brush model");
            return 0u;
        }
        const int model = std::atoi(name.c_str() + 1);
        Vec3f mins{}, maxs{};
        if (!world_ || model <= 0 || !world_->modelBounds(model, mins, maxs)) {
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
    import(Trace, [this](x86::Cpu& c) {
        const Vec3f zero{};
        int32_t hit = kEntityNone;
        const TraceResult t = traceMove(readVec(c.arg(1)), c.arg(2) ? readVec(c.arg(2)) : zero,
                                        c.arg(3) ? readVec(c.arg(3)) : zero, readVec(c.arg(4)), int32_t(c.arg(5)),
                                        int32_t(c.arg(6)), hit);
        writeTrace(c.arg(0), t, hit);
        return 0u;
    });
    import(PointContents, [this](x86::Cpu& c) { return uint32_t(pointContents(readVec(c.arg(0)), int32_t(c.arg(1)))); });
    auto inPvs = [this](x86::Cpu& c, bool ignorePortals) -> uint32_t {
        if (!world_)
            return 1;
        const int l1 = world_->leafAt(readVec(c.arg(0))), l2 = world_->leafAt(readVec(c.arg(1)));
        if (l1 < 0 || l2 < 0)
            return 0;
        if (!map_->clusterVisible(map_->leafs[size_t(l1)].cluster, map_->leafs[size_t(l2)].cluster))
            return 0;
        return ignorePortals || areasConnected(world_->leafArea(l1), world_->leafArea(l2)) ? 1 : 0;
    };
    import(InPvs, [inPvs](x86::Cpu& c) { return inPvs(c, false); });
    import(InPvsIgnorePortals, [inPvs](x86::Cpu& c) { return inPvs(c, true); });
    import(AdjustAreaPortalState, [this](x86::Cpu& c) {
        if (const int32_t n = entityNumber(c.arg(0)); n >= 0 && areaPortals_) {
            const auto [area1, area2] = entityAreas(n);
            if (area2 != -1)
                areaPortals_->adjust(area1, area2, c.arg(1) != 0);
        }
        return 0u;
    });
    import(AreasConnected, [this](x86::Cpu& c) { return areasConnected(int(c.arg(0)), int(c.arg(1))) ? 1u : 0u; });
    import(LinkEntity, [this](x86::Cpu& c) {
        linkEntity(c.arg(0));
        return 0u;
    });
    import(UnlinkEntity, [this](x86::Cpu& c) {
        unlinkEntity(c.arg(0));
        return 0u;
    });
    import(EntitiesInBox, [this](x86::Cpu& c) {
        // (mins, maxs, list of entity pointers, max) -> how many
        const Vec3f mins = readVec(c.arg(0)), maxs = readVec(c.arg(1));
        uint32_t count = 0;
        for (int32_t n : linked_) {
            if (count >= c.arg(3))
                break;
            const Vec3f absMin = entityVec(n, kEntity.absMin), absMax = entityVec(n, kEntity.absMax);
            bool apart = false;
            for (int i = 0; i < 3; ++i)
                apart |= absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)];
            if (!apart)
                c.write32(c.arg(2) + 4 * count++, entityAddress(n));
        }
        return count;
    });
    import(EntityContact, [this](x86::Cpu& c) -> uint32_t {
        const Vec3f mins = readVec(c.arg(0)), maxs = readVec(c.arg(1));
        const int32_t n = entityNumber(c.arg(2));
        if (n < 0)
            return 0;
        const Vec3f origin = entityVec(n, kEntity.currentOrigin);
        if (entityInt(n, kEntity.bmodel) && world_) {
            // The box moved into the model's own space.
            Vec3f lo = mins, hi = maxs;
            for (int i = 0; i < 3; ++i) {
                lo[size_t(i)] -= origin[size_t(i)];
                hi[size_t(i)] -= origin[size_t(i)];
            }
            const Vec3f centre{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            const Vec3f half{(hi[0] - lo[0]) / 2, (hi[1] - lo[1]) / 2, (hi[2] - lo[2]) / 2};
            const Vec3f negHalf{-half[0], -half[1], -half[2]};
            return world_->trace(centre, centre, negHalf, half, entityInt(n, kEntity.modelIndex), -1).startSolid ? 1 : 0;
        }
        const Vec3f absMin = entityVec(n, kEntity.absMin), absMax = entityVec(n, kEntity.absMax);
        for (int i = 0; i < 3; ++i)
            if (absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)])
                return 0;
        return 1;
    });
    // Not a function: the voices' volumes, one int an entity.
    cpu_->write32(imports_ + 4 * VoiceVolume, voiceVolume_);
    import(Malloc, [this](x86::Cpu& c) -> uint32_t {
        const uint32_t p = process_->memory().alloc(std::max(c.arg(0), 1u));
        if (!p)
            c.abort("Malloc: out of memory (" + std::to_string(c.arg(0)) + " bytes)");
        return p;
    });
    import(Free, [this](x86::Cpu& c) {
        process_->memory().free(c.arg(0));
        return 0u;
    });
}

} // namespace reluva::idtech3::eliteforce
