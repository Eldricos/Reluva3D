// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/nfs/fce.h"
#include "engines/nfs/col.h"
#include "engines/nfs/frd.h"
#include "engines/nfs/hrz.h"
#include "engines/nfs/bnk.h"
#include "engines/nfs/mad.h"
#include "engines/nfs/mus.h"
#include "engines/nfs/particles.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/text_table.h"

#include <array>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Data viewers used by the Need for Speed engine until the game itself
// runs. The 2D ones draw into the engine's 640x480 RGBA32 screen when
// asked; the 3D ones take the GPU renderer while shown and draw a frame
// each update. Tab switches between them (nfs.cpp).

namespace reluva::nfs {

// How the Track and Cars pages sample the game's textures: bilinear, no
// mipmaps. The game never sends THRASH's filter, LOD bias or mipmap
// states (7, 8, 0xb), but voodoo2a.dll sends itself 7 1 and 0xb 1 after
// setvideomode, so on a Voodoo2 the game's textures are bilinear-filtered
// (thrash.md, Texture filtering, from running the DLL on the Glide
// stand-in); its textures have one level each, so no mipmaps.
// reluva.ini's texturefilter=point holds them point-sampled (makePages).
inline bool pagesPointSampled = false;
inline gfx::TextureParams glideTextureParams(bool clamp = false) {
    gfx::TextureParams params;
    params.clamp = clamp;
    params.mipmaps = false;
    params.nearest = pagesPointSampled;
    return params;
}

class Page {
public:
    virtual ~Page() = default;
    virtual const char* name() const = 0;
    // Whether there's anything to show; pages without are skipped.
    virtual bool empty() const { return false; }
    // Called when the page is shown and before switching away. A 3D page
    // takes the renderer in activate(); the engine puts the framebuffer
    // back after it's left.
    virtual void activate(System& /*sys*/) {}
    virtual void deactivate() {}
    // Whether Esc goes to the page rather than back to the launcher.
    virtual bool capturesEscape() const { return false; }
    // Whether the page is done (the game it ran has ended): back to the
    // launcher.
    virtual bool finished() const { return false; }
    // Whether the page is drawing with the GPU (else draw() is used).
    virtual bool uses3D() const { return false; }
    virtual void handleEvent(const Event& ev) {
        if (ev.type == EventType::KeyDown)
            handleKey(ev.key, ev.mods);
    }
    virtual void handleKey(uint32_t /*key*/, uint16_t /*mods*/) {}
    // 2D pages: draws the page into the screen, after any event.
    virtual void draw(Surface& /*screen*/) {}
    // 3D pages: draws a frame.
    virtual void update(uint64_t /*nowMs*/) {}
    // 2D pages: called often while shown, for playback; true when the page
    // needs drawing again.
    virtual bool tick(uint64_t /*nowMs*/) { return false; }
};

// What the engine found: the folders, the CD's files, tracks and cars.
class InfoPage : public Page {
public:
    InfoPage(const char* gameName, GameFolders folders);
    const char* name() const override { return "Info"; }
    void handleKey(uint32_t, uint16_t) override {}
    void draw(Surface& screen) override;

private:
    std::string gameName_;
    GameFolders folders_;
    std::vector<std::string> tracks_, cars_, music_, movies_;
};

// Every picture in every .fsh/.qfs, loose or in a .viv.
//   Up/Down: bank   PageUp/PageDown: 10 banks   Left/Right: picture
//   Home/End: first/last picture   B: background   A: alpha on/off
class TexturePage : public Page {
public:
    explicit TexturePage(const GameFiles& files);
    const char* name() const override { return "Textures"; }
    bool empty() const override { return banks_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int bank);
    void decodeCurrent();

    const GameFiles& files_;
    std::vector<FileRef> banks_;
    int bank_ = -1, shape_ = 0;
    ShapeBank loaded_;
    std::string error_;
    image::Image image_;
    std::string imageError_;
    int background_ = 0; // checkerboard, black, white
    bool alpha_ = true;
};

// Each car's cockpit (dash.qfs): the dash, the steering wheel, the gear
// indicator and the speedometer and tachometer needles, drawn as the game
// draws them.
//   Left/Right: car   Space: sweep the needles and wheel   Up/Down: move
//   the needles (paused)   A/D: steer   G: next gear
class CockpitPage : public Page {
public:
    explicit CockpitPage(const GameFiles& files);
    const char* name() const override { return "Cockpits"; }
    bool empty() const override { return dashes_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int index);

