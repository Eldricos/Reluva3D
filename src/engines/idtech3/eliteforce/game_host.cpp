// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_layout.h"
#include "engines/idtech3/info_string.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force qagame.qvm system calls (Quake III's game order, the seen
// ones checked against the game's own calls; see
// docs/formats/idtech3/eliteforce-syscalls.md).
enum GameCall : int {
    Print = 0,
    Error_ = 1,
    Milliseconds = 2,
    CvarRegister = 3,
    CvarUpdate = 4,
    CvarSet = 5,
    CvarVariableIntegerValue = 6,
    CvarVariableStringBuffer = 7,
    Argc = 8,
    Argv = 9,
    FsFOpenFile = 10,
    FsRead = 11,
    FsWrite = 12,
    FsFCloseFile = 13,
    SendConsoleCommand = 14,
    LocateGameData = 15,
    DropClient = 16,
    SendServerCommand = 17,
    SetConfigstring = 18,
    GetConfigstring = 19,
    GetUserinfo = 20,
    SetUserinfo = 21,
    GetServerinfo = 22,
    SetBrushModel = 23,
    Trace = 24,
    PointContents = 25,
    InPvs = 26,
    InPvsIgnorePortals = 27,
    AdjustAreaPortalState = 28,
    AreasConnected = 29,
    LinkEntity = 30,
    UnlinkEntity = 31,
    EntitiesInBox = 32,
    EntityContact = 33,
    BotAllocateClient = 34,
    BotFreeClient = 35,
    GetUsercmd = 36,
    GetEntityToken = 37,
    FsGetFileList = 38,
    Ceil = 111, // Quake III's game numbers floor 110 and ceil 111; Elite Force's code uses 111
};



constexpr int32_t kTraceSize = 56; // Quake III's trace_t, as Elite Force's code reads it

} // namespace

GameHost::GameHost(const SearchSet& files, CvarStore& cvars)
    : files_(files), cvars_(cvars), vmFiles_(files),
      serverWorld_([this](int32_t n, EntityShape& out) { return entityShape(n, out); }) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

bool GameHost::entityShape(int32_t n, EntityShape& out) {
    // Entities without the shared part (not Elite Force's layout) have none.
    const int32_t a = entityAddress(n);
    if (a < 0 || gameData_.entitySize < layout_.sharedEnd || !vm_.valid(a, layout_.sharedEnd))
        return false;
    out.origin = readVec(a + layout_.currentOrigin);
    out.angles = readVec(a + layout_.currentAngles);
    out.mins = readVec(a + layout_.mins);
    out.maxs = readVec(a + layout_.maxs);
    out.contents = vm_.readInt(a + layout_.contents);
    out.owner = vm_.readInt(a + layout_.ownerNum);
    out.bmodel = vm_.readInt(a + layout_.bmodel) != 0;
    out.modelIndex = vm_.readInt(a + layout_.modelIndex);
    return true;
}

Error GameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/qagame.qvm", data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    layout_ = detectLayout(vm_);
    qvmData_ = std::move(data);
    return Error::success();
}

Error GameHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    return vm_.call(command, args, result);
}

