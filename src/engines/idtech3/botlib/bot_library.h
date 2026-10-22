// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

// The bot library's error codes (0: none), as the game reads them.
namespace blerr {
constexpr int None = 0, LibraryNotSetup = 1, InvalidEntityNumber = 4, NoAasFile = 6, CannotOpenAasFile = 7,
              WrongAasFileId = 8, WrongAasFileVersion = 9, CannotReadAasLump = 10;
} // namespace blerr

// What a bot does this frame (the elementary actions), which the game
// turns into its user command.
namespace action {
constexpr int Attack = 0x1, Use = 0x2, Respawn = 0x8, Jump = 0x10, MoveUp = 0x20, Crouch = 0x80, MoveDown = 0x100,
              MoveForward = 0x200, MoveBack = 0x800, MoveLeft = 0x1000, MoveRight = 0x2000, DelayedJump = 0x8000,
              Talk = 0x10000, Gesture = 0x20000, Walk = 0x80000, JumpedLastFrame = 0x10000000;
} // namespace action

struct BotInput {
    float thinkTime = 0;
    Vec3f dir{};
    float speed = 0; // 0..400
    Vec3f viewAngles{};
    int actionFlags = 0;
    int weapon = 0;
};

// An entity as the game describes it to the bot library each frame.
struct BotEntityState {
    int type = 0, flags = 0;
    Vec3f origin{}, angles{}, oldOrigin{}, mins{}, maxs{};
    int groundEnt = 0, solid = 0, modelIndex = 0, modelIndex2 = 0, frame = 0, event = 0, eventParm = 0, powerups = 0,
        weapon = 0, legsAnim = 0, torsoAnim = 0;
};

// And as the bot library tells the game about it.
struct BotEntityInfo {
    bool valid = false;
    int type = 0, flags = 0;
    float lastTime = 0, updateTime = 0;
    int number = 0;
    Vec3f origin{}, angles{}, oldOrigin{}, lastVisOrigin{}, mins{}, maxs{};
    int groundEnt = 0, solid = 0, modelIndex = 0, modelIndex2 = 0, frame = 0, event = 0, eventParm = 0, powerups = 0,
        weapon = 0, legsAnim = 0, torsoAnim = 0;
};

// The engine side of Quake III's bot library, as Elite Force's game code
// uses it: its settings, the level's .aas map, the entities, the bots'
// characters and what each bot does each frame. Behaviour read from the
// GPL ioquake3 bot library (be_interface.c, be_aas_*.c, be_ea.c,
// be_ai_char.c); new code.
class BotLibrary {
public:
    static constexpr int kMaxClients = 64, kMaxEntities = 1024, kMaxUserMove = 400;

    // Reads a file of the game's (pk3s and folders), for the scripts and maps.
    std::function<bool(const std::string& path, std::vector<uint8_t>& data)> readFile;
    // A command a bot sends as if typed ("say hello").
    std::function<void(int client, const std::string& command)> clientCommand;
    std::function<void(const std::string& text)> print;

    int setup();
    int shutdown();
    bool isSetup() const { return setup_; }
    void setLibVar(const std::string& name, const std::string& value) { libVars_[name] = value; }
    std::string libVar(const std::string& name) const;
    void addGlobalDefine(const std::string& text) { defines_.push_back(text); }
    // Loads maps/<name>.aas.
    int loadMap(const std::string& mapName);
    int startFrame(float time);
    int updateEntity(int number, const BotEntityState* state);
    float time() const { return time_; }

    AasWorld& aas() { return aas_; }
    bool aasInitialized() const { return aas_.loaded(); }
    const BotEntityInfo& entityInfo(int number) const;

    // A loader for the bot scripts: paths under botfiles/.
    BotScript::Loader scriptLoader() const;
    const std::vector<std::string>& globalDefines() const { return defines_; }
    BotCharacters& characters() { return characters_; }

    // Elementary actions.
    BotInput& input(int client) { return inputs_[size_t(std::clamp(client, 0, kMaxClients - 1))]; }
    void say(int client, const std::string& text) { command(client, "say " + text); }
    void sayTeam(int client, const std::string& text) { command(client, "say_team " + text); }
    void command(int client, const std::string& text);
    void jump(int client);
    void delayedJump(int client);
    void move(int client, const Vec3f& dir, float speed);
    BotInput getInput(int client, float thinkTime);
    void resetInput(int client);

    // Handles for the game's per-bot AI states (chat, goals, movement,
    // weapons): numbers from 1 handed out and taken back.
    int allocState(std::vector<bool>& used);
    static bool freeState(std::vector<bool>& used, int handle);
    std::vector<bool> chatStates, goalStates, moveStates, weaponStates;

private:
    bool setup_ = false;
    std::map<std::string, std::string> libVars_;
    std::vector<std::string> defines_;
    AasWorld aas_;
    float time_ = 0;
    int frames_ = 0;
    std::vector<BotEntityInfo> entities_ = std::vector<BotEntityInfo>(size_t(kMaxEntities));
    std::array<BotInput, kMaxClients> inputs_{};
    BotCharacters characters_;
};

} // namespace reluva::idtech3::botlib
