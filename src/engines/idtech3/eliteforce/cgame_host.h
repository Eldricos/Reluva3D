// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/vm_services.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's client game (vm/cgame.qvm: the player's view, HUD and
// effects) run with the engine side of its system calls, in Elite Force's
// numbering (docs/formats/idtech3/eliteforce-syscalls.md). For now it
// starts: it's given the server's configstrings as its game state, and
// what it loads and registers is recorded. Snapshots and drawing come
// next; calls not implemented are counted in unhandled().
class CgameHost {
public:
    // vmMain's commands (Quake III's order; Init seen on Elite Force's
    // cgame.qvm).
    enum Command : int32_t {
        Init = 0, // (serverMessageNum, serverCommandSequence, clientNum)
        Shutdown = 1,
        ConsoleCommand = 2,
        DrawActiveFrame = 3, // (serverTime, stereoView, demoPlayback)
    };

    CgameHost(const SearchSet& files, CvarStore& cvars);

    // Reads vm/cgame.qvm.
    Error load();
    // The configstrings the server has set, as the client receives them
    // (its game state).
    void setConfigstrings(std::map<int32_t, std::string> configstrings) { configstrings_ = std::move(configstrings); }
    // CG_INIT for the player `clientNum`.
    Error init(int32_t clientNum, int32_t serverMessageNum = 0, int32_t serverCommandSequence = 0);
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);
    // A new snapshot from the server, numbered from 1: what
    // GetCurrentSnapshotNumber reports and GetSnapshot hands over (the
    // last 32 are kept).
    void newSnapshot(GameHost::Snapshot snapshot);
    // The commands the server has sent this player, in order (what
    // GetServerCommand hands over, by sequence number from 1).
    void setServerCommands(std::vector<std::string> commands) { serverCommands_ = std::move(commands); }
    // Its 2D drawing since clearPictures(), as the menus' (the loading
    // screen, the HUD): a part of a shader stretched over a rectangle of
    // the screen, tinted.
    struct Picture {
        float x, y, w, h;
        float s1, t1, s2, t2;
        int32_t shader; // in shaders()
        std::array<float, 4> color;
    };
    const std::vector<Picture>& pictures() const { return pictures_; }
    void clearPictures() { pictures_.clear(); }
    // The weapon the client game last selected (SetUserCmdValue), which
    // the player's next commands carry.
    int32_t weaponSelect() const { return weaponSelect_; }
    // A command the player gave (numbered from 1), for the client game's
    // prediction: GetCurrentCmdNumber, GetUserCmd (the last 64 kept).
    void addUserCommand(const GameHost::UserCommand& cmd) {
        userCommands_[++userCommandNumber_] = cmd;
        while (userCommands_.size() > 64)
            userCommands_.erase(userCommands_.begin());
    }
    // A frame's 3D scene, as the client game hands it over: the entities
    // it adds (each a refEntity_t, kept as the 256 bytes from its address
    // until the layout is known, with the size of the memset that cleared
    // it, if one did) and the view it renders them from (refdef_t, the
    // 112 bytes the client game clears at start-up: Quake III's without
    // its text).
    struct SceneEntity {
        std::vector<uint8_t> bytes;
        int32_t clearedSize = 0;
    };
    // Each RenderScene draws the entities added since the last ClearScene
    // or RenderScene from its own view: the player's, and others such as
    // the HUD's heads (which don't draw the world).
    struct View {
        std::vector<uint8_t> refdef;
        size_t firstEntity = 0, entityCount = 0; // in entities
        size_t firstPoly = 0, polyCount = 0;     // in polys
        size_t firstLight = 0, lightCount = 0;   // in lights
    };
    struct Scene {
        std::vector<SceneEntity> entities; // every one added this frame
        // Polygons (AddPolyToScene: shader, vertex count, the vertices'
        // bytes, 24 each as Quake III's polyVert_t, until the layout's
        // confirmed) and lights (AddLightToScene: origin, intensity, which
        // is the radius it reaches, and colour).
        struct Poly {
            int32_t shader = 0, vertexCount = 0;
            std::vector<uint8_t> vertices;
        };
        std::vector<Poly> polys;
        struct Light {
            std::array<float, 3> origin{};
            float radius = 0;
            std::array<float, 3> color{};
        };
        std::vector<Light> lights;
        std::vector<uint8_t> view; // set by RenderScene: the last one
        int renders = 0;
        std::vector<View> views; // every RenderScene, in order
        size_t pending = 0;      // entities from here wait for the next RenderScene
        size_t pendingPolys = 0; // the same for polygons
        size_t pendingLights = 0; // and lights

    };
    const Scene& scene() const { return scene_; }
    void clearScene() { scene_ = Scene{}; }
    // Where the client game asked for each snapshot (GetSnapshot's
    // number and buffer), in order.
    const std::vector<std::pair<int32_t, int32_t>>& snapshotRequests() const { return snapshotRequests_; }
    // A command typed or bound to a key that the client game may handle
    // itself (CG_CONSOLE_COMMAND: "weapnext", "weapon 3"...): whether it did.
    Error consoleCommand(const std::string& text, bool& handled);

    Qvm& vm() { return vm_; }
    // What it registered, by handle.
    const HandleTable& models() const { return models_; }
    const HandleTable& skins() const { return skins_; }
    const HandleTable& shaders() const { return shaders_; }
    const HandleTable& sounds() const { return sounds_; }
    // The console commands it added (AddCommand).
    const std::vector<std::string>& commands() const { return commands_; }
    // The map it asked for, for collision (CM_LoadMap) and drawing
    // (R_LoadWorldMap), and the music (S_StartBackgroundTrack: intro, loop).
    const std::string& collisionMap() const { return collisionMap_; }
    const std::string& worldMap() const { return worldMap_; }
    const std::pair<std::string, std::string>& music() const { return music_; }
    // The loaded collision map, if CM_LoadMap found it.
    std::shared_ptr<const Bsp> map() const { return map_; }

    std::function<void(const std::string&)> print;
    const std::string& log() const { return log_; }
    const std::map<int, int>& unhandled() const { return unhandled_; }
    // The arguments of the first call to each call not implemented.
    const std::map<int, std::array<int32_t, 6>>& firstUnhandled() const { return firstUnhandled_; }
    std::function<void(int number, const int32_t* args)> trace;

