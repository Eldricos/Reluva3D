// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A level drawn and played (see the keys in level_view.cpp).

#include "engines/unreal/render/view.h"

#include <unordered_set>

namespace reluva::unreal {

/**
 * The renderer as the Maps page draws with it, noting which textures its
 * draws used since asked last (UE1 ticks a procedural texture only as it's
 * drawn: UTexture::Get calls Update, which ticks it by the time since it
 * was last updated).
 */
class TextureUseRecorder : public gfx::Renderer3D {
public:
    /** Passes everything on to `to`, which must outlive it. */
    explicit TextureUseRecorder(gfx::Renderer3D &to) : _to(to) {}
    /** The textures drawn with since the set was last cleared (the caller clears it). */
    std::unordered_set<gfx::TextureId> &used() { return _used; }

    /** Passed on to the renderer it wraps as they are; draw() notes the call's texture first. */
    ///@{
    Error createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                        gfx::TextureId &out) override {
        return _to.createTexture(width, height, rgba, params, out);
    }
    void destroyTexture(gfx::TextureId id) override { _to.destroyTexture(id); }
    Error updateTextures(const std::vector<gfx::TextureRegion> &regions) override {
        return _to.updateTextures(regions);
    }
    Error createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                     gfx::MeshId &out) override {
        return _to.createMesh(vertices, indices, out);
    }
    Error createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t> &indices, gfx::MeshId &out) override {
        return _to.createIndexedMesh(vertices, indices, out);
    }
    Error setMeshIndices(gfx::MeshId id, const std::vector<uint32_t> &indices) override {
        return _to.setMeshIndices(id, indices);
    }
    void destroyMesh(gfx::MeshId id) override { _to.destroyMesh(id); }
    int width() const override { return _to.width(); }
    int height() const override { return _to.height(); }
    std::string deviceName() const override { return _to.deviceName(); }
    void beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, Color clear) override {
        _to.beginFrame(viewProj, eye, clear);
    }
    void draw(const gfx::DrawCall &call) override {
        if (call.texture)
            _used.insert(call.texture);
        _to.draw(call);
    }
    void setView(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye) override { _to.setView(viewProj, eye); }
    void setDynamicLights(const std::vector<gfx::DynamicLight> &lights) override { _to.setDynamicLights(lights); }
    void setVolumeLights(const std::vector<gfx::DynamicLight> &lights) override { _to.setVolumeLights(lights); }
    void setViewport(int x, int y, int w, int h) override { _to.setViewport(x, y, w, h); }
    void setDepthRange(float maxDepth) override { _to.setDepthRange(maxDepth); }
    void clearDepth() override { _to.clearDepth(); }
    void clearDepthAndStencil() override { _to.clearDepthAndStencil(); }
    bool hasStencil() const override { return _to.hasStencil(); }
    void setStencilMode(StencilMode mode) override { _to.setStencilMode(mode); }
    void setStencilLevel(uint8_t level) override { _to.setStencilLevel(level); }
    void setMirrored(bool mirrored) override { _to.setMirrored(mirrored); }
    void setGamma(float gamma) override { _to.setGamma(gamma); }
    void endFrame(const Surface *overlay) override { _to.endFrame(overlay); }
    void setStencilReference(uint8_t ref) override { _to.setStencilReference(ref); }
    int setMultisampling(int samples) override { return _to.setMultisampling(samples); }
    void captureNextFrame() override { _to.captureNextFrame(); }
    bool takeCapture(std::vector<uint8_t> &rgba) override { return _to.takeCapture(rgba); }
    ///@}

private:
    gfx::Renderer3D &_to;
    std::unordered_set<gfx::TextureId> _used;
};

