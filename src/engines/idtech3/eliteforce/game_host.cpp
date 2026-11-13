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
// docs/engines/idtech3/games/stvef/systems/syscalls.md).
enum GameCall : int {
    kPrint = 0,
    kError = 1,
    kMilliseconds = 2,
    kCvarRegister = 3,
    kCvarUpdate = 4,
    kCvarSet = 5,
    kCvarVariableIntegerValue = 6,
    kCvarVariableStringBuffer = 7,
    kArgc = 8,
    kArgv = 9,
    kFsFOpenFile = 10,
    kFsRead = 11,
    kFsWrite = 12,
    kFsFCloseFile = 13,
    kSendConsoleCommand = 14,
    kLocateGameData = 15,
    kDropClient = 16,
    kSendServerCommand = 17,
    kSetConfigstring = 18,
    kGetConfigstring = 19,
    kGetUserinfo = 20,
    kSetUserinfo = 21,
    kGetServerinfo = 22,
    kSetBrushModel = 23,
    kTrace = 24,
    kPointContents = 25,
    kInPvs = 26,
    kInPvsIgnorePortals = 27,
    kAdjustAreaPortalState = 28,
    kAreasConnected = 29,
    kLinkEntity = 30,
    kUnlinkEntity = 31,
    kEntitiesInBox = 32,
    kEntityContact = 33,
    kBotAllocateClient = 34,
    kBotFreeClient = 35,
    kGetUsercmd = 36,
    kGetEntityToken = 37,
    kFsGetFileList = 38,
    kCeil = 111, // Quake III's game numbers floor 110 and ceil 111; Elite Force's code uses 111
};

constexpr int32_t kTraceSize = 56; // Quake III's trace_t, as Elite Force's code reads it

} // namespace

GameHost::GameHost(const SearchSet &files, CvarStore &cvars)
    : _files(files), _cvars(cvars), _vmFiles(files),
      _serverWorld([this](int32_t n, EntityShape &out) { return entityShape(n, out); }) {
    _vm.setSystemCall([this](Qvm &vm, int number, const int32_t *args) { return systemCall(vm, number, args); });
    _vmFiles.setOnRead([this](const std::string &name) { _filesRead.insert(name); });
}

bool GameHost::entityShape(int32_t n, EntityShape &out) {
    // Entities without the shared part (not Elite Force's layout) have none.
    const int32_t a = entityAddress(n);
    if (a < 0 || _gameData.entitySize < _layout.sharedEnd || !_vm.valid(a, _layout.sharedEnd))
        return false;
    out.origin = readVec(a + _layout.currentOrigin);
    out.angles = readVec(a + _layout.currentAngles);
    out.mins = readVec(a + _layout.mins);
    out.maxs = readVec(a + _layout.maxs);
    out.contents = _vm.readInt(a + _layout.contents);
    out.owner = _vm.readInt(a + _layout.ownerNum);
    out.bmodel = _vm.readInt(a + _layout.bmodel) != 0;
    out.modelIndex = _vm.readInt(a + _layout.modelIndex);
    return true;
}

