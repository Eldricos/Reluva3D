// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot library's system calls for Elite Force's game logic: bots
// joining (BotAllocateClient), the library's own calls (200 on: setting
// up, the level's .aas map, the entities, the bots' commands), the map
// queries (300 on), the elementary actions (400 on) and the AI (500 on).
// Quake III's numbering, which Elite Force's game uses in the ranges
// 200-211, 303-318, 401-427 and 500-572 (see eliteforce-syscalls.md);
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
    BotAllocateClient = 34,
    BotFreeClient = 35,
    // The library.
    LibSetup = 200,
    LibShutdown = 201,
    LibVarSet = 202,
    LibVarGet = 203,
    LibAddGlobalDefine = 204,
    LibStartFrame = 205,
    LibLoadMap = 206,
    LibUpdateEntity = 207,
    LibTest = 208,
    GetSnapshotEntity = 209,
    GetConsoleMessage = 210,
    LibUserCommand = 211,
    // The map.
    AasEntityInfo = 303,
    AasInitialized = 304,
    AasPresenceTypeBoundingBox = 305,
    AasTime = 306,
    AasPointAreaNum = 307,
    AasTraceAreas = 308,
    AasPointContents = 309,
    AasNextBspEntity = 310,
    AasValueForBspEpairKey = 311,
    AasVectorForBspEpairKey = 312,
    AasFloatForBspEpairKey = 313,
    AasIntForBspEpairKey = 314,
    AasAreaReachability = 315,
    AasAreaTravelTimeToGoalArea = 316,
    AasSwimming = 317,
    AasPredictClientMovement = 318,
    // Elementary actions: Quake III's numbers, assumed until Elite
    // Force's (401-427, three more than Quake III's) are known.
    EaSay = 400,
    EaSayTeam = 401,
    EaCommand = 402,
    EaAction = 403,
    EaGesture = 404,
    EaTalk = 405,
    EaAttack = 406,
    EaUse = 407,
    EaRespawn = 408,
    EaCrouch = 409,
    EaMoveUp = 410,
    EaMoveDown = 411,
    EaMoveForward = 412,
    EaMoveBack = 413,
    EaMoveLeft = 414,
    EaMoveRight = 415,
    EaSelectWeapon = 416,
    EaJump = 417,
    EaDelayedJump = 418,
    EaMove = 419,
    EaView = 420,
    EaEndRegular = 421,
    EaGetInput = 422,
    EaResetInput = 423,
    // AI.
    AiLoadCharacter = 500,
    AiFreeCharacter = 501,
    AiCharacteristicFloat = 502,
    AiCharacteristicBFloat = 503,
    AiCharacteristicInteger = 504,
    AiCharacteristicBInteger = 505,
    AiCharacteristicString = 506,
    AiAllocChatState = 507,
    AiFreeChatState = 508,
    AiQueueConsoleMessage = 509,
    AiRemoveConsoleMessage = 510,
    AiNextConsoleMessage = 511,
    AiNumConsoleMessage = 512,
    AiInitialChat = 513,
    AiReplyChat = 514,
    AiChatLength = 515,
    AiEnterChat = 516,
    AiStringContains = 517,
    AiFindMatch = 518,
    AiMatchVariable = 519,
    AiUnifyWhiteSpaces = 520,
    AiReplaceSynonyms = 521,
    AiLoadChatFile = 522,
    AiSetChatGender = 523,
    AiSetChatName = 524,
    AiResetGoalState = 525,
    AiResetAvoidGoals = 526,
    AiPushGoal = 527,
    AiPopGoal = 528,
    AiEmptyGoalStack = 529,
    AiDumpAvoidGoals = 530,
    AiDumpGoalStack = 531,
    AiGoalName = 532,
    AiGetTopGoal = 533,
    AiGetSecondGoal = 534,
    AiChooseLtgItem = 535,
    AiChooseNbgItem = 536,
    AiTouchingGoal = 537,
    AiItemGoalInVisButNotVisible = 538,
    AiGetLevelItemGoal = 539,
    AiAvoidGoalTime = 540,
    AiInitLevelItems = 541,
    AiUpdateEntityItems = 542,
    AiLoadItemWeights = 543,
    AiFreeItemWeights = 544,
    AiSaveGoalFuzzyLogic = 545,
    AiAllocGoalState = 546,
    AiFreeGoalState = 547,
    AiResetMoveState = 548,
    AiMoveToGoal = 549,
    AiMoveInDirection = 550,
    AiResetAvoidReach = 551,
    AiResetLastAvoidReach = 552,
    AiReachabilityArea = 553,
    AiMovementViewTarget = 554,
    AiAllocMoveState = 555,
    AiFreeMoveState = 556,
    AiInitMoveState = 557,
    AiChooseBestFightWeapon = 558,
    AiGetWeaponInfo = 559,
    AiLoadWeaponWeights = 560,
    AiAllocWeaponState = 561,
    AiFreeWeaponState = 562,
    AiResetWeaponState = 563,
    AiGeneticParentsAndChildSelection = 564,
    AiInterbreedGoalFuzzyLogic = 565,
    AiMutateGoalFuzzyLogic = 566,
    AiGetNextCampSpotGoal = 567,
    AiGetMapLocationGoal = 568,
    AiNumInitialChats = 569,
    AiGetChatMessage = 570,
    AiRemoveFromAvoidGoals = 571,
    AiPredictVisiblePosition = 572,
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

} // namespace