Error GameHost::startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed) {
    if (!map)
        return Error::fail("No map");
    map_ = std::move(map);
    world_ = std::make_unique<CollisionWorld>(*map_);
    areaPortals_ = std::make_unique<AreaPortals>(world_->areaCount());
    closedDoorBlocks_ = 0;
    cvars_.create("cm_noAreas", "0", 0); // 1: every area joined, as the original's
    serverWorld_.setLevel(map_.get(), world_.get(), areaPortals_.get(),
                          [this] { return cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"); });
    // What the original server sets before starting the game code.
    cvars_.create("mapname", "", CvarStore::ServerInfo | CvarStore::ReadOnly);
    cvars_.set("mapname", mapName, true);
    cvars_.create("sv_maxclients", "8", CvarStore::ServerInfo | CvarStore::Latch | CvarStore::Archive);
    cvars_.create("g_gametype", "0", CvarStore::ServerInfo | CvarStore::Latch);
    cvars_.create("dedicated", "0", CvarStore::Latch);
    cvars_.create("sv_cheats", "0", CvarStore::ServerInfo | CvarStore::ReadOnly);
    // What the original's server adds for clients over the network
    // (eliteforce-network.md): its protocol and name in the server info,
    // and in the system info the level's id (changing with every level,
    // so commands from before it can be told apart), whether it's pure
    // (Reluva's isn't: it doesn't check clients' paks) and its frame rate.
    cvars_.create("protocol", "24", CvarStore::ServerInfo | CvarStore::ReadOnly);
    cvars_.create("sv_hostname", "Reluva", CvarStore::ServerInfo | CvarStore::Archive);
    cvars_.create("sv_serverid", "0", CvarStore::SystemInfo | CvarStore::ReadOnly);
    cvars_.set("sv_serverid", std::to_string(randomSeed & 0x7fffffff), true);
    cvars_.create("sv_pure", "0", CvarStore::SystemInfo);
    cvars_.set("sv_pure", "0", true);
    cvars_.create("sv_fps", "20", CvarStore::SystemInfo);
    // The engine's bot settings (the game reads bot_enable when starting).
    cvars_.create("bot_enable", "1", 0);
    cvars_.create("bot_thinktime", "100", 0);
    entityText_ = map_->entities;
    entityPos_ = 0;
    levelTime_ = 0;
    mapName_ = mapName;
    randomSeed_ = randomSeed;
    restartTime_ = 0;
    levelGametype_ = cvars_.string("g_gametype");
    levelMaxClients_ = cvars_.string("sv_maxclients");
    int32_t ignored = 0;
    if (Error err = call(Init, {levelTime_, randomSeed, 0}, ignored))
        return err;
    updateInfoConfigstrings();
    // "Run a few frames to allow everything to settle", as the original does.
    for (int i = 0; i < 3; ++i)
        if (Error err = runFrame())
            return err;
    return Error::success();
}

Error GameHost::runFrame() {
    // A restart asked for with a delay (map_restart 5), when it's time.
    if (restartTime_ && levelTime_ >= restartTime_)
        if (Error err = restartLevel())
            return err;
    levelTime_ += kFrameMs;
    // The bots think first (and send their commands), then the level runs.
    runBotFrame();
    int32_t ignored = 0;
    Error err = call(RunFrame, {levelTime_}, ignored);
    updateInfoConfigstrings();
    if (!err)
        runQueuedConsoleCommands();
    return err;
}

void GameHost::updateInfoConfigstrings() {
    // Configstrings 0 and 1 are the server's: every server-info cvar
    // (mapname, g_gametype...) and every system-info one, as the client
    // game reads them (it finds the map's name in 0).
    configstrings_[0] = infoString(CvarStore::ServerInfo);
    configstrings_[1] = infoString(CvarStore::SystemInfo);
}

Error GameHost::connectClient(int32_t clientNum, const std::string& userinfo, std::string& refused) {
    refused.clear();
    userinfo_[clientNum] = userinfo;
    clientsInUse_.insert(clientNum);
    int32_t reason = 0;
    if (Error err = call(ClientConnect, {clientNum, 1, 0}, reason)) // first time, not a bot
        return err;
    if (reason)
        refused = vm_.string(reason, 1024);
    return Error::success();
}

Error GameHost::reloadGame() {
    // The game code from scratch, as the original reloads it, and what
    // the engine knew of its entities forgotten.
    if (Error err = vm_.load(qvmData_))
        return Error::fail("vm/qagame.qvm: " + err.message);
    serverWorld_.clear();
    gameData_ = GameData{};
    restartTime_ = 0;
    if (world_) {
        areaPortals_ = std::make_unique<AreaPortals>(world_->areaCount());
        serverWorld_.setPortals(areaPortals_.get());
    }
    return Error::success();
}

Error GameHost::reconnectClients(const std::set<int32_t>& clients, bool beginHumans) {
    // Every player and bot connected again, not for the first time (the
    // game keeps what it saved of them); bots are put in the level at
    // once, players too if `beginHumans`.
    for (const int32_t c : clients) {
        const bool bot = botClients_.count(c) != 0;
        clientsInUse_.insert(c);
        int32_t reason = 0;
        if (Error err = call(ClientConnect, {c, 0, bot ? 1 : 0}, reason))
            return err;
        if (reason) {
            dropped_[c] = vm_.string(reason, 1024);
            drops_.emplace_back(c, dropped_[c]);
            continue;
        }
        if (bot || beginHumans)
            if (Error err = beginClient(c))
                return err;
    }
    return Error::success();
}

Error GameHost::restartLevel() {
    // map_restart, as the original: a variable that needs the level loaded
    // again (the game type, the number of players) makes it a full change
    // to the same map; otherwise the game code restarts in place, a few
    // frames settle it, and every player and bot is back in the game.
    restartTime_ = 0;
    if (cvars_.string("g_gametype") != levelGametype_ || cvars_.string("sv_maxclients") != levelMaxClients_) {
        requestedMap_ = mapName_;
        return Error::success();
    }
    int32_t ignored = 0;
    if (Error err = call(Shutdown, {1}, ignored))
        return err;
    if (Error err = reloadGame())
        return err;
    cvars_.set("sv_serverid", std::to_string((randomSeed_ + levelTime_ + 1) & 0x7fffffff), true);
    entityText_ = map_ ? map_->entities : std::string();
    entityPos_ = 0;
    if (Error err = call(Init, {levelTime_, randomSeed_ + levelTime_, 1}, ignored))
        return err;
    updateInfoConfigstrings();
    for (int i = 0; i < 3; ++i)
        if (Error err = runFrame())
            return err;
    const std::set<int32_t> clients = clientsInUse_;
    if (Error err = reconnectClients(clients, true))
        return err;
    ++restarts_;
    return Error::success();
}

Error GameHost::changeLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed,
                            std::vector<int32_t>& players) {
    // A new level, as the original's map command: the game code shut down
    // and started afresh on it, and everyone connected again; bots are put
    // in the level, players (`players`) when they've loaded it.
    const std::set<int32_t> clients = clientsInUse_;
    int32_t ignored = 0;
    if (Error err = call(Shutdown, {0}, ignored))
        return err;
    if (Error err = reloadGame())
        return err;
    configstrings_.clear();
    requestedMap_.clear();
    if (Error err = startLevel(mapName, std::move(map), randomSeed))
        return err;
    if (Error err = reconnectClients(clients, false))
        return err;
    players.clear();
    for (const int32_t c : clients)
        if (!botClients_.count(c) && clientsInUse_.count(c) && !dropped_.count(c))
            players.push_back(c);
    return Error::success();
}

bool GameHost::engineCommand(const std::vector<std::string>& w) {
    // The original engine's own console commands that the game runs
    // (SendConsoleCommand) or a player types at the server.
    if (w.empty())
        return false;
    std::string verb = w[0];
    for (char& ch : verb)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (verb == "map_restart") {
        // With a delay (5 seconds unless one is given) when g_doWarmup is
        // off: the restart time goes in the warmup configstring (5) for
        // the client game to count down.
        const int delay = w.size() > 1 ? std::atoi(w[1].c_str()) : 5;
        if (restartTime_)
            return true;
        if (delay > 0 && !cvars_.integer("g_doWarmup")) {
            restartTime_ = levelTime_ + delay * 1000;
            configstrings_[5] = std::to_string(restartTime_);
            return true;
        }
        if (Error err = restartLevel())
            if (print)
                print("map_restart: " + err.message + "\n");
        return true;
    }
    if (verb == "map" || verb == "devmap" || verb == "spmap" || verb == "spdevmap") {
        if (w.size() > 1)
            requestedMap_ = w[1];
        return true;
    }
    if (verb == "vstr") {
        // A variable's text run as commands (the game's nextmap).
        if (w.size() > 1)
            consoleCommands_.push_back(cvars_.string(w[1]));
        return true;
    }
    if (verb == "kick" || verb == "clientkick") {
        // By number (clientkick, or kick with a number), or by name.
        if (w.size() < 2)
            return true;
        int32_t target = -1;
        const std::string& who = w[1];
        if (verb == "clientkick" || (!who.empty() && std::isdigit(static_cast<unsigned char>(who[0]))))
            target = std::atoi(who.c_str());
        else
            for (const int32_t c : clientsInUse_) {
                const auto it = configstrings_.find(544 + c); // CS_PLAYERS
                if (it != configstrings_.end() && infoValue(it->second, "n") == who)
                    target = c;
            }
        if (target < 0 || !clientsInUse_.count(target))
            return true;
        if (botClients_.count(target)) {
            disconnectClient(target);
        } else {
            // Players: the network server sends them off and disconnects them.
            dropped_[target] = "was kicked";
            drops_.emplace_back(target, "was kicked");
        }
        return true;
    }
    if (verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") {
        if (w.size() > 2)
            cvars_.set(w[1], w[2]);
        return true;
    }
    if (verb == "wait")
        return true;
    // A variable's name and a value sets it, as the original's console.
    if (w.size() == 2 && cvars_.find(w[0])) {
        cvars_.set(w[0], w[1]);
        return true;
    }
    return false;
}

Error GameHost::userinfoChanged(int32_t clientNum, const std::string& userinfo) {
    userinfo_[clientNum] = userinfo;
    int32_t ignored = 0;
    return call(ClientUserinfoChanged, {clientNum}, ignored);
}

std::vector<uint8_t> GameHost::entityState(int32_t number) {
    const int32_t a = entityAddress(number);
    if (a < 0 || !vm_.valid(a, kEntityStateSize))
        return {};
    return {&vm_.memory()[size_t(a)], &vm_.memory()[size_t(a)] + kEntityStateSize};
}

Error GameHost::disconnectClient(int32_t clientNum) {
    int32_t ignored = 0;
    Error err = call(ClientDisconnect, {clientNum}, ignored);
    clientsInUse_.erase(clientNum);
    userinfo_.erase(clientNum);
    commands_.erase(clientNum);
    return err;
}

Error GameHost::clientCommand(int32_t clientNum, const std::string& text) {
    args_ = splitCommand(text);
    int32_t ignored = 0;
    Error err = call(ClientCommand, {clientNum}, ignored);
    args_.clear();
    return err;
}

Error GameHost::beginClient(int32_t clientNum) {
    int32_t ignored = 0;
    return call(ClientBegin, {clientNum}, ignored);
}

void GameHost::writeUserCommand(Qvm& vm, int32_t address, const UserCommand& cmd) {
    vm.writeInt(address, cmd.serverTime);
    // The buttons and the weapon a byte each, then two bytes of padding.
    vm.writeInt(address + 4, (cmd.buttons & 0xff) | int32_t(cmd.weapon) << 8);
    for (int i = 0; i < 3; ++i)
        vm.writeInt(address + 8 + 4 * i, cmd.angles[size_t(i)]);
    vm.memory()[size_t(address + 20)] = uint8_t(cmd.forward);
    vm.memory()[size_t(address + 21)] = uint8_t(cmd.right);
    vm.memory()[size_t(address + 22)] = uint8_t(cmd.up);
    vm.memory()[size_t(address + 23)] = 0;
}

Error GameHost::clientThink(int32_t clientNum, const UserCommand& cmd) {
    commands_[clientNum] = cmd;
    int32_t ignored = 0;
    return call(ClientThink, {clientNum}, ignored);
}

std::string GameHost::nextEntityToken() {
    const std::string& s = entityText_;
    size_t& i = entityPos_;
    while (true) {
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ' && s[i] != '\0')
            ++i;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            continue;
        }
        break;
    }
    if (i >= s.size() || s[i] == '\0')
        return {};
    std::string token;
    if (s[i] == '"') {
        ++i;
        while (i < s.size() && s[i] != '"' && s[i] != '\0')
            token += s[i++];
        if (i < s.size() && s[i] == '"')
            ++i;
        return token;
    }
    while (i < s.size() && static_cast<unsigned char>(s[i]) > ' ')
        token += s[i++];
    return token;
}

