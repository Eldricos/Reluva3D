// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_chat.h"
#include "engines/idtech3/botlib/bot_config.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// The bot library's error codes (0: none), as the game reads them.
namespace blerr {
constexpr int kNone = 0;
constexpr int kLibraryNotSetup = 1;
constexpr int kInvalidEntityNumber = 2;
constexpr int kNoAasFile = 3;
constexpr int kCannotOpenAasFile = 4;
constexpr int kWrongAasFileId = 5;
constexpr int kWrongAasFileVersion = 6;
constexpr int kCannotReadAasLump = 7;
constexpr int kCannotLoadInitialChat = 8;
constexpr int kCannotLoadItemWeights = 9;
constexpr int kCannotLoadItemConfig = 10;
constexpr int kCannotLoadWeaponWeights = 11;
constexpr int kCannotLoadWeaponConfig = 12;
} // namespace blerr

// What a bot does this frame (the elementary actions), which the game
// turns into its user command. Elite Force's values, the bot library's
// older ones (read from the game's conversion; docs/engines/idtech3/games/stvef/systems/syscalls.md):
// jumping and moving up share a bit, as do crouching and moving down.
namespace action {
constexpr int kAttack = 0x1;
constexpr int kUse = 0x2;
constexpr int kRespawn = 0x4;
constexpr int kJump = 0x8;
constexpr int kMoveUp = 0x8;
constexpr int kCrouch = 0x10;
constexpr int kMoveDown = 0x10;
constexpr int kMoveForward = 0x20;
constexpr int kMoveBack = 0x40;
constexpr int kMoveLeft = 0x80;
constexpr int kMoveRight = 0x100;
constexpr int kDelayedJump = 0x200;
constexpr int kTalk = 0x400;
constexpr int kGesture = 0x800;
constexpr int kWalk = 0x1000;
constexpr int kAltAttack = 0x2000;
constexpr int kJumpedLastFrame = 0x10000000;
} // namespace action

struct BotInput {
    float thinkTime = 0;
    Vec3f dir{};
    float speed = 0; // 0..400
    Vec3f viewAngles{};
    int actionFlags = 0;
    int weapon = 0;
};

// What a bot goes for: an item, a spot.
struct BotGoal {
    Vec3f origin{};
    int area = 0;
    Vec3f mins{};
    Vec3f maxs{};
    int entity = 0;
    int number = 0;
    int flags = 0;
    int itemInfo = 0;
};
namespace goalflag {
constexpr int kItem = 1;
constexpr int kRoam = 2;
constexpr int kDropped = 4;
} // namespace goalflag

// How moving went this frame, for the game.
struct BotMoveResult {
    bool failure = false;
    int type = 0;
    bool blocked = false;
    int blockEntity = 0;
    int travelType = 0;
    int flags = 0;
    int weapon = 0;
    Vec3f moveDir{};
    Vec3f idealViewAngles{};
};
namespace moveflag {
constexpr int kBarrierJump = 1;
constexpr int kOnGround = 2;
constexpr int kSwimming = 4;
constexpr int kAgainstLadder = 8;
constexpr int kWaterJump = 16;
constexpr int kTeleported = 32;
constexpr int kWalk = 512;
} // namespace moveflag
namespace moveresult {
constexpr int kMovementView = 1;
constexpr int kSwimView = 2;
constexpr int kWaiting = 4;
constexpr int kMovementViewSet = 8;
constexpr int kMovementWeapon = 16;
constexpr int kOnTopOfObstacle = 32;
constexpr int kOnTopOfFuncBob = 64;
constexpr int kOnTopOfElevator = 128;
// result types
constexpr int kTypeInSolidArea = 7;
constexpr int kTypeElevatorUp = 2;
} // namespace moveresult
namespace movetype {
constexpr int kWalk = 1;
constexpr int kCrouch = 2;
constexpr int kJump = 4;
} // namespace movetype
// The state the game sets each frame before moving (bot_initmove_t).
struct BotInitMove {
    Vec3f origin{};
    Vec3f velocity{};
    Vec3f viewOffset{};
    int entity = 0;
    int client = 0;
    float thinkTime = 0;
    int presence = 0;
    Vec3f viewAngles{};
    int orMoveFlags = 0;
};