Error GameHost::serverCommand(const std::string& text, bool* handled) {
    args_ = splitCommand(text);
    int32_t result = 0;
    Error err = call(ConsoleCommand, {}, result);
    args_.clear();
    if (handled)
        *handled = result != 0;
    return err;
}

void GameHost::runQueuedConsoleCommands() {
    // What the game asked the server to run (SendConsoleCommand): the
    // engine's own commands aren't here yet, so each goes back to the game
    // as a console command (addbot, kick...), as the original does with
    // commands it doesn't know.
    while (consoleCommandsRun_ < consoleCommands_.size()) {
        const std::string text = consoleCommands_[consoleCommandsRun_++];
        for (const std::string& command : splitCommands(text)) {
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
    const Vec3f origin = entityVec(clientNum, ent::CurrentOrigin);
    const Vec3f eye{origin[0], origin[1], origin[2] + 26};
    const int32_t svFlags = svFlagsOffset();
    const int eyeLeaf = world_ ? world_->leafAt(eye) : -1;
    for (const int32_t n : linked_) {
        if (n == clientNum || (entityInt(n, svFlags) & 1))
            continue;
        if (eyeLeaf >= 0 && !entityInt(n, ent::BModel)) {
            const int leaf = world_->leafAt(entityVec(n, ent::CurrentOrigin));
            if (leaf >= 0 && !map_->clusterVisible(map_->leafs[size_t(eyeLeaf)].cluster, map_->leafs[size_t(leaf)].cluster))
                continue;
        }
        out.push_back(n);
        if (out.size() >= 256)
            break;
    }
    return out;
}

void GameHost::runBotFrame() {
    if (!bots_.isSetup() || botClients_.empty())
        return;
    for (const int32_t c : botClients_)
        botSnapshots_[c] = botSnapshotEntities(c);
    int32_t ignored = 0;
    call(BotAiStartFrame, {levelTime_}, ignored);
}

int32_t GameHost::botCall(Qvm& vm, int number, const int32_t* a) {
    auto& samples = botCallSamples_[number];
    if (samples.size() < 3)
        samples.emplace_back(a, a + 6);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto flt = [&](int i) { return Qvm::asFloat(a[i]); };
    auto vec = [&](int32_t address) { return readVec(address); };
    auto fail = [&]() {
        ++unhandled_[number];
        return 0;
    };
    botlib::BotLibrary& b = bots_;
    botlib::AasWorld& aas = b.aas();

    switch (number) {
    case BotAllocateClient: { // -> a free client number for a bot, -1 if none
        const int32_t max = std::max(1, cvars_.integer("sv_maxclients"));
        for (int32_t c = 0; c < max; ++c)
            if (!clientsInUse_.count(c)) {
                clientsInUse_.insert(c);
                botClients_.insert(c);
                botMessagesRead_[c] = serverCommands_.size();
                return c;
            }
        return -1;
    }
    case BotFreeClient:
        clientsInUse_.erase(a[0]);
        botClients_.erase(a[0]);
        botSnapshots_.erase(a[0]);
        return 0;
    case LibSetup:
        b.readFile = [this](const std::string& path, std::vector<uint8_t>& data) { return !files_.read(path, data); };
        b.clientCommand = [this](int client, const std::string& text) { clientCommand(client, text); };
        b.print = [this](const std::string& text) {
            if (print)
                print(text);
            else
                log_ += text;
        };
        return b.setup();
    case LibShutdown: return b.shutdown();
    case LibVarSet: b.setLibVar(str(0, 256), str(1, 256)); return 0;
    case LibVarGet: vm.writeString(a[1], b.libVar(str(0, 256)), a[2]); return 0;
    case LibAddGlobalDefine: b.addGlobalDefine(str(0, 1024)); return 0;
    case LibStartFrame: return b.startFrame(flt(0));
    case LibLoadMap: {
        const int err = b.loadMap(str(0, 256));
        if (map_)
            parseEntities(map_->entities, bspEntities_);
        return err;
    }
    case LibUpdateEntity: {
        if (!a[1])
            return b.updateEntity(a[0], nullptr);
        if (!vm.valid(a[1], kEntityStateSize))
            return botlib::blerr::InvalidEntityNumber;
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
        int32_t* ints[] = {&s.groundEnt, &s.solid, &s.modelIndex, &s.modelIndex2, &s.frame, &s.event,
                           &s.eventParm, &s.powerups, &s.weapon, &s.legsAnim, &s.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            *ints[i] = vm.readInt(p + 68 + 4 * int32_t(i));
        return b.updateEntity(a[0], &s);
    }
    case LibTest: return 0;
    case GetSnapshotEntity: { // (client, sequence) -> entity number, -1 past the end
        const auto it = botSnapshots_.find(a[0]);
        if (it == botSnapshots_.end() || a[1] < 0 || size_t(a[1]) >= it->second.size())
            return -1;
        return it->second[size_t(a[1])];
    }
    case GetConsoleMessage: { // (client, buffer, size) -> whether there was one
        size_t& read = botMessagesRead_[a[0]];
        while (read < serverCommands_.size()) {
            const auto& [to, text] = serverCommands_[read++];
            if (to == a[0] || to == -1) {
                vm.writeString(a[1], text, a[2]);
                return 1;
            }
        }
        return 0;
    }
    case LibUserCommand: { // (client, usercmd_t*): the bot's command for this frame
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

    case AasEntityInfo: {
        if (!vm.checkWrite(a[1], kEntityInfoSize))
            return 0;
        const botlib::BotEntityInfo& e = b.entityInfo(a[0]);
        const int32_t p = a[1];
        vm.writeInt(p, e.valid);
        vm.writeInt(p + 4, e.type);
        vm.writeInt(p + 8, e.flags);
        vm.writeInt(p + 12, Qvm::fromFloat(e.lastTime));
        vm.writeInt(p + 16, Qvm::fromFloat(e.updateTime));
        vm.writeInt(p + 20, e.number);
        const Vec3f* vecs[] = {&e.origin, &e.angles, &e.oldOrigin, &e.lastVisOrigin, &e.mins, &e.maxs};
        for (size_t i = 0; i < std::size(vecs); ++i)
            writeVec(p + 24 + 12 * int32_t(i), *vecs[i]);
        const int ints[] = {e.groundEnt, e.solid, e.modelIndex, e.modelIndex2, e.frame, e.event,
                            e.eventParm, e.powerups, e.weapon, e.legsAnim, e.torsoAnim};
        for (size_t i = 0; i < std::size(ints); ++i)
            vm.writeInt(p + 96 + 4 * int32_t(i), ints[i]);
        return 0;
    }
    case AasInitialized: return b.aasInitialized();
    case AasPresenceTypeBoundingBox: {
        Vec3f mins{}, maxs{};
        botlib::AasWorld::presenceBox(a[0], mins, maxs);
        if (vm.checkWrite(a[1], 12) && vm.checkWrite(a[2], 12)) {
            writeVec(a[1], mins);
            writeVec(a[2], maxs);
        }
        return 0;
    }
    case AasTime: return Qvm::fromFloat(b.time());
    case AasPointAreaNum: return aas.pointArea(vec(a[0]));
    case AasTraceAreas: { // (start, end, areas*, points*, max) -> count
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
    case AasPointContents: return world_ ? world_->pointContents(vec(a[0]), 0) : 0;
    case AasNextBspEntity: {
        const int next = a[0] + 1;
        return next >= 1 && size_t(next) <= bspEntities_.size() ? next : 0;
    }
    case AasValueForBspEpairKey:
    case AasVectorForBspEpairKey:
    case AasFloatForBspEpairKey:
    case AasIntForBspEpairKey: {
        // Entities from 1, in the map's order.
        const int e = a[0] - 1;
        const std::string key = str(1, 256);
        const bool found = e >= 0 && size_t(e) < bspEntities_.size() && bspEntities_[size_t(e)].has(key);
        const std::string value = found ? bspEntities_[size_t(e)].get(key) : std::string();
        if (number == AasValueForBspEpairKey) {
            vm.writeString(a[2], value, a[3]);
        } else if (number == AasVectorForBspEpairKey) {
            float v[3] = {0, 0, 0};
            std::sscanf(value.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
            if (vm.checkWrite(a[2], 12))
                writeVec(a[2], {v[0], v[1], v[2]});
        } else if (vm.checkWrite(a[2], 4)) {
            vm.writeInt(a[2], number == AasFloatForBspEpairKey ? Qvm::fromFloat(float(std::atof(value.c_str())))
                                                               : std::atoi(value.c_str()));
        }
        return found;
    }
    case AasAreaReachability: return aas.validArea(a[0]) ? aas.settings(a[0]).numReach : 0;
    case AasAreaTravelTimeToGoalArea: return aas.travelTime(a[0], vec(a[1]), a[2], a[3]);
    case AasSwimming: {
        Vec3f p = vec(a[0]);
        p[2] -= 2;
        return world_ && (world_->pointContents(p, 0) & kContentsLiquid) ? 1 : 0;
    }
    case AasPredictClientMovement: return fail(); // not yet

    case EaSay: b.say(a[0], str(1)); return 0;
    case EaSayTeam: b.sayTeam(a[0], str(1)); return 0;
    case EaCommand: b.command(a[0], str(1)); return 0;
    case EaAction: b.input(a[0]).actionFlags |= a[1]; return 0;
    case EaGesture: b.input(a[0]).actionFlags |= botlib::action::Gesture; return 0;
    case EaTalk: b.input(a[0]).actionFlags |= botlib::action::Talk; return 0;
    case EaAttack: b.input(a[0]).actionFlags |= botlib::action::Attack; return 0;
    case EaUse: b.input(a[0]).actionFlags |= botlib::action::Use; return 0;
    case EaRespawn: b.input(a[0]).actionFlags |= botlib::action::Respawn; return 0;
    case EaCrouch: b.input(a[0]).actionFlags |= botlib::action::Crouch; return 0;
    case EaMoveUp: b.input(a[0]).actionFlags |= botlib::action::MoveUp; return 0;
    case EaMoveDown: b.input(a[0]).actionFlags |= botlib::action::MoveDown; return 0;
    case EaMoveForward: b.input(a[0]).actionFlags |= botlib::action::MoveForward; return 0;
    case EaMoveBack: b.input(a[0]).actionFlags |= botlib::action::MoveBack; return 0;
    case EaMoveLeft: b.input(a[0]).actionFlags |= botlib::action::MoveLeft; return 0;
    case EaMoveRight: b.input(a[0]).actionFlags |= botlib::action::MoveRight; return 0;
    case EaSelectWeapon: b.input(a[0]).weapon = a[1]; return 0;
    case EaJump: b.jump(a[0]); return 0;
    case EaDelayedJump: b.delayedJump(a[0]); return 0;
    case EaMove: b.move(a[0], vec(a[1]), flt(2)); return 0;
    case EaView: b.input(a[0]).viewAngles = vec(a[1]); return 0;
    case EaEndRegular: return 0;
    case EaGetInput: {
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
    case EaResetInput: b.resetInput(a[0]); return 0;

    case AiLoadCharacter: {
        std::string log;
        const int h = b.characters().load(b.scriptLoader(), str(0, 256), flt(1), &log);
        if (!h && print)
            print("couldn't load bot character " + str(0, 256) + ":\n" + log);
        return h;
    }
    case AiFreeCharacter: b.characters().free(a[0]); return 0;
    case AiCharacteristicFloat: return Qvm::fromFloat(b.characters().floatValue(a[0], a[1]));
    case AiCharacteristicBFloat: return Qvm::fromFloat(b.characters().boundedFloat(a[0], a[1], flt(2), flt(3)));
    case AiCharacteristicInteger: return b.characters().integer(a[0], a[1]);
    case AiCharacteristicBInteger: return b.characters().boundedInteger(a[0], a[1], a[2], a[3]);
    case AiCharacteristicString: vm.writeString(a[2], b.characters().string(a[0], a[1]), a[3]); return 0;

    // Chat: not yet; the bots load nothing and say nothing.
    case AiAllocChatState: return b.allocState(b.chatStates);
    case AiFreeChatState: botlib::BotLibrary::freeState(b.chatStates, a[0]); return 0;
    case AiLoadChatFile: return botlib::blerr::None;
    case AiSetChatGender:
    case AiSetChatName:
    case AiQueueConsoleMessage:
    case AiRemoveConsoleMessage:
    case AiInitialChat:
    case AiEnterChat: return 0;
    case AiNextConsoleMessage:
    case AiNumConsoleMessage:
    case AiReplyChat:
    case AiChatLength:
    case AiNumInitialChats:
    case AiStringContains: return number == AiStringContains ? -1 : 0;
    case AiFindMatch:
    case AiMatchVariable:
    case AiUnifyWhiteSpaces:
    case AiReplaceSynonyms:
    case AiGetChatMessage: return fail();

    // Goals: the states and their stacks; choosing goals not yet.
    case AiAllocGoalState: return b.allocState(b.goalStates);
    case AiFreeGoalState:
        botlib::BotLibrary::freeState(b.goalStates, a[0]);
        goalStacks_.erase(a[0]);
        return 0;
    case AiResetGoalState:
    case AiEmptyGoalStack: goalStacks_[a[0]].clear(); return 0;
    case AiPushGoal:
        if (vm.valid(a[1], kGoalSize))
            goalStacks_[a[0]].emplace_back(&vm.memory()[size_t(a[1])], &vm.memory()[size_t(a[1])] + kGoalSize);
        return 0;
    case AiPopGoal:
        if (!goalStacks_[a[0]].empty())
            goalStacks_[a[0]].pop_back();
        return 0;
    case AiGetTopGoal:
    case AiGetSecondGoal: {
        const auto& stack = goalStacks_[a[0]];
        const size_t depth = number == AiGetTopGoal ? 1 : 2;
        if (stack.size() < depth || !vm.checkWrite(a[1], kGoalSize))
            return 0;
        std::memcpy(&vm.memory()[size_t(a[1])], stack[stack.size() - depth].data(), size_t(kGoalSize));
        return 1;
    }
    case AiTouchingGoal: { // (origin, goal*): whether a player there touches it
        if (!vm.valid(a[1], kGoalSize))
            return 0;
        const Vec3f o = vec(a[0]), g = vec(a[1]), gmins = vec(a[1] + 16), gmaxs = vec(a[1] + 28);
        Vec3f mins{}, maxs{};
        botlib::AasWorld::presenceBox(botlib::presence::Normal, mins, maxs);
        for (int i = 0; i < 3; ++i) {
            const size_t k = size_t(i);
            // The goal's box grown by the player's, a little more round it.
            if (o[k] < g[k] + gmins[k] - maxs[k] - 4 || o[k] > g[k] + gmaxs[k] - mins[k] + 4)
                return 0;
        }
        return 1;
    }
    case AiResetAvoidGoals:
    case AiDumpAvoidGoals:
    case AiDumpGoalStack:
    case AiInitLevelItems:
    case AiUpdateEntityItems:
    case AiRemoveFromAvoidGoals:
    case AiFreeItemWeights:
    case AiSaveGoalFuzzyLogic: return 0;
    case AiGoalName: vm.writeString(a[1], "", a[2]); return 0;
    case AiLoadItemWeights: return botlib::blerr::None;
    case AiChooseLtgItem:
    case AiChooseNbgItem:
    case AiItemGoalInVisButNotVisible:
    case AiGetNextCampSpotGoal:
    case AiGetMapLocationGoal: return 0;
    case AiGetLevelItemGoal: return -1;
    case AiAvoidGoalTime: return Qvm::fromFloat(0);

    // Movement: the states; moving not yet.
    case AiAllocMoveState: return b.allocState(b.moveStates);
    case AiFreeMoveState: botlib::BotLibrary::freeState(b.moveStates, a[0]); return 0;
    case AiResetMoveState:
    case AiResetAvoidReach:
    case AiResetLastAvoidReach:
    case AiInitMoveState: return 0;
    case AiMoveToGoal:
        if (vm.checkWrite(a[0], kMoveResultSize))
            std::memset(&vm.memory()[size_t(a[0])], 0, size_t(kMoveResultSize));
        return 0;
    case AiMoveInDirection: return 0;
    case AiReachabilityArea: return aas.pointArea(vec(a[0]));
    case AiMovementViewTarget:
    case AiPredictVisiblePosition: return 0;

    // Weapons: the states; choosing not yet.
    case AiAllocWeaponState: return b.allocState(b.weaponStates);
    case AiFreeWeaponState: botlib::BotLibrary::freeState(b.weaponStates, a[0]); return 0;
    case AiResetWeaponState: return 0;
    case AiLoadWeaponWeights: return botlib::blerr::None;
    case AiChooseBestFightWeapon: return 0;
    case AiGetWeaponInfo: return fail();

    case AiGeneticParentsAndChildSelection:
    case AiInterbreedGoalFuzzyLogic:
    case AiMutateGoalFuzzyLogic: return 0;
    default: return fail();
    }
}

} // namespace reluva::idtech3::eliteforce