int32_t GameHost::entityNumber(int32_t address) const {
    if (!gameData_.entitySize || address < gameData_.entities)
        return -1;
    const int32_t offset = address - gameData_.entities;
    return offset % gameData_.entitySize == 0 ? offset / gameData_.entitySize : -1;
}

int32_t GameHost::entityAddress(int32_t number) const {
    if (number < 0 || number >= gameData_.count || gameData_.entitySize < layout_.sharedEnd)
        return -1;
    return gameData_.entities + number * gameData_.entitySize;
}

Vec3f GameHost::readVec(int32_t address) {
    Vec3f v{};
    if (address)
        for (int i = 0; i < 3; ++i)
            v[size_t(i)] = Qvm::asFloat(vm_.readInt(address + 4 * i));
    return v;
}

void GameHost::writeVec(int32_t address, const Vec3f& v) {
    for (int i = 0; i < 3; ++i)
        vm_.writeInt(address + 4 * i, Qvm::fromFloat(v[size_t(i)]));
}

int32_t GameHost::entityInt(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? 0 : vm_.readInt(a + offset);
}

Vec3f GameHost::entityVec(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? Vec3f{} : readVec(a + offset);
}

int32_t GameHost::clientInt(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !gameData_.clientSize || offset + 4 > gameData_.clientSize)
        return 0;
    return vm_.readInt(gameData_.clients + clientNum * gameData_.clientSize + offset);
}