// Why a movement prediction stopped (what the game asked to stop at).
namespace stopevent {
constexpr int kNone = 0;
constexpr int kHitGround = 1;
constexpr int kLeaveGround = 2;
constexpr int kEnterWater = 4;
constexpr int kEnterSlime = 8;
constexpr int kEnterLava = 16;
constexpr int kHitGroundDamage = 32;
constexpr int kGap = 64;
constexpr int kTouchJumpPad = 128;
constexpr int kTouchTeleporter = 256;
constexpr int kEnterArea = 512;
constexpr int kHitGroundArea = 1024;
constexpr int kHitBoundingBox = 2048;
constexpr int kTouchClusterPortal = 4096;
} // namespace stopevent

// Where a player moving as given ends up (aas_clientmove_t).
struct BotClientMove {
    Vec3f endPos{};
    int endArea = 0;
    Vec3f velocity{};
    // The last trace: started in solid, how far, where, what it hit.
    bool traceStartSolid = false;
    float traceFraction = 1;
    Vec3f traceEnd{};
    int traceEntity = 0;
    int presence = 0;
    int stopEvent = 0;
    int endContents = 0;
    float time = 0;
    int frames = 0;
};

// Contents the bot library traces against (Quake III's numbers).
namespace contents {
constexpr int kSolid = 1;
constexpr int kLava = 8;
constexpr int kSlime = 16;
constexpr int kWater = 32;
constexpr int kPlayerClip = 0x10000;
constexpr int kBody = 0x2000000;
} // namespace contents

// An entity as the game describes it to the bot library each frame.
struct BotEntityState {
    int type = 0;
    int flags = 0;
    Vec3f origin{};
    Vec3f angles{};
    Vec3f oldOrigin{};
    Vec3f mins{};
    Vec3f maxs{};
    int groundEnt = 0;
    int solid = 0;
    int modelIndex = 0;
    int modelIndex2 = 0;
    int frame = 0;
    int event = 0;
    int eventParm = 0;
    int powerups = 0;
    int weapon = 0;
    int legsAnim = 0;
    int torsoAnim = 0;
};

// And as the bot library tells the game about it.
struct BotEntityInfo {
    bool valid = false;
    int type = 0;
    int flags = 0;
    float lastTime = 0;
    float updateTime = 0;
    int number = 0;
    Vec3f origin{};
    Vec3f angles{};
    Vec3f oldOrigin{};
    Vec3f lastVisOrigin{};
    Vec3f mins{};
    Vec3f maxs{};
    int groundEnt = 0;
    int solid = 0;
    int modelIndex = 0;
    int modelIndex2 = 0;
    int frame = 0;
    int event = 0;
    int eventParm = 0;
    int powerups = 0;
    int weapon = 0;
    int legsAnim = 0;
    int torsoAnim = 0;
};

// The engine side of Quake III's bot library, as Elite Force's game code
// uses it: its settings, the level's .aas map, the entities, the bots'
// characters and what each bot does each frame. Behaviour read from the
// GPL ioquake3 bot library (be_interface.c, be_aas_*.c, be_ea.c,
// be_ai_char.c); new code.
class BotLibrary {
public:
    static constexpr int kMaxClients = 64;
    static constexpr int kMaxEntities = 1024;
    static constexpr int kMaxUserMove = 400;

    // Reads a file of the game's (pk3s and folders), for the scripts and maps.
    std::function<bool(const std::string &path, std::vector<uint8_t> &data)> readFile;
    // A command a bot sends as if typed ("say hello").
    std::function<void(int client, const std::string &command)> clientCommand;
    std::function<void(const std::string &text)> print;
    // A box swept through the world and its entities (the engine's trace).
    struct Trace {
        float fraction = 1;
        Vec3f end{};
        bool startSolid = false;
        int entity = 1023; // 1022 the world, 1023 nothing
        Vec3f normal{};    // of what it hit
    };
    std::function<Trace(const Vec3f &start, const Vec3f &mins, const Vec3f &maxs, const Vec3f &end, int passEntity,
                        int mask)>
        trace;
    std::function<int(const Vec3f &point)> pointContents;
    // A brush model's box (*1, *2... as numbers), unmoved, and where the
    // brush entity with it (a lift) is now.
    std::function<bool(int model, Vec3f &mins, Vec3f &maxs)> modelBounds;
    std::function<bool(int model, Vec3f &origin)> moverOrigin;
    // The map's entities (classname, origin...), from 1 as the original
    // numbers them.
    std::function<bool(int entity, const std::string &key, std::string &value)> entityKey;
    int entityCount = 0;

    int setup();
    int shutdown();
    bool isSetup() const { return _setup; }
    void setLibVar(const std::string &name, const std::string &value) { _libVars[name] = value; }
    std::string libVar(const std::string &name) const;
    void addGlobalDefine(const std::string &text) { _defines.push_back(text); }
    // Loads maps/<name>.aas.
    int loadMap(const std::string &mapName);
    int startFrame(float time);
    int updateEntity(int number, const BotEntityState *state);
    float time() const { return _time; }

