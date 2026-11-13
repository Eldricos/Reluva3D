// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot library's system calls for Elite Force's game logic: bots
// joining (BotAllocateClient), the library's own calls (200 on: setting
// up, the level's .aas map, the entities, the bots' commands), the map
// queries (300 on), the elementary actions (400 on) and the AI (500 on).
// Quake III's numbering, which Elite Force's game uses in the ranges
// 200-211, 303-318, 401-427 and 500-572 (see docs/engines/idtech3/games/stvef/systems/syscalls.md);
// where Elite Force's may differ it's marked. The structures are Quake
// III's (botlib.h, be_aas.h, be_ai_*.h of the GPL ioquake3 source), read
// for their layout; the code is new.

#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_layout.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

enum BotCall : int {
    kBotAllocateClient = 34,
    kBotFreeClient = 35,
    // The library.
    kLibSetup = 200,
    kLibShutdown = 201,
    kLibVarSet = 202,
    kLibVarGet = 203,
    kLibAddGlobalDefine = 204,
    kLibStartFrame = 205,
    kLibLoadMap = 206,
    kLibUpdateEntity = 207,
    kLibTest = 208,
    kGetSnapshotEntity = 209,
    kGetConsoleMessage = 210,
    kLibUserCommand = 211,
    // The map.
    kAasEntityInfo = 303,
    kAasInitialized = 304,
    kAasPresenceTypeBoundingBox = 305,
    kAasTime = 306,
    kAasPointAreaNum = 307,
    kAasTraceAreas = 308,
    kAasPointContents = 309,
    kAasNextBspEntity = 310,
    kAasValueForBspEpairKey = 311,
    kAasVectorForBspEpairKey = 312,
    kAasFloatForBspEpairKey = 313,
    kAasIntForBspEpairKey = 314,
    kAasAreaReachability = 315,
    kAasAreaTravelTimeToGoalArea = 316,
    kAasSwimming = 317,
    kAasPredictClientMovement = 318,
    // Elementary actions: Elite Force's order (found from its calls: what
    // each passes and where; see docs/engines/idtech3/games/stvef/systems/syscalls.md), with an alt
    // attack at the end.
    kEaSay = 400,
    kEaSayTeam = 401,
    kEaUseItem = 402,
    kEaDropItem = 403,
    kEaUseInv = 404,
    kEaDropInv = 405,
    kEaGesture = 406,
    kEaCommand = 407,
    kEaSelectWeapon = 408,
    kEaTalk = 409,
    kEaAttack = 410,
    kEaUse = 411,
    kEaRespawn = 412,
    kEaJump = 413,
    kEaDelayedJump = 414,
    kEaCrouch = 415,
    kEaMoveUp = 416,
    kEaMoveDown = 417,
    kEaMoveForward = 418,
    kEaMoveBack = 419,
    kEaMoveLeft = 420,
    kEaMoveRight = 421,
    kEaMove = 422,
    kEaView = 423,
    kEaEndRegular = 424,
    kEaGetInput = 425,
    kEaResetInput = 426,
    kEaAltAttack = 427,
    // AI.
    kAiLoadCharacter = 500,
    kAiFreeCharacter = 501,
    kAiCharacteristicFloat = 502,
    kAiCharacteristicBFloat = 503,
    kAiCharacteristicInteger = 504,
    kAiCharacteristicBInteger = 505,
    kAiCharacteristicString = 506,
    kAiAllocChatState = 507,
    kAiFreeChatState = 508,
    kAiQueueConsoleMessage = 509,
    kAiRemoveConsoleMessage = 510,
    kAiNextConsoleMessage = 511,
    kAiNumConsoleMessage = 512,
    kAiInitialChat = 513,
    kAiReplyChat = 514,
    kAiChatLength = 515,
    kAiEnterChat = 516,
    kAiStringContains = 517,
    kAiFindMatch = 518,
    kAiMatchVariable = 519,
    kAiUnifyWhiteSpaces = 520,
    kAiReplaceSynonyms = 521,
    kAiLoadChatFile = 522,
    kAiSetChatGender = 523,
    kAiSetChatName = 524,
    kAiResetGoalState = 525,
    kAiResetAvoidGoals = 526,
    kAiPushGoal = 527,
    kAiPopGoal = 528,
    kAiEmptyGoalStack = 529,
    kAiDumpAvoidGoals = 530,
    kAiDumpGoalStack = 531,
    kAiGoalName = 532,
    kAiGetTopGoal = 533,
    kAiGetSecondGoal = 534,
    kAiChooseLtgItem = 535,
    kAiChooseNbgItem = 536,
    kAiTouchingGoal = 537,
    kAiItemGoalInVisButNotVisible = 538,
    kAiGetLevelItemGoal = 539,
    kAiAvoidGoalTime = 540,
    kAiInitLevelItems = 541,
    kAiUpdateEntityItems = 542,
    kAiLoadItemWeights = 543,
    kAiFreeItemWeights = 544,
    kAiSaveGoalFuzzyLogic = 545,
    kAiAllocGoalState = 546,
    kAiFreeGoalState = 547,
    kAiResetMoveState = 548,
    kAiMoveToGoal = 549,
    kAiMoveInDirection = 550,
    kAiResetAvoidReach = 551,
    kAiResetLastAvoidReach = 552,
    kAiReachabilityArea = 553,
    kAiMovementViewTarget = 554,
    kAiAllocMoveState = 555,
    kAiFreeMoveState = 556,
    kAiInitMoveState = 557,
    kAiChooseBestFightWeapon = 558,
    kAiGetWeaponInfo = 559,
    kAiLoadWeaponWeights = 560,
    kAiAllocWeaponState = 561,
    kAiFreeWeaponState = 562,
    kAiResetWeaponState = 563,
    kAiGeneticParentsAndChildSelection = 564,
    kAiInterbreedGoalFuzzyLogic = 565,
    kAiMutateGoalFuzzyLogic = 566,
    kAiGetNextCampSpotGoal = 567,
    kAiGetMapLocationGoal = 568,
    kAiNumInitialChats = 569,
    kAiGetChatMessage = 570,
    kAiRemoveFromAvoidGoals = 571,
    kAiPredictVisiblePosition = 572,
};