Vec3f GameHost::clientVec(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !gameData_.clientSize || offset + 12 > gameData_.clientSize)
        return {};
    return readVec(gameData_.clients + clientNum * gameData_.clientSize + offset);
}

std::pair<int, int> GameHost::svFlagsEvidence() {
    // svFlags holds small flag bits (Quake III's SVF_NOCLIENT is 1, for
    // triggers and other entities the client never sees); the other word
    // (linkcount or singleClient, never written by Reluva) stays 0.
    int at208 = 0, at212 = 0;
    for (int32_t n : serverWorld_.linked()) {
        const int32_t v208 = entityInt(n, 208), v212 = entityInt(n, 212);
        at208 += v208 > 0 && v208 < 0x10000;
        at212 += v212 > 0 && v212 < 0x10000;
    }
    return {at208, at212};
}

int32_t GameHost::svFlagsOffset() {
    if (layout_.svFlags >= 0)
        return layout_.svFlags;
    const auto [at208, at212] = svFlagsEvidence();
    return at208 > at212 ? 208 : 212;
}

GameHost::Snapshot GameHost::snapshot(int32_t clientNum) {
    ++snapshotsMade_;
    Snapshot snap;
    snap.serverTime = levelTime_;
    const int32_t size = layout_.playerStateSize;
    snap.playerState.assign(size_t(size), 0);
    const int32_t client = gameData_.clients + clientNum * gameData_.clientSize;
    if (gameData_.clientSize >= size && vm_.valid(client, size))
        std::memcpy(snap.playerState.data(), &vm_.memory()[size_t(client)], size_t(size));
    // The original sends the player state over its network protocol, which
    // carries only some fields: the rest reach the client game as 0, even
    // on a listen server. The word at 48, a timer of the server's (set to
    // 200 on firing, counting down without stopping at 0), is one: the
    // client game draws the phaser's beam only while it's 0 (see
    // eliteforce-syscalls.md).
    for (const int32_t at : {kPlayerStateServerOnly})
        std::memset(&snap.playerState[size_t(at)], 0, 4);
    // The entities the player can see, as the original's server picks them
    // (visibleEntities()); at most 256, the snapshot's room.
    for (const int32_t n : visibleEntities(clientNum)) {
        const int32_t a = entityAddress(n);
        if (a < 0 || snap.entities.size() >= 256)
            continue;
        snap.entities.emplace_back(&vm_.memory()[size_t(a)], &vm_.memory()[size_t(a)] + kEntityStateSize);
    }
    for (const auto& [to, text] : serverCommands_)
        snap.serverCommandSequence += to == clientNum || to == -1;
    // The areas: from the player's eye (taken as 26 units up, the
    // standing view height), each not joined to its area hidden. Outside
    // any area, or with cm_noAreas, none.
    if (world_ && areaPortals_ && !(cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas"))) {
        const Vec3f eye = eyeOf(clientNum);
        const int area = world_->leafArea(world_->leafAt(eye));
        if (area >= 0)
            for (int a = 0; a < std::min(world_->areaCount(), 256); ++a)
                if (!areaPortals_->connected(area, a))
                    snap.areaMask[size_t(a >> 3)] |= uint8_t(1 << (a & 7));
    }
    return snap;
}

void GameHost::linkEntity(int32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0)
        return;
    EntityShape shape;
    if (!entityShape(n, shape) || !vm_.checkWrite(address, layout_.sharedEnd)) {
        serverWorld_.linkUnplaced(n); // only recorded
        return;
    }
    Vec3f absMin{}, absMax{};
    serverWorld_.link(n, shape, absMin, absMax);
    writeVec(address + layout_.absMin, absMin);
    writeVec(address + layout_.absMax, absMax);
    vm_.writeInt(address + layout_.linked, 1);
    vm_.writeInt(address + layout_.solid, ServerWorld::packedSolid(shape, kSolidBrushModel));
}