    AasWorld &aas() { return _aas; }
    bool aasInitialized() const { return _aas.loaded(); }
    const BotEntityInfo &entityInfo(int number) const;

    // A loader for the bot scripts: paths under botfiles/.
    BotScript::Loader scriptLoader() const;
    const std::vector<std::string> &globalDefines() const { return _defines; }
    BotCharacters &characters() { return _characters; }

    // Elementary actions.
    BotInput &input(int client) { return _inputs[size_t(std::clamp(client, 0, kMaxClients - 1))]; }
    void say(int client, const std::string &text) { command(client, "say " + text); }
    void sayTeam(int client, const std::string &text) { command(client, "say_team " + text); }
    void command(int client, const std::string &text);
    void jump(int client);
    void delayedJump(int client);
    void move(int client, const Vec3f &dir, float speed);
    BotInput getInput(int client, float thinkTime);
    void resetInput(int client);

    // Chat (bot_chat.cpp): its shared files read at setup.
    BotChat chat;

    // Goals (bot_goals.cpp): the level's items, each bot's goal stack and
    // goals to avoid for a while, and choosing items by their weights.
    int allocGoalState(int client);
    void freeGoalState(int handle);
    void resetGoalState(int handle);
    void resetAvoidGoals(int handle);
    void pushGoal(int handle, const BotGoal &goal);
    void popGoal(int handle);
    void emptyGoalStack(int handle);
    bool topGoal(int handle, BotGoal &goal, int depth = 1) const;
    int loadItemWeights(int handle, const std::string &file);
    void freeItemWeights(int handle);
    bool chooseLtgItem(int handle, const Vec3f &origin, const std::vector<int> &inventory, int travelFlags);
    bool chooseNbgItem(int handle, const Vec3f &origin, const std::vector<int> &inventory, int travelFlags,
                       const BotGoal *ltg, float maxTime);
    static bool touchingGoal(const Vec3f &origin, const BotGoal &goal);
    bool itemGoalInVisButNotVisible(int viewer, const Vec3f &eye, const BotGoal &goal) const;
    int levelItemGoal(int index, const std::string &name, BotGoal &goal) const;
    float avoidGoalTime(int handle, int number) const;
    void removeFromAvoidGoals(int handle, int number);
    void initLevelItems();
    void updateEntityItems();
    std::string goalName(int number) const;
    int nextCampSpotGoal(int num, BotGoal &goal) const;
    bool mapLocationGoal(const std::string &name, BotGoal &goal) const;
    // The area a bot at a point is in, or its last one with reachabilities.
    int reachabilityArea(const Vec3f &origin) const;

    // Weapons (bot_weapons.cpp).
    int allocWeaponState();
    void freeWeaponState(int handle);
    int loadWeaponWeights(int handle, const std::string &file);
    int chooseBestFightWeapon(int handle, const std::vector<int> &inventory) const;
    const WeaponInfo *weaponInfo(int weapon) const;