// Structure sizes and layouts (Quake III's).
constexpr int32_t kEntityStateSize = 108; // bot_entitystate_t
constexpr int32_t kEntityInfoSize = 140;  // aas_entityinfo_t
constexpr int32_t kInputSize = 40;        // bot_input_t
constexpr int32_t kGoalSize = 56;         // bot_goal_t: origin, area, mins, maxs, entity, number, flags, item
constexpr int32_t kMoveResultSize = 52;   // bot_moveresult_t
constexpr int32_t kUserCommandSize = 24;
// Bodies, water, slime and lava (Quake III's contents).
constexpr int32_t kContentsLiquid = 8 | 16 | 32;
constexpr int32_t kInitMoveSize = 68;    // bot_initmove_t
constexpr int32_t kWeaponInfoSize = 552; // weaponinfo_t: strings of 80
constexpr int32_t kInventorySize = 256;  // the bots' inventory counts
constexpr int32_t kMatchSize = 328;      // bot_match_t: text, type, subtype, 8 x (offset byte, length)

} // namespace

Error GameHost::serverCommand(const std::string &text, bool *handled) {
    // The engine's own commands first (map_restart, map, kick...).
    if (engineCommand(splitCommand(text))) {
        if (handled)
            *handled = true;
        return Error::success();
    }
    _args = splitCommand(text);
    int32_t result = 0;
    Error err = call(kConsoleCommand, {}, result);
    _args.clear();
    if (handled)
        *handled = result != 0;
    return err;
}

void GameHost::runQueuedConsoleCommands() {
    // What the game asked the server to run (SendConsoleCommand): the
    // engine's own commands, and the rest back to the game as a console
    // command (addbot...), as the original does with commands it doesn't
    // know.
    while (_consoleCommandsRun < _consoleCommands.size()) {
        const std::string text = _consoleCommands[_consoleCommandsRun++];
        for (const std::string &command : splitCommands(text)) {
            if (command.empty())
                continue;
            bool handled = false;
            serverCommand(command, &handled);
            if (!handled && print)
                print("(console command not run: " + command + ")\n");
        }
    }
}

std::vector<int32_t> GameHost::botSnapshotEntities(int32_t clientNum) {
    // What the bot would get in a snapshot: linked entities the client
    // game is sent (not SVF_NOCLIENT), but itself, that its eyes could see
    // (the PVS); brush models always.
    std::vector<int32_t> out;
    const Vec3f origin = entityVec(clientNum, _layout.currentOrigin);
    const Vec3f eye{origin[0], origin[1], origin[2] + 26};
    const int32_t svFlags = svFlagsOffset();
    const int eyeLeaf = _world ? _world->leafAt(eye) : -1;
    for (const int32_t n : _serverWorld.linked()) {
        if (n == clientNum || (entityInt(n, svFlags) & 1))
            continue;
        if (eyeLeaf >= 0 && !entityInt(n, _layout.bmodel)) {
            const int leaf = _world->leafAt(entityVec(n, _layout.currentOrigin));
            if (leaf >= 0 &&
                !_map->clusterVisible(_map->leafs[size_t(eyeLeaf)].cluster, _map->leafs[size_t(leaf)].cluster))
                continue;
        }
        out.push_back(n);
        if (out.size() >= 256)
            break;
    }
    return out;
}

void GameHost::runBotFrame() {
    if (!_bots.isSetup() || _botClients.empty())
        return;
    for (const int32_t c : _botClients)
        _botSnapshots[c] = botSnapshotEntities(c);
    int32_t ignored = 0;
    call(kBotAiStartFrame, {_levelTime}, ignored);
}