/** The Maps page: a level drawn, and played with its scripts (see the top of this file). */
class LevelView : public Page {
public:
    /** Over the maps in the package set. */
    explicit LevelView(ViewContext &ctx);
    ~LevelView() override;
    const char *name() const override { return "Maps"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }
    /** Shows the map of that name (once active); false if there's none. */
    bool selectMap(const std::string &name);
    /** Whether the level is being played (its scripts running). */
    bool playing() const { return _play != nullptr; }
    /**
     * As the game's own screen rather than the data viewer's page: the info
     * hidden, and the viewer's keys (picking maps, free camera, stopping
     * play, the drawing toggles) only in developer builds.
     */
    void setGame(bool game) {
        _game = game;
        _showInfo = !game;
    }
    /** What the level being played has playing (LevelAudio::describe). */
    std::vector<std::string> sounds() const { return _audio ? _audio->describe() : std::vector<std::string>{}; }
    /** The level being played saved now, as the game's SaveGame command does. */
    Error saveGame(const std::string &name);
    /**
     * The game's ClientTravel ("CM01a_SW/CM01_A_Sisko01?options"): that
     * map loaded and played from the start the portal tags.
     * `items`: as ClientTravel's bItems (else as the level last asked).
     */
    void travel(const std::string &url, bool items = false);
    /** Playing with the game's own view, Escape is the game's (its menu). */
    bool ownsKey(uint32_t k) const override { return _play && !_freeCam && k == key::kEscape; }
    /**
     * Playing: the camera let loose, in front of the live actor of that
     * name looking at it; false if there's none.
     */
    bool watchActor(const std::string &name);
    /**
     * A changing light's share of its brightness now: its curve, and while
     * playing, for a light the scripts may change, as its live actor has
     * it (0 once that's gone). `whole` as for lightLevel.
     */
    float liveLightLevel(const LightSource &l, bool whole = true);
    /**
     * The player put by an actor (`offset` from it, facing `yaw` degrees),
     * falling (ue-play's tests); false with no player or no such actor.
     */
    bool teleportPlayer(const std::string &name, const Vec3f &offset, float yaw);
    /**
     * The fly camera put at a place in Unreal's axes, facing yaw and pitch
     * degrees (when not playing); and the live actors whose class has
     * `classPart` in its name, with where they are (ue-play's tests: views
     * to match against a recording).
     */
    ///@{
    void placeCamera(const Vec3f &at, float yaw, float pitch);
    std::vector<std::pair<std::string, Vec3f>> actorPlaces(const std::string &classPart);
    ///@}
    /**
     * The player as text: where it is, its state, physics, animation and
     * weapon ("no player" if there's none); ue-play's tests.
     */
    std::string describePlayer();
    /** The level's seconds played (0 if not playing). */
    float playTime() const;
    /**
     * A console command as the player types it (its ConsoleCommand), and
     * what it answers; ue-play's tests.
     */
    std::string consoleCommand(const std::string &command);
    /**
     * A live actor's event or function with no arguments run ("player":
     * the player's pawn); ue-play's tests (BeginEvent: the use key).
     */
    bool callFunction(const std::string &actor, const std::string &function);
    /**
     * A live actor of that name: where it is, its state, physics and
     * collision, whether it's hidden ("no such actor" if there's none).
     */
    std::string describeActor(const std::string &name);
    /** The game's camera now: where, its pitch, yaw and roll (degrees) and field of view. */
    std::string describeView();
    /** A live actor's property as text ("no such actor/property" else). */
    std::string actorProperty(const std::string &name, const std::string &property);
    /** The dialogue lines said so far, a key and its text each. */
    std::vector<std::string> linesSaid();

private:
    void load(int index);
    void unload();
    void toStart();
    void drawOverlay(uint64_t nowMs);

