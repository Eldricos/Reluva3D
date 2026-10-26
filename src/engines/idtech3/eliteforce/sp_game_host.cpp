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
    return callGame(Init, {text(mapName), uint32_t(checksum), text(map_->entities), uint32_t(levelTime_),
                           uint32_t(randomSeed), now, 0});
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

Error SpGameHost::runFrame() {
    levelTime_ += kFrameMs;
    return callGame(RunFrame, {uint32_t(levelTime_)});
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
    import(FsFOpenFile, [this, str](x86::Cpu& c) -> uint32_t {
        const std::string path = str(c, 0, 256);
        const uint32_t handleAddress = c.arg(1), mode = c.arg(2);
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
    });
    import(FsRead, [this](x86::Cpu& c) -> uint32_t {
        auto it = openFiles_.find(c.arg(2));
        const uint32_t buffer = c.arg(0), length = c.arg(1);
        if (it == openFiles_.end() || !c.valid(buffer, length))
            return 0;
        OpenFile& f = it->second;
        const size_t n = std::min<size_t>(length, f.data.size() - f.position);
        std::memcpy(c.memory().data() + buffer, f.data.data() + f.position, n);
        f.position += n;
        return uint32_t(n);
    });
    import(FsWrite, [this](x86::Cpu& c) -> uint32_t {
        auto it = openFiles_.find(c.arg(2));
        const uint32_t buffer = c.arg(0), length = c.arg(1);
        if (it == openFiles_.end() || !c.valid(buffer, length))
            return 0;
        const uint8_t* p = c.memory().data() + buffer;
        it->second.data.insert(it->second.data.end(), p, p + length);
        return length;
    });
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
        configstrings_[int32_t(c.arg(0))] = str(c, 1, 8192);
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