Vec3f GameHost::eyeOf(int32_t clientNum) {
    // The player's eye: with patch 1.2's layout, the player state's origin
    // (20) raised by its view height (180), as the original's server takes
    // it; otherwise the body's origin and the standing view height, 26.
    const int32_t client = gameData_.clients + clientNum * gameData_.clientSize;
    if (layout_.version == 12 && gameData_.clientSize >= layout_.playerStateSize &&
        vm_.valid(client, layout_.playerStateSize)) {
        Vec3f eye = readVec(client + 20);
        eye[2] += float(vm_.readInt(client + 180));
        return eye;
    }
    Vec3f eye = entityVec(clientNum, layout_.currentOrigin);
    eye[2] += 26;
    return eye;
}

std::vector<int32_t> GameHost::visibleEntities(int32_t clientNum) {
    // As the original's server builds a snapshot: never the player's own
    // entity (the client game makes it from the player state), nor those
    // flagged not to be sent (SVF_NOCLIENT 1), nor, with patch 1.2's
    // layout, those for one other client (SVF_SINGLECLIENT 0x100), those for
    // all but this one (SVF_NOTSINGLECLIENT 0x800) or not in a client mask
    // that leaves it out (Elite Force's SVF_CLIENTMASK 4), each by
    // singleClient. Those sent to all (SVF_BROADCAST 0x20) always; the rest
    // only in an area the player's joins (behind no closed door) and in a
    // cluster the player's can see. A portal's camera (SVF_PORTAL 0x40)
    // adds what can be seen from its other end (origin2).
    std::vector<int32_t> out;
    const int32_t svFlags = svFlagsOffset();
    const bool singleClient = layout_.version == 12;
    const bool noAreas = cvars_.find("cm_noAreas") && cvars_.integer("cm_noAreas");
    std::set<int32_t> added;
    auto look = [&](const Vec3f& from, auto& self, int depth) -> void {
        const int leaf = world_ ? world_->leafAt(from) : -1;
        const int area = leaf >= 0 ? world_->leafArea(leaf) : -1;
        const int cluster = leaf >= 0 && map_ && size_t(leaf) < map_->leafs.size() ? map_->leafs[size_t(leaf)].cluster : -1;
        for (const int32_t n : serverWorld_.linked()) {
            if (n == clientNum || added.count(n))
                continue;
            const int32_t flags = entityInt(n, svFlags);
            if (flags & 1)
                continue;
            if (singleClient) {
                const int32_t single = entityInt(n, 216);
                if (((flags & 0x100) && single != clientNum) || ((flags & 0x800) && single == clientNum) ||
                    ((flags & 0x4) && (clientNum >= 32 || !(single & (1 << clientNum)))))
                    continue;
            }
            if (!(flags & 0x20)) {
                const ServerWorld::Placement* place = serverWorld_.placement(n);
                if (!place)
                    continue;
                const ServerWorld::Placement& es = *place;
                if (!noAreas && area >= 0 && es.areas.first >= 0 && areaPortals_ &&
                    !areaPortals_->connected(area, es.areas.first) &&
                    (es.areas.second < 0 || !areaPortals_->connected(area, es.areas.second)))
                    continue;
                if (cluster >= 0 && map_ && !es.clusters.empty()) {
                    bool seen = false;
                    for (const int c : es.clusters)
                        seen = seen || map_->clusterVisible(cluster, c);
                    if (!seen)
                        continue;
                }
            }
            added.insert(n);
            out.push_back(n);
            if ((flags & 0x40) && depth < 2)
                self(entityVec(n, 104), self, depth + 1); // s.origin2
        }
    };
    look(eyeOf(clientNum), look, 0);
    std::sort(out.begin(), out.end());
    return out;
}