Error GameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = _files.read("vm/qagame.qvm", data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    if (Error err = _vm.load(data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    _layout = detectLayout(_vm);
    _qvmData = std::move(data);
    return Error::success();
}

Error GameHost::call(int32_t command, const std::vector<int32_t> &args, int32_t &result) {
    return _vm.call(command, args, result);
}

Error GameHost::startLevel(const std::string &mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed) {
    if (!map)
        return Error::fail("No map");
    _filesRead.clear();
    // Variables waiting for a new level (the game type, the Holomatch
    // modes...) take their values, as the original's server does first.
    _cvars.applyLatched();
    // The level after this one: the same again unless the game or a
    // command says otherwise, as the original's server sets it on
    // spawning a level (for a level change, when the map command ran:
    // a map cycle's "map x ; set nextmap vstr d2" sets its own after).
    if (!_changingLevel)
        _cvars.set("nextmap", "map_restart 0", true);
    _map = std::move(map);
    _world = std::make_unique<CollisionWorld>(*_map);
    _areaPortals = std::make_unique<AreaPortals>(_world->areaCount());
    _closedDoorBlocks = 0;
    _cvars.create("cm_noAreas", "0", 0); // 1: every area joined, as the original's
    _serverWorld.setLevel(_map.get(), _world.get(), _areaPortals.get(),
                          [this] { return _cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas"); });
    // What the original server sets before starting the game code.
    _cvars.create("mapname", "", CvarStore::kServerInfo | CvarStore::kReadOnly);
    _cvars.set("mapname", mapName, true);
    _cvars.create("sv_maxclients", "8", CvarStore::kServerInfo | CvarStore::kLatch | CvarStore::kArchive);
    _cvars.create("g_gametype", "0", CvarStore::kServerInfo | CvarStore::kLatch);
    _cvars.create("dedicated", "0", CvarStore::kLatch);
    _cvars.create("sv_cheats", "0", CvarStore::kServerInfo | CvarStore::kReadOnly);
    // What the original's server adds for clients over the network
    // (docs/engines/idtech3/games/stvef/systems/network.md): its protocol and name in the server info,
    // and in the system info the level's id (changing with every level,
    // so commands from before it can be told apart), whether it's pure
    // (checked by the network server, which also lists the paks) and its
    // frame rate.
    _cvars.create("protocol", "24", CvarStore::kServerInfo | CvarStore::kReadOnly);
    _cvars.create("sv_hostname", "Reluva", CvarStore::kServerInfo | CvarStore::kArchive);
    _cvars.create("sv_serverid", "0", CvarStore::kSystemInfo | CvarStore::kReadOnly);
    _cvars.set("sv_serverid", std::to_string(randomSeed & 0x7fffffff), true);
    _cvars.create("sv_pure", "0", CvarStore::kSystemInfo);
    _cvars.create("sv_fps", "20", CvarStore::kSystemInfo);
    // The engine's bot settings (the game reads bot_enable when starting).
    _cvars.create("bot_enable", "1", 0);
    _cvars.create("bot_thinktime", "100", 0);
    _entityText = _map->entities;
    _entityPos = 0;
    _levelTime = 0;
    _mapName = mapName;
    _randomSeed = randomSeed;
    _restartTime = 0;
    _levelGametype = _cvars.string("g_gametype");
    _levelMaxClients = _cvars.string("sv_maxclients");
    int32_t ignored = 0;
    if (Error err = call(kInit, {_levelTime, randomSeed, 0}, ignored))
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
    if (_restartTime && _levelTime >= _restartTime)
        if (Error err = restartLevel())
            return err;
    _levelTime += kFrameMs;
    // The bots think first (and send their commands), then the level runs.
    runBotFrame();
    int32_t ignored = 0;
    Error err = call(kRunFrame, {_levelTime}, ignored);
    updateInfoConfigstrings();
    if (!err)
        runQueuedConsoleCommands();
    return err;
}

void GameHost::updateInfoConfigstrings() {
    // Configstrings 0 and 1 are the server's: every server-info cvar
    // (mapname, g_gametype...) and every system-info one, as the client
    // game reads them (it finds the map's name in 0).
    _configstrings[0] = infoString(CvarStore::kServerInfo);
    _configstrings[1] = infoString(CvarStore::kSystemInfo);
}

Error GameHost::connectClient(int32_t clientNum, const std::string &userinfo, std::string &refused) {
    refused.clear();
    _userinfo[clientNum] = userinfo;
    _clientsInUse.insert(clientNum);
    int32_t reason = 0;
    if (Error err = call(kClientConnect, {clientNum, 1, 0}, reason)) // first time, not a bot
        return err;
    if (reason)
        refused = _vm.string(reason, 1024);
    return Error::success();
}

Error GameHost::reloadGame() {
    // The game code from scratch, as the original reloads it, and what
    // the engine knew of its entities forgotten.
    if (Error err = _vm.load(_qvmData))
        return Error::fail("vm/qagame.qvm: " + err.message);
    _serverWorld.clear();
    _gameData = GameData{};
    _restartTime = 0;
    if (_world) {
        _areaPortals = std::make_unique<AreaPortals>(_world->areaCount());
        _serverWorld.setPortals(_areaPortals.get());
    }
    return Error::success();
}

Error GameHost::reconnectClients(const std::set<int32_t> &clients, bool beginHumans) {
    // Every player and bot connected again, not for the first time (the
    // game keeps what it saved of them); bots are put in the level at
    // once, players too if `beginHumans`.
    for (const int32_t c : clients) {
        const bool bot = _botClients.count(c) != 0;
        _clientsInUse.insert(c);
        int32_t reason = 0;
        if (Error err = call(kClientConnect, {c, 0, bot ? 1 : 0}, reason))
            return err;
        if (reason) {
            _dropped[c] = _vm.string(reason, 1024);
            _drops.emplace_back(c, _dropped[c]);
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
    _restartTime = 0;
    if (_cvars.pending("g_gametype") != _levelGametype || _cvars.pending("sv_maxclients") != _levelMaxClients) {
        _requestedMap = _mapName;
        return Error::success();
    }
    int32_t ignored = 0;
    if (Error err = call(kShutdown, {1}, ignored))
        return err;
    if (Error err = reloadGame())
        return err;
    _cvars.set("sv_serverid", std::to_string((_randomSeed + _levelTime + 1) & 0x7fffffff), true);
    _entityText = _map ? _map->entities : std::string();
    _entityPos = 0;
    if (Error err = call(kInit, {_levelTime, _randomSeed + _levelTime, 1}, ignored))
        return err;
    updateInfoConfigstrings();
    for (int i = 0; i < 3; ++i)
        if (Error err = runFrame())
            return err;
    const std::set<int32_t> clients = _clientsInUse;
    if (Error err = reconnectClients(clients, true))
        return err;
    ++_restarts;
    return Error::success();
}

bool GameHost::applyMapCommand(CvarStore &cvars, std::string_view verb) {
    cvars.create("g_gametype", "0", CvarStore::kServerInfo | CvarStore::kLatch);
    if (verb.substr(0, 2) == "sp") {
        cvars.set("g_gametype", "2", true);
        cvars.set("g_doWarmup", "0", true);
        cvars.create("sv_maxclients", "8", CvarStore::kServerInfo | CvarStore::kLatch | CvarStore::kArchive);
        cvars.set("sv_maxclients", "8", true);
        return true;
    }
    if (std::atoi(cvars.pending("g_gametype").c_str()) == 2)
        cvars.set("g_gametype", "0", true);
    return verb == "devmap";
}

Error GameHost::changeLevel(const std::string &mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed,
                            std::vector<int32_t> &players) {
    // A new level, as the original's map command: the game code shut down
    // and started afresh on it, and everyone connected again; bots are put
    // in the level, players (`players`) when they've loaded it. Unless the
    // command said so, the bots too (a solo match's own are added by the
    // game as it starts).
    std::set<int32_t> clients = _clientsInUse;
    if (std::exchange(_killBots, false)) {
        for (const int32_t c : _botClients) {
            clients.erase(c);
            _clientsInUse.erase(c);
            _botSnapshots.erase(c);
        }
        _botClients.clear();
    }
    int32_t ignored = 0;
    if (Error err = call(kShutdown, {0}, ignored))
        return err;
    if (Error err = reloadGame())
        return err;
    _configstrings.clear();
    _requestedMap.clear();
    _changingLevel = true;
    Error started = startLevel(mapName, std::move(map), randomSeed);
    _changingLevel = false;
    if (started)
        return started;
    if (Error err = reconnectClients(clients, false))
        return err;
    players.clear();
    for (const int32_t c : clients)
        if (!_botClients.count(c) && _clientsInUse.count(c) && !_dropped.count(c))
            players.push_back(c);
    return Error::success();
}

bool GameHost::engineCommand(const std::vector<std::string> &w) {
    // The original engine's own console commands that the game runs
    // (SendConsoleCommand) or a player types at the server.
    if (w.empty())
        return false;
    std::string verb = w[0];
    for (char &ch : verb)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (verb == "map_restart") {
        // With a delay (5 seconds unless one is given) when g_doWarmup is
        // off: the restart time goes in the warmup configstring (5) for
        // the client game to count down.
        const int delay = w.size() > 1 ? std::atoi(w[1].c_str()) : 5;
        if (_restartTime)
            return true;
        if (delay > 0 && !_cvars.integer("g_doWarmup")) {
            _restartTime = _levelTime + delay * 1000;
            _configstrings[5] = std::to_string(_restartTime);
            return true;
        }
        if (Error err = restartLevel())
            if (print)
                print("map_restart: " + err.message + "\n");
        return true;
    }
    if (verb == "map" || verb == "devmap" || verb == "spmap" || verb == "spdevmap") {
        if (w.size() > 1) {
            _requestedMap = w[1];
            _killBots = applyMapCommand(_cvars, verb);
            _cvars.set("nextmap", "map_restart 0", true);
        }
        return true;
    }
    if (verb == "vstr") {
        // A variable's text run as commands (the game's nextmap).
        if (w.size() > 1)
            _consoleCommands.push_back(_cvars.string(w[1]));
        return true;
    }
    if (verb == "kick" || verb == "clientkick") {
        // By number (clientkick, or kick with a number), or by name.
        if (w.size() < 2)
            return true;
        int32_t target = -1;
        const std::string &who = w[1];
        if (verb == "clientkick" || (!who.empty() && std::isdigit(static_cast<unsigned char>(who[0]))))
            target = std::atoi(who.c_str());
        else
            for (const int32_t c : _clientsInUse) {
                const auto it = _configstrings.find(544 + c); // CS_PLAYERS
                if (it != _configstrings.end() && infoValue(it->second, "n") == who)
                    target = c;
            }
        if (target < 0 || !_clientsInUse.count(target))
            return true;
        if (_botClients.count(target)) {
            disconnectClient(target);
        } else {
            // Players: the network server sends them off and disconnects them.
            _dropped[target] = "was kicked";
            _drops.emplace_back(target, "was kicked");
        }
        return true;
    }
    if (verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") {
        if (w.size() > 2)
            _cvars.set(w[1], w[2]);
        return true;
    }
    if (verb == "wait")
        return true;
    // A variable's name and a value sets it, as the original's console.
    if (w.size() == 2 && _cvars.find(w[0])) {
        _cvars.set(w[0], w[1]);
        return true;
    }
    return false;
}

Error GameHost::userinfoChanged(int32_t clientNum, const std::string &userinfo) {
    _userinfo[clientNum] = userinfo;
    int32_t ignored = 0;
    return call(kClientUserinfoChanged, {clientNum}, ignored);
}

std::vector<uint8_t> GameHost::entityState(int32_t number) {
    const int32_t a = entityAddress(number);
    if (a < 0 || !_vm.valid(a, kEntityStateSize))
        return {};
    return {&_vm.memory()[size_t(a)], &_vm.memory()[size_t(a)] + kEntityStateSize};
}

Error GameHost::disconnectClient(int32_t clientNum) {
    int32_t ignored = 0;
    Error err = call(kClientDisconnect, {clientNum}, ignored);
    _clientsInUse.erase(clientNum);
    _userinfo.erase(clientNum);
    _commands.erase(clientNum);
    return err;
}

Error GameHost::clientCommand(int32_t clientNum, const std::string &text) {
    _args = splitCommand(text);
    int32_t ignored = 0;
    Error err = call(kClientCommand, {clientNum}, ignored);
    _args.clear();
    return err;
}

Error GameHost::beginClient(int32_t clientNum) {
    int32_t ignored = 0;
    return call(kClientBegin, {clientNum}, ignored);
}

void GameHost::writeUserCommand(Qvm &vm, int32_t address, const UserCommand &cmd) {
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

Error GameHost::clientThink(int32_t clientNum, const UserCommand &cmd) {
    _commands[clientNum] = cmd;
    int32_t ignored = 0;
    return call(kClientThink, {clientNum}, ignored);
}

std::string GameHost::nextEntityToken() {
    const std::string &s = _entityText;
    size_t &i = _entityPos;
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
    if (!_gameData.entitySize || address < _gameData.entities)
        return -1;
    const int32_t offset = address - _gameData.entities;
    return offset % _gameData.entitySize == 0 ? offset / _gameData.entitySize : -1;
}

int32_t GameHost::entityAddress(int32_t number) const {
    if (number < 0 || number >= _gameData.count || _gameData.entitySize < _layout.sharedEnd)
        return -1;
    return _gameData.entities + number * _gameData.entitySize;
}

Vec3f GameHost::readVec(int32_t address) {
    Vec3f v{};
    if (address)
        for (int i = 0; i < 3; ++i)
            v[size_t(i)] = Qvm::asFloat(_vm.readInt(address + 4 * i));
    return v;
}

void GameHost::writeVec(int32_t address, const Vec3f &v) {
    for (int i = 0; i < 3; ++i)
        _vm.writeInt(address + 4 * i, Qvm::fromFloat(v[size_t(i)]));
}

int32_t GameHost::entityInt(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? 0 : _vm.readInt(a + offset);
}

Vec3f GameHost::entityVec(int32_t number, int32_t offset) {
    const int32_t a = entityAddress(number);
    return a < 0 ? Vec3f{} : readVec(a + offset);
}

int32_t GameHost::clientInt(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !_gameData.clientSize || offset + 4 > _gameData.clientSize)
        return 0;
    return _vm.readInt(_gameData.clients + clientNum * _gameData.clientSize + offset);
}

Vec3f GameHost::clientVec(int32_t clientNum, int32_t offset) {
    if (clientNum < 0 || !_gameData.clientSize || offset + 12 > _gameData.clientSize)
        return {};
    return readVec(_gameData.clients + clientNum * _gameData.clientSize + offset);
}

std::pair<int, int> GameHost::svFlagsEvidence() {
    // svFlags holds small flag bits (Quake III's SVF_NOCLIENT is 1, for
    // triggers and other entities the client never sees); the other word
    // (linkcount or singleClient, never written by Reluva) stays 0.
    int at208 = 0;
    int at212 = 0;
    for (int32_t n : _serverWorld.linked()) {
        const int32_t v208 = entityInt(n, 208);
        const int32_t v212 = entityInt(n, 212);
        at208 += v208 > 0 && v208 < 0x10000;
        at212 += v212 > 0 && v212 < 0x10000;
    }
    return {at208, at212};
}

int32_t GameHost::svFlagsOffset() {
    if (_layout.svFlags >= 0)
        return _layout.svFlags;
    const auto [at208, at212] = svFlagsEvidence();
    return at208 > at212 ? 208 : 212;
}

GameHost::Snapshot GameHost::snapshot(int32_t clientNum) {
    ++_snapshotsMade;
    Snapshot snap;
    snap.serverTime = _levelTime;
    const int32_t size = _layout.playerStateSize;
    snap.playerState.assign(size_t(size), 0);
    const int32_t client = _gameData.clients + clientNum * _gameData.clientSize;
    if (_gameData.clientSize >= size && _vm.valid(client, size))
        std::memcpy(snap.playerState.data(), &_vm.memory()[size_t(client)], size_t(size));
    // The original sends the player state over its network protocol, which
    // carries only some fields: the rest reach the client game as 0, even
    // on a listen server. The word at 48, a timer of the server's (set to
    // 200 on firing, counting down without stopping at 0), is one: the
    // client game draws the phaser's beam only while it's 0 (see
    // docs/engines/idtech3/games/stvef/systems/syscalls.md).
    for (const int32_t at : {kPlayerStateServerOnly})
        std::memset(&snap.playerState[size_t(at)], 0, 4);
    // The entities the player can see, as the original's server picks them
    // (visibleEntities()); at most 256, the snapshot's room.
    for (const int32_t n : visibleEntities(clientNum)) {
        const int32_t a = entityAddress(n);
        if (a < 0 || snap.entities.size() >= 256)
            continue;
        // As the original's server: an entity whose number isn't its own
        // (the game copied another's state into it, as for some weapons'
        // effects) gets it put right, or the client game would take it for
        // that other one and stop drawing it.
        if (_vm.readInt(a + _layout.number) != n)
            _vm.writeInt(a + _layout.number, n);
        snap.entities.emplace_back(&_vm.memory()[size_t(a)], &_vm.memory()[size_t(a)] + kEntityStateSize);
    }
    for (const auto &[to, text] : _serverCommands)
        snap.serverCommandSequence += to == clientNum || to == -1;
    // The areas: from the player's eye (taken as 26 units up, the
    // standing view height), each not joined to its area hidden. Outside
    // any area, or with cm_noAreas, none.
    if (_world && _areaPortals && !(_cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas"))) {
        const Vec3f eye = eyeOf(clientNum);
        const int area = _world->leafArea(_world->leafAt(eye));
        if (area >= 0)
            for (int a = 0; a < std::min(_world->areaCount(), 256); ++a)
                if (!_areaPortals->connected(area, a))
                    snap.areaMask[size_t(a >> 3)] |= uint8_t(1 << (a & 7));
    }
    return snap;
}

void GameHost::linkEntity(int32_t address) {
    const int32_t n = entityNumber(address);
    if (n < 0)
        return;
    EntityShape shape;
    if (!entityShape(n, shape) || !_vm.checkWrite(address, _layout.sharedEnd)) {
        _serverWorld.linkUnplaced(n); // only recorded
        return;
    }
    Vec3f absMin{};
    Vec3f absMax{};
    _serverWorld.link(n, shape, absMin, absMax);
    writeVec(address + _layout.absMin, absMin);
    writeVec(address + _layout.absMax, absMax);
    _vm.writeInt(address + _layout.linked, 1);
    _vm.writeInt(address + _layout.solid, ServerWorld::packedSolid(shape, kSolidBrushModel));
}

Vec3f GameHost::eyeOf(int32_t clientNum) {
    // The player's eye: with patch 1.2's layout, the player state's origin
    // (20) raised by its view height (180), as the original's server takes
    // it; otherwise the body's origin and the standing view height, 26.
    const int32_t client = _gameData.clients + clientNum * _gameData.clientSize;
    if (_layout.version == 12 && _gameData.clientSize >= _layout.playerStateSize &&
        _vm.valid(client, _layout.playerStateSize)) {
        Vec3f eye = readVec(client + 20);
        eye[2] += float(_vm.readInt(client + 180));
        return eye;
    }
    Vec3f eye = entityVec(clientNum, _layout.currentOrigin);
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
    const bool singleClient = _layout.version == 12;
    const bool noAreas = _cvars.find("cm_noAreas") && _cvars.integer("cm_noAreas");
    std::set<int32_t> added;
    auto look = [&](const Vec3f &from, auto &self, int depth) -> void {
        const int leaf = _world ? _world->leafAt(from) : -1;
        const int area = leaf >= 0 ? _world->leafArea(leaf) : -1;
        const int cluster =
            leaf >= 0 && _map && size_t(leaf) < _map->leafs.size() ? _map->leafs[size_t(leaf)].cluster : -1;
        for (const int32_t n : _serverWorld.linked()) {
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
                const ServerWorld::Placement *place = _serverWorld.placement(n);
                if (!place)
                    continue;
                const ServerWorld::Placement &es = *place;
                if (!noAreas && area >= 0 && es.areas.first >= 0 && _areaPortals &&
                    !_areaPortals->connected(area, es.areas.first) &&
                    (es.areas.second < 0 || !_areaPortals->connected(area, es.areas.second)))
                    continue;
                if (cluster >= 0 && _map && !es.clusters.empty()) {
                    bool seen = false;
                    for (const int c : es.clusters)
                        seen = seen || _map->clusterVisible(cluster, c);
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
    const ServerWorld::Placement *p = _serverWorld.placement(n);
    return p ? p->areas : std::pair<int, int>{-1, -1};
}

bool GameHost::areasConnected(int area1, int area2) const {
    return _serverWorld.areasConnected(area1, area2);
}

TraceResult GameHost::traceMove(const Vec3f &start, const Vec3f &mins, const Vec3f &maxs, const Vec3f &end,
                                int32_t passEntity, int32_t mask, int32_t &hit, bool entities) {
    return _serverWorld.trace(start, mins, maxs, end, passEntity, mask, hit, entities);
}

void GameHost::clipMove(int32_t results, const Vec3f &start, const Vec3f &mins, const Vec3f &maxs, const Vec3f &end,
                        int32_t passEntity, int32_t mask) {
    if (!_vm.checkWrite(results, kTraceSize))
        return;
    int32_t hit = kEntityNone;
    const TraceResult best = traceMove(start, mins, maxs, end, passEntity, mask, hit);
    // Quake III's trace_t: allsolid, startsolid, fraction, endpos, the
    // plane (normal, dist, type, sign bits), surfaceFlags, contents,
    // entityNum.
    _vm.writeInt(results + 0, best.allSolid);
    _vm.writeInt(results + 4, best.startSolid);
    _vm.writeInt(results + 8, Qvm::fromFloat(best.fraction));
    writeVec(results + 12, best.endPos);
    writeVec(results + 24, best.normal);
    _vm.writeInt(results + 36, Qvm::fromFloat(best.dist));
    int type = 3;
    int signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (best.normal[size_t(i)] == 1 || best.normal[size_t(i)] == -1)
            type = i;
        if (best.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    _vm.memory()[size_t(results + 40)] = uint8_t(type);
    _vm.memory()[size_t(results + 41)] = uint8_t(signs);
    _vm.memory()[size_t(results + 42)] = 0;
    _vm.memory()[size_t(results + 43)] = 0;
    _vm.writeInt(results + 44, best.surfaceFlags);
    _vm.writeInt(results + 48, best.contents);
    _vm.writeInt(results + 52, hit);
}

int32_t GameHost::pointContents(const Vec3f &point, int32_t passEntity) {
    return _serverWorld.pointContents(point, passEntity);
}

std::string GameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar &cv : _cvars.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

int32_t GameHost::systemCall(Qvm &vm, int number, const int32_t *a) {
    if (trace)
        trace(number, a);
    _callCounts.add(number);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case kPrint: {
        const std::string text = str(0, 4096);
        // Kept (the last 4 KB) as well as handed on.
        _log += text;
        if (_log.size() > 8192)
            _log.erase(0, _log.size() - 4096);
        if (print)
            print(text);
        return 0;
    }
    case kError:
        vm.abort("qagame.qvm: " + str(0));
        return 0;
    case kMilliseconds:
        return _levelTime;
    case kCvarRegister: {
        const CvarStore::Cvar &cv = _cvars.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            _cvars.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case kCvarUpdate:
        _cvars.updateVmCvar(vm, a[0]);
        return 0;
    case kCvarSet:
        _cvars.set(str(0, 256), str(1, 256), true);
        return 0; // forced, as the original's Cvar_Set
    case kCvarVariableIntegerValue:
        return _cvars.integer(str(0, 256));
    case kCvarVariableStringBuffer:
        vm.writeString(a[1], _cvars.string(str(0, 256)), a[2]);
        return 0;
    case kArgc:
        return int32_t(_args.size());
    case kArgv:
        vm.writeString(a[1], a[0] >= 0 && size_t(a[0]) < _args.size() ? _args[size_t(a[0])] : "", a[2]);
        return 0;
    case kFsFOpenFile:
        return _vmFiles.open(vm, a[0], a[1], a[2]);
    case kFsRead:
        return _vmFiles.read(vm, a[0], a[1], a[2]);
    case kFsFCloseFile:
        _vmFiles.close(a[0]);
        return 0;
    case kFsGetFileList:
        return _vmFiles.list(vm, a[0], a[1], a[2], a[3]);
    case kSendConsoleCommand:
        _consoleCommands.push_back(str(1, 4096));
        return 0;
    case kLocateGameData:
        _gameData = {a[0], a[1], a[2], a[3], a[4]};
        return 0;
    case kSendServerCommand:
        _serverCommands.emplace_back(a[0], str(1, 8192));
        return 0;
    case kDropClient:
        _dropped[a[0]] = str(1, 1024);
        _drops.emplace_back(a[0], _dropped[a[0]]);
        return 0;
    case kGetUserinfo: {
        const auto it = _userinfo.find(a[0]);
        vm.writeString(a[1], it == _userinfo.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case kSetUserinfo:
        _userinfo[a[0]] = str(1, 1024);
        return 0;
    case kGetUsercmd: { // (clientNum, usercmd_t*): the last command, or an empty one
        const auto it = _commands.find(a[0]);
        const UserCommand cmd = it == _commands.end() ? UserCommand{_levelTime} : it->second;
        if (!vm.checkWrite(a[1], 24))
            return 0;
        writeUserCommand(vm, a[1], cmd);
        return 0;
    }
    case kSetConfigstring:
        _configstrings[a[0]] = str(1, 8192);
        return 0;
    case kGetConfigstring: {
        const auto it = _configstrings.find(a[0]);
        vm.writeString(a[1], it == _configstrings.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case kGetServerinfo:
        vm.writeString(a[0], infoString(CvarStore::kServerInfo), a[1]);
        return 0;
    case kLinkEntity:
        linkEntity(a[0]);
        return 0;
    case kUnlinkEntity:
        if (const int32_t n = entityNumber(a[0]); n >= 0) {
            _serverWorld.unlink(n);
            if (_gameData.entitySize >= _layout.sharedEnd)
                vm.writeInt(a[0] + _layout.linked, 0);
        }
        return 0;
    case kSetBrushModel: { // (ent, "*N"): one of the map's brush models
        const std::string name = str(1, 64);
        if (name.size() < 2 || name[0] != '*') {
            vm.abort("SetBrushModel: '" + name + "' isn't a brush model");
            return 0;
        }
        const int model = std::atoi(name.c_str() + 1);
        Vec3f mins{};
        Vec3f maxs{};
        if (!_world || model <= 0 || !_world->modelBounds(model, mins, maxs)) {
            vm.abort("SetBrushModel: the map has no model " + name);
            return 0;
        }
        if (_gameData.entitySize < _layout.sharedEnd || !vm.checkWrite(a[0], _layout.sharedEnd))
            return 0;
        vm.writeInt(a[0] + _layout.modelIndex, model);
        vm.writeInt(a[0] + _layout.bmodel, 1);
        writeVec(a[0] + _layout.mins, mins);
        writeVec(a[0] + _layout.maxs, maxs);
        vm.writeInt(a[0] + _layout.contents, -1); // whatever its brushes hold
        linkEntity(a[0]);
        return 0;
    }
    case kTrace: {
        const Vec3f zero{};
        clipMove(a[0], readVec(a[1]), a[2] ? readVec(a[2]) : zero, a[3] ? readVec(a[3]) : zero, readVec(a[4]), a[5],
                 a[6]);
        return 0;
    }
    case kPointContents:
        return pointContents(readVec(a[0]), a[1]);
    case kInPvs:
    case kInPvsIgnorePortals: {
        if (!_world)
            return 1;
        const int l1 = _world->leafAt(readVec(a[0]));
        const int l2 = _world->leafAt(readVec(a[1]));
        if (l1 < 0 || l2 < 0)
            return 0;
        if (!_map->clusterVisible(_map->leafs[size_t(l1)].cluster, _map->leafs[size_t(l2)].cluster))
            return 0;
        // Not through a closed door, unless asked to ignore portals.
        if (number == kInPvsIgnorePortals || areasConnected(_world->leafArea(l1), _world->leafArea(l2)))
            return 1;
        ++_closedDoorBlocks;
        return 0;
    }
    case kAdjustAreaPortalState: // (entity, open): the portal its door stands in
        if (const int32_t n = entityNumber(a[0]); n >= 0 && _areaPortals && _gameData.entitySize >= _layout.sharedEnd) {
            const auto [area1, area2] = entityAreas(n);
            if (area2 != -1)
                _areaPortals->adjust(area1, area2, a[1] != 0);
        }
        return 0;
    case kAreasConnected:
        return areasConnected(a[0], a[1]);
    case kEntitiesInBox: { // (mins, maxs, list, max) -> how many
        const Vec3f mins = readVec(a[0]);
        const Vec3f maxs = readVec(a[1]);
        std::vector<int32_t> found;
        _serverWorld.entitiesInBox(mins, maxs, found);
        int32_t count = 0;
        for (const int32_t n : found) {
            if (count >= a[3])
                break;
            vm.writeInt(a[2] + 4 * count++, n);
        }
        return count;
    }
    case kEntityContact: { // (mins, maxs, ent): whether the box touches it
        const Vec3f mins = readVec(a[0]);
        const Vec3f maxs = readVec(a[1]);
        const int32_t n = entityNumber(a[2]);
        if (n < 0)
            return 0;
        const Vec3f origin = entityVec(n, _layout.currentOrigin);
        if (entityInt(n, _layout.bmodel) && _world) {
            // The box against the model where it stands, turned as it is.
            const Vec3f zero{};
            return _world
                ->transformedTrace(zero, zero, mins, maxs, entityInt(n, _layout.modelIndex), -1, origin,
                                   entityVec(n, _layout.currentAngles))
                .startSolid;
        }
        const Vec3f absMin = entityVec(n, _layout.absMin);
        const Vec3f absMax = entityVec(n, _layout.absMax);
        for (int i = 0; i < 3; ++i)
            if (absMin[size_t(i)] > maxs[size_t(i)] || absMax[size_t(i)] < mins[size_t(i)])
                return 0;
        return 1;
    }
    case kGetEntityToken: {
        const std::string token = nextEntityToken();
        vm.writeString(a[0], token, a[1]);
        return token.empty() ? 0 : 1;
    }
    case kCeil:
        return Qvm::fromFloat(std::ceil(Qvm::asFloat(a[0])));
    default:
        if (number == kBotAllocateClient || number == kBotFreeClient || number >= 200)
            return botCall(vm, number, a);
        ++_unhandled[number];
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
