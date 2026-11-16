// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's game module calls beyond those it numbers as Elite Force's
// (docs/engines/idtech3/games/jk2/systems/game-calls.md): the order of the
// GPL multiplayer g_public.h, each checked against what 1.02a's
// jk2mpgame.qvm passes.

#include "engines/idtech3/eliteforce/game_host.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <cstring>
#include <ctime>

namespace reluva::idtech3::eliteforce {

namespace {

// entityState_t's trajectories (Quake III's): s.pos, s.apos and the
// latter's base.
constexpr int32_t kPos = 12;
constexpr int32_t kApos = 48;
constexpr int32_t kAposBase = 60;

enum Jk2GameCall : int {
    kTrace = 24,
    kEntityContact = 33,
    kDebugPolygonCreate = 39,
    kDebugPolygonDelete = 40,
    kRealTime = 41,
    kSnapVector = 42,
    kTraceCapsule = 43,
    kEntityContactCapsule = 44,
    kSpRegisterServerCmd = 45,
    kSpGetStringTextString = 46,
    kRoffClean = 47,
    kRoffUpdateEntities = 48,
    kRoffCache = 49,
    kRoffPlay = 50,
    kRoffPurgeEnt = 51,
    kMatrixMultiply = 107,
    kAngleVectors = 108,
    kPerpendicularVector = 109,
    kFloor = 110,
    kAcos = 114,
    kAsin = 115,
    // The bot library's, as Jedi Outcast numbers them (its elementary
    // actions have alt attack and force power after attack).
    kBotSetup = 200,
    kBotShutdown = 201,
    kBotLibVarSet = 202,
    kBotLibVarGet = 203,
    kBotStartFrame = 205,
    kBotLoadMap = 206,
    kBotUpdateEntity = 207,
    kBotGetSnapshotEntity = 209,
    kBotGetConsoleMessage = 210,
    kBotUserCommand = 211,
    kAasEntityInfo = 303,
    kEaSay = 400,
    kEaSayTeam = 401,
    kEaCommand = 402,
    kEaAttack = 406,
    kEaAltAttack = 407,
    kEaForcePower = 408,
    kEaUse = 409,
    kEaRespawn = 410,
    kEaCrouch = 411,
    kEaMoveUp = 412,
    kEaMoveDown = 413,
    kEaMoveForward = 414,
    kEaMoveBack = 415,
    kEaMoveLeft = 416,
    kEaMoveRight = 417,
    kEaSelectWeapon = 418,
    kEaJump = 419,
    kEaDelayedJump = 420,
    kEaMove = 421,
    kEaView = 422,
    kEaEndRegular = 423,
    kEaGetInput = 424,
    kEaResetInput = 425,
    kAiResetGoalState = 525,
    kAiResetAvoidGoals = 526,
    kAiUpdateEntityItems = 542,
    kAiAllocGoalState = 546,
    kAiFreeGoalState = 547,
    kAiResetMoveState = 548,
    kAiResetAvoidReach = 551,
    kAiAllocMoveState = 555,
    kAiFreeMoveState = 556,
    kAiAllocWeaponState = 561,
    kAiFreeWeaponState = 562,
    kAiResetWeaponState = 563,
    // Ghoul2's, on the server (bolts for the saber and hits).
    kG2ListBones = 582,
    kG2CleanModels = 599,
};

// Quake III's action bits as Jedi Outcast's bot input carries them.
constexpr int32_t kActionAttack = 0x1;
constexpr int32_t kActionUse = 0x2;
constexpr int32_t kActionRespawn = 0x8;
constexpr int32_t kActionJump = 0x10;
constexpr int32_t kActionMoveUp = 0x20;
constexpr int32_t kActionCrouch = 0x80;
constexpr int32_t kActionMoveDown = 0x100;
constexpr int32_t kActionMoveForward = 0x200;
constexpr int32_t kActionMoveBack = 0x800;
constexpr int32_t kActionMoveLeft = 0x1000;
constexpr int32_t kActionMoveRight = 0x2000;
constexpr int32_t kActionDelayedJump = 0x8000;
constexpr int32_t kActionForcePower = 0x100000;
constexpr int32_t kActionAltAttack = 0x200000;
// The jump held last frame (no second jump without letting go).
constexpr int32_t kActionJumpedLastFrame = 0x4000000;

constexpr float kPi = 3.14159265358979323846f;

} // namespace

int32_t GameHost::jk2Call(Qvm &vm, int number, const int32_t *a) {
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    auto readVec3 = [&](int32_t address) { return readVec(address); };
    BotInputJk2 &in = _jk2BotInputs[size_t(std::clamp(a[0], 0, 63))];
    // Ghoul2's, numbered 582 on in the order Ghoul2Instances lists them
    // (but its client game's own).
    if (number >= kG2ListBones && number <= kG2CleanModels)
        return _ghoul2.call(vm, jk2::Ghoul2Instances::Call(number - kG2ListBones), a);
    switch (number) {
    case kDebugPolygonCreate:
    case kDebugPolygonDelete:
        return 0;
    case kRealTime: { // (qtime_t *)
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        const int32_t fields[9] = {local.tm_sec,  local.tm_min,  local.tm_hour, local.tm_mday, local.tm_mon,
                                   local.tm_year, local.tm_wday, local.tm_yday, local.tm_isdst};
        if (a[0] && vm.checkWrite(a[0], 36))
            for (int i = 0; i < 9; ++i)
                vm.writeInt(a[0] + 4 * i, fields[i]);
        return int32_t(now);
    }
    case kSnapVector: // (vec3_t): each part to the nearest whole number, as the original rounds
        if (vm.checkWrite(a[0], 12))
            for (int i = 0; i < 3; ++i)
                vm.writeInt(a[0] + 4 * i, Qvm::fromFloat(std::nearbyint(Qvm::asFloat(vm.readInt(a[0] + 4 * i)))));
        return 0;
    case kTraceCapsule: { // as Trace, the capsule in the box moving
        const Vec3f zero{};
        clipMove(a[0], readVec(a[1]), a[2] ? readVec(a[2]) : zero, a[3] ? readVec(a[3]) : zero, readVec(a[4]), a[5],
                 a[6], true);
        return 0;
    }
    case kEntityContactCapsule: // (mins, maxs, entity)
        return entityContact(readVec(a[0]), readVec(a[1]), entityNumber(a[2]), true);
    case kSpRegisterServerCmd:
        return 1;
    case kSpGetStringTextString:
        vm.writeString(a[1], "", a[2]);
        ++_unhandled[number];
        return 0;
    // Movers' recorded paths (ROFF, docs/engines/idtech3/formats/roff.md).
    case kRoffClean:
        _roffs->clean(false);
        return 1;
    case kRoffUpdateEntities:
        _roffs->update(false, _levelTime, roffSide());
        return 0;
    case kRoffCache: // (file) -> its number, 0 if it can't be read
        return _roffs->cache(vm.string(a[0], 64), false);
    case kRoffPlay: { // (entity, roff, translate)
        const int32_t e = entityAddress(a[0]);
        if (e < 0)
            return 0;
        if (_layout.isRoffing >= 0)
            vm.writeInt(e + _layout.isRoffing, 1);
        return _roffs->play(a[0], a[1], a[2] != 0, false, _levelTime, readVec(e + kAposBase)) ? 1 : 0;
    }
    case kRoffPurgeEnt: // (entity)
        return _roffs->purge(a[0], false, _levelTime, roffSide()) ? 1 : 0;
    case kMatrixMultiply: { // (float in1[3][3], float in2[3][3], float out[3][3])
        float m1[9];
        float m2[9];
        for (int i = 0; i < 9; ++i) {
            m1[i] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
            m2[i] = Qvm::asFloat(vm.readInt(a[1] + 4 * i));
        }
        if (!vm.checkWrite(a[2], 36))
            return 0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                vm.writeInt(a[2] + 4 * (3 * r + c),
                            Qvm::fromFloat(m1[3 * r] * m2[c] + m1[3 * r + 1] * m2[3 + c] + m1[3 * r + 2] * m2[6 + c]));
        return 0;
    }
    case kAngleVectors: { // (angles, forward, right, up): any of the three may be 0
        const Vec3f ang = readVec3(a[0]);
        const float yaw = ang[1] * kPi / 180;
        const float pitch = ang[0] * kPi / 180;
        const float roll = ang[2] * kPi / 180;
        const float sy = std::sin(yaw);
        const float cy = std::cos(yaw);
        const float sp = std::sin(pitch);
        const float cp = std::cos(pitch);
        const float sr = std::sin(roll);
        const float cr = std::cos(roll);
        if (a[1])
            writeVec(a[1], {cp * cy, cp * sy, -sp});
        if (a[2])
            writeVec(a[2], {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp});
        if (a[3])
            writeVec(a[3], {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp});
        return 0;
    }
    case kPerpendicularVector: { // (dst, src): a unit vector at right angles to src
        const Vec3f src = readVec3(a[1]);
        int least = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(src[size_t(i)]) < std::fabs(src[size_t(least)]))
                least = i;
        Vec3f axis{};
        axis[size_t(least)] = 1;
        const float d = axis[0] * src[0] + axis[1] * src[1] + axis[2] * src[2];
        Vec3f p{axis[0] - d * src[0], axis[1] - d * src[1], axis[2] - d * src[2]};
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len > 0)
            p = {p[0] / len, p[1] / len, p[2] / len};
        writeVec(a[0], p);
        return 0;
    }
    case kFloor:
        return Qvm::fromFloat(std::floor(f(0)));
    case kAcos:
        return Qvm::fromFloat(std::acos(std::clamp(f(0), -1.0f, 1.0f)));
    case kAsin:
        return Qvm::fromFloat(std::asin(std::clamp(f(0), -1.0f, 1.0f)));

