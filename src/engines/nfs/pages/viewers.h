// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "image/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/nfs/formats/fce.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/formats/hrz.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/render/particles.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/ui/page.h"
#include "engines/nfs/ui/page_draw.h"

#include <array>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The Need for Speed engine's data viewer, in reluva-viewer: pages showing
// what the game's files hold, and the game's own (PlayPage). The 2D ones
// draw into the engine's 640x480 RGBA32 screen when asked; the 3D ones
// take the GPU renderer while shown and draw a frame each update. Tab
// switches between them (runPages).

namespace reluva::nfs {

namespace page {

// A viewer page's title line and, at the bottom, its keys.
inline void frame(Surface &s, const std::string &title, const std::string &keys) {
    clear(s);
    text(s, kMargin, 8, title, kTitleColor);
    text(s, kMargin, s.h - font::kLineHeight - 4, keys + "   Tab: next page   Esc: launcher", kLabelColor);
}

} // namespace page

// How the Track and Cars pages sample the game's textures: bilinear, no
// mipmaps. The game never sends THRASH's filter, LOD bias or mipmap
// states (7, 8, 0xb), but voodoo2a.dll sends itself 7 1 and 0xb 1 after
// setvideomode, so on a Voodoo2 the game's textures are bilinear-filtered
// (thrash.md, Texture filtering, from running the DLL on the Glide
// stand-in); its textures have one level each, so no mipmaps.
// reluva.ini's texturefilter=point holds them point-sampled (makePages).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): a reluva.ini setting every viewer page reads
inline bool g_pagesPointSampled = false;
inline gfx::TextureParams glideTextureParams(bool clamp = false) {
    gfx::TextureParams params;
    params.clamp = clamp;
    params.mipmaps = false;
    params.nearest = g_pagesPointSampled;
    return params;
}

// What the engine found: the folders, the CD's files, tracks and cars.
class InfoPage : public Page {
public:
    InfoPage(const char *gameName, GameFolders folders);
    const char *name() const override { return "Info"; }
    void handleKey(uint32_t, uint16_t) override {}
    void draw(Surface &screen) override;

private:
    std::string _gameName;
    GameFolders _folders;
    std::vector<std::string> _tracks;
    std::vector<std::string> _cars;
    std::vector<std::string> _music;
    std::vector<std::string> _movies;
};

// Every picture in every .fsh/.qfs, loose or in a .viv.
//   Up/Down: bank   PageUp/PageDown: 10 banks   Left/Right: picture
//   Home/End: first/last picture   B: background   A: alpha on/off
class TexturePage : public Page {
public:
    explicit TexturePage(const GameFiles &files);
    const char *name() const override { return "Textures"; }
    bool empty() const override { return _banks.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int bank);
    void decodeCurrent();

    const GameFiles &_files;
    std::vector<FileRef> _banks;
    int _bank = -1;
    int _shape = 0;
    ShapeBank _loaded;
    std::string _error;
    image::Image _image;
    std::string _imageError;
    int _background = 0; // checkerboard, black, white
    bool _alpha = true;
};

// Each car's cockpit (dash.qfs): the dash, the steering wheel, the gear
// indicator and the speedometer and tachometer needles, drawn as the game
// draws them.
//   Left/Right: car   Space: sweep the needles and wheel   Up/Down: move
//   the needles (paused)   A/D: steer   G: next gear
class CockpitPage : public Page {
public:
    explicit CockpitPage(const GameFiles &files);
    const char *name() const override { return "Cockpits"; }
    bool empty() const override { return _dashes.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int index);

    const GameFiles &_files;
    std::vector<FileRef> _dashes;
    int _index = -1;
    std::string _error;
    image::Image _dash;
    image::Image _wheel;
    // where the wheel's pivot goes
    int _wheelX = 0;
    int _wheelY = 0;
    // in the wheel's picture
    int _wheelPivotX = 0;
    int _wheelPivotY = 0;
    std::array<image::Image, std::size(kGearPictures)> _gears;
    // the gear indicator's top left
    int _gearX = 0;
    int _gearY = 0;
    bool _haveGauges = false;
    Cockpit _cockpit;
    float _position = 0; // 0-1 of each gauge's range
    int _steering = 0;   // -128 to 127, as the car's
    int _gear = 1;       // neutral
    bool _sweeping = true;
    uint64_t _lastMs = 0;
};

// The front-end string tables, in the game's own font.
//   Left/Right: language   Up/Down: line   PageUp/PageDown: page
//   Home/End: first/last   F: the game's font or Reluva's
class StringPage : public Page {
public:
    explicit StringPage(const GameFiles &files);
    const char *name() const override { return "Strings"; }
    bool empty() const override { return _tables.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int table);
    int linesPerPage() const;

    const GameFiles &_files;
    std::vector<FileRef> _tables;
    int _table = -1;
    int _top = 0;
    TextTable _loaded;
    std::string _error;
    std::optional<Font> _font; // arial10b.ffn, if it reads
    bool _gameFont = true;
};

// Every font, with all its characters.
//   Left/Right: font   +/-: size
class FontPage : public Page {
public:
    explicit FontPage(const GameFiles &files);
    const char *name() const override { return "Fonts"; }
    bool empty() const override { return _fonts.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int font);