std::pair<int, int> GameHost::entityAreas(int32_t n) {
    // The areas its box (as last linked) touches.
    const ServerWorld::Placement* p = serverWorld_.placement(n);
    return p ? p->areas : std::pair<int, int>{-1, -1};
}

bool GameHost::areasConnected(int area1, int area2) const { return serverWorld_.areasConnected(area1, area2); }

TraceResult GameHost::traceMove(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                                int32_t passEntity, int32_t mask, int32_t& hit, bool entities) {
    return serverWorld_.trace(start, mins, maxs, end, passEntity, mask, hit, entities);
}

void GameHost::clipMove(int32_t results, const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                        int32_t passEntity, int32_t mask) {
    if (!vm_.checkWrite(results, kTraceSize))
        return;
    int32_t hit = kEntityNone;
    const TraceResult best = traceMove(start, mins, maxs, end, passEntity, mask, hit);
    // Quake III's trace_t: allsolid, startsolid, fraction, endpos, the
    // plane (normal, dist, type, sign bits), surfaceFlags, contents,
    // entityNum.
    vm_.writeInt(results + 0, best.allSolid);
    vm_.writeInt(results + 4, best.startSolid);
    vm_.writeInt(results + 8, Qvm::fromFloat(best.fraction));
    writeVec(results + 12, best.endPos);
    writeVec(results + 24, best.normal);
    vm_.writeInt(results + 36, Qvm::fromFloat(best.dist));
    int type = 3, signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (best.normal[size_t(i)] == 1 || best.normal[size_t(i)] == -1)
            type = i;
        if (best.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    vm_.memory()[size_t(results + 40)] = uint8_t(type);
    vm_.memory()[size_t(results + 41)] = uint8_t(signs);
    vm_.memory()[size_t(results + 42)] = 0;
    vm_.memory()[size_t(results + 43)] = 0;
    vm_.writeInt(results + 44, best.surfaceFlags);
    vm_.writeInt(results + 48, best.contents);
    vm_.writeInt(results + 52, hit);
}

int32_t GameHost::pointContents(const Vec3f& point, int32_t passEntity) {
    return serverWorld_.pointContents(point, passEntity);
}

std::string GameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

int32_t GameHost::systemCall(Qvm& vm, int number, const int32_t* a) {
    if (trace)
        trace(number, a);
    callCounts_.add(number);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case Print: {
        const std::string text = str(0, 4096);
        // Kept (the last 4 KB) as well as handed on.
        log_ += text;
        if (log_.size() > 8192)
            log_.erase(0, log_.size() - 4096);
        if (print)
            print(text);
        return 0;
    }
    case Error_: vm.abort("qagame.qvm: " + str(0)); return 0;
    case Milliseconds: return levelTime_;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
    case CvarVariableIntegerValue: return cvars_.integer(str(0, 256));
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case Argc: return int32_t(args_.size());
    case Argv: vm.writeString(a[1], a[0] >= 0 && size_t(a[0]) < args_.size() ? args_[size_t(a[0])] : "", a[2]); return 0;
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case FsGetFileList: return vmFiles_.list(vm, a[0], a[1], a[2], a[3]);
    case SendConsoleCommand: consoleCommands_.push_back(str(1, 4096)); return 0;
    case LocateGameData:
        gameData_ = {a[0], a[1], a[2], a[3], a[4]};
        return 0;
    case SendServerCommand: serverCommands_.emplace_back(a[0], str(1, 8192)); return 0;
    case DropClient:
        dropped_[a[0]] = str(1, 1024);
        drops_.emplace_back(a[0], dropped_[a[0]]);
        return 0;
    case GetUserinfo: {
        const auto it = userinfo_.find(a[0]);
        vm.writeString(a[1], it == userinfo_.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case SetUserinfo: userinfo_[a[0]] = str(1, 1024); return 0;
    case GetUsercmd: { // (clientNum, usercmd_t*): the last command, or an empty one
        const auto it = commands_.find(a[0]);
        const UserCommand cmd = it == commands_.end() ? UserCommand{levelTime_} : it->second;
        if (!vm.checkWrite(a[1], 24))
            return 0;
        writeUserCommand(vm, a[1], cmd);
        return 0;
    }
    case SetConfigstring: configstrings_[a[0]] = str(1, 8192); return 0;
    case GetConfigstring: {
        const auto it = configstrings_.find(a[0]);
        vm.writeString(a[1], it == configstrings_.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case GetServerinfo: vm.writeString(a[0], infoString(CvarStore::ServerInfo), a[1]); return 0;
    case LinkEntity: linkEntity(a[0]); return 0;
    case UnlinkEntity:
        if (const int32_t n = entityNumber(a[0]); n >= 0) {
            serverWorld_.unlink(n);
            if (gameData_.entitySize >= layout_.sharedEnd)
                vm.writeInt(a[0] + layout_.linked, 0);
        }
        return 0;
    case SetBrushModel: { // (ent, "*N"): one of the map's brush models
        const std::string name = str(1, 64);
        if (name.size() < 2 || name[0] != '*') {
            vm.abort("SetBrushModel: '" + name + "' isn't a brush model");
            return 0;
        }
        const int model = std::atoi(name.c_str() + 1);
        Vec3f mins{}, maxs{};
        if (!world_ || model <= 0 || !world_->modelBounds(model, mins, maxs)) {
            vm.abort("SetBrushModel: the map has no model " + name);
            return 0;
        }
        if (gameData_.entitySize < layout_.sharedEnd || !vm.checkWrite(a[0], layout_.sharedEnd))
            return 0;
        vm.writeInt(a[0] + layout_.modelIndex, model);
        vm.writeInt(a[0] + layout_.bmodel, 1);
        writeVec(a[0] + layout_.mins, mins);
        writeVec(a[0] + layout_.maxs, maxs);
        vm.writeInt(a[0] + layout_.contents, -1); // whatever its brushes hold
        linkEntity(a[0]);
        return 0;
    }
    case Trace: {
        const Vec3f zero{};
        clipMove(a[0], readVec(a[1]), a[2] ? readVec(a[2]) : zero, a[3] ? readVec(a[3]) : zero, readVec(a[4]), a[5],
              a[6]);
        return 0;
    }
    case PointContents: return pointContents(readVec(a[0]), a[1]);
    case InPvs:
    case InPvsIgnorePortals: {
        if (!world_)
            return 1;
        const int l1 = world_->leafAt(readVec(a[0])), l2 = world_->leafAt(readVec(a[1]));
        if (l1 < 0 || l2 < 0)
            return 0;
        if (!map_->clusterVisible(map_->leafs[size_t(l1)].cluster, map_->leafs[size_t(l2)].cluster))
            return 0;
        // Not through a closed door, unless asked to ignore portals.
        if (number == InPvsIgnorePortals || areasConnected(world_->leafArea(l1), world_->leafArea(l2)))
            return 1;
        ++closedDoorBlocks_;
        return 0;
    }
    case AdjustAreaPortalState: // (entity, open): the portal its door stands in
        if (const int32_t n = entityNumber(a[0]); n >= 0 && areaPortals_ && gameData_.entitySize >= layout_.sharedEnd) {
            const auto [area1, area2] = entityAreas(n);
            if (area2 != -1)
                areaPortals_->adjust(area1, area2, a[1] != 0);
        }
        return 0;
    case AreasConnected: return areasConnected(a[0], a[1]);
    case EntitiesInBox: { // (mins, maxs, list, max) -> how many
        const Vec3f mins = readVec(a[0]), maxs = readVec(a[1]);
        std::vector<int32_t> found;
        serverWorld_.entitiesInBox(mins, maxs, found);
        int32_t count = 0;
        for (const int32_t n : found) {
            if (count >= a[3])
                break;
            vm.writeInt(a[2] + 4 * count++, n);
        }
        return count;
    }
    case EntityContact: { // (mins, maxs, ent): whether the box touches it
        const Vec3f mins = readVec(a[0]), maxs = readVec(a[1]);
        const int32_t n = entityNumber(a[2]);
        if (n < 0)
            return 0;
        const Vec3f origin = entityVec(n, layout_.currentOrigin);
        if (entityInt(n, layout_.bmodel) && world_) {
            const Vec3f at{-origin[0], -origin[1], -origin[2]};
            return world_->trace(at, at, mins, maxs, entityInt(n, layout_.modelIndex), -1).startSolid;
        }
        const Vec3f absMin = entityVec(n, layout_.absMin), absMax = entityVec(n, layout_.absMax);
        for (int i = 0; i < 3; ++i)
            if (absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)])
                return 0;
        return 1;
    }
    case GetEntityToken: {
        const std::string token = nextEntityToken();
        vm.writeString(a[0], token, a[1]);
        return token.empty() ? 0 : 1;
    }
    case Ceil: return Qvm::fromFloat(std::ceil(Qvm::asFloat(a[0])));
    default:
        if (number == BotAllocateClient || number == BotFreeClient || number >= 200)
            return botCall(vm, number, a);
        ++unhandled_[number];
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