    struct Batch {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        gfx::TextureId texture = 0;
        gfx::TextureId lightmap = 0; // 0: none (unlit, blended, or the level has no lighting)
        uint32_t polyFlags = 0;
        ObjectRef textureRef{};               // the level's: the texture it's drawn with, for the scripts' swaps
        uint64_t zones = ~uint64_t(0);        // the zones it's seen from (zones.h): drawn when one of them is seen
        std::array<int, 2> textureSize{1, 1}; // texels, for panning its texture
        int tint = -1;                        // a translucent lit batch's: its entry in _tints
        gfx::Vec3 mirrorNormal{};             // a mirror's plane: normal . x = mirrorD (the renderer's axes)
        float mirrorD = 0;
    };
    void drawMirrors(const gfx::Mat4 &proj, const gfx::Mat4 &view, const gfx::Mat4 &viewProj, const gfx::Vec3 &eye,
                     const gfx::Vec3 &forward, const gfx::Vec3 &right, int level, const Batch *through);
    void drawWorld(const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &eye);
    std::array<float, 2> surfacePan(const Batch &b) const;
    /** Whether a batch shows: one of its zones is seen, or it's drawn whatever is seen. */
    bool seen(const Batch &b) const;
    int _mirrorLevel = 0;                  // how many reflections deep the view drawn is
    const Batch *_mirrorThrough = nullptr; // the mirror it's seen through
    // A translucent surface the level's lights light (The Fallen's logos,
    // flagged translucent and special-lit): drawn in one pass, so its
    // light is a tint, the average over points of it (each polygon's
    // corners and middle) of its lightmaps' light and the dynamic lights'
    // (a TriggerLight, not static, lights as those do), as they change.
    struct TintSample {
        int anim = -1;    // in _animatedLightmaps, or -1: a lightmap that doesn't change
        size_t texel = 0; // in it
        Vec3f light{};    // the light there, if it doesn't change
        Vec3f at{};       // where, for the lights that move and change (_dynamic)
    };
    std::vector<std::vector<TintSample>> _tints;
    Vec3f tintOf(int tint) const;
    // Where each of the level's surfaces is in its mesh (a batch and a run
    // of its indices for each of its polygons), to draw one with another
    // texture (SetSurfaceTexture).
    struct SurfRange {
        size_t batch;
        uint32_t firstIndex;
        uint32_t indexCount;
    };
    std::map<int, std::vector<SurfRange>> _surfRanges;
    // The level's batches as the scripts' swaps leave them (whole-world
    // swaps), made again when they change.
    std::vector<Batch> _swappedBatches;
    int _swappedSerial = -1;
    const std::vector<Batch> &levelBatches();
    void drawSurfaceSwaps();

    // A mover: its polygons (in its brush's own coordinates, in
    // _moverMesh) and where it is between its keyframes.
    struct MoverDraw {
        Mover mover;
        std::vector<Batch> batches;
        // moving from key to target
        int key = 0;
        int target = 0;
        float t = 0;       // 0-1 of the way
        float reach = 0;   // how far its brush reaches from its pivot
        gfx::Mat4 built{}; // where its vertices' baked light was worked out
    };
    // A mover's light: the lights reaching its brush, worked out on its
    // vertices as surfaces are lit (no lightmap of its own); `reaching`,
    // how many reach it (the renderer takes six).
    MeshLight moverLightAt(const Vec3f &at, float reach, size_t *reaching = nullptr) const;
    // A dynamic light mover's: the lights permeating its leaf (the level
    // model's leaf lists), as Render.dll lights such a mover's surfaces.
    MeshLight dynamicMoverLight(const Vec3f &at, float reach) const;
    std::vector<int32_t> _leafLights; // each leaf's first entry in Model::lights (-1 none)
    gfx::Mat4 moverTransform(const MoverDraw &m) const;
    static gfx::Mat4 moverPlacement(const Mover &m, const Vec3f &location, const Rotator &rotation);
    // The lights that move and change while playing (lit actors not among
    // the level's own lights), each frame; given to the renderer too.
    void gatherDynamicLights(const gfx::Vec3 &eye);
    std::vector<LightSource> _dynamic;
    // Each lit actor's lights as they fade (Render.dll keeps them in its
    // cache from frame to frame): by the light's actor, the moving ones
    // with the top bit set; and when it was last lit.
    struct ActorFade {
        std::map<uint64_t, float> lights;
        float time = -1;
    };
    mutable std::unordered_map<const void *, ActorFade> _actorFades;
    mutable std::unordered_map<int32_t, const LightSource *> _uniqueByRef; // _lights.unique by their actor
    std::set<int32_t> _staticLightRefs;                                    // the level's own lights (in its lightmaps)
    std::vector<ScriptLevel::LitActor> _litScratch;
    // A light's share of its brightness now (its level, if it changes).
    float levelOf(const LightSource &l) const {
        return l.animated >= 0 && size_t(l.animated) < _lightLevels.size() ? _lightLevels[size_t(l.animated)] : 1;
    }
    // over: on top of the same polygons drawn already; moverLight: lit
    // as a whole by it (a mover, where it is now), not by its vertices.
    void drawBatches(const std::vector<Batch> &batches, gfx::MeshId mesh, const gfx::Mat4 &model, bool levelLit,
                     bool over = false, const MeshLight *moverLight = nullptr);

