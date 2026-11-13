// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bot library's engine side. See bot_library.h.

#include "engines/idtech3/botlib/bot_library.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::idtech3::botlib {

int BotLibrary::setup() {
    _setup = true;
    if (print)
        print("------- BotLib Initialization -------\n");
    chat.time = [this] { return _time; };
    chat.command = [this](int client, const std::string &text) { command(client, text); };
    std::string log;
    chat.setup(scriptLoader(), [this](const std::string &name) { return libVar(name); }, &log);
    if (print)
        print(log);
    return blerr::kNone;
}

int BotLibrary::shutdown() {
    _setup = false;
    _aas = AasWorld{};
    _entities.assign(size_t(kMaxEntities), {});
    _levelItems.clear();
    _goalStates.clear();
    _weaponStates.clear();
    _moveStates.clear();
    return blerr::kNone;
}

std::string BotLibrary::libVar(const std::string &name) const {
    const auto it = _libVars.find(name);
    return it == _libVars.end() ? std::string() : it->second;
}

int BotLibrary::loadMap(const std::string &mapName) {
    if (!_setup)
        return blerr::kLibraryNotSetup;
    const std::string path = "maps/" + mapName + ".aas";
    std::vector<uint8_t> data;
    if (!readFile || !readFile(path, data)) {
        if (print)
            print("can't open " + path + "\n");
        _aas = AasWorld{};
        return blerr::kCannotOpenAasFile;
    }
    if (Error err = _aas.load(data)) {
        if (print)
            print(path + ": " + err.message + "\n");
        return blerr::kWrongAasFileVersion;
    }
    if (print)
        print("loaded " + path + ": " + std::to_string(_aas.areaCount()) + " areas, " +
              std::to_string(_aas.reachabilities().size()) + " reachabilities\n");
    _entities.assign(size_t(kMaxEntities), {});
    _frames = 0;
    // The brush models' kinds (elevators, doors...), for movement; the
    // item and weapon descriptions; the level's items.
    _modelTypes.clear();
    for (int e = 1; entityKey && e <= entityCount; ++e) {
        std::string model;
        std::string classname;
        if (entityKey(e, "model", model) && model.size() > 1 && model[0] == '*' && entityKey(e, "classname", classname))
            _modelTypes[std::atoi(model.c_str() + 1)] = classname;
    }
    loadConfigs();
    initLevelItems();
    return blerr::kNone;
}

int BotLibrary::startFrame(float time) {
    if (!_setup)
        return blerr::kLibraryNotSetup;
    _time = time;
    // Entities not updated since the last frame count as gone.
    for (BotEntityInfo &e : _entities)
        e.valid = false;
    ++_frames;
    return blerr::kNone;
}

int BotLibrary::updateEntity(int number, const BotEntityState *state) {
    if (!_setup)
        return blerr::kLibraryNotSetup;
    if (number < 0 || number >= kMaxEntities)
        return blerr::kInvalidEntityNumber;
    if (!_aas.loaded())
        return blerr::kNoAasFile;
    BotEntityInfo &e = _entities[size_t(number)];
    if (!state)
        return blerr::kNone;
    e.updateTime = _time - e.lastTime;
    e.lastTime = _time;
    e.lastVisOrigin = e.origin;
    e.type = state->type;
    e.flags = state->flags;
    e.oldOrigin = state->oldOrigin;
    e.origin = state->origin;
    e.angles = state->angles;
    e.mins = state->mins;
    e.maxs = state->maxs;
    e.groundEnt = state->groundEnt;
    e.solid = state->solid;
    e.modelIndex = state->modelIndex;
    e.modelIndex2 = state->modelIndex2;
    e.frame = state->frame;
    e.event = state->event;
    e.eventParm = state->eventParm;
    e.powerups = state->powerups;
    e.weapon = state->weapon;
    e.legsAnim = state->legsAnim;
    e.torsoAnim = state->torsoAnim;
    e.number = number;
    e.valid = true;
    return blerr::kNone;
}

const BotEntityInfo &BotLibrary::entityInfo(int number) const {
    static const BotEntityInfo kNone;
    return number >= 0 && number < kMaxEntities ? _entities[size_t(number)] : kNone;
}

BotScript::Loader BotLibrary::scriptLoader() const {
    return [this](const std::string &path, std::string &text) {
        std::vector<uint8_t> data;
        if (!readFile || !readFile("botfiles/" + path, data))
            return false;
        text.assign(data.begin(), data.end());
        return true;
    };
}

// NOLINTNEXTLINE(readability-make-member-function-const): makes a bot act
void BotLibrary::command(int client, const std::string &text) {
    if (clientCommand)
        clientCommand(client, text);
}

void BotLibrary::jump(int client) {
    // A jump only every other frame, so the game sees the key let go.
    BotInput &in = input(client);
    if (in.actionFlags & action::kJumpedLastFrame)
        in.actionFlags &= ~action::kJump;
    else
        in.actionFlags |= action::kJump;
}

void BotLibrary::delayedJump(int client) {
    BotInput &in = input(client);
    if (in.actionFlags & action::kJumpedLastFrame)
        in.actionFlags &= ~action::kDelayedJump;
    else
        in.actionFlags |= action::kDelayedJump;
}

void BotLibrary::move(int client, const Vec3f &dir, float speed) {
    BotInput &in = input(client);
    in.dir = dir;
    in.speed = std::clamp(speed, -float(kMaxUserMove), float(kMaxUserMove));
}

BotInput BotLibrary::getInput(int client, float thinkTime) {
    BotInput &in = input(client);
    in.thinkTime = thinkTime;
    return in;
}

void BotLibrary::resetInput(int client) {
    BotInput &in = input(client);
    const bool jumped = in.actionFlags & action::kJump;
    in.thinkTime = 0;
    in.dir = {};
    in.speed = 0;
    in.actionFlags = jumped ? action::kJumpedLastFrame : 0;
}

} // namespace reluva::idtech3::botlib
