// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level being played: its scripts run live (vm_level.h) with a player
// logged in, ticked in fixed steps as real time passes, steered by the
// controls, seen from where the game's own PlayerCalcView puts the camera.
// The Maps page draws it; reluva-tool ue-play drives it without a window.
// See docs/engines/unreal_vm.md.

#include "engines/unreal/formats/actors.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/vm/vm.h"
#include "engines/unreal/vm/vm_level.h"

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

class LevelPlay {
public:
    static constexpr float kTick = 1.0f / 30; // seconds a tick moves the level on

    LevelPlay(const PackageSet &packages, const Package &map);
    // Starts the level and logs a player in (as the .ini's [URL] says), at
    // the start tagged `portal` if one is given; or, given a saved game of
    // this map, carries on from it. `saveFolder`: where its games are
    // saved and listed.
    Error start(const std::string &portal = {}, const std::filesystem::path &saved = {},
                const std::filesystem::path &saveFolder = {});

    // Moves the level on by `seconds` of real time, in whole ticks: at
    // most `maxTicks` at once, so when the scripts can't keep up the game
    // runs slower than real time rather than falling ever further behind.
    void advance(float seconds, int maxTicks = 4);
    ScriptLevel::Controls &controls() { return _level->controls; }
    // A key bound to an exec function pressed: the player's Jump, Fire...
    void press(const char *exec);
    // A key pressed or let go ("2", "F1"), as the player's .ini binds it
    // ([Engine.Input] in User.ini, else DefUser.ini: "2=Rifle"): the exec
    // functions it names, the buttons it holds. False if it's bound to none.
    bool pressKey(const std::string &key, bool down = true);
    // A key going down or up as the engine first hands it on: to the
    // console's KeyEvent (`inputKey` an EInputKey: 13 Enter, 27 Escape, 37
    // to 40 the arrows, 0x70 F1...). The Fallen's console gives the arrows
    // and Enter to the HUD while its menus show. True if it took the key;
    // otherwise it's the bindings' (pressKey).
    bool consoleKey(int inputKey, bool down);
    // The game paused (Level.Pauser set: the menus).
    bool paused() const;
    // The game's UWindow windows open (the options): the console's
    // Root.bUWindowActive; the mouse is theirs then.
    bool windowsOpen() const;
    // Where the mouse is (in the screen's pixels) for those windows: the
    // console's MouseX and MouseY.
    void consoleMouse(float x, float y);
    // The game's menu showing (the player's bShowMenu): the keys are all
    // the game's then.
    bool menuShowing() const;
    // Where the game asked to travel ("CM01a_SW/CM01_A_Sisko01": a map,
    // then the start to begin at), once: empty after.
    std::string takeTravel();
    // The game asked to quit (its menu's Quit).
    bool quitAsked() const;
    // The HUD told of something (HudEvent: 1010, no game to quick load).
    void hudEvent(int code);

    // Sounds, for the page to play (level_audio.cpp): what the scripts
    // asked for since last time, where an actor is now (false once it's
    // gone), the actors' ambient sounds, and the music.
    std::vector<ScriptLevel::SoundRequest> takeSounds();
    bool locationOf(VmObject *actor, Vec3f &out);
    // Its Velocity (false once it's gone).
    bool velocityOf(VmObject *actor, Vec3f &out);
    // The listener's velocity, as the audio driver takes it: the player's
    // ViewTarget's, else the player's own.
    Vec3f viewerVelocity();
    // The reverb where the listener is, as the driver sets it from the
    // zone: in water room 22 at 0.5, else the zone's ZoneRoomType at its
    // ZoneRoomFactor (room 0: none).
    struct Room {
        int type = 0;
        float level = 0;
    };
    Room roomAt(const Vec3f &at);
    // Whether the level's walls (its BSP, not movers) stand between two
    // points, short of the last 24 units to `to`.
    bool wallBetween(const Vec3f &from, const Vec3f &to);
    struct Ambient {
        VmObject *actor = nullptr;
        ObjectRef sound;
        bool streamed = false; // one of the MP3s (at the music volume)
        bool music = false;    // named MUSIC: heard alike everywhere in range, faded in and out
        Vec3f at{};
        float radius = 0;
        float volume = 0;
        float pitch = 1;
    };
    void ambientSounds(std::vector<Ambient> &out);
    VmObject *currentMusic() { return _level->currentMusic(); }
    bool streamed(VmObject *sound) { return _level->streamedSound(sound); }
    std::vector<std::pair<VmObject *, int>> takeSoundStops() { return std::exchange(_level->soundStops(), {}); }
    const ScriptLevel::Music &music() { return _level->music(); }
    void musicEnded(VmObject *sound) { _level->musicEnded(sound); }
    // The audio settings (the .ini's AudioDevice: MasterVolume,
    // SFXVolume, MusicVolume, SpeechVolume 0-255; UseStereo,
    // ReverseStereo) as the options leave them; the master volume as
    // SetMasterVolume has it for now.
    std::string audioSetting(const std::string &key);
    float masterVolume();
    bool takeKillSounds();