    const GameFiles &_files;
    std::vector<FileRef> _fonts;
    int _font = -1;
    Font _loaded;
    std::string _error;
    int _scale = 1;
};

// Every model (cars, traffic, police, the front end's track models) in 3D,
// with its skin, drawn by its triangles' flags (two-sided, unlit) and with
// the game's reflection of a track's sky.
//   Left/Right: model   drag or A/D/W/S: turn   wheel or +/-: zoom
//   L: detail level   T: textures   C: back faces   E: reflection's sky
//   Space: spin   R: reset
class CarPage : public Page {
public:
    explicit CarPage(const GameFiles &files);
    ~CarPage() override;
    const char *name() const override { return "Cars"; }
    bool empty() const override { return _models.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return _renderer != nullptr; }
    void handleEvent(const Event &ev) override;
    void draw(Surface &screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    void load(int index);
    void release();
    void resetView();

    const GameFiles &_files;
    std::vector<FileRef> _models;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::string _unavailable; // why there's no 3D
    Surface _overlay;
    Model _model;
    std::string _error;
    std::string _skinName;
    gfx::MeshId _mesh = 0;
    gfx::TextureId _skin = 0;
    // A Render/pc model's pages (its .art), and each part's runs of
    // triangles drawn alike: by page and flags, a range of the mesh's
    // indices each.
    std::vector<gfx::TextureId> _pages;
    struct Run {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t page = 0;
        bool twoSided = false;
        bool unlit = false;
        bool strong = false;
    };
    std::vector<std::vector<Run>> _runs;
    // The reflection (0x49db70, 0x43400f): the triangles without
    // kNoReflection again, added over the model with source alpha, their
    // sphere-map coordinates from the normals as the camera sees them,
    // remade each frame. Its runs index envMesh_.
    gfx::MeshId _envMesh = 0;
    std::vector<gfx::Vertex> _envVertices;
    std::vector<std::array<float, 3>> _envNormals; // in the world
    std::vector<std::vector<Run>> _envRuns;
    std::vector<std::pair<std::string, gfx::TextureId>> _skies; // a track's sphere maps: name, texture
    int _sky = 0;                                               // which, or skies_.size() for no reflection
    void loadSkies();
    std::vector<int> _partLevels; // 0 high, 1 medium, 2 small, 3 tiny
    int _level = 0;               // 0-3, or 4 for all
    bool _textures = true;
    bool _cullBack = true;
    bool _spin = false;
    bool _dragging = false;
    float _yaw = 0;
    float _pitch = 0;
    float _distance = 5;
    float _radius = 2;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
};

// The tracks in 3D, with their textures, objects, lane markings and sky.
//   PageUp/PageDown: track   W/S/A/D: move   arrows or drag: look
//   Space/C: up/down   Shift: faster   N/B: next/previous block
//   1/2/3/4: road detail (4: by distance, as the game)   K: all blocks, not
//   only those the game draws from here   H: day, night, weather   T: textures
//   V: vertex light   L: lanes   O: objects   G: the virtual road's walls and edges
//   X: next texture set (two, then their 8-bit versions)   F: back faces   R: start
//   I: the game's effects intensity (100, 75, 50, 25%), which only the
//   Direct3D look (Shift+Y) applies: lights, glows, added particles and
//   clouds, rain and snow dimmed
//   P: a wall hit at the nearest virtual-road wall ahead, its particles as
//   the game makes them for that side's code (Shift+P: a fast hit, 40;
//   else 18); held, it repeats
//   E: the camera's car's headlights (off, low, high; Shift+E the z-buffer
//   and projection, off by default as in nfs host; Ctrl+E the game's light
//   test)   J: computer cars (Shift+J stops them)
//   Z: the cars' reflections (the game's option at 0x6fbc24)
//   Q: the Special Effects option: High, Low, Off (no track lights, rain
//   flares or fog beams)   Y: fog   M: lights and sounds   U: mirrored
class TrackPage : public Page {
public:
    TrackPage(const GameFiles &files, const GameFolders &folders, audio::Mixer *mixer = nullptr);
    ~TrackPage() override;
    const char *name() const override { return "Tracks"; }
    bool empty() const override { return _tracks.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return _renderer != nullptr; }
    void handleEvent(const Event &ev) override;
    void draw(Surface &screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    // Draw calls of one kind of polygon, a range of the mesh's indices
    // each, one per picture.
    struct Batch {
        gfx::TextureId texture = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        bool additive = false;
        bool twoSided = false;   // polygon flag 0x10: never culled
        bool seeThrough = false; // texture flag 0x02: added in half grey at half alpha
        // Where its polygons' first corners lie (within radius of centre,
        // in the world), for the game's cut-off of far polygons; and
        // whether this frame draws it from the cut-off's own indices.
        std::array<float, 3> centre{};
        float radius = 0;
        bool cut = false;
    };
    // kObjectsDay: object layer 3 and each block's fourth group of placed
    // objects, which the game draws only by day without weather (0x41cdc0).
    enum Kind { kRoadLow, kRoadMedium, kRoadHigh, kObjects, kObjectsDay, kLanes, kWalls, kKinds };
    // An object moving along its keys; its vertices are stored as in the
    // file (unmirrored, relative to the key) and placed each frame.
    struct Moving {
        int block = -1; // whose group it is in (-1: drawn from everywhere)
        std::vector<Track::ExtraObject::Key> keys;
        int keyDelay = 0;
        std::vector<Batch> batches;
        // in track_.extraObjects
        size_t group = 0;
        size_t object = 0;
    };
    // Animated polygons of one kind with the same delay and frame count:
    // a copy of each for every frame, which drawing picks by the tick.
    struct Animation {
        int delay = 1;
        std::vector<std::vector<Batch>> frames;
        const std::vector<Batch> &at(uint64_t tick) const { return frames[(tick / delay) % frames.size()]; }
    };

    void load(int index);
    void loadSky();
    void release();
    void releaseSky();
    void toBlock(int block);
    gfx::TextureId picture(const std::string &sheet, int number, bool lane, bool *additive);
    // A picture of the track's sky.fsh by name, and its text.
    gfx::TextureId skyPicture(const std::string &name, std::string *text);
    std::string textureSheet() const; // the track's texture sheet, of the set shown
    gfx::TextureId horizonStrip();    // the horizon picture, from it

    const GameFiles &_files;
    GameFolders _folders;
    std::vector<std::string> _tracks; // trkNNN folders
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::string _unavailable;
    Surface _overlay;
    Track _track;
    std::string _error;
    gfx::MeshId _mesh = 0;
    // The mesh's vertices and indices, kept for the cut-off and the
    // headlights; and the cut-off's indices over the same vertices, made
    // anew each frame: the polygons of batches that straddle the reach
    // (kPolygonReach) whose first corner is within it.
    std::vector<gfx::Vertex> _meshVertices;
    std::vector<uint32_t> _meshIndices;
    gfx::MeshId _cutMesh = 0;
    std::vector<uint32_t> _cutIndices;
    void cutFar(std::vector<Batch> &list, const gfx::Vec3 &eye, const gfx::Vec3 &forward,
                const gfx::Mat4 *model = nullptr);
    std::vector<gfx::TextureId> _textures;
    std::map<std::string, ShapeBank> _banks;                          // loaded sheets, by file name
    std::map<std::string, std::pair<gfx::TextureId, bool>> _pictures; // "sheet/number" -> texture, additive
    // By block and kind: block × kKinds + kind; one more block's worth
    // last for what isn't drawn by block (the walls, the last group of
    // placed objects).
    std::vector<std::vector<Batch>> _batches;
    std::vector<Moving> _moving;
    std::vector<std::vector<Animation>> _animations;
    // The blocks the game would draw from block `here` (0x41cdc0): those
    // in its neighbour list whose centre is within the view distance on
    // the ground, each with the road's detail (0-2) for its distance.
    std::vector<std::pair<int, int>> blocksInView(int here) const;
    TrackCollision _collision;
    std::string _collisionError;
    // The sky, for the variant shown (0 day, 1 night, 2 weather, 3 both).
    Horizon _horizon;
    std::string _skyError;
    gfx::MeshId _skyMesh = 0;
    // The sky's shading, the earth's and the horizon picture as the game
    // draws them (skyRow, hrz.md): 8 flat quads each in front of the
    // camera, remade each frame; and whether the game would draw no sky
    // and clear to black (skyBlack).
    gfx::MeshId _horizonMesh = 0;
    gfx::TextureId _horizonPicture = 0;
    bool _skyBlack = false;
    void updateHorizon(const gfx::Vec3 &forward);
    int roadSliceAt() const; // the virtual road's slice nearest the camera
    // The sun (or moon) and the clouds, ranges of the sky mesh's indices.
    gfx::TextureId _sunPicture = 0;
    gfx::TextureId _cloudPicture = 0;
    uint32_t _sunFirst = 0;
    uint32_t _sunIndices = 0;
    uint32_t _cloudFirst = 0;
    uint32_t _cloudIndices = 0;
    bool _sunAdditive = true;
    bool _sunInFront = false;
    // The sky mesh's vertices, kept to change as it goes: the sun turning
    // (ROTATE), the clouds lit by lightning.
    std::vector<gfx::Vertex> _skyVertices;
    std::vector<float> _skyHeights; // their heights as made, over the world's 0
    float _skyEyeY = 0;             // the camera's height they were last moved by
    uint32_t _sunVertex = 0;
    uint32_t _cloudVertex = 0;
    std::array<uint8_t, kCloudPoints * kCloudPoints> _cloudBase{};
    gfx::Vec3 _sunCentre{};
    gfx::Vec3 _sunRight{};
    gfx::Vec3 _sunUp{};
    bool _sunRotate = false;
    bool _skyChanged = false;
    // Lightning, ticked at the game's rate, and its bolts' mesh (thin
    // quads, rebuilt each frame).
    std::vector<LightningBolt> _bolts;
    SkyRandom _skyRandom;
    uint64_t _skyTick = 0;
    gfx::MeshId _boltMesh = 0;
    uint32_t _boltIndices = 0;
    void tickSky(uint64_t nowMs);
    void updateSky(const gfx::Vec3 &forward, const gfx::Vec3 &right);
    bool _fog = true;
    // Shift+Y: the Direct3D renderer's look (d3da), as the game draws for
    // it: no fog (0x4b59c0 sends none) and the clouds' alpha the same at
    // every point (cloudAlpha).
    bool _direct3d = false;
    float _lightIntensity = 1; // the game's SFX_INTENSITY, which dims lights
    // The effects intensity as applied: only the Direct3D renderer (bit
    // 0x40 of 0x7a3a58) dims by it (the lights, glows, particles with flag
    // bit 0, the added clouds, the rain and snow).
    float effectsIntensity() const { return _direct3d ? _lightIntensity : 1.0f; }
    // The fog where the camera is: the base fog, or a dynamic region's
    // (hrz.md), its colour and density (percent).
    void fogAt(int slice, Horizon::Rgb &colour, float &density) const;
    int nearestBlock() const; // to the camera, by the blocks' centres

    // The blocks' lights (glows) and sounds (markers), in the world: a
    // quad each, turned to face the camera every frame.
    struct Mark {
        std::array<float, 3> at{};
        bool light = false;
        int value = 0;      // the light's type, or the sound's slot
        uint16_t kind = 0;  // a sound's: 0 a loop, 1 a one-shot
        bool heard = false; // the first sound of its block, the only one the game plays
    };
    std::vector<Mark> _marks;
    gfx::MeshId _markMesh = 0;
    gfx::TextureId _glowPicture = 0;
    bool _marksShown = true;
    void loadMarks();
    // Each light as 0x491190 draws it (lightQuads): glw0 at a fixed size
    // on the screen, fading with the depth, and glw3 in perspective.
    void drawMarks(const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &up);
    // sfx.fsh glw1 (the fog's beams), glw4 (the rain's flares)
    gfx::TextureId _glow1Picture = 0;
    gfx::TextureId _glow4Picture = 0;
    int _sfxLevel = 0; // Q: the Special Effects option (0x6fbc30): 0 High, 1 Low, 2 Off
    // The track's placed sounds (tr3dNN.bnk of the variant), as the game
    // plays them (frd.md, Sounds): one loop at a time, one-shots at random.
    audio::Mixer *_mixer = nullptr;
    std::vector<uint8_t> _soundData;
    SoundBank _soundBank;
    std::map<int, std::shared_ptr<audio::Sound>> _soundSlots;
    std::map<size_t, audio::SoundHandle> _soundsPlaying; // the loop, by mark
    uint32_t _soundFrame = 0;                            // frames drawn, the one-shots' clock
    OneShotTimer _oneShots;
    SkyRandom _soundRandom;
    void loadSounds();
    void stopSounds();
    void updateSounds(const gfx::Vec3 &eye, const gfx::Vec3 &right);
    // Wall hits' and tyres' particles (particles.h, tyreSmoke), run at the
    // game's 64 ticks a second and drawn as it draws them, with sfx.fsh's
    // SPnn (SNnn at night) pictures.
    ParticleSystem _particles;
    ParticleRandom _particleRandom;
    uint64_t _particleTick = 0;
    uint64_t _particleStartMs = 0;
    bool _particlesHeld = false;       // RELUVA_NFS_TRACK_PARTICLES: a dump's, held as it has them
    std::vector<WallHit> _pendingHits; // made at the next tick
    std::string _hitText;
    std::map<std::string, gfx::TextureId> _particlePictures;            // by picture name, owned by textures_
    std::map<gfx::TextureId, std::array<int, 2>> _particlePictureSizes; // their widths and heights
    gfx::MeshId _particleMesh = 0;
    // This frame's particles: runs of the mesh's quads drawn alike.
    struct ParticleRun {
        gfx::TextureId texture = 0;
        bool added = false;
        // quads
        uint32_t first = 0;
        uint32_t count = 0;
    };
    std::vector<ParticleRun> _particleRuns;
    // Mud and water on the lens (particles.h, SplatScreen): particles of
    // kinds 1, 4, 10 and 11 drawn within 3 of the camera turned into
    // splats on the screen (0x4ca500), aged on the odd ticks (0x4cb220),
    // drawn over everything with SLnn (0x4cb750).
    SplatScreen _splats;
    gfx::MeshId _splatMesh = 0;
    std::vector<ParticleRun> _splatRuns;
    // The weather's rain or snow and its splashes on the screen (particles.h,
    // Weather) as the .hrz's weather type sets them up, ticked with the
    // particles and drawn after them over everything, through the pass's
    // own 120-degree view, with weather.fsh's DRPn and SPLn.
    Weather _weather;
    ParticleRandom _weatherRandom;
    bool _weatherStarted = false;
    int _weatherActive = 0;
    gfx::MeshId _weatherMesh = 0;
    // quads this frame: the drops', then the splashes'
    uint32_t _weatherDrops = 0;
    uint32_t _weatherSplashes = 0;
    // The camera has just been moved (0x552ff4: the weather's drops put
    // back round it, 0x4dd230, and the streaks' tails forgotten, 0x4dd510).
    bool _cameraCut = false;
    // The rain's splash sounds (rain.bnk, the race's bank entry 4) and the
    // cars' hit sounds (Gen3d.bnk, entry 2; Gen.bnk without it), loaded
    // with the placed sounds.
    struct EffectBank {
        std::vector<uint8_t> data;
        SoundBank bank;
        std::map<int, std::shared_ptr<audio::Sound>> decoded;
    };
    EffectBank _rainSounds;
    EffectBank _hitSounds;
    ParticleRandom _effectSoundRandom;
    int _hitSoundLast = 0; // 0x552d80
    void loadEffectBank(EffectBank &b, const std::vector<std::string> &names);
    void playEffect(EffectBank &b, int slot, int volume, int pan);
    // The hits aiCollide reported this step, for the impacts task's
    // particles and sound (carHitParticles, startCarSmoke).
    std::vector<AiHit> _carHits;
    gfx::TextureId weatherPicture(const char *name);
    void prepareWeather(const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &up);
    void drawWeather(float fog, const std::array<float, 3> &fogColour);
    // `prefix` "SL" for a splat's picture; else SPnn, or SNnn at night.
    gfx::TextureId particlePicture(int kind, const char *prefix = nullptr);
    void hitWall(bool fast);
    void tickParticles(uint64_t nowMs);
    // The cars' particles of one 64 Hz step, before the particles' own
    // (the impacts task 0x42b900 -> 0x42b780): the hits aiCollide reported
    // (0x42ac40), each car's hit smoke (car_smoke_tick 0x42b480), and each
    // wheel's tyre smoke (0x42a480) and spray (0x42a280).
    void tickCarEffects(uint32_t tick);
    // Before the frame: the particles' quads; after the scene: drawn.
    void prepareParticles(const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &up);
    void drawParticles(float fog, const std::array<float, 3> &fogColour);
    void prepareSplats(const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &up);
    void drawSplats();
    // The headlights of a car at the camera, by night (frd.md, the
    // headlights): the game's pass over the blocks within 150, glw2 of
    // sfx.fsh projected on polygons whose texture lacks flag 0x04, the
    // light's colour as a floor on the vertex colours of those that have
    // it. Rebuilt each frame as runs of triangles: the picture's, or a
    // texture's with the colour added.
    int _beams = 1; // E: 0 off, 1 low, 2 high, as the game's key
    // Shift+E: the z-buffer in use (0x7a3a60) with the projtex option, so
    // the headlights' projected picture; off as nfs host runs the game
    // (its renderer is softtria's path: 0x472eaa turns the z-buffer and
    // projected textures off), which also draws the depth-sorted items
    // (cars, particles) back to front without a depth test.
    bool _projected = false;
    // Each car's view depths this frame (prepareCars): the box's mean (its
    // sort depth, +0x8B8) and nearest corner, and its box's screen bounds.
    struct CarSort {
        float mean = 0;
        float nearest = 0;
        // in the camera's x / z and y / z
        float x0 = 0;
        float y0 = 0;
        float x1 = 0;
        float y1 = 0;
    };
    std::vector<CarSort> _carSort;
    bool _lightTest = true; // Ctrl+E: the game's test of each block against each light (0x41bf30)
    gfx::MeshId _lightMesh = 0;
    size_t _lightCapacity = 0;            // vertices the mesh holds
    gfx::TextureId _headlightPicture = 0; // owned by textures_
    struct LightRun {
        gfx::TextureId texture = 0;
        bool picture = false; // the projected picture, else a texture's colour floor
        // indices
        uint32_t first = 0;
        uint32_t count = 0;
        bool twoSided = false; // polygons with flag 0x10, not culled
    };
    std::vector<LightRun> _lightRuns;
    void prepareHeadlights(int here, const gfx::Vec3 &eye, const gfx::Vec3 &forward);
    // Computer cars (J): two Corvettes going the camera's way and a police
    // car coming with its siren on, on the virtual road ahead in lanes as
    // the computer picks their middles (0x40daf0), driven along the road's
    // points as the computer drives (aiStep, 64 steps a second): the
    // speed tables' speeds by the driver, braking for bends, the lane
    // votes and lane changes, flashing their lights when held up (Shift+J
    // stops and starts them). Each sits on the road as the game's box
    // does, lit as the game lights a car, with its reflection, its light
    // glows (the tail lights brighter while braking), the rain's flares
    // and the fog's beams, and lights the track by night after the
    // camera's car (headlightIntensity), its roof light too.
    struct CarModel {
        // Each detail's parts (carDetailParts: the body and wheels, then
        // the headlights, drawn with the lights on), one after another, z
        // negated as on the Cars page, a two-sided triangle twice (once
        // each way round, carTriangleCorners) so that the rest can be
        // culled; and each vertex's normal as stored (the model's own,
        // left-handed coordinates).
        std::vector<gfx::Vertex> vertices;
        std::vector<std::array<float, 3>> normals;
        struct Detail {
            // vertices: from first, the body's, then the lights'
            uint32_t first = 0;
            uint32_t body = 0;
            uint32_t lights = 0;
            uint32_t depthPart = 0; // the first body part's (its nearest corner is the glows' depth)
            // the same in envFrom
            uint32_t envFirst = 0;
            uint32_t envBody = 0;
            uint32_t envLights = 0;
        };
        std::array<Detail, 4> details{};
        gfx::TextureId skin = 0; // owned by textures_
        std::array<float, 3> halfSize{1.183f, 0.669f, 2.40644f};
        // Its wheels' places (parts 1-4: x right, z forward), for the strips.
        std::array<std::array<float, 2>, 4> wheels{{{-0.8f, 1.3f}, {0.8f, 1.3f}, {-0.8f, -1.3f}, {0.8f, -1.3f}}};
        std::vector<Model::Dummy> dummies;
        std::string error;
        // The reflection (0x49de20 with 0x4ba350's sphere map): by detail,
        // the vertices of the first body part's (0, 5, 10 or 11) and then
        // the headlights' (12) triangles without kNoReflection, by their
        // index in vertices (two-sided ones twice, as there), and
        // whether each has kStrongReflection (0x55e6e0's colour).
        std::vector<uint32_t> envFrom;
        std::vector<bool> envStrong;
        AiCarData ai;       // its carp.txt
        int paintValue = 0; // the first paint colour's value (fce.md; +0x8B0)
        std::string folder; // gamedata/carmodel/<name>/car.viv/
        int serial = -1;    // carp.txt's serial number, the set-up's car key
        // The skin as stored and the model's paints, for recolouring it
        // (skinTexel); the textures made, by primary colour.
        image::Image skinImage;
        Model colours;                          // only the colour lists, for primaryFor/secondaryFor
        std::map<size_t, gfx::TextureId> skins; // owned by textures_
    };
    // The Corvette, the police Corvette, then any other car a dump's
    // set-up names (RELUVA_NFS_TRACK_CARS_SETUP), by its serial number.
    std::vector<CarModel> _carModels{2};
    void loadCarModel(CarModel &cm);
    int carModelForSerial(int serial);
    // The skin of a model in one of its paints (0x49d320's recolouring).
    gfx::TextureId carSkin(size_t model, size_t paint);
    std::vector<std::array<uint8_t, 16>> _carSetup; // RELUVA_NFS_TRACK_CARS_SETUP: each car's key
    struct ComputerCar {
        int model = 0;
        bool siren = false; // a police car with its siren on
        float slice = 0;    // along the virtual road: a slice and the way to the next
        int way = 1;        // +1 along the slices' order, -1 against it
        int lane = 7;       // 0-13, 7 and up right of the road's middle
        float speed = 0;    // m/s
        int beams = 1;      // startingBeams(), and as flashed
        int detail = 0;     // carDetailLevel this frame (-1: not drawn)
        AiCar ai;           // the computer's driving (aiStep)
        int beamScroll = 0; // +0x944: the fog beams' picture, moved by the speed
        // This frame, in track coordinates: the position (+0x98) and rows.
        std::array<float, 3> at{};
        std::array<float, 3> right{};
        std::array<float, 3> up{};
        std::array<float, 3> forward{};
        uint32_t colour = 0xff7f7f7f; // its shading (carShadeColour, carLitBy)
        size_t paint = 0;             // its model's primary colour (+0x8A8 in a dump)
        int paintValue = -1;          // +0x8B0 as a dump has it; else the paint's
        // From a dump: the officer on foot beside it (+0x8E4 and +0x8E8 set,
        // cop_officer_place 0x401c20): where he stands on the road (+0x910)
        // and his rows (+0x8EC: right, up, forward).
        bool dumpedOfficer = false;
        std::array<float, 3> officerAt{};
        std::array<float, 3> officerRight{1, 0, 0};
        std::array<float, 3> officerForward{0, 0, 1};
        // From a dump, held as it has them: each wheel's skid level
        // (wheelSkidLevel), the velocity (+0xA4), the wheels' road points
        // and surfaces and the weather, for the tyres' smoke (wheelSmokes).
        std::array<float, 4> skid{};
        std::array<float, 3> velocity{};
        std::array<std::array<float, 3>, 4> wheelAt{};
        std::array<int, 4> wheelSurface{};
        int weather = 0;
        CarSmoke smoke; // a hit's smoke (+0x8D4, +0x8D8: car_smoke_tick)
        gfx::MeshId mesh = 0;
        gfx::MeshId envMesh = 0;
    };
    bool _computerCars = false;
    bool _carsMoving = true;
    bool _reflections = true;
    float _aiTime = 0; // seconds not yet stepped
    int _aiStep = 0;   // the cars' steps, the game's step count for them
    // The race's laps for the computer racers (0x6fd4b4; 0: none, they race
    // on): Ctrl+Shift+J; Ctrl+J puts them on the grid (gridCars).
    int _laps = 0;
    void gridCars();
    gfx::TextureId _envPicture = 0; // sky.fsh's sphere map of the variant (CHRD, CHRN, CHRW, CHNW)
    std::vector<ComputerCar> _cars;
    // speedsF.bin, speedsR.bin
    std::vector<uint8_t> _speedsForward;
    std::vector<uint8_t> _speedsBackward;
    std::string _carError;
    gfx::TextureId _glow3Picture = 0; // sfx.fsh glw3, the cars' glows; owned by textures_
    gfx::MeshId _carGlowMesh = 0;
    gfx::MeshId _carFlareMesh = 0;
    gfx::MeshId _carBeamMesh = 0;
    uint32_t _carGlowQuads = 0;
    uint32_t _carGlowCapacity = 0;
    uint32_t _carFlareQuads = 0;
    uint32_t _carFlareCapacity = 0;
    uint32_t _carBeamQuads = 0;
    uint32_t _carBeamCapacity = 0;
    // Quads (4 vertices each) into a mesh of indexed quads, grown as
    // needed; the count drawn.
    uint32_t uploadQuads(std::vector<gfx::Vertex> &quads, gfx::MeshId &mesh, uint32_t &capacity);
    void loadCars();
    void releaseCars();
    void placeCars();
    std::vector<DumpedCar> _carDump; // RELUVA_NFS_TRACK_CARS: the cars as a game dump has them
    void placeDumpedCars();
    // The human's car of a dump: its headlights are the camera car's
    // (0x42d160 places them from the car's own position and rows, not the
    // camera's, which may be in the cockpit or behind it).
    struct HumanPose {
        bool set = false;
        std::array<float, 3> at{};
        std::array<float, 3> right{};
        std::array<float, 3> up{};
        std::array<float, 3> forward{};
        float halfLength = 0;
    };
    HumanPose _humanPose;
    void moveCars(float dt);
    // Key C, the dispatcher: the police car ordered as a chasing cop's
    // calls order a free one (physics.md): a strip (0x4039c0), then a
    // roadblock (0x403b20) 20 slices on, then back to patrol, watching the
    // racer nearest it; the strips laid drawn as dark bars across a lane.
    void dispatchCop();
    int _dispatchNext = 0;
    // Shift+C: the camera behind the police car last given an order (or the
    // first), following it (-1 not).
    int _followCop = -1;
    int _orderedCop = -1;
    gfx::MeshId _stripMesh = 0;
    // The officer on foot (draw_officer 0x4b9bc0): the track's cop model
    // (0x4b7c60: Render/pc/cop0 on tracks 0 and 4, cop2 on 2 and 6, cop3 on
    // 3 and 7, cop4 on 8, cop1 on the others) with its .art pages, one run
    // of triangles a page; its half height (+0x2C).
    struct OfficerRun {
        gfx::TextureId texture = 0; // owned by textures_
        uint32_t first = 0;
        uint32_t count = 0;
    };
    gfx::MeshId _officerMesh = 0;
    std::vector<OfficerRun> _officerRuns;
    float _officerHalfHeight = 0;
    std::vector<gfx::Vertex> _officerVertices;
    // The triangles that reflect (not fce flag 1; only cop2's), as indices
    // into officerVertices_, strong (flag 2) or not, and their mesh.
    std::vector<uint32_t> _officerEnvFrom;
    std::vector<bool> _officerEnvStrong;
    gfx::MeshId _officerEnvMesh = 0;
    std::array<float, 3> _officerDummy{}; // the model's first dummy (cop4's red light)
    bool _officerHasDummy = false;
    // This frame's officer (prepareCars): where he stands, upright, turned
    // to the racer's heading, and his shading colour (draw_officer's: the
    // road's under the racer, by night with the cars' and the track's
    // lights on the racer).
    struct OfficerShown {
        bool shown = false;
        // track coordinates, the model's origin
        std::array<float, 3> at{};
        std::array<float, 3> right{1, 0, 0};
        std::array<float, 3> forward{0, 0, 1};
        uint32_t colour = 0xff7f7f7f;
    };
    OfficerShown _officer;
    void loadOfficer();
    void drawOfficers(const gfx::DrawCall &base);
    uint32_t _stripQuads = 0;
    uint32_t _stripCapacity = 0;
    // The cars' shadows (0x4b81a0, carShadowQuad): sfx.fsh's shad in
    // 0x6FFFFFFF, one quad a car, blended by source alpha.
    gfx::MeshId _shadowMesh = 0;
    uint32_t _shadowQuads = 0;
    uint32_t _shadowCapacity = 0;
    gfx::TextureId _shadowPicture = 0; // owned by textures_
    void drawShadows(const gfx::DrawCall &base);
    void carRows(ComputerCar &car) const;
    // The cars' colours and glows for this frame (prepareCars), before
    // the headlights are made; drawn by drawCars.
    // fogDensity: the fog's percent this frame, for the beams.
    void prepareCars(const gfx::Vec3 &eye, const gfx::Vec3 &forward, const gfx::Vec3 &right, const gfx::Vec3 &up,
                     float fogDensity);
    void drawCars(const gfx::DrawCall &base);
    // The road polygon under a point (track coordinates): its four
    // corners' colours as the game keeps them, scaled by the ambient light.
    bool roadColoursAt(const std::array<float, 3> &at, std::array<uint32_t, 4> &corners) const;
    // The high-detail road polygon (list 4) under a point on the ground:
    // its block and index.
    bool roadPolygonAt(const std::array<float, 3> &at, int &block, size_t &index) const;
    // The height of a road polygon's plane at a point on the ground.
    float roadHeightAt(int block, size_t index, float x, float z) const;
    // The camera's car's light (prepareHeadlights), for the cars' shading.
    Headlight cameraLight() const;
    // Moving objects this frame: each one's matrix and its batches, cut
    // at the reach as the placed objects are (0x41b640 queues them too).
    struct MovingFrame {
        gfx::Mat4 model{};
        std::vector<Batch> batches;
        int moving = -1; // in moving_
    };
    std::vector<MovingFrame> _movingFrame;
    long long _fixedStep = -1; // RELUVA_NFS_TRACK_STEP: the moving objects at that game step
    bool _mirrored = false;    // U: the mirrored track (0x6fd4c4), with set 1
    int _variant = 0;
    // detail 3: by distance, as the game
    int _detail = 3;
    int _textureSet = 0;
    int _block = 0;
    bool _allBlocks = false; // K: every block at once, not the game's view
    bool _textured = true;
    bool _vertexLight = true;
    bool _lanes = true;
    bool _objects = true;
    bool _cullBack = true;
    bool _walls = false;
    std::set<uint32_t> _held;
    bool _dragging = false;
    int _lastX = 0;
    int _lastY = 0;
    std::array<float, 3> _eye{};
    float _yaw = 0;
    float _pitch = 0;
    uint64_t _lastMs = 0;
};

// Every sound in every bank (.bnk), loose or in a .viv: speech, engines,
// effects. Without audio output the sounds are only listed.
//   Up/Down: sound   PageUp/PageDown: bank   Home/End: first/last sound
//   Space/Enter: play   S: stop   P: play as selected on/off
//   Left/Right: a sound with a bend range (most): its bend, or for a car's
//   sounds its revs; others: the pitch, a semitone (sounds with a loop loop
//   until stopped)
class SoundPage : public Page {
public:
    SoundPage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Sounds"; }
    bool empty() const override { return _banks.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int bank);
    void select(int sound);
    void play();

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _banks;
    int _bank = -1;
    std::vector<uint8_t> _data;
    SoundBank _loaded;
    std::vector<int> _sounds; // the bank's slots that hold a sound
    int _sound = 0;
    std::string _error;
    std::string _soundError;
    std::shared_ptr<audio::Sound> _decoded;
    audio::SoundHandle _playing = audio::kNoSound;
    bool _autoPlay = true;
    int _semitones = 0; // the pitch, up or down
    // Engines, played as the game plays them (bnk.md): at key 60, bent by
    // the revs, with the car's revs from its carp.txt when the bank has one
    // beside it (else the bend is chosen directly).
    int _bend = 64;
    int _rpm = 0;
    int _minRpm = 0;
    int _redline = 0;
    bool engine() const;            // the selected sound has a bend range (0x0A)
    double rate() const;            // the rate to play it at, of its own
    std::string engineText() const; // the revs, bend and pitch, for the header
};

// The music streams (.mus), played as their Pathfinder map (.map) leads
// from segment to segment by an intensity, or in the linear order (.lin).
//   Up/Down: stream   Space: play/stop   Left/Right: intensity
//   L: map or linear order   N: next segment now   Home: first segment
//   0-9: an event for the next segment (the game's: 1 a pursuit, 4-9 the
//   track's zones)
class MusicPage : public Page {
public:
    MusicPage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Music"; }
    bool empty() const override { return _streams.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int stream);
    void start(int section);
    void stop();
    int following(int section); // takes the pending event, if any
    int firstSection() const;
    const MusicMap *activeMap() const;

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _streams;
    int _stream = -1;
    MusicStream _loaded;
    MusicMap _map;
    MusicMap _linear;
    bool _haveMap = false;
    bool _haveLinear = false;
    bool _useLinear = false;
    std::string _error;
    std::string _mapError;
    int _intensity = 50;
    int _pendingEvent = -1; // an event (0-9 keys) for the next section, as the game's SNDpathevent
    int _section = -1;      // playing
    std::vector<int> _history;
    std::shared_ptr<audio::Sound> _current;
    // The segments playing one after another, one queued behind the one
    // playing; the section last queued.
    std::shared_ptr<audio::SoundQueue> _queue;
    int _queuedSection = -1;
    audio::SoundHandle _playing = audio::kNoSound;
    uint64_t _lastDraw = 0;
    std::shared_ptr<audio::Sound> decodeSection(int section);
};

// The movies (.mad) with their sound.
//   Up/Down: movie   Space: pause   Left/Right: 5 seconds back/on
//   Home: start   PageUp/PageDown: frame by frame (paused)
//   G: shown as the game shows it (its decoder, then its display modes 3,
//   2 or 1, mad.md) or by Reluva's plain conversion
class MoviePage : public Page {
public:
    MoviePage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Movies"; }
    bool empty() const override { return _movies.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int movie);
    void seek(int frame, uint64_t nowMs);
    void showFrame(int frame);
    void stopSound();
    double frameMs() const;

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _movies;
    int _movie = -1;
    MadMovie _loaded;
    std::string _error;
    std::shared_ptr<audio::Sound> _audio;
    MadDecoder _decoder;
    int _decoded = -1; // the frame the decoder holds
    int _display = 3;  // the game's display mode; 0: Reluva's conversion
    Surface _picture;
    bool _paused = false;
    // The clock: the audio's position while it plays, else time since
    // startMs_ from startFrame_.
    int _startFrame = 0;
    uint64_t _startMs = 0;
    uint64_t _nowMs = 0;
    audio::SoundHandle _playing = audio::kNoSound;
};

// Every page, in Tab order, those with nothing to show left out. The
// mixer plays the pages' sounds; null without audio output. The game
// writes under `saveFolder`. `settings` are the game's section of
// reluva.ini, for the Play page (makePlayPage) and texturefilter (point:
// the viewer's textures point-sampled too).
std::vector<std::unique_ptr<Page>> makePages(const char *gameName, const GameFolders &folders, const GameFiles &files,
                                             audio::Mixer *mixer = nullptr,
                                             const std::filesystem::path &saveFolder = {},
                                             const std::map<std::string, std::string> &settings = {});

} // namespace reluva::nfs