    const GameFiles& files_;
    std::vector<FileRef> dashes_;
    int index_ = -1;
    std::string error_;
    image::Image dash_, wheel_;
    int wheelX_ = 0, wheelY_ = 0;           // where the wheel's pivot goes
    int wheelPivotX_ = 0, wheelPivotY_ = 0; // in the wheel's picture
    std::array<image::Image, std::size(kGearPictures)> gears_;
    int gearX_ = 0, gearY_ = 0; // the gear indicator's top left
    bool haveGauges_ = false;
    Cockpit cockpit_;
    float position_ = 0; // 0-1 of each gauge's range
    int steering_ = 0;   // -128 to 127, as the car's
    int gear_ = 1;       // neutral
    bool sweeping_ = true;
    uint64_t lastMs_ = 0;
};

// The front-end string tables, in the game's own font.
//   Left/Right: language   Up/Down: line   PageUp/PageDown: page
//   Home/End: first/last   F: the game's font or Reluva's
class StringPage : public Page {
public:
    explicit StringPage(const GameFiles& files);
    const char* name() const override { return "Strings"; }
    bool empty() const override { return tables_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int table);
    int linesPerPage() const;

    const GameFiles& files_;
    std::vector<FileRef> tables_;
    int table_ = -1, top_ = 0;
    TextTable loaded_;
    std::string error_;
    std::optional<Font> font_; // arial10b.ffn, if it reads
    bool gameFont_ = true;
};

// Every font, with all its characters.
//   Left/Right: font   +/-: size
class FontPage : public Page {
public:
    explicit FontPage(const GameFiles& files);
    const char* name() const override { return "Fonts"; }
    bool empty() const override { return fonts_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int font);