int32_t GameHost::botCall(Qvm &vm, int number, const int32_t *a) {
    auto &samples = _botCallSamples[number];
    if (samples.size() < 3)
        samples.emplace_back(a, a + 6);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto flt = [&](int i) { return Qvm::asFloat(a[i]); };
    auto vec = [&](int32_t address) { return readVec(address); };
    auto fail = [&]() {
        ++_unhandled[number];
        return 0;
    };
    botlib::BotLibrary &b = _bots;
    botlib::AasWorld &aas = b.aas();
    // bot_goal_t: origin, area, mins, maxs, entity, number, flags, item info.
    auto readGoal = [&](int32_t p, botlib::BotGoal &g) {
        if (!vm.valid(p, kGoalSize))
            return false;
        g.origin = vec(p);
        g.area = vm.readInt(p + 12);
        g.mins = vec(p + 16);
        g.maxs = vec(p + 28);
        g.entity = vm.readInt(p + 40);
        g.number = vm.readInt(p + 44);
        g.flags = vm.readInt(p + 48);
        g.itemInfo = vm.readInt(p + 52);
        return true;
    };
    auto writeGoal = [&](int32_t p, const botlib::BotGoal &g) {
        if (!vm.checkWrite(p, kGoalSize))
            return 0;
        writeVec(p, g.origin);
        vm.writeInt(p + 12, g.area);
        writeVec(p + 16, g.mins);
        writeVec(p + 28, g.maxs);
        vm.writeInt(p + 40, g.entity);
        vm.writeInt(p + 44, g.number);
        vm.writeInt(p + 48, g.flags);
        vm.writeInt(p + 52, g.itemInfo);
        return 1;
    };
    // bot_match_t: the text (256), type, subtype, then each variable's
    // offset (a signed byte) and length (an int after 3 bytes' padding).
    auto writeMatch = [&](int32_t p, const botlib::BotMatch &m) {
        std::memset(&vm.memory()[size_t(p)], 0, size_t(kMatchSize));
        vm.writeString(p, m.text, 256);
        vm.writeInt(p + 256, m.type);
        vm.writeInt(p + 260, m.subtype);
        for (int i = 0; i < botlib::BotMatch::kVariables; ++i) {
            vm.memory()[size_t(p + 264 + 8 * i)] = uint8_t(int8_t(m.offset[size_t(i)]));
            vm.writeInt(p + 268 + 8 * i, m.length[size_t(i)]);
        }
    };
    auto readMatch = [&](int32_t p, botlib::BotMatch &m) {
        if (!vm.valid(p, kMatchSize))
            return false;
        m.text = vm.string(p, 256);
        m.type = vm.readInt(p + 256);
        m.subtype = vm.readInt(p + 260);
        for (int i = 0; i < botlib::BotMatch::kVariables; ++i) {
            m.offset[size_t(i)] = int8_t(vm.memory()[size_t(p + 264 + 8 * i)]);
            m.length[size_t(i)] = vm.readInt(p + 268 + 8 * i);
        }
        return true;
    };
    auto inventory = [&](int32_t p) {
        std::vector<int> counts(size_t(kInventorySize), 0);
        if (vm.valid(p, 4 * kInventorySize))
            for (int i = 0; i < kInventorySize; ++i)
                counts[size_t(i)] = vm.readInt(p + 4 * i);
        return counts;
    };
    auto writeWeaponInfo = [&](int32_t p, const botlib::WeaponInfo &w) {
        // valid, number, name, model, level, weaponindex, flags,
        // projectile, numprojectiles, spreads, speed, acceleration,
        // recoil, offset, angleoffset, extrazvelocity, ammo amount and
        // index, activate, reload, spinup, spindown, then the projectile:
        // name, model, flags, gravity, damage, radius, visdamage,
        // damagetype, healthinc, push, detonation, bounce, bouncefric,
        // bouncestop. Strings are 80 bytes.
        std::memset(&vm.memory()[size_t(p)], 0, size_t(kWeaponInfoSize));
        int32_t at = p;
        auto i32 = [&](int v) { vm.writeInt(at, v), at += 4; };
        auto f32 = [&](float v) { vm.writeInt(at, Qvm::fromFloat(v)), at += 4; };
        auto text = [&](const std::string &t) { vm.writeString(at, t, 80), at += 80; };
        auto v3 = [&](const Vec3f &v) { writeVec(at, v), at += 12; };
        i32(w.valid), i32(w.number), text(w.name), text(w.model), i32(w.level), i32(w.weaponIndex), i32(w.flags);
        text(w.projectile), i32(w.numProjectiles), f32(w.hspread), f32(w.vspread), f32(w.speed), f32(w.acceleration);
        v3(w.recoil), v3(w.offset), v3(w.angleOffset), f32(w.extraZVelocity), i32(w.ammoAmount), i32(w.ammoIndex);
        f32(w.activate), f32(w.reload), f32(w.spinUp), f32(w.spinDown);
        text(w.proj.name), text(w.proj.model), i32(w.proj.flags), f32(w.proj.gravity), i32(w.proj.damage);
        f32(w.proj.radius), i32(w.proj.visDamage), i32(w.proj.damageType), i32(w.proj.healthInc), f32(w.proj.push);
        f32(w.proj.detonation), f32(w.proj.bounce), f32(w.proj.bounceFric), f32(w.proj.bounceStop);
    };

    switch (number) {
    case kBotAllocateClient: { // -> a free client number for a bot, -1 if none
        const int32_t max = std::max(1, _cvars.integer("sv_maxclients"));
        for (int32_t c = 0; c < max; ++c)
            if (!_clientsInUse.count(c) && !_reserved.count(c)) {
                _clientsInUse.insert(c);
                _botClients.insert(c);
                _botMessagesRead[c] = _serverCommands.size();
                return c;
            }
        return -1;
    }
    case kBotFreeClient:
        _clientsInUse.erase(a[0]);
        _botClients.erase(a[0]);
        _botSnapshots.erase(a[0]);
        return 0;
    case kLibSetup:
        b.readFile = [this](const std::string &path, std::vector<uint8_t> &data) {
            if (_files.read(path, data))
                return false;
            _filesRead.insert(path);
            return true;
        };
        b.clientCommand = [this](int client, const std::string &text) { clientCommand(client, text); };
        b.print = [this](const std::string &text) {
            if (print)
                print(text);
            else
                _log += text;
        };
        return b.setup();
    case kLibShutdown:
        return b.shutdown();
    case kLibVarSet:
        b.setLibVar(str(0, 256), str(1, 256));
        return 0;
    case kLibVarGet:
        vm.writeString(a[1], b.libVar(str(0, 256)), a[2]);
        return 0;
    case kLibAddGlobalDefine:
        b.addGlobalDefine(str(0, 1024));
        return 0;
    case kLibStartFrame:
        return b.startFrame(flt(0));
    case kLibLoadMap: {
        if (_map)
            parseEntities(_map->entities, _bspEntities);
        b.entityCount = int(_bspEntities.size());
        b.entityKey = [this](int e, const std::string &key, std::string &value) {
            if (e < 1 || size_t(e) > _bspEntities.size() || !_bspEntities[size_t(e) - 1].has(key))
                return false;
            value = _bspEntities[size_t(e) - 1].get(key);
            return true;
        };
        b.trace = [this](const Vec3f &start, const Vec3f &mins, const Vec3f &maxs, const Vec3f &end, int pass,
                         int mask) {
            int32_t hit = kEntityNone;
            const TraceResult t = traceMove(start, mins, maxs, end, pass, mask, hit);
            return botlib::BotLibrary::Trace{t.fraction, t.endPos, t.startSolid, hit, t.normal};
        };
        b.pointContents = [this](const Vec3f &p) { return _world ? _world->pointContents(p, 0) : 0; };
        b.modelBounds = [this](int model, Vec3f &mins, Vec3f &maxs) {
            return _world && _world->modelBounds(model, mins, maxs);
        };
        b.moverOrigin = [this](int model, Vec3f &origin) {
            // The linked brush entity with that model, as the original's
            // bot library finds a lift.
            for (const int32_t n : _serverWorld.linked())
                if (entityInt(n, _layout.bmodel) && entityInt(n, _layout.modelIndex) == model) {
                    origin = entityVec(n, _layout.currentOrigin);
                    return true;
                }
            return false;
        };
        return b.loadMap(str(0, 256));
    }
    case kLibUpdateEntity: {
        if (!a[1])
            return b.updateEntity(a[0], nullptr);
        if (!vm.valid(a[1], kEntityStateSize))
            return botlib::blerr::kInvalidEntityNumber;
        // type, flags, origin, angles, old_origin, mins, maxs, then ints.
        botlib::BotEntityState s;
        const int32_t p = a[1];
        s.type = vm.readInt(p);
        s.flags = vm.readInt(p + 4);
        s.origin = vec(p + 8);
        s.angles = vec(p + 20);
        s.oldOrigin = vec(p + 32);
        s.mins = vec(p + 44);
        s.maxs = vec(p + 56);
        int32_t *ints[] = {&s.groundEnt, &s.solid,    &s.modelIndex, &s.modelIndex2, &s.frame,    &s.event,
                           &s.eventParm, &s.powerups, &s.weapon,     &s.legsAnim,    &s.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            *ints[i] = vm.readInt(p + 68 + 4 * int32_t(i));
        return b.updateEntity(a[0], &s);
    }
    case kLibTest:
        return 0;
    case kGetSnapshotEntity: { // (client, sequence) -> entity number, -1 past the end
        const auto it = _botSnapshots.find(a[0]);
        if (it == _botSnapshots.end() || a[1] < 0 || size_t(a[1]) >= it->second.size())
            return -1;
        return it->second[size_t(a[1])];
    }
    case kGetConsoleMessage: { // (client, buffer, size) -> whether there was one
        size_t &read = _botMessagesRead[a[0]];
        while (read < _serverCommands.size()) {
            const auto &[to, text] = _serverCommands[read++];
            if (to == a[0] || to == -1) {
                vm.writeString(a[1], text, a[2]);
                return 1;
            }
        }
        return 0;
    }
    case kLibUserCommand: { // (client, usercmd_t*): the bot's command for this frame
        if (!vm.valid(a[1], kUserCommandSize))
            return 0;
        UserCommand cmd;
        cmd.serverTime = vm.readInt(a[1]);
        const int32_t bw = vm.readInt(a[1] + 4);
        cmd.buttons = bw & 0xff;
        cmd.weapon = uint8_t(bw >> 8);
        for (int i = 0; i < 3; ++i)
            cmd.angles[size_t(i)] = vm.readInt(a[1] + 8 + 4 * i);
        cmd.forward = int8_t(vm.memory()[size_t(a[1] + 20)]);
        cmd.right = int8_t(vm.memory()[size_t(a[1] + 21)]);
        cmd.up = int8_t(vm.memory()[size_t(a[1] + 22)]);
        clientThink(a[0], cmd);
        return 0;
    }

    case kAasEntityInfo: {
        if (!vm.checkWrite(a[1], kEntityInfoSize))
            return 0;
        const botlib::BotEntityInfo &e = b.entityInfo(a[0]);
        const int32_t p = a[1];
        vm.writeInt(p, e.valid);
        vm.writeInt(p + 4, e.type);
        vm.writeInt(p + 8, e.flags);
        vm.writeInt(p + 12, Qvm::fromFloat(e.lastTime));
        vm.writeInt(p + 16, Qvm::fromFloat(e.updateTime));
        vm.writeInt(p + 20, e.number);
        const Vec3f *vecs[] = {&e.origin, &e.angles, &e.oldOrigin, &e.lastVisOrigin, &e.mins, &e.maxs};
        for (size_t i = 0; i < std::size(vecs); ++i)
            writeVec(p + 24 + 12 * int32_t(i), *vecs[i]);
        const int ints[] = {e.groundEnt, e.solid,    e.modelIndex, e.modelIndex2, e.frame,    e.event,
                            e.eventParm, e.powerups, e.weapon,     e.legsAnim,    e.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            vm.writeInt(p + 96 + 4 * int32_t(i), ints[i]);
        return 0;
    }
    case kAasInitialized:
        return b.aasInitialized();
    case kAasPresenceTypeBoundingBox: {
        Vec3f mins{};
        Vec3f maxs{};
        botlib::AasWorld::presenceBox(a[0], mins, maxs);
        if (vm.checkWrite(a[1], 12) && vm.checkWrite(a[2], 12)) {
            writeVec(a[1], mins);
            writeVec(a[2], maxs);
        }
        return 0;
    }
    case kAasTime:
        return Qvm::fromFloat(b.time());
    case kAasPointAreaNum:
        return aas.pointArea(vec(a[0]));
    case kAasTraceAreas: { // (start, end, areas*, points*, max) -> count
        std::vector<int> areas;
        std::vector<Vec3f> points;
        const int n = aas.traceAreas(vec(a[0]), vec(a[1]), areas, a[3] ? &points : nullptr, a[4]);
        if (!vm.checkWrite(a[2], 4 * n) || (a[3] && !vm.checkWrite(a[3], 12 * n)))
            return 0;
        for (int i = 0; i < n; ++i) {
            vm.writeInt(a[2] + 4 * i, areas[size_t(i)]);
            if (a[3])
                writeVec(a[3] + 12 * i, points[size_t(i)]);
        }
        return n;
    }
    case kAasPointContents:
        return _world ? _world->pointContents(vec(a[0]), 0) : 0;
    case kAasNextBspEntity: {
        const int next = a[0] + 1;
        return next >= 1 && size_t(next) <= _bspEntities.size() ? next : 0;
    }
    case kAasValueForBspEpairKey:
    case kAasVectorForBspEpairKey:
    case kAasFloatForBspEpairKey:
    case kAasIntForBspEpairKey: {
        // Entities from 1, in the map's order.
        const int e = a[0] - 1;
        const std::string key = str(1, 256);
        const bool found = e >= 0 && size_t(e) < _bspEntities.size() && _bspEntities[size_t(e)].has(key);
        const std::string value = found ? _bspEntities[size_t(e)].get(key) : std::string();
        if (number == kAasValueForBspEpairKey) {
            vm.writeString(a[2], value, a[3]);
        } else if (number == kAasVectorForBspEpairKey) {
            float v[3] = {0, 0, 0};
            std::sscanf(value.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
            if (vm.checkWrite(a[2], 12))
                writeVec(a[2], {v[0], v[1], v[2]});
        } else if (vm.checkWrite(a[2], 4)) {
            vm.writeInt(a[2], number == kAasFloatForBspEpairKey ? Qvm::fromFloat(float(std::atof(value.c_str())))
                                                                : std::atoi(value.c_str()));
        }
        return found;
    }
    case kAasAreaReachability:
        return aas.validArea(a[0]) ? aas.settings(a[0]).numReach : 0;
    case kAasAreaTravelTimeToGoalArea:
        return aas.travelTime(a[0], vec(a[1]), a[2], a[3]);
    case kAasSwimming: {
        Vec3f p = vec(a[0]);
        p[2] -= 2;
        return _world && (_world->pointContents(p, 0) & kContentsLiquid) ? 1 : 0;
    }
    case kAasPredictClientMovement: {
        // (move*, entity, origin, presencetype, onground, velocity, cmdmove,
        // cmdframes, maxframes, frametime, stopevent, stoparea, visualize)
        botlib::BotClientMove m;
        const bool ok = b.predictClientMovement(m, a[1], vec(a[2]), a[3], a[4] != 0, vec(a[5]), vec(a[6]), a[7], a[8],
                                                flt(9), a[10], a[11]);
        // aas_clientmove_t: endpos, endarea, velocity, the trace
        // (startsolid, fraction, endpos, ent, lastarea, area, planenum),
        // presencetype, stopevent, endcontents, time, frames.
        if (!vm.checkWrite(a[0], 84))
            return 0;
        const int32_t p = a[0];
        std::memset(&vm.memory()[size_t(p)], 0, 84);
        writeVec(p, m.endPos);
        vm.writeInt(p + 12, m.endArea);
        writeVec(p + 16, m.velocity);
        vm.writeInt(p + 28, m.traceStartSolid);
        vm.writeInt(p + 32, Qvm::fromFloat(m.traceFraction));
        writeVec(p + 36, m.traceEnd);
        vm.writeInt(p + 48, m.traceEntity);
        vm.writeInt(p + 64, m.presence);
        vm.writeInt(p + 68, m.stopEvent);
        vm.writeInt(p + 72, m.endContents);
        vm.writeInt(p + 76, Qvm::fromFloat(m.time));
        vm.writeInt(p + 80, m.frames);
        return ok;
    }

    case kEaSay:
        b.say(a[0], str(1));
        return 0;
    case kEaSayTeam:
        b.sayTeam(a[0], str(1));
        return 0;
    case kEaCommand:
        b.command(a[0], str(1));
        return 0;
    case kEaUseItem:
        b.command(a[0], "use " + str(1));
        return 0;
    case kEaDropItem:
        b.command(a[0], "drop " + str(1));
        return 0;
    case kEaUseInv:
        b.command(a[0], "invuse " + str(1));
        return 0;
    case kEaDropInv:
        b.command(a[0], "invdrop " + str(1));
        return 0;
    case kEaGesture:
        b.input(a[0]).actionFlags |= botlib::action::kGesture;
        return 0;
    case kEaTalk:
        b.input(a[0]).actionFlags |= botlib::action::kTalk;
        return 0;
    case kEaAttack:
        b.input(a[0]).actionFlags |= botlib::action::kAttack;
        return 0;
    case kEaAltAttack:
        b.input(a[0]).actionFlags |= botlib::action::kAltAttack;
        return 0;
    case kEaUse:
        b.input(a[0]).actionFlags |= botlib::action::kUse;
        return 0;
    case kEaRespawn:
        b.input(a[0]).actionFlags |= botlib::action::kRespawn;
        return 0;
    case kEaCrouch:
        b.input(a[0]).actionFlags |= botlib::action::kCrouch;
        return 0;
    case kEaMoveUp:
        b.input(a[0]).actionFlags |= botlib::action::kMoveUp;
        return 0;
    case kEaMoveDown:
        b.input(a[0]).actionFlags |= botlib::action::kMoveDown;
        return 0;
    case kEaMoveForward:
        b.input(a[0]).actionFlags |= botlib::action::kMoveForward;
        return 0;
    case kEaMoveBack:
        b.input(a[0]).actionFlags |= botlib::action::kMoveBack;
        return 0;
    case kEaMoveLeft:
        b.input(a[0]).actionFlags |= botlib::action::kMoveLeft;
        return 0;
    case kEaMoveRight:
        b.input(a[0]).actionFlags |= botlib::action::kMoveRight;
        return 0;
    case kEaSelectWeapon:
        b.input(a[0]).weapon = a[1];
        return 0;
    case kEaJump:
        b.jump(a[0]);
        return 0;
    case kEaDelayedJump:
        b.delayedJump(a[0]);
        return 0;
    case kEaMove:
        b.move(a[0], vec(a[1]), flt(2));
        return 0;
    case kEaView:
        b.input(a[0]).viewAngles = vec(a[1]);
        return 0;
    case kEaEndRegular:
        return 0;
    case kEaGetInput: {
        if (!vm.checkWrite(a[2], kInputSize))
            return 0;
        const botlib::BotInput in = b.getInput(a[0], flt(1));
        vm.writeInt(a[2], Qvm::fromFloat(in.thinkTime));
        writeVec(a[2] + 4, in.dir);
        vm.writeInt(a[2] + 16, Qvm::fromFloat(in.speed));
        writeVec(a[2] + 20, in.viewAngles);
        vm.writeInt(a[2] + 32, in.actionFlags);
        vm.writeInt(a[2] + 36, in.weapon);
        return 0;
    }
    case kEaResetInput:
        b.resetInput(a[0]);
        return 0;

    case kAiLoadCharacter: {
        std::string log;
        const int h = b.characters().load(b.scriptLoader(), str(0, 256), flt(1), &log);
        if (!h && print)
            print("couldn't load bot character " + str(0, 256) + ":\n" + log);
        return h;
    }
    case kAiFreeCharacter:
        b.characters().free(a[0]);
        return 0;
    case kAiCharacteristicFloat:
        return Qvm::fromFloat(b.characters().floatValue(a[0], a[1]));
    case kAiCharacteristicBFloat:
        return Qvm::fromFloat(b.characters().boundedFloat(a[0], a[1], flt(2), flt(3)));
    case kAiCharacteristicInteger:
        return b.characters().integer(a[0], a[1]);
    case kAiCharacteristicBInteger:
        return b.characters().boundedInteger(a[0], a[1], a[2], a[3]);
    case kAiCharacteristicString:
        vm.writeString(a[2], b.characters().string(a[0], a[1]), a[3]);
        return 0;

    // Chat.
    case kAiAllocChatState:
        return b.chat.allocState();
    case kAiFreeChatState:
        b.chat.freeState(a[0]);
        return 0;
    case kAiLoadChatFile: // (chatstate, file, name)
        if (b.chat.loadChatFile(a[0], b.scriptLoader(), str(1, 256), str(2, 256)))
            return botlib::blerr::kNone;
        if (print)
            print("couldn't load chat " + str(2, 256) + " from " + str(1, 256) + "\n");
        return botlib::blerr::kCannotLoadInitialChat;
    case kAiSetChatGender:
        b.chat.setGender(a[0], a[1]);
        return 0;
    case kAiSetChatName:
        b.chat.setName(a[0], str(1, 256));
        return 0;
    case kAiQueueConsoleMessage:
        b.chat.queueConsoleMessage(a[0], a[1], str(2, 256));
        return 0;
    case kAiRemoveConsoleMessage:
        b.chat.removeConsoleMessage(a[0], a[1]);
        return 0;
    case kAiNextConsoleMessage: { // (chatstate, bot_consolemessage_t*): handle, time, type, text
        const botlib::BotChat::ConsoleMessage *m = b.chat.nextConsoleMessage(a[0]);
        if (!m || !vm.checkWrite(a[1], 12 + 256))
            return 0;
        vm.writeInt(a[1], m->handle);
        vm.writeInt(a[1] + 4, Qvm::fromFloat(m->time));
        vm.writeInt(a[1] + 8, m->type);
        vm.writeString(a[1] + 12, m->text, 256);
        return m->handle;
    }
    case kAiNumConsoleMessage:
        return b.chat.numConsoleMessages(a[0]);
    case kAiInitialChat: // (chatstate, type, mcontext, var0..var7)
    case kAiReplyChat: { // (chatstate, message, mcontext, vcontext, var0..var7)
        const int first = number == kAiInitialChat ? 3 : 4;
        std::vector<std::string> texts(8);
        std::vector<const char *> vars(8, nullptr);
        for (int i = 0; i < 8; ++i)
            if (a[first + i]) {
                texts[size_t(i)] = str(first + i, 256);
                vars[size_t(i)] = texts[size_t(i)].c_str();
            }
        if (number == kAiInitialChat) {
            b.chat.initialChat(a[0], str(1, 256), a[2], vars);
            return 0;
        }
        return b.chat.replyChat(a[0], str(1, 256), a[2], a[3], vars);
    }
    case kAiChatLength:
        return b.chat.chatLength(a[0]);
    case kAiEnterChat:
        b.chat.enterChat(a[0], a[1], a[2]);
        return 0; // (chatstate, the bot's client, to whom)
    case kAiStringContains:
        return botlib::BotChat::stringContains(str(0), str(1), a[2] != 0);
    case kAiFindMatch: { // (text, bot_match_t*, context)
        botlib::BotMatch m;
        const bool found = b.chat.findMatch(str(0, 256), m, a[2]);
        if (vm.checkWrite(a[1], kMatchSize))
            writeMatch(a[1], m);
        return found;
    }
    case kAiMatchVariable: { // (bot_match_t*, variable, buffer, size)
        botlib::BotMatch m;
        if (!readMatch(a[0], m))
            return 0;
        vm.writeString(a[2], botlib::BotChat::matchVariable(m, a[1]), a[3]);
        return 0;
    }
    case kAiUnifyWhiteSpaces: { // (text): in place, never longer
        std::string text = str(0, 1024);
        botlib::BotChat::unifyWhiteSpaces(text);
        vm.writeString(a[0], text, int32_t(text.size()) + 1);
        return 0;
    }
    case kAiReplaceSynonyms: { // (text, context): in place, kept to a message's length
        std::string text = str(0, 256);
        b.chat.replaceSynonyms(text, a[1]);
        vm.writeString(a[0], text, 256);
        return 0;
    }
    case kAiNumInitialChats:
        return b.chat.numInitialChats(a[0], str(1, 256));
    case kAiGetChatMessage:
        vm.writeString(a[1], b.chat.takeChatMessage(a[0]), a[2]);
        return 0;

    // Goals.
    case kAiAllocGoalState:
        return b.allocGoalState(a[0]);
    case kAiFreeGoalState:
        b.freeGoalState(a[0]);
        return 0;
    case kAiResetGoalState:
        b.resetGoalState(a[0]);
        return 0;
    case kAiResetAvoidGoals:
        b.resetAvoidGoals(a[0]);
        return 0;
    case kAiEmptyGoalStack:
        b.emptyGoalStack(a[0]);
        return 0;
    case kAiPushGoal: {
        botlib::BotGoal g;
        if (readGoal(a[1], g))
            b.pushGoal(a[0], g);
        return 0;
    }
    case kAiPopGoal:
        b.popGoal(a[0]);
        return 0;
    case kAiGetTopGoal:
    case kAiGetSecondGoal: {
        botlib::BotGoal g;
        if (!b.topGoal(a[0], g, number == kAiGetTopGoal ? 1 : 2))
            return 0;
        return writeGoal(a[1], g);
    }
    case kAiChooseLtgItem:
        return b.chooseLtgItem(a[0], vec(a[1]), inventory(a[2]), a[3]);
    case kAiChooseNbgItem: {
        botlib::BotGoal ltg;
        const bool haveLtg = a[4] && readGoal(a[4], ltg);
        return b.chooseNbgItem(a[0], vec(a[1]), inventory(a[2]), a[3], haveLtg ? &ltg : nullptr, flt(5));
    }
    case kAiTouchingGoal: {
        botlib::BotGoal g;
        return readGoal(a[1], g) && botlib::BotLibrary::touchingGoal(vec(a[0]), g);
    }
    case kAiItemGoalInVisButNotVisible: {
        botlib::BotGoal g;
        return readGoal(a[3], g) && b.itemGoalInVisButNotVisible(a[0], vec(a[1]), g);
    }
    case kAiGetLevelItemGoal: {
        botlib::BotGoal g;
        const int n = b.levelItemGoal(a[0], str(1, 256), g);
        if (n >= 0)
            writeGoal(a[2], g);
        return n;
    }
    case kAiAvoidGoalTime:
        return Qvm::fromFloat(b.avoidGoalTime(a[0], a[1]));
    case kAiRemoveFromAvoidGoals:
        b.removeFromAvoidGoals(a[0], a[1]);
        return 0;
    case kAiInitLevelItems:
        b.initLevelItems();
        return 0;
    case kAiUpdateEntityItems:
        b.updateEntityItems();
        return 0;
    case kAiLoadItemWeights:
        return b.loadItemWeights(a[0], str(1, 256));
    case kAiFreeItemWeights:
        b.freeItemWeights(a[0]);
        return 0;
    case kAiGoalName:
        vm.writeString(a[1], b.goalName(a[0]), a[2]);
        return 0;
    case kAiGetNextCampSpotGoal: {
        botlib::BotGoal g;
        const int n = b.nextCampSpotGoal(a[0], g);
        if (n)
            writeGoal(a[1], g);
        return n;
    }
    case kAiGetMapLocationGoal: {
        botlib::BotGoal g;
        return b.mapLocationGoal(str(0, 256), g) && writeGoal(a[1], g);
    }
    case kAiDumpAvoidGoals:
    case kAiDumpGoalStack:
    case kAiSaveGoalFuzzyLogic:
        return 0;

    // Movement.
    case kAiAllocMoveState:
        return b.allocMoveState();
    case kAiFreeMoveState:
        b.freeMoveState(a[0]);
        return 0;
    case kAiResetMoveState:
        b.resetMoveState(a[0]);
        return 0;
    case kAiInitMoveState: {
        if (!vm.valid(a[1], kInitMoveSize))
            return 0;
        botlib::BotInitMove m;
        m.origin = vec(a[1]);
        m.velocity = vec(a[1] + 12);
        m.viewOffset = vec(a[1] + 24);
        m.entity = vm.readInt(a[1] + 36);
        m.client = vm.readInt(a[1] + 40);
        m.thinkTime = Qvm::asFloat(vm.readInt(a[1] + 44));
        m.presence = vm.readInt(a[1] + 48);
        m.viewAngles = vec(a[1] + 52);
        m.orMoveFlags = vm.readInt(a[1] + 64);
        b.initMoveState(a[0], m);
        return 0;
    }
    case kAiMoveToGoal: { // (result*, movestate, goal*, travelflags)
        botlib::BotGoal g;
        botlib::BotMoveResult r;
        if (readGoal(a[2], g))
            r = b.moveToGoal(a[1], g, a[3]);
        else
            r.failure = true;
        if (!vm.checkWrite(a[0], kMoveResultSize))
            return 0;
        const int32_t p = a[0];
        vm.writeInt(p, r.failure);
        vm.writeInt(p + 4, r.type);
        vm.writeInt(p + 8, r.blocked);
        vm.writeInt(p + 12, r.blockEntity);
        vm.writeInt(p + 16, r.travelType);
        vm.writeInt(p + 20, r.flags);
        vm.writeInt(p + 24, r.weapon);
        writeVec(p + 28, r.moveDir);
        writeVec(p + 40, r.idealViewAngles);
        return 0;
    }
    case kAiMoveInDirection:
        return b.moveInDirection(a[0], vec(a[1]), flt(2), a[3]);
    case kAiResetAvoidReach:
        b.resetAvoidReach(a[0]);
        return 0;
    case kAiResetLastAvoidReach:
        b.resetLastAvoidReach(a[0]);
        return 0;
    case kAiReachabilityArea:
        return b.reachabilityArea(vec(a[0]));
    case kAiMovementViewTarget: { // (movestate, goal*, travelflags, lookahead, target*)
        botlib::BotGoal g;
        Vec3f target{};
        if (!readGoal(a[1], g) || !b.movementViewTarget(a[0], g, a[2], flt(3), target) || !vm.checkWrite(a[4], 12))
            return 0;
        writeVec(a[4], target);
        return 1;
    }
    case kAiPredictVisiblePosition: { // (origin, area, goal*, travelflags, target*)
        botlib::BotGoal g;
        Vec3f target{};
        if (!readGoal(a[2], g) || !b.predictVisiblePosition(vec(a[0]), a[1], g, a[3], target) ||
            !vm.checkWrite(a[4], 12))
            return 0;
        writeVec(a[4], target);
        return 1;
    }

    // Weapons.
    case kAiAllocWeaponState:
        return b.allocWeaponState();
    case kAiFreeWeaponState:
        b.freeWeaponState(a[0]);
        return 0;
    case kAiResetWeaponState:
        return 0;
    case kAiLoadWeaponWeights:
        return b.loadWeaponWeights(a[0], str(1, 256));
    case kAiChooseBestFightWeapon:
        return b.chooseBestFightWeapon(a[0], inventory(a[1]));
    case kAiGetWeaponInfo: { // (weaponstate, weapon, weaponinfo_t*)
        const botlib::WeaponInfo *w = b.weaponInfo(a[1]);
        if (!w || !vm.checkWrite(a[2], kWeaponInfoSize))
            return 0;
        writeWeaponInfo(a[2], *w);
        return 0;
    }

    case kAiGeneticParentsAndChildSelection:
    case kAiInterbreedGoalFuzzyLogic:
    case kAiMutateGoalFuzzyLogic:
        return 0;
    default:
        return fail();
    }
}

} // namespace reluva::idtech3::eliteforce