    // Where the camera is, as the game puts it each frame
    // (PlayerCalcView): behind the player, or a cut scene's camera.
    struct View {
        Vec3f location{};
        Rotator rotation;
        float fov = 90;            // degrees across
        bool letterbox = false;    // the HUD's black bars (a cut scene)
        VmObject *actor = nullptr; // whose eyes, if anyone's
    };
    View view();

    // How an actor looks, as its properties say now.
    struct Look {
        Vec3f location{};
        Vec3f prePivot{};
        Rotator rotation;
        float drawScale = 1;
        int drawType = 0; // 1 sprite, 2 mesh, 3 brush...
        bool hidden = false;
        ObjectRef mesh;
        ObjectRef skin;
        ObjectRef texture;
        std::array<ObjectRef, 8> multiSkins{};
        std::string animSequence;
        float animFrame = 0;
        float animRate = 0;
        bool animLoop = false;
        // Tweening into it (animFrame below 0): what it's tweening from.
        std::string tweenFrom;
        float tweenFromFrame = 0;
        bool tweenFromLoop = false;
        uint8_t style = 1;
        float scaleGlow = 1;
        std::array<float, 3> colorFilter{1, 1, 1}; // ColorFilter: a sprite's tint
        uint8_t ambientGlow = 0; // AmbientGlow: added to the zone's ambient light on it (255: pulsing)
        // The Fallen's MultiTexture: drawn twice, its own skins in one style
        // and glow, then MultiTextureTwo over them in another (bMultiTextureOn).
        struct MultiTexture {
            bool on = false;
            bool envMap = false;
            ObjectRef two;
            uint8_t oneStyle = 1;
            uint8_t twoStyle = 1;
            uint8_t twoFatness = 128;
            float oneGlow = 1;
            float twoGlow = 1;
            bool oneUnlit = false;
            bool twoUnlit = false;
            std::array<float, 3> colorFilter{1, 1, 1};
        } multi;
        float specular = 1;         // Specular: its highlights' strength
        bool unlit = false;         // bUnlit: as bright as its ScaleGlow, not the light where it is
        bool renderInitial = false; // bMultiMeshRenderInitial: drawn itself as well as its copies
        // The Fallen's layered animations on parts of it (C3DPlayAnim).
        struct Layer {
            std::string anim;
            std::string bone;
            bool rest = false; // the part is all but the bone and those under it
            float frame = 0;
            float weight = 0;
            std::array<bool, 3> clear{}; // its first bone keeps the actor's own yaw, pitch, roll
            bool loop = false;
            std::string group; // its animation group's name (FullBody, LowerTorso, UpperTorso)
            // Fading: the pose showing when the fade began, and whether out.
            std::shared_ptr<const std::vector<std::array<float, 12>>> from{};
            bool fadingOut = false;
        };
        std::vector<Layer> layers;
        // Its face's channels away from the neutral face (C3DSetSkinAnim:
        // Blink, EyeBrows, Phonemes), seconds into each.
        struct Face {
            std::string anim;
            float seconds = 0;
            float weight = 0;
            // a tween's cross-fade
            float fromSeconds = 0;
            float fromWeight = 0;
            float mix = 1;
        };
        std::vector<Face> faces;
        // What its scripts do to its bones on top (a back bent, a hand on a gun).
        LayeredPoser::Adjust adjust;
    };
    bool look(VmObject *actor, Look &out);
    // Whether an actor may be seen: not hidden, and in one of `zones` (a
    // bit for each of the level's zones seen) if its zone is known. Cheap,
    // to pass over the rest before look().
    bool mayBeSeen(VmObject *actor, uint64_t zones);
    // The Fallen's distance fog where `at` is: its zone's DistanceFogging
    // (units to full fog; 0 none) and DistanceFogColor (0-1).
    float distanceFog(const Vec3f &at, std::array<float, 3> &color);
    // The lights drawing a corona (bCorona): where, its texture (the
    // actor's Skin, else its Texture), DrawScale, and its zone (-1 not
    // known). Hidden or not: a light actor is hidden, its corona isn't.
    struct Corona {
        VmObject *actor = nullptr;
        Vec3f location{};
        ObjectRef texture; // its Skin
        float drawScale = 1;
        int zone = -1;
        uint8_t hue = 0;
        uint8_t saturation = 255;
        float radius = 0;     // its light's, world units (25 x (LightRadius + 1))
        bool isStatic = true; // bStatic: one of the level's own lights
        int32_t ref = 0;      // its export (object reference) in the level, 0 if spawned
    };
    void coronas(std::vector<Corona> &out);
    // Its copies (The Fallen's MultiMesh: particles, sparks, a beam's
    // sections), each drawn as it is, in its place, turned and scaled
    // (AxisScale, along its own axes), glowing and in its style; with its
    // own texture, for sprites. False if it has no list of them (it's
    // drawn as itself); with one, it's drawn as its copies only, even none
    // yet, unless it asks to be drawn as well (renderInitial).
    struct Copy {
        Vec3f location{};
        Vec3f axisScale{1, 1, 1};
        Rotator rotation;
        float drawScale = 1;
        float scaleGlow = 1;
        std::array<float, 3> colorFilter{1, 1, 1};
        ObjectRef texture;
        uint8_t style = 1;
    };
    bool copies(VmObject *actor, std::vector<Copy> &out);
    // The decals stuck to the world (Decal.AttachDecal), and their size:
    // their texture's, by their scale.
    std::vector<ScriptLevel::Decal> decals() const;
    // It was drawn this frame (its LastRenderTime: The Fallen's heads and
    // faces only move on pawns drawn in the last few seconds).
    void drawn(VmObject *actor);
    // The zones drawn this frame (bit n, zone n), the view in `cameraZone`.
    void zonesDrawn(uint64_t zones, int cameraZone) { _level->zonesRendered(zones, cameraZone); }
    // It's about to be drawn: C3DPreRender, as The Fallen's engine sends
    // (the player's weapon works out its tip and the crosshair).
    void preRender(VmObject *actor);