    const GameFiles& files_;
    std::vector<FileRef> fonts_;
    int font_ = -1;
    Font loaded_;
    std::string error_;
    int scale_ = 1;
};

// Every model (cars, traffic, police, the front end's track models) in 3D,
// with its skin, drawn by its triangles' flags (two-sided, unlit) and with
// the game's reflection of a track's sky.
//   Left/Right: model   drag or A/D/W/S: turn   wheel or +/-: zoom
//   L: detail level   T: textures   C: back faces   E: reflection's sky
//   Space: spin   R: reset
class CarPage : public Page {
public:
    explicit CarPage(const GameFiles& files);
    ~CarPage() override;
    const char* name() const override { return "Cars"; }
    bool empty() const override { return models_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    bool uses3D() const override { return renderer_ != nullptr; }
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    void load(int index);
    void release();
    void resetView();

    const GameFiles& files_;
    std::vector<FileRef> models_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::string unavailable_; // why there's no 3D
    Surface overlay_;
    Model model_;
    std::string error_, skinName_;
    gfx::MeshId mesh_ = 0;
    gfx::TextureId skin_ = 0;
    // A Render/pc model's pages (its .art), and each part's runs of
    // triangles drawn alike: by page and flags, a range of the mesh's
    // indices each.
    std::vector<gfx::TextureId> pages_;
    struct Run {
        uint32_t firstIndex = 0, indexCount = 0, page = 0;
        bool twoSided = false, unlit = false, strong = false;
    };
    std::vector<std::vector<Run>> runs_;
    // The reflection (0x49db70, 0x43400f): the triangles without
    // kNoReflection again, added over the model with source alpha, their
    // sphere-map coordinates from the normals as the camera sees them,
    // remade each frame. Its runs index envMesh_.
    gfx::MeshId envMesh_ = 0;
    std::vector<gfx::Vertex> envVertices_;
    std::vector<std::array<float, 3>> envNormals_; // in the world
    std::vector<std::vector<Run>> envRuns_;
    std::vector<std::pair<std::string, gfx::TextureId>> skies_; // a track's sphere maps: name, texture
    int sky_ = 0;       // which, or skies_.size() for no reflection
    void loadSkies();
    std::vector<int> partLevels_; // 0 high, 1 medium, 2 small, 3 tiny
    int level_ = 0;               // 0-3, or 4 for all
    bool textures_ = true, cullBack_ = true, spin_ = false, dragging_ = false;
    float yaw_ = 0, pitch_ = 0, distance_ = 5, radius_ = 2;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0;
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
//   and projection, off by default as in nfs-host; Ctrl+E the game's light
//   test)   J: computer cars (Shift+J stops them)
//   Z: the cars' reflections (the game's option at 0x6fbc24)
//   Q: the Special Effects option: High, Low, Off (no track lights, rain
//   flares or fog beams)   Y: fog   M: lights and sounds   U: mirrored
class TrackPage : public Page {
public:
    TrackPage(const GameFiles& files, const GameFolders& folders, audio::Mixer* mixer = nullptr);
    ~TrackPage() override;
    const char* name() const override { return "Tracks"; }
    bool empty() const override { return tracks_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    bool uses3D() const override { return renderer_ != nullptr; }
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    // Draw calls of one kind of polygon, a range of the mesh's indices
    // each, one per picture.
    struct Batch {
        gfx::TextureId texture = 0;
        uint32_t firstIndex = 0, indexCount = 0;
        bool additive = false;
        bool twoSided = false;  // polygon flag 0x10: never culled
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
        size_t group = 0, object = 0; // in track_.extraObjects
    };
    // Animated polygons of one kind with the same delay and frame count:
    // a copy of each for every frame, which drawing picks by the tick.
    struct Animation {
        int delay = 1;
        std::vector<std::vector<Batch>> frames;
        const std::vector<Batch>& at(uint64_t tick) const { return frames[(tick / delay) % frames.size()]; }
    };

    void load(int index);
    void loadSky();
    void release();
    void releaseSky();
    void toBlock(int block);
    gfx::TextureId picture(const std::string& sheet, int number, bool lane, bool* additive);
    // A picture of the track's sky.fsh by name, and its text.
    gfx::TextureId skyPicture(const std::string& name, std::string* text);
    std::string textureSheet() const; // the track's texture sheet, of the set shown
    gfx::TextureId horizonStrip();    // the horizon picture, from it

    const GameFiles& files_;
    GameFolders folders_;
    std::vector<std::string> tracks_; // trkNNN folders
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::string unavailable_;
    Surface overlay_;
    Track track_;
    std::string error_;
    gfx::MeshId mesh_ = 0;
    // The mesh's vertices and indices, kept for the cut-off and the
    // headlights; and the cut-off's indices over the same vertices, made
    // anew each frame: the polygons of batches that straddle the reach
    // (kPolygonReach) whose first corner is within it.
    std::vector<gfx::Vertex> meshVertices_;
    std::vector<uint32_t> meshIndices_;
    gfx::MeshId cutMesh_ = 0;
    std::vector<uint32_t> cutIndices_;
    void cutFar(std::vector<Batch>& list, const gfx::Vec3& eye, const gfx::Vec3& forward,
                const gfx::Mat4* model = nullptr);
    std::vector<gfx::TextureId> textures_;
    std::map<std::string, ShapeBank> banks_; // loaded sheets, by file name
    std::map<std::string, std::pair<gfx::TextureId, bool>> pictures_; // "sheet/number" -> texture, additive
    // By block and kind: block × kKinds + kind; one more block's worth
    // last for what isn't drawn by block (the walls, the last group of
    // placed objects).
    std::vector<std::vector<Batch>> batches_;
    std::vector<Moving> moving_;
    std::vector<std::vector<Animation>> animations_;
    // The blocks the game would draw from block `here` (0x41cdc0): those
    // in its neighbour list whose centre is within the view distance on
    // the ground, each with the road's detail (0-2) for its distance.
    std::vector<std::pair<int, int>> blocksInView(int here) const;
    TrackCollision collision_;
    std::string collisionError_;
    // The sky, for the variant shown (0 day, 1 night, 2 weather, 3 both).
    Horizon horizon_;
    std::string skyError_;
    gfx::MeshId skyMesh_ = 0;
    // The sky's shading, the earth's and the horizon picture as the game
    // draws them (skyRow, hrz.md): 8 flat quads each in front of the
    // camera, remade each frame; and whether the game would draw no sky
    // and clear to black (skyBlack).
    gfx::MeshId horizonMesh_ = 0;
    gfx::TextureId horizonPicture_ = 0;
    bool skyBlack_ = false;
    void updateHorizon(const gfx::Vec3& forward);
    int roadSliceAt() const; // the virtual road's slice nearest the camera
    // The sun (or moon) and the clouds, ranges of the sky mesh's indices.
    gfx::TextureId sunPicture_ = 0, cloudPicture_ = 0;
    uint32_t sunFirst_ = 0, sunIndices_ = 0, cloudFirst_ = 0, cloudIndices_ = 0;
    bool sunAdditive_ = true, sunInFront_ = false;
    // The sky mesh's vertices, kept to change as it goes: the sun turning
    // (ROTATE), the clouds lit by lightning.
    std::vector<gfx::Vertex> skyVertices_;
    std::vector<float> skyHeights_; // their heights as made, over the world's 0
    float skyEyeY_ = 0;             // the camera's height they were last moved by
    uint32_t sunVertex_ = 0, cloudVertex_ = 0;
    std::array<uint8_t, kCloudPoints * kCloudPoints> cloudBase_{};
    gfx::Vec3 sunCentre_{}, sunRight_{}, sunUp_{};
    bool sunRotate_ = false, skyChanged_ = false;
    // Lightning, ticked at the game's rate, and its bolts' mesh (thin
    // quads, rebuilt each frame).
    std::vector<LightningBolt> bolts_;
    SkyRandom skyRandom_;
    uint64_t skyTick_ = 0;
    gfx::MeshId boltMesh_ = 0;
    uint32_t boltIndices_ = 0;
    void tickSky(uint64_t nowMs);
    void updateSky(const gfx::Vec3& forward, const gfx::Vec3& right);
    bool fog_ = true;
    // Shift+Y: the Direct3D renderer's look (d3da), as the game draws for
    // it: no fog (0x4b59c0 sends none) and the clouds' alpha the same at
    // every point (cloudAlpha).
    bool direct3d_ = false;
    float lightIntensity_ = 1; // the game's SFX_INTENSITY, which dims lights
    // The effects intensity as applied: only the Direct3D renderer (bit
    // 0x40 of 0x7a3a58) dims by it (the lights, glows, particles with flag
    // bit 0, the added clouds, the rain and snow).
    float effectsIntensity() const { return direct3d_ ? lightIntensity_ : 1.0f; }
    // The fog where the camera is: the base fog, or a dynamic region's
    // (hrz.md), its colour and density (percent).
    void fogAt(int slice, Horizon::Rgb& colour, float& density) const;
    int nearestBlock() const; // to the camera, by the blocks' centres

    // The blocks' lights (glows) and sounds (markers), in the world: a
    // quad each, turned to face the camera every frame.
    struct Mark {
        std::array<float, 3> at{};
        bool light = false;
        int value = 0; // the light's type, or the sound's slot
        uint16_t kind = 0; // a sound's: 0 a loop, 1 a one-shot
        bool heard = false; // the first sound of its block, the only one the game plays
    };
    std::vector<Mark> marks_;
    gfx::MeshId markMesh_ = 0;
    gfx::TextureId glowPicture_ = 0;
    bool marksShown_ = true;
    void loadMarks();
    // Each light as 0x491190 draws it (lightQuads): glw0 at a fixed size
    // on the screen, fading with the depth, and glw3 in perspective.
    void drawMarks(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up);
    gfx::TextureId glow1Picture_ = 0, glow4Picture_ = 0; // sfx.fsh glw1 (the fog's beams), glw4 (the rain's flares)
    int sfxLevel_ = 0; // Q: the Special Effects option (0x6fbc30): 0 High, 1 Low, 2 Off
    // The track's placed sounds (tr3dNN.bnk of the variant), as the game
    // plays them (frd.md, Sounds): one loop at a time, one-shots at random.
    audio::Mixer* mixer_ = nullptr;
    std::vector<uint8_t> soundData_;
    SoundBank soundBank_;
    std::map<int, std::shared_ptr<audio::Sound>> soundSlots_;
    std::map<size_t, audio::SoundHandle> soundsPlaying_; // the loop, by mark
    uint32_t soundFrame_ = 0; // frames drawn, the one-shots' clock
    OneShotTimer oneShots_;
    SkyRandom soundRandom_;
    void loadSounds();
    void stopSounds();
    void updateSounds(const gfx::Vec3& eye, const gfx::Vec3& right);
    // Wall hits' and tyres' particles (particles.h, tyreSmoke), run at the
    // game's 64 ticks a second and drawn as it draws them, with sfx.fsh's
    // SPnn (SNnn at night) pictures.
    ParticleSystem particles_;
    ParticleRandom particleRandom_;
    uint64_t particleTick_ = 0, particleStartMs_ = 0;
    bool particlesHeld_ = false; // RELUVA_NFS_TRACK_PARTICLES: a dump's, held as it has them
    std::vector<WallHit> pendingHits_; // made at the next tick
    std::string hitText_;
    std::map<std::string, gfx::TextureId> particlePictures_; // by picture name, owned by textures_
    std::map<gfx::TextureId, std::array<int, 2>> particlePictureSizes_; // their widths and heights
    gfx::MeshId particleMesh_ = 0;
    // This frame's particles: runs of the mesh's quads drawn alike.
    struct ParticleRun {
        gfx::TextureId texture = 0;
        bool added = false;
        uint32_t first = 0, count = 0; // quads
    };
    std::vector<ParticleRun> particleRuns_;
    // Mud and water on the lens (particles.h, SplatScreen): particles of
    // kinds 1, 4, 10 and 11 drawn within 3 of the camera turned into
    // splats on the screen (0x4ca500), aged on the odd ticks (0x4cb220),
    // drawn over everything with SLnn (0x4cb750).
    SplatScreen splats_;
    gfx::MeshId splatMesh_ = 0;
    std::vector<ParticleRun> splatRuns_;
    // The weather's rain or snow and its splashes on the screen (particles.h,
    // Weather) as the .hrz's weather type sets them up, ticked with the
    // particles and drawn after them over everything, through the pass's
    // own 120-degree view, with weather.fsh's DRPn and SPLn.
    Weather weather_;
    ParticleRandom weatherRandom_;
    bool weatherStarted_ = false;
    int weatherActive_ = 0;
    gfx::MeshId weatherMesh_ = 0;
    uint32_t weatherDrops_ = 0, weatherSplashes_ = 0; // quads this frame: the drops', then the splashes'
    // The camera has just been moved (0x552ff4: the weather's drops put
    // back round it, 0x4dd230, and the streaks' tails forgotten, 0x4dd510).
    bool cameraCut_ = false;
    // The rain's splash sounds (rain.bnk, the race's bank entry 4) and the
    // cars' hit sounds (Gen3d.bnk, entry 2; Gen.bnk without it), loaded
    // with the placed sounds.
    struct EffectBank {
        std::vector<uint8_t> data;
        SoundBank bank;
        std::map<int, std::shared_ptr<audio::Sound>> decoded;
    };
    EffectBank rainSounds_, hitSounds_;
    ParticleRandom effectSoundRandom_;
    int hitSoundLast_ = 0; // 0x552d80
    void loadEffectBank(EffectBank& b, const std::vector<std::string>& names);
    void playEffect(EffectBank& b, int slot, int volume, int pan);
    // The hits aiCollide reported this step, for the impacts task's
    // particles and sound (carHitParticles, startCarSmoke).
    std::vector<AiHit> carHits_;
    gfx::TextureId weatherPicture(const char* name);
    void prepareWeather(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up);
    void drawWeather(float fog, const std::array<float, 3>& fogColour);
    // `prefix` "SL" for a splat's picture; else SPnn, or SNnn at night.
    gfx::TextureId particlePicture(int kind, const char* prefix = nullptr);
    void hitWall(bool fast);
    void tickParticles(uint64_t nowMs);
    // The cars' particles of one 64 Hz step, before the particles' own
    // (the impacts task 0x42b900 -> 0x42b780): the hits aiCollide reported
    // (0x42ac40), each car's hit smoke (car_smoke_tick 0x42b480), and each
    // wheel's tyre smoke (0x42a480) and spray (0x42a280).
    void tickCarEffects(uint32_t tick);
    // Before the frame: the particles' quads; after the scene: drawn.
    void prepareParticles(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up);
    void drawParticles(float fog, const std::array<float, 3>& fogColour);
    void prepareSplats(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up);
    void drawSplats();
    // The headlights of a car at the camera, by night (frd.md, the
    // headlights): the game's pass over the blocks within 150, glw2 of
    // sfx.fsh projected on polygons whose texture lacks flag 0x04, the
    // light's colour as a floor on the vertex colours of those that have
    // it. Rebuilt each frame as runs of triangles: the picture's, or a
    // texture's with the colour added.
    int beams_ = 1;         // E: 0 off, 1 low, 2 high, as the game's key
    // Shift+E: the z-buffer in use (0x7a3a60) with the projtex option, so
    // the headlights' projected picture; off as nfs-host runs the game
    // (its renderer is softtria's path: 0x472eaa turns the z-buffer and
    // projected textures off), which also draws the depth-sorted items
    // (cars, particles) back to front without a depth test.
    bool projected_ = false;
    // Each car's view depths this frame (prepareCars): the box's mean (its
    // sort depth, +0x8B8) and nearest corner, and its box's screen bounds.
    struct CarSort {
        float mean = 0, nearest = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // in the camera's x / z and y / z
    };
    std::vector<CarSort> carSort_;
    bool lightTest_ = true; // Ctrl+E: the game's test of each block against each light (0x41bf30)
    gfx::MeshId lightMesh_ = 0;
    size_t lightCapacity_ = 0; // vertices the mesh holds
    gfx::TextureId headlightPicture_ = 0; // owned by textures_
    struct LightRun {
        gfx::TextureId texture = 0;
        bool picture = false; // the projected picture, else a texture's colour floor
        uint32_t first = 0, count = 0; // indices
        bool twoSided = false; // polygons with flag 0x10, not culled
    };
    std::vector<LightRun> lightRuns_;
    void prepareHeadlights(int here, const gfx::Vec3& eye, const gfx::Vec3& forward);
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
            uint32_t first = 0, body = 0, lights = 0; // vertices: from first, the body's, then the lights'
            uint32_t depthPart = 0; // the first body part's (its nearest corner is the glows' depth)
            uint32_t envFirst = 0, envBody = 0, envLights = 0; // the same in envFrom
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
        AiCarData ai;      // its carp.txt
        int paintValue = 0; // the first paint colour's value (fce.md; +0x8B0)
        std::string folder; // gamedata/carmodel/<name>/car.viv/
        int serial = -1;    // carp.txt's serial number, the set-up's car key
        // The skin as stored and the model's paints, for recolouring it
        // (skinTexel); the textures made, by primary colour.
        image::Image skinImage;
        Model colours; // only the colour lists, for primaryFor/secondaryFor
        std::map<size_t, gfx::TextureId> skins; // owned by textures_
    };
    // The Corvette, the police Corvette, then any other car a dump's
    // set-up names (RELUVA_NFS_TRACK_CARS_SETUP), by its serial number.
    std::vector<CarModel> carModels_{2};
    void loadCarModel(CarModel& cm);
    int carModelForSerial(int serial);
    // The skin of a model in one of its paints (0x49d320's recolouring).
    gfx::TextureId carSkin(size_t model, size_t paint);
    std::vector<std::array<uint8_t, 16>> carSetup_; // RELUVA_NFS_TRACK_CARS_SETUP: each car's key
    struct ComputerCar {
        int model = 0;
        bool siren = false;    // a police car with its siren on
        float slice = 0;       // along the virtual road: a slice and the way to the next
        int way = 1;           // +1 along the slices' order, -1 against it
        int lane = 7;          // 0-13, 7 and up right of the road's middle
        float speed = 0;       // m/s
        int beams = 1;         // startingBeams(), and as flashed
        int detail = 0;        // carDetailLevel this frame (-1: not drawn)
        AiCar ai;              // the computer's driving (aiStep)
        int beamScroll = 0;    // +0x944: the fog beams' picture, moved by the speed
        // This frame, in track coordinates: the position (+0x98) and rows.
        std::array<float, 3> at{}, right{}, up{}, forward{};
        uint32_t colour = 0xff7f7f7f; // its shading (carShadeColour, carLitBy)
        size_t paint = 0;     // its model's primary colour (+0x8A8 in a dump)
        int paintValue = -1;  // +0x8B0 as a dump has it; else the paint's
        // From a dump: the officer on foot beside it (+0x8E4 and +0x8E8 set,
        // cop_officer_place 0x401c20): where he stands on the road (+0x910)
        // and his rows (+0x8EC: right, up, forward).
        bool dumpedOfficer = false;
        std::array<float, 3> officerAt{}, officerRight{1, 0, 0}, officerForward{0, 0, 1};
        // From a dump, held as it has them: each wheel's skid level
        // (wheelSkidLevel), the velocity (+0xA4), the wheels' road points
        // and surfaces and the weather, for the tyres' smoke (wheelSmokes).
        std::array<float, 4> skid{};
        std::array<float, 3> velocity{};
        std::array<std::array<float, 3>, 4> wheelAt{};
        std::array<int, 4> wheelSurface{};
        int weather = 0;
        CarSmoke smoke; // a hit's smoke (+0x8D4, +0x8D8: car_smoke_tick)
        gfx::MeshId mesh = 0, envMesh = 0;
    };
    bool computerCars_ = false, carsMoving_ = true, reflections_ = true;
    float aiTime_ = 0; // seconds not yet stepped
    int aiStep_ = 0;   // the cars' steps, the game's step count for them
    // The race's laps for the computer racers (0x6fd4b4; 0: none, they race
    // on): Ctrl+Shift+J; Ctrl+J puts them on the grid (gridCars).
    int laps_ = 0;
    void gridCars();
    gfx::TextureId envPicture_ = 0; // sky.fsh's sphere map of the variant (CHRD, CHRN, CHRW, CHNW)
    std::vector<ComputerCar> cars_;
    std::vector<uint8_t> speedsForward_, speedsBackward_; // speedsF.bin, speedsR.bin
    std::string carError_;
    gfx::TextureId glow3Picture_ = 0; // sfx.fsh glw3, the cars' glows; owned by textures_
    gfx::MeshId carGlowMesh_ = 0, carFlareMesh_ = 0, carBeamMesh_ = 0;
    uint32_t carGlowQuads_ = 0, carGlowCapacity_ = 0, carFlareQuads_ = 0, carFlareCapacity_ = 0, carBeamQuads_ = 0,
             carBeamCapacity_ = 0;
    // Quads (4 vertices each) into a mesh of indexed quads, grown as
    // needed; the count drawn.
    uint32_t uploadQuads(std::vector<gfx::Vertex>& quads, gfx::MeshId& mesh, uint32_t& capacity);
    void loadCars();
    void releaseCars();
    void placeCars();
    std::vector<DumpedCar> carDump_; // RELUVA_NFS_TRACK_CARS: the cars as a game dump has them
    void placeDumpedCars();
    // The human's car of a dump: its headlights are the camera car's
    // (0x42d160 places them from the car's own position and rows, not the
    // camera's, which may be in the cockpit or behind it).
    struct HumanPose {
        bool set = false;
        std::array<float, 3> at{}, right{}, up{}, forward{};
        float halfLength = 0;
    };
    HumanPose humanPose_;
    void moveCars(float dt);
    // Key C, the dispatcher: the police car ordered as a chasing cop's
    // calls order a free one (physics.md): a strip (0x4039c0), then a
    // roadblock (0x403b20) 20 slices on, then back to patrol, watching the
    // racer nearest it; the strips laid drawn as dark bars across a lane.
    void dispatchCop();
    int dispatchNext_ = 0;
    // Shift+C: the camera behind the police car last given an order (or the
    // first), following it (-1 not).
    int followCop_ = -1, orderedCop_ = -1;
    gfx::MeshId stripMesh_ = 0;
    // The officer on foot (draw_officer 0x4b9bc0): the track's cop model
    // (0x4b7c60: Render/pc/cop0 on tracks 0 and 4, cop2 on 2 and 6, cop3 on
    // 3 and 7, cop4 on 8, cop1 on the others) with its .art pages, one run
    // of triangles a page; its half height (+0x2C).
    struct OfficerRun {
        gfx::TextureId texture = 0; // owned by textures_
        uint32_t first = 0, count = 0;
    };
    gfx::MeshId officerMesh_ = 0;
    std::vector<OfficerRun> officerRuns_;
    float officerHalfHeight_ = 0;
    std::vector<gfx::Vertex> officerVertices_;
    // The triangles that reflect (not fce flag 1; only cop2's), as indices
    // into officerVertices_, strong (flag 2) or not, and their mesh.
    std::vector<uint32_t> officerEnvFrom_;
    std::vector<bool> officerEnvStrong_;
    gfx::MeshId officerEnvMesh_ = 0;
    std::array<float, 3> officerDummy_{}; // the model's first dummy (cop4's red light)
    bool officerHasDummy_ = false;
    // This frame's officer (prepareCars): where he stands, upright, turned
    // to the racer's heading, and his shading colour (draw_officer's: the
    // road's under the racer, by night with the cars' and the track's
    // lights on the racer).
    struct OfficerShown {
        bool shown = false;
        std::array<float, 3> at{}, right{1, 0, 0}, forward{0, 0, 1}; // track coordinates, the model's origin
        uint32_t colour = 0xff7f7f7f;
    };
    OfficerShown officer_;
    void loadOfficer();
    void drawOfficers(const gfx::DrawCall& base);
    uint32_t stripQuads_ = 0, stripCapacity_ = 0;
    // The cars' shadows (0x4b81a0, carShadowQuad): sfx.fsh's shad in
    // 0x6FFFFFFF, one quad a car, blended by source alpha.
    gfx::MeshId shadowMesh_ = 0;
    uint32_t shadowQuads_ = 0, shadowCapacity_ = 0;
    gfx::TextureId shadowPicture_ = 0; // owned by textures_
    void drawShadows(const gfx::DrawCall& base);
    void carRows(ComputerCar& car) const;
    // The cars' colours and glows for this frame (prepareCars), before
    // the headlights are made; drawn by drawCars.
    // fogDensity: the fog's percent this frame, for the beams.
    void prepareCars(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up,
                     float fogDensity);
    void drawCars(const gfx::DrawCall& base);
    // The road polygon under a point (track coordinates): its four
    // corners' colours as the game keeps them, scaled by the ambient light.
    bool roadColoursAt(const std::array<float, 3>& at, std::array<uint32_t, 4>& corners) const;
    // The high-detail road polygon (list 4) under a point on the ground:
    // its block and index.
    bool roadPolygonAt(const std::array<float, 3>& at, int& block, size_t& index) const;
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
    std::vector<MovingFrame> movingFrame_;
    long long fixedStep_ = -1; // RELUVA_NFS_TRACK_STEP: the moving objects at that game step
    bool mirrored_ = false; // U: the mirrored track (0x6fd4c4), with set 1
    int variant_ = 0;
    int detail_ = 3, textureSet_ = 0, block_ = 0; // detail 3: by distance, as the game
    bool allBlocks_ = false; // K: every block at once, not the game's view
    bool textured_ = true, vertexLight_ = true, lanes_ = true, objects_ = true, cullBack_ = true, walls_ = false;
    std::set<uint32_t> held_;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    std::array<float, 3> eye_{};
    float yaw_ = 0, pitch_ = 0;
    uint64_t lastMs_ = 0;
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
    SoundPage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Sounds"; }
    bool empty() const override { return banks_.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int bank);
    void select(int sound);
    void play();

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> banks_;
    int bank_ = -1;
    std::vector<uint8_t> data_;
    SoundBank loaded_;
    std::vector<int> sounds_; // the bank's slots that hold a sound
    int sound_ = 0;
    std::string error_, soundError_;
    std::shared_ptr<audio::Sound> decoded_;
    audio::SoundHandle playing_ = audio::kNoSound;
    bool autoPlay_ = true;
    int semitones_ = 0; // the pitch, up or down
    // Engines, played as the game plays them (bnk.md): at key 60, bent by
    // the revs, with the car's revs from its carp.txt when the bank has one
    // beside it (else the bend is chosen directly).
    int bend_ = 64, rpm_ = 0, minRpm_ = 0, redline_ = 0;
    bool engine() const; // the selected sound has a bend range (0x0A)
    double rate() const; // the rate to play it at, of its own
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
    MusicPage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Music"; }
    bool empty() const override { return streams_.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int stream);
    void start(int section);
    void stop();
    int following(int section); // takes the pending event, if any
    int firstSection() const;
    const MusicMap* activeMap() const;

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> streams_;
    int stream_ = -1;
    MusicStream loaded_;
    MusicMap map_, linear_;
    bool haveMap_ = false, haveLinear_ = false, useLinear_ = false;
    std::string error_, mapError_;
    int intensity_ = 50;
    int pendingEvent_ = -1; // an event (0-9 keys) for the next section, as the game's SNDpathevent
    int section_ = -1;      // playing
    std::vector<int> history_;
    std::shared_ptr<audio::Sound> current_;
    // The segments playing one after another, one queued behind the one
    // playing; the section last queued.
    std::shared_ptr<audio::SoundQueue> queue_;
    int queuedSection_ = -1;
    audio::SoundHandle playing_ = audio::kNoSound;
    uint64_t lastDraw_ = 0;
    std::shared_ptr<audio::Sound> decodeSection(int section);
};

// The movies (.mad) with their sound.
//   Up/Down: movie   Space: pause   Left/Right: 5 seconds back/on
//   Home: start   PageUp/PageDown: frame by frame (paused)
//   G: shown as the game shows it (its decoder, then its display modes 3,
//   2 or 1, mad.md) or by Reluva's plain conversion
class MoviePage : public Page {
public:
    MoviePage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Movies"; }
    bool empty() const override { return movies_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int movie);
    void seek(int frame, uint64_t nowMs);
    void showFrame(int frame);
    void stopSound();
    double frameMs() const;

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> movies_;
    int movie_ = -1;
    MadMovie loaded_;
    std::string error_;
    std::shared_ptr<audio::Sound> audio_;
    MadDecoder decoder_;
    int decoded_ = -1; // the frame the decoder holds
    int display_ = 3;  // the game's display mode; 0: Reluva's conversion
    Surface picture_;
    bool paused_ = false;
    // The clock: the audio's position while it plays, else time since
    // startMs_ from startFrame_.
    int startFrame_ = 0;
    uint64_t startMs_ = 0, nowMs_ = 0;
    audio::SoundHandle playing_ = audio::kNoSound;
};

// Every page, in Tab order, those with nothing to show left out. The
// mixer plays the pages' sounds; null without audio output. The game
// writes under `saveFolder`. `settings` are the game's section of
// reluva.ini: renderscale (1-4, the Play page's frame against the game's
// 640x480; 2 if not set), texturefilter (point: point-sampled textures; bilinear otherwise) and
// joystick (off: a game controller isn't offered to the game) and
// positionalupdates (on: network races with positional updates).
std::vector<std::unique_ptr<Page>> makePages(const char* gameName, const GameFolders& folders,
                                             const GameFiles& files, audio::Mixer* mixer = nullptr,
                                             const std::filesystem::path& saveFolder = {},
                                             const std::map<std::string, std::string>& settings = {});

} // namespace reluva::nfs