    // Movement (bot_move.cpp): following routes, per kind of travel.
    int allocMoveState();
    void freeMoveState(int handle);
    void resetMoveState(int handle);
    void initMoveState(int handle, const BotInitMove &init);
    BotMoveResult moveToGoal(int handle, const BotGoal &goal, int travelFlags);
    bool moveInDirection(int handle, const Vec3f &dir, float speed, int type);
    void resetAvoidReach(int handle);
    void resetLastAvoidReach(int handle);
    bool movementViewTarget(int handle, const BotGoal &goal, int travelFlags, float lookahead, Vec3f &target);
    bool predictVisiblePosition(const Vec3f &origin, int area, const BotGoal &goal, int travelFlags, Vec3f &target);
    // A player's movement simulated frame by frame (`frameTime` seconds
    // each): moving by `cmdMove` (a direction and speed; up to jump, down
    // a lot to crouch) for the first `cmdFrames`, falling, sliding along
    // walls and up steps, until one of the `stopEvents` or `maxFrames`.
    bool predictClientMovement(BotClientMove &move, int entity, const Vec3f &origin, int presenceType, bool onGround,
                               const Vec3f &velocity, const Vec3f &cmdMove, int cmdFrames, int maxFrames,
                               float frameTime, int stopEvents, int stopArea);

private:
    struct LevelItem {
        int number = 0;
        int itemInfo = 0;
        int flags = 0;
        float weight = 0;
        Vec3f origin{};
        int goalArea = 0;
        Vec3f goalOrigin{};
        int entity = 0;
        float timeout = 0;
    };
    struct GoalState {
        int client = 0;
        int lastArea = 0;
        std::shared_ptr<WeightConfig> weights;
        std::vector<int> weightIndex; // per item info
        std::vector<BotGoal> stack;
        std::map<int, float> avoid; // goal number -> until when
    };
    struct WeaponState {
        std::shared_ptr<WeightConfig> weights;
        std::vector<int> weightIndex; // per weapon
    };
    struct MoveState {
        Vec3f origin{};
        Vec3f velocity{};
        Vec3f viewOffset{};
        Vec3f viewAngles{};
        Vec3f lastOrigin{};
        int entity = 0;
        int client = 0;
        int presence = 0;
        int moveFlags = 0;
        float thinkTime = 0;
        int area = 0;
        int lastArea = 0;
        int lastGoalArea = 0;
        int lastReach = 0;
        int reachArea = 0;
        int jumpReach = 0;
        float reachTime = 0;
        std::map<int, std::pair<float, int>> avoidReach; // reachability -> until when, tries
    };
    struct Spot {
        Vec3f origin{};
        int area = 0;
        std::string name;
    };
    template<class T>
    static T *state(std::vector<std::unique_ptr<T>> &v, int handle) {
        return handle >= 1 && size_t(handle) <= v.size() ? v[size_t(handle) - 1].get() : nullptr;
    }
    template<class T>
    static const T *state(const std::vector<std::unique_ptr<T>> &v, int handle) {
        return handle >= 1 && size_t(handle) <= v.size() ? v[size_t(handle) - 1].get() : nullptr;
    }
    template<class T>
    static int alloc(std::vector<std::unique_ptr<T>> &v) {
        for (size_t i = 0; i < v.size(); ++i)
            if (!v[i]) {
                v[i] = std::make_unique<T>();
                return int(i) + 1;
            }
        v.push_back(std::make_unique<T>());
        return int(v.size());
    }
    std::shared_ptr<WeightConfig> weights(const std::string &file);
    void loadConfigs();
    int bestReachableArea(const Vec3f &origin, const Vec3f &mins, const Vec3f &maxs, Vec3f &goalOrigin) const;
    bool itemAllowed(const LevelItem &li) const;
    bool chooseItem(int handle, const Vec3f &origin, const std::vector<int> &inventory, int travelFlags,
                    const BotGoal *ltg, float maxTime, bool nearby);
    int reachabilityToGoal(const Vec3f &origin, int area, int lastGoalArea, int lastArea, MoveState *ms,
                           const BotGoal &goal, int travelFlags);
    bool onGround(const MoveState &ms) const;
    void checkBlocked(const MoveState &ms, const Vec3f &dir, bool checkBottom, BotMoveResult &result) const;
    float gapDistance(const MoveState &ms, const Vec3f &hordir) const;
    BotMoveResult travel(MoveState &ms, const AasReach &reach, bool finishing);
    int entityOnTopOf(const MoveState &ms) const;

    bool _setup = false;
    std::map<std::string, std::string> _libVars;
    std::vector<std::string> _defines;
    AasWorld _aas;
    float _time = 0;
    int _frames = 0;
    std::vector<BotEntityInfo> _entities = std::vector<BotEntityInfo>(size_t(kMaxEntities));
    std::array<BotInput, kMaxClients> _inputs{};
    BotCharacters _characters;
    bool _configsLoaded = false;
    std::vector<ItemInfo> _items;
    std::vector<WeaponInfo> _weapons;
    std::map<std::string, std::shared_ptr<WeightConfig>> _weightFiles;
    std::vector<LevelItem> _levelItems;
    std::vector<Spot> _mapLocations;
    std::vector<Spot> _campSpots;
    std::vector<std::unique_ptr<GoalState>> _goalStates;
    std::vector<std::unique_ptr<WeaponState>> _weaponStates;
    std::vector<std::unique_ptr<MoveState>> _moveStates;
    std::map<int, std::string> _modelTypes; // brush model number -> its entity's class
    // Lifts, as the original's movement sees them: where the mover with
    // brush model `model` is now; whether it's down (its top below the
    // reachability's start); the middle of its top at the start's height;
    // whether a bot at `origin` stands on it.
    bool moverDown(const AasReach &reach) const;
    Vec3f moverBottomCenter(const AasReach &reach) const;
    bool onMover(const Vec3f &origin, int entity, const AasReach &reach) const;
};

} // namespace reluva::idtech3::botlib