private:
    int32_t systemCall(Qvm& vm, int number, const int32_t* args);
    void writeGameState(int32_t address);
    void writeGlconfig(int32_t address);

    const SearchSet& files_;
    CvarStore& cvars_;
    Qvm vm_;
    VmFiles vmFiles_;
    std::map<int32_t, std::string> configstrings_;
    HandleTable models_, skins_, shaders_, sounds_;
    std::vector<std::string> commands_;
    std::string collisionMap_, worldMap_;
    std::pair<std::string, std::string> music_;
    std::shared_ptr<const Bsp> map_;
    std::string log_;
    std::map<int, int> unhandled_;
    std::map<int, std::array<int32_t, 6>> firstUnhandled_;
    int32_t time_ = 0;
    int32_t snapshotNumber_ = 0, snapshotTime_ = 0;
    std::map<int32_t, GameHost::Snapshot> snapshots_; // by number
    std::vector<std::string> serverCommands_;
    std::vector<std::string> args_; // the command being handled, split as Quake III's Cmd_TokenizeString does
    std::vector<Picture> pictures_;
    std::array<float, 4> color_{1, 1, 1, 1};
    std::vector<std::pair<int32_t, int32_t>> snapshotRequests_;
    std::map<int32_t, GameHost::UserCommand> userCommands_;
    int32_t userCommandNumber_ = 0;
    Scene scene_;
    std::map<int32_t, int32_t> cleared_; // memset(address, 0, size): the last size by address
    std::map<int32_t, std::shared_ptr<const Md3Model>> md3s_; // by model handle, for tags
    std::array<float, 3> tempBoxMins_{}, tempBoxMaxs_{};
    std::unique_ptr<CollisionWorld> world_;
    int32_t weaponSelect_ = 0;
};

} // namespace reluva::idtech3::eliteforce