    std::vector<std::string> _names;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr; // valid while this page is shown
    gfx::MeshId _mesh = 0;
    std::vector<Batch> _batches;
    gfx::MeshId _moverMesh = 0;
    std::vector<MoverDraw> _movers;
    std::optional<Vec3f> _sky;                    // the sky zone's viewpoint, Unreal's coordinates (the first)
    std::vector<std::optional<Vec3f>> _zoneSkies; // each zone's own (ZoneInfo.LinkToSkybox)
    bool _showSky = true;
    // The zones seen through the portals from where the view is drawn
    // from; Z draws them all.
    ZoneVisibility _zones;
    uint64_t _zonesSeen = ~uint64_t(0);
    bool _zoneCulling = true;
    void seeZonesFrom(const gfx::Vec3 &eye, const gfx::Mat4 &viewProj, bool mainView);

public:
    /**
     * The last frame's times (ms): the level's ticks (its scripts, physics,
     * animation, posing for the scripts) and the renderer's endFrame
     * (submitting, presenting); the rest of update is preparing and
     * handing over the draws. For RELUVA_BENCH.
     */
    struct FrameTimes {
        double play = 0;
        double endFrame = 0;
    };
    /** The last frame's FrameTimes. */
    const FrameTimes &frameTimes() const { return _frameTimes; }

private:
    FrameTimes _frameTimes;
    bool _game = false;
    bool viewerKeys() const;
    // What goes with the player into the next level started (travel).
    std::string _pendingTravel;
    float _pendingPlayedTime = -1;
    static float tweenProgress(const LevelPlay::Look &look, const MeshAnimSeq &anim);
    // Actors drawn as meshes, each mesh uploaded once.
    struct MeshDraw {
        size_t mesh = 0; // into _meshes / _gpuMeshes
        gfx::Mat4 placement{};
        std::vector<gfx::TextureId> textures; // by texture index
        MeshLight light;
        int anim = -1; // into the mesh's animations
        float startFraction = 0;
        std::string label; // the actor's and its mesh's names
        gfx::Vec3 at{};    // where it stands, the renderer's axes
        float drawScale = 1;
        bool skeletal = false; // its mesh is posed from bones
    };
    // Actors drawn as sprites.
    struct SpriteDraw {
        gfx::TextureId texture = 0;
        gfx::Vec3 at{}; // its centre, the renderer's axes
        // world units
        float width = 0;
        float height = 0;
        std::array<float, 3> color{1, 1, 1};
        uint8_t style = kStyleNormal;
        bool vertical = false;
        // Lying as given rather than facing the camera (a decal on a
        // wall): its right and up, in the renderer's axes; pulled towards
        // the camera so it shows on the surface it's on.
        bool oriented = false;
        gfx::Vec3 right{};
        gfx::Vec3 up{};
        bool onTop = false; // drawn over everything (a corona: hidden by walls by a trace instead)
    };
    // Lights' coronas while playing (bCorona): added to `sprites`, each
    // fading in and out as walls come between it and the camera.
    void addCoronas(std::vector<SpriteDraw> &sprites, const gfx::Vec3 &forward, float dt);
    void drawScreenFlash(const gfx::Vec3 &forward, const gfx::Vec3 &right);
    std::map<VmObject *, float> _coronaFade;
    MeshFog _fog; // this frame's distance fog (none: distance 0)
    std::vector<LevelPlay::Corona> _coronaScratch;
    float _coronaDt = 0;
    void drawParticles(VmObject *actor, const LevelPlay::Look &look, const Mesh &mesh, const MeshPose &pose,
                       std::vector<SpriteDraw> &sprites);
    void drawCopies(VmObject *actor, const LevelPlay::Look &look, const std::vector<LevelPlay::Copy> &copies,
                    std::vector<SpriteDraw> &sprites);
    void addDecals(std::vector<SpriteDraw> &sprites);
    void drawSprites(const std::vector<SpriteDraw> &sprites, const gfx::Vec3 &forward, const gfx::Vec3 &right);
    gfx::MeshId _spriteQuad = 0;
    std::vector<SpriteDraw> _sprites;
    void jumpToMeshActor(bool skeletalOnly);
    static std::string idleFor(const std::string &anim);
    int _animationSetsMs = 0; // how long reading the shared animation sets took
    int _jump = -1;           // the mesh actor last jumped to (G / H)
    std::vector<Mesh> _meshes;
    std::vector<GpuMesh> _gpuMeshes;
    std::vector<MeshDraw> _meshActors;
    bool _animate = false;
    float _animTime = 0;
    std::vector<gfx::TextureId> _textures;
    // Textures uploaded, each once: its id (0: unreadable) and size.
    // With `masked`, colour 0 see-through (drawn on a masked surface).
    std::pair<gfx::TextureId, std::array<int, 2>> textureOf(const ObjectRef &texture, bool *unreadable = nullptr,
                                                            bool masked = false);
    std::map<std::tuple<const Package *, int, bool>, std::pair<gfx::TextureId, std::array<int, 2>>> _textureCache;
    LevelLights _lights; // the level's, for lighting what moves
    // `actor`: an actor's look (its ScaleGlow, AmbientGlow, Specular), lit
    // per vertex as the original lights actors; without, a mover's.
    MeshLight meshLightAt(const Vec3f &location, const LevelPlay::Look *actor = nullptr,
                          const void *who = nullptr) const;
    void actorLights(const Vec3f &at, const LevelPlay::Look &look, MeshLight &light, const void *who = nullptr) const;
    void drawActorMesh(const GpuMesh &gpu, const gfx::Mat4 &placement, const MeshPose &pose,
                       const std::vector<gfx::TextureId> &skins, const MeshLight &light, const LevelPlay::Look &look);
    // The level's lights with a volume, in fog zones (drawVolumes).
    std::vector<const LightSource *> _volumes;
    void drawVolumes(const gfx::Vec3 &eye);
    void volumesOver(const std::vector<Batch> &batches, gfx::MeshId mesh, const gfx::Mat4 &model, gfx::Cull cull,
                     bool flagged);
    bool inFogZone(const Vec3f &at) const;