    // The bot library: Jedi Outcast's bots find their way by the game's
    // own waypoints, so the engine's part is their input and what they see.
    case kBotSetup:
        _jk2BotsSetup = true;
        return 0;
    case kBotShutdown:
        _jk2BotsSetup = false;
        return 0;
    case kBotLibVarSet:
    case kBotStartFrame:
    case kBotLoadMap:
    case kBotUpdateEntity:
        return 0;
    case kBotLibVarGet:
        vm.writeString(a[1], "", a[2]);
        return 0;
    case kBotGetSnapshotEntity:
    case kBotGetConsoleMessage:
        return botCall(vm, number, a);
    case kBotUserCommand: { // (client, usercmd_t*): the bot's command, run as a client's
        if (!vm.valid(a[1], _layout.userCommandSize))
            return 0;
        UserCommand cmd;
        cmd.serverTime = vm.readInt(a[1]);
        for (int i = 0; i < 3; ++i)
            cmd.angles[size_t(i)] = vm.readInt(a[1] + 4 + 4 * i);
        cmd.buttons = vm.readInt(a[1] + 16);
        const uint8_t *b = &vm.memory()[size_t(a[1] + 20)];
        cmd.weapon = b[0];
        cmd.forceSelect = b[1];
        cmd.inventorySelect = b[2];
        cmd.genericCommand = b[3];
        cmd.forward = int8_t(b[4]);
        cmd.right = int8_t(b[5]);
        cmd.up = int8_t(b[6]);
        clientThink(a[0], cmd);
        return 0;
    }
    // The bot library's goal, move and weapon states: the game allocates
    // them and resets them, but its own code does the thinking, so a
    // handle is all it needs.
    case kAiAllocGoalState:
    case kAiAllocMoveState:
    case kAiAllocWeaponState:
        return _nextAiState++;
    case kAiResetGoalState:
    case kAiResetAvoidGoals:
    case kAiUpdateEntityItems:
    case kAiFreeGoalState:
    case kAiResetMoveState:
    case kAiResetAvoidReach:
    case kAiFreeMoveState:
    case kAiFreeWeaponState:
    case kAiResetWeaponState:
        return 0;
    case kAasEntityInfo: // (entity, aas_entityinfo_t*): none, the game keeps its own
        if (vm.checkWrite(a[1], 4))
            vm.writeInt(a[1], 0);
        return 0;
    case kEaSay:
        clientCommand(a[0], "say " + str(1));
        return 0;
    case kEaSayTeam:
        clientCommand(a[0], "say_team " + str(1));
        return 0;
    case kEaCommand:
        clientCommand(a[0], str(1));
        return 0;
    case kEaAttack:
        in.actionFlags |= kActionAttack;
        return 0;
    case kEaAltAttack:
        in.actionFlags |= kActionAltAttack;
        return 0;
    case kEaForcePower:
        in.actionFlags |= kActionForcePower;
        return 0;
    case kEaUse:
        in.actionFlags |= kActionUse;
        return 0;
    case kEaRespawn:
        in.actionFlags |= kActionRespawn;
        return 0;
    case kEaCrouch:
        in.actionFlags |= kActionCrouch;
        return 0;
    case kEaMoveUp:
        in.actionFlags |= kActionMoveUp;
        return 0;
    case kEaMoveDown:
        in.actionFlags |= kActionMoveDown;
        return 0;
    case kEaMoveForward:
        in.actionFlags |= kActionMoveForward;
        return 0;
    case kEaMoveBack:
        in.actionFlags |= kActionMoveBack;
        return 0;
    case kEaMoveLeft:
        in.actionFlags |= kActionMoveLeft;
        return 0;
    case kEaMoveRight:
        in.actionFlags |= kActionMoveRight;
        return 0;
    case kEaSelectWeapon:
        in.weapon = a[1];
        return 0;
    case kEaJump:
        // Not twice running: a held jump is let go for a frame.
        if (in.actionFlags & kActionJumpedLastFrame)
            in.actionFlags &= ~kActionJump;
        else
            in.actionFlags |= kActionJump;
        return 0;
    case kEaDelayedJump:
        if (in.actionFlags & kActionJumpedLastFrame)
            in.actionFlags &= ~kActionDelayedJump;
        else
            in.actionFlags |= kActionDelayedJump;
        return 0;
    case kEaMove: { // (client, dir, speed)
        in.dir = readVec3(a[1]);
        in.speed = std::clamp(f(2), -400.0f, 400.0f);
        return 0;
    }
    case kEaView:
        in.viewAngles = readVec3(a[1]);
        return 0;
    case kEaEndRegular:
        return 0;
    case kEaGetInput: { // (client, thinktime, bot_input_t*): 40 bytes
        in.thinkTime = f(1);
        if (!vm.checkWrite(a[2], 40))
            return 0;
        vm.writeInt(a[2], Qvm::fromFloat(in.thinkTime));
        writeVec(a[2] + 4, in.dir);
        vm.writeInt(a[2] + 16, Qvm::fromFloat(in.speed));
        writeVec(a[2] + 20, in.viewAngles);
        vm.writeInt(a[2] + 32, in.actionFlags & ~kActionJumpedLastFrame);
        vm.writeInt(a[2] + 36, in.weapon);
        return 0;
    }
    case kEaResetInput: {
        // Starting afresh, but remembering a jump (and where it looks).
        const bool jumped = (in.actionFlags & (kActionJump | kActionDelayedJump)) != 0;
        const Vec3f view = in.viewAngles;
        const int32_t weapon = in.weapon;
        in = BotInputJk2{};
        in.viewAngles = view;
        in.weapon = weapon;
        if (jumped)
            in.actionFlags = kActionJumpedLastFrame;
        return 0;
    }