    Vm &vm() { return *_vm; }
    ScriptLevel &level() { return *_level; }
    VmObject *player() const { return _player; }
    // The live actor an object reference in the level's package names
    // (a placed light...); null if none or destroyed.
    VmObject *actorAt(int32_t ref);
    const Package &map() const { return _map; }
    const std::string &loginError() const { return _loginError; }
    float time() const { return _time; }     // seconds played
    float behind() const { return _behind; } // real seconds not yet played (it's slower than real time)
    int health();
    // The level's title (from its .int, as CTVE_Document sets it) and its
    // objectives that are active now (LevelInfo's Objective_State 0).
    std::string title();
    std::vector<std::string> activeObjectives();
    std::string playerState();

private:
    ObjectRef refOf(const Value *v) const;
    std::string inputSetting(const std::string &key) const;
    std::map<std::string, std::string> _aliases; // the input aliases, by name (lower case)
    // Where each property look() reads is in a class's values, found
    // once per class (looking properties up by name for every actor every
    // frame took as long as the scripts' ticks).
    enum Field {
        kLocation,
        kPrePivot,
        kRotation,
        kDrawScale,
        kDrawType,
        kHidden,
        kMeshField,
        kSkin,
        kTexture,
        kMultiSkins,
        kAnimSequence,
        kAnimFrame,
        kAnimRate,
        kAnimLoop,
        kStyle,
        kScaleGlow,
        kLastRenderTime,
        kUnlit,
        kRenderInitial,
        kRegion,
        kAmbientSoundField,
        kSoundRadius,
        kSoundVolume,
        kSoundPitch,
        kCoronaField,
        kAmbientGlow,
        kSpecularField,
        kMultiTextureOn,
        kFieldCount
    };
    const std::array<int, kFieldCount> &slotsOf(const VmClass *cls);
    int _regionZoneField = -1; // ZoneNumber's place in PointRegion
    std::unordered_map<const VmClass *, std::array<int, kFieldCount>> _slots;
    struct AmbientKind {
        bool known = false;
        bool keypoint = false;
    };
    std::unordered_map<const VmClass *, AmbientKind> _ambientKinds;
    std::array<NameId, kFieldCount> _names{};

    const PackageSet &_packages;
    const Package &_map;
    std::unique_ptr<Vm> _vm;
    std::unique_ptr<ScriptLevel> _level;
    struct WallKey {
        std::array<uint32_t, 6> bits;
        bool operator==(const WallKey &o) const { return bits == o.bits; }
    };
    struct WallHash {
        size_t operator()(const WallKey &k) const {
            size_t h = 1469598103934665603ull;
            for (uint32_t b : k.bits)
                h = (h ^ b) * 1099511628211ull;
            return h;
        }
    };
    std::unordered_map<WallKey, bool, WallHash> _walls; // wallBetween's answers
    VmObject *_player = nullptr;
    std::string _loginError;
    float _time = 0;
    float _owed = 0;
    float _behind = 0;
};

} // namespace reluva::unreal