    // Playing (Enter): the level live, drawn as its scripts have it.
    // From the start the portal tags, or from a saved game.
    void startPlay(const std::string &portal = {}, const std::filesystem::path &saved = {});
    void stopPlay();
    void updatePlay(float dt);
    void drawLiveActors(const gfx::Vec3 &forward, const gfx::Vec3 &right);
    void drawPlayHud();
    void drawSubtitles(int bottom);
    void drawGameHud();
    void drawLiveActor(VmObject *actor, std::vector<SpriteDraw> &sprites, const ScriptLevel::HudActor *hud = nullptr);
    std::pair<gfx::TextureId, std::array<int, 2>> hudFliTexture(const ObjectRef &texture, int frame, bool seeThrough);
    struct HudFli {
        std::unique_ptr<AnimatedTexture> anim;
        gfx::TextureId id = 0;
        int frame = 0;
    };
    std::map<std::tuple<const Package *, int, bool>, HudFli> _hudFlis;
    std::pair<gfx::TextureId, std::array<int, 2>> hudTexture(const ObjectRef &texture, bool seeThrough);
    std::map<std::pair<const Package *, int>, std::pair<gfx::TextureId, std::array<int, 2>>> _seeThroughTextures;
    // A mesh for live actors (by the mesh object), with the animation
    // they play borrowed from the shared sets if it's not its own: an
    // index into _meshes / _gpuMeshes, -1 if it doesn't read.
    int liveMesh(const ObjectRef &mesh, const std::string &anim);
    std::unique_ptr<LevelPlay> _play;
    // A travel's level, started as it loads (its start's changes drawn
    // from it, rather than from a second start), for startPlay to take.
    struct StartOnLoad {
        bool wanted = false;
        std::string portal;
        std::filesystem::path saved;
    };
    StartOnLoad _startOnLoad;
    std::unique_ptr<LevelPlay> _started;
    std::string _startedError;
    std::unique_ptr<LevelPlay> startForPlay(const Package &map, LevelStart &changes, std::string &error);
    std::unique_ptr<LevelAudio> _audio; // its sounds, when there's a mixer
    std::string _playError;
    std::string _portal;                  // the start playing began at, for the game's ?RESTART
    std::vector<VmObject *> _moverActors; // the live actor of each of _movers
    std::map<std::pair<const Package *, int>, int> _liveMeshes;
    // Meshes posed at draw time for The Fallen's layered animations: each
    // mesh's poser (none if it has no skeleton) and its parts, and the
    // frames posed for the last frame drawn (kept until the next).
    struct Poser {
        std::unique_ptr<LayeredPoser> poser;
        std::map<std::pair<std::string, bool>, std::vector<bool>> parts;
    };
    std::map<std::pair<const Package *, int>, Poser> _posers;
    std::vector<GpuMesh> _posedMeshes;
    // The character mesh drawLiveActor drew last, for its shadow.
    struct LastMesh {
        bool set = false;
        GpuMesh gpu;
        gfx::Mat4 placement{};
        MeshPose pose;
    } _lastMesh;
    void drawShadow(VmObject *actor, const Vec3f &location);
    // Each caster's shadow: its light (in _lights.unique), how strong.
    struct ShadowState {
        int light = -1;
        float strength = 0;
        float lastSeen = 0;
        bool seen = false;
    };
    std::map<VmObject *, ShadowState> _shadows;
    bool drawLayered(VmObject *actor, const LevelPlay::Look &look, int mesh, const gfx::Mat4 &placement,
                     const std::vector<gfx::TextureId> &skins, const MeshLight &light);
    float _playClock = 0;
    float _fov = 90;
    LevelPlay::View _view;
    float _mouseDx = 0;
    float _mouseDy = 0;
    bool _freeCam = false; // playing with the camera flown (V), the player standing
    Surface _overlay;
    std::string _error;
    std::vector<std::string> _info;
    bool _textured = true;
    bool _lightmaps = true; // drawn with the lightmaps (else shaded by facing)
    float _lightScale = 1;  // the lightmaps' brightness, [ and ]
    // How the finished frame is shown: raised to _displayGamma (- and =).
    // The original's Direct3D renderer sets the display's gamma ramp to
    // 1 / (2.5 x Brightness), Brightness 0.5 (the game's default) giving
    // 0.8: see lighting.md.
    float _displayGamma = 1 / (2.5f * 0.5f);
    bool _hasLightmaps = false;
    bool _runScripts = true; // the level's scripts started, for how it looks when play begins (P)
    // Lights whose brightness changes, and the lightmaps they reach,
    // redrawn as they do.
    void updateLightmaps(float dt);
    bool strobeSeen() const;
    std::vector<gfx::TextureId> _lightmapPages;
    std::vector<LightSource> _changingLights;
    std::vector<float> _lightLevels;            // each one's brightness now, as a whole (meshes are lit by it)
    std::vector<float> _mapLevels;              // as the lightmaps were last drawn with it (less a pattern)
    std::vector<PatternNow> _patternNow;        // each one's pattern this frame
    std::vector<bool> _staleLightmaps;          // changed while out of sight: drawn once seen
    std::map<int, MeshTopology> _posedTopology; // by _meshes index: those posed each frame
    std::vector<AnimatedLightmap> _animatedLightmaps;
    float _lightTime = 0;
    LightClock _lightClock; // the changing lights' (lighting.h)
    float _strobeTime = 0;  // the level's time when the strobes were last worked out
    // Fire, water, wet, ice and FLC textures, playing.
    void updateAnimatedTextures(float dt);
    std::vector<std::pair<gfx::TextureId, std::unique_ptr<AnimatedTexture>>> _animatedTextures;
    std::vector<float> _animatedSince;                     // each one's time since it was last ticked
    std::vector<std::pair<ObjectRef, bool>> _animatedRefs; // each one's texture, and whether colour 0 is see-through
    std::unique_ptr<TextureUseRecorder> _recorder;         // _renderer while shown: what's drawn
    // The overlay's rows drawn last frame, cleared before the next: [0,
    // top) and [bottom, the frame's height).
    // the first frame clears it all
    int _overlayTop = 0;
    int _overlayBottom = 0;
    // The info box's lines as drawn, its width and its error line: kept
    // between frames, only changed lines drawn again (drawOverlay).
    std::vector<std::string> _overlayLines;
    int _overlayBoxW = 0;
    size_t _overlayErrorLine = size_t(-1);

    // Fly camera, in the renderer's world: Unreal's axes with Y negated
    // (so right-handed), Z up; yaw 0 looks along +X.
    std::array<float, 3> _start{};
    std::array<float, 3> _pos{};
    float _startYaw = 0;
    float _yaw = 0;
    float _pitch = 0;
    bool _held[12] = {}; // by level_view.cpp's Held
    bool _dragging = false;
    bool _showInfo = true;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
    uint64_t _fpsStart = 0;
    int _frames = 0;
    float _fps = 0;
};

} // namespace reluva::unreal