    default:
        ++_unhandled[number];
        return 0;
    }
}

raven::RoffSystem::Side GameHost::roffSide() {
    raven::RoffSystem::Side side;
    side.current = [this](int32_t entity, raven::RoffSystem::Vec3 &origin, raven::RoffSystem::Vec3 &angles) {
        const int32_t e = entityAddress(entity);
        if (e < 0)
            return false;
        origin = readVec(e + _layout.currentOrigin);
        angles = readVec(e + _layout.currentAngles);
        return true;
    };
    // The entity state's position and angle trajectories (Quake III's:
    // type, time, duration, base, delta).
    side.setTrajectories = [this](int32_t entity, int32_t type, int32_t time, const raven::RoffSystem::Vec3 &origin,
                                  const raven::RoffSystem::Vec3 &originDelta, const raven::RoffSystem::Vec3 &angles,
                                  const raven::RoffSystem::Vec3 &anglesDelta) {
        const int32_t e = entityAddress(entity);
        if (e < 0)
            return;
        for (const auto &[at, base, delta] :
             {std::tuple{kPos, origin, originDelta}, std::tuple{kApos, angles, anglesDelta}}) {
            _vm.writeInt(e + at, type);
            _vm.writeInt(e + at + 4, time);
            writeVec(e + at + 12, base);
            writeVec(e + at + 24, delta);
        }
    };
    side.finished = [this](int32_t entity) {
        if (const int32_t e = entityAddress(entity); e >= 0 && _layout.isRoffing >= 0)
            _vm.writeInt(e + _layout.isRoffing, 0);
    };
    // A frame's note: GAME_ROFF_NOTETRACK_CALLBACK (11), (entity, text).
    side.note = [this](int32_t entity, const std::string &note) {
        constexpr int32_t kRoffNotetrack = 11;
        int32_t ignored = 0;
        if (Error err = _vm.callWithText(kRoffNotetrack, {entity, 0}, 1, note, ignored); err && print)
            print("ROFF note \"" + note + "\": " + err.message + "\n");
    };
    return side;
}

} // namespace reluva::idtech3::eliteforce
