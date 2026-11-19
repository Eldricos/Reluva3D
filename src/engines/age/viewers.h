// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Angel Game Engine data viewer's pages. Tab switches pages; Esc
// goes back to the launcher.
//
// Race (3D): the game's races. In the menu Up/Down pick a line, Left/Right
// change it (city, event, race, level, car), Enter starts. In a race the
// Drive page's keys drive and C changes the camera; Esc leaves the race,
// and Enter goes back to the menu from the results. The race's traffic,
// pedestrians and police are about as its settings say.
//
// Drive (3D): a car driven round a city. Up/Down (or W/S) accelerate and
// brake (held when stopped, backs up), Left/Right (or A/D) steer, Space
// is the handbrake. C changes the camera (near, far, in the car),
// PageUp/PageDown the car, P its colour, Home/End the city, T the time and
// weather, R puts the car back at the start, I shows or hides the info.
// The city has traffic and people on its sidewalks (Y steps through none,
// light, normal and heavy), traffic lights, and police, who chase a car
// speeding past them (O turns them off and on); while not chasing they
// patrol. In the evening, at night and in fog and rain the cars'
// headlights are on. In a police car H turns its siren on and off.
//
// World (3D): a city (London, San Francisco) flown through. Left/Right
// pick the city, drag to look, W/A/S/D move, Space/C (or E/Q) go up and
// down, Shift goes faster, T steps through the 16 times and weathers, M
// shows or hides the placed models, I the info, R returns to the start.
//
// Models (3D): every model (geometry/*.pkg), its parts placed by their
// .mtx files. Left/Right step through them (PageUp/PageDown by 10,
// Home/End), Up/Down jump to the next kind (the name's first part:
// vp cars, sp props...), drag to turn, the wheel (or +/-) zooms, L
// changes the level of detail, P the paint job, G shows the parts that
// only show when lit (headlights' and sirens' glows, traffic lights),
// I shows or hides the info and R resets the view.
//
// Textures: every texture (texture/*.tex and .tga, jpg/*.jpg) over a
// checkerboard where it's transparent. Left/Right step (PageUp/PageDown
// by 10, Home/End), Up/Down jump to the next kind (the name's first
// part), +/- zoom, A shows alpha as transparency or not.
//
// Files: every file in the search path with its size and archive.
// Up/Down, PageUp/PageDown and Home/End move, the wheel scrolls,
// Left/Right show one top-level folder at a time (anim, aud, bound,
// city...), Enter opens the file: text as text, anything else as a hex
// dump, scrolled the same way; Esc or Backspace goes back to the list.

#include "common/archive.h"
#include "audio/mixer.h"
#include "common/video.h"
#include "common/system.h"
#include "graphics/gfx3d.h"
#include "engines/age/game/car.h"
#include "engines/age/render/car_actor.h"
#include "engines/age/game/city_life.h"
#include "engines/age/render/city_scene.h"
#include "engines/age/game/cops_robbers.h"
#include "engines/age/game/damage.h"
#include "engines/age/render/dashboard.h"
#include "engines/age/ui/front_end.h"
#include "engines/age/ui/game_menus.h"
#include "engines/age/ui/help_viewer.h"
#include "engines/age/ui/hud_map.h"
#include "engines/age/ui/hud_pictures.h"
#include "engines/age/formats/lang.h"
#include "engines/age/game/lesson.h"
#include "engines/age/ui/pad_input.h"
#include "engines/age/game/record.h"
#include "engines/age/ui/view_controls.h"
#include "engines/age/audio/engine_sound.h"
#include "engines/age/audio/music.h"
#include "engines/age/ui/net_menus.h"
#include "engines/age/game/opponent.h"
#include "engines/age/game/options.h"
#include "engines/age/formats/race.h"
#include "engines/age/formats/replay.h"

#include "engines/age/game/road_graph.h"
#include "engines/age/games.h"
#include "engines/age/formats/pkg.h"
#include "engines/age/render/render_cache.h"
#include "engines/age/formats/texture.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
    audio::Mixer* mixer = nullptr;            // sound output; none if the system has no audio
    std::filesystem::path savePath;           // where this game's own files go (GameTarget::savePath); may be empty
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : _ctx(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0; // pages with nothing to show are skipped
    virtual void activate() = 0;      // redraw and retitle after switching here
    virtual void deactivate() {}      // before switching away
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    // Pages that draw with System::init3D; the engine puts the 2D
    // framebuffer back when leaving one.
    virtual bool uses3D() const { return false; }
    // Whether Esc and Tab (which otherwise leave and switch pages) go to
    // the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }
    // The player asked to leave the game (its Quit dialog).
    virtual bool wantsToQuit() const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& _ctx;
};

// A car the menus offer (tune/<car>.info), if its simulation and model
// are there.
struct CarInfo {
    std::string base;        // "vpbug"
    std::string description; // "VW New Beetle"
    std::vector<std::string> colors;
};
std::vector<CarInfo> findCars(const SearchSet& files);

// The driving keys held, as the Drive and Race pages read them: Up/W
// throttle, Down/S brake, Left/A and Right/D steer, Space handbrake.
enum DriveKeys : uint32_t {
    kDriveThrottle = 1,
    kDriveBrake = 2,
    kDriveLeft = 4,
    kDriveRight = 8,
    kDriveHandbrake = 16,
    kDriveHorn = 32,
    kDriveReverse = 64
};
// The Race page's events in its menu's order (Blitz, Circuit, Checkpoint,
// Crash Course, Cruise).
RaceKind racePageKind(int index);
constexpr int kRacePageKinds = 5;
// What a key is bound to (the Control Options' keys).
Control controlOf(uint32_t key);
// The player's car takes the Control Options (auto reverse, steering).
void applyControlOptions(CarSim& sim);
// The scene and the city's life take the Graphics and Audio Options:
// the sky, smart rendering (the .cpvs), how far and how finely models are
// drawn, the ambient sounds. Cheap: called every frame.
void applyGraphicsOptions(CityScene& scene, CityLife& life);
// How many times the city's textures are halved (the Texture Quality:
// none at Very High, three at Low). Taken when a city is loaded.
int textureHalvings();
// The share of pedestrians the options leave (Show Pedestrians).
float pedestrianShare();
// Headlights lighting the road (the Lighting Quality).
bool headlightPoolsOn();
uint32_t driveKeyOf(uint32_t key);
CarControls controlsOf(uint32_t keys);

// The speed, gear and revs, bottom right of an RGBA overlay.
// The speed, gear, revs and (`damage` 0..1) damage: with the game's
// pictures, if loaded, along the bottom left of the map (which starts at
// x = `right`); else as text, bottom right, ending there.
void drawSpeedometer(Surface& overlay, const CarSim& sim, int right, float damage, const HudPictures& pictures);
// The game's rear-view mirror, top right of the frame (x 447, y 0, 193 x 77
// at 640 x 480, as on its screens): what's behind the car, flipped as a
// mirror shows it. `world` draws the world for a view; the view and the
// viewport are the whole frame's again after (the caller's view to set).
void drawRearMirror(gfx::Renderer3D& renderer, const CarSim& sim, float farClip,
                    const std::function<void(const gfx::Mat4& viewProj, const Vec3& eye)>& world);

// The first part of a name's file name, for jumping between kinds:
// "geometry/vpbug.pkg" -> "vpbug", "texture/sw_stc_tan.tex" -> "sw".
std::string kindOf(const std::string& name);

// A file's contents as lines to show: its text (tabs expanded, CR LF and
// LF ends) if it looks like text, else a hex dump of 16 bytes a line.
std::vector<std::string> fileLines(const std::vector<uint8_t>& data, bool& isText);

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;
    bool takesKey(const Event& ev) const override;

private:
    struct Row {
        std::string name;
        std::string source; // the archive's file name
        uint64_t size = 0;
    };

    void applyFolder();
    void open();
    void draw();
    int rows() const;

    std::vector<Row> _files;
    std::vector<std::string> _folders; // top-level folders, for Left/Right
    int _folder = -1;                  // index into folders_, -1: all
    std::vector<size_t> _shown;        // files_ indices in the folder
    int _selected = 0, _top = 0;

    // The open file, if any.
    bool _viewing = false, _isText = false;
    std::string _viewName;
    std::vector<std::string> _lines;
    int _lineTop = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void show(int index);
    void draw();

    std::vector<std::string> _names;
    int _index = 0;
    int _zoom = 0; // 0: fit the screen, else that many times the size
    bool _alpha = true;
    Tex _tex;
    std::string _error;
};

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    void show(int index);
    void build(); // the current level of detail and paint job, uploaded
    void resetView();
    void drawInfo();

    std::vector<std::string> _names;
    int _index = 0;
    gfx::Renderer3D* _renderer = nullptr;
    std::unique_ptr<TextureCache> _textures;
    Surface _overlay;

    Pkg _pkg;
    std::string _error;
    std::vector<std::string> _levels; // the model's levels of detail, finest first
    int _level = 0;
    uint32_t _paintJob = 0;
    ModelMesh _mesh;

    Vec3 _center{};
    float _radius = 1;
    float _yaw = 0, _pitch = 0, _distance = 1;
    bool _dragging = false, _showInfo = true, _showLights = false;
    int _lastX = 0, _lastY = 0;
};

// A city flown through: its streets and buildings from the PSDL, the
// models placed in it, its sky, lit as at one of its 16 times and
// weathers. Keys in the header's comment.
class WorldViewer : public Page {
public:
    explicit WorldViewer(ViewerContext& ctx);
    ~WorldViewer() override;
    const char* name() const override { return "World"; }
    size_t count() const override { return _cities.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    void load(int index);
    void resetView();
    void drawInfo(const std::string& stats);

    std::vector<CityInfo> _cities;
    int _index = 0;
    int _lighting = 4;
    gfx::Renderer3D* _renderer = nullptr;
    std::unique_ptr<CityScene> _scene;
    Surface _overlay;
    std::string _error;

    gfx::Vec3 _eye{};
    float _yaw = 0, _pitch = 0;
    bool _dragging = false, _showInfo = true, _showModels = true;
    int _lastX = 0, _lastY = 0;
    uint64_t _lastMs = 0;
    uint32_t _keys = 0; // movement keys held, bit per kMove*
};

// A car driven round a city: Reluva's simulation of it (car.h) against
// the city's collisions, seen from the car's own cameras, with its engine
// sound and a HUD. Keys in the header's comment.
class DriveViewer : public Page {
public:
    explicit DriveViewer(ViewerContext& ctx);
    ~DriveViewer() override;
    const char* name() const override { return "Drive"; }
    size_t count() const override { return _cities.empty() ? 0 : _cars.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    void loadCity(int index);
    void loadCar(int index);
    void resetCar();
    void startSounds(); // the horn, clutch, tyres and knocks: wet roads in the rain
    void startLife();
    void cutBridgeGaps();
    void drawHud();

    std::vector<CityInfo> _cities;
    std::vector<CarInfo> _cars;
    int _city = 0, _carIndex = 0;
    uint32_t _paintJob = 0;
    gfx::Renderer3D* _renderer = nullptr;
    std::unique_ptr<CityScene> _scene;
    CollisionWorld _world;
    size_t _boundsUsed = 0;
    Surface _overlay;
    std::string _error;

    CarActor _car;
    EngineSound _engine;
    CarSounds _sounds;
    GameMusic _music;
    Dashboard _dash; // for the in-car camera
    HudMap _hudMap;
    HudPictures _pictures;
    int _shift = 0;       // a gear change asked for (manual gears)
    bool _manual = false; // manual gears (T)
    std::vector<CarCamera> _cameras;
    int _camera = 0;
    CameraFollow _cameraPos{};
    CameraBlend _blend;      // a change of camera, blended
    ViewControls _view;      // the game's view controls (Thrill Cam, looking round...)
    PadDrive _pad;           // a game controller's sticks and triggers
    CdPlayer _cd;            // the CD player
    float _airFor = 0;       // seconds the player's car has been off the ground
    bool _airPlayed = false; // the big-air motif, this jump
    CameraView _lastView;    // the last frame's
    bool _cameraReset = true;
    int _lighting = 4;
    bool _showInfo = true;
    bool _hudShown = true; // the game's HUD Toggle
    bool _mirror = false;  // the rear-view mirror (Rear View Mirror)

    CityLife _life;
    std::unique_ptr<FlareDrawer> _flares; // with the scene's textures
    std::unique_ptr<SmokePuffs> _smoke;   // a damaged car's
    CarWear _wear;                        // the player's car's damage
    RoadGraph _roads;
    int _trafficLevel = 2; // none, light, normal, heavy
    bool _cops = true;
    bool _fill = true;     // place the traffic anywhere round the car next update
    SirenSound _siren;     // the player's, in a police car
    bool _sirenOn = false;
    float _clock = 0;
    float _busted = 0;     // seconds the "busted" message has left
    float _wrecked = 0;    // seconds the "wrecked" message has left
    uint32_t _keys = 0; // driving keys held: DriveKeys
    uint64_t _lastMs = 0;
};

// A race (Blitz, Circuit, Checkpoint) from the game's race files, against
// opponents driving their recorded paths. Keys in the header's comment.
class RaceViewer : public Page {
public:
    explicit RaceViewer(ViewerContext& ctx);
    ~RaceViewer() override;
    const char* name() const override { return "Race"; }
    size_t count() const override { return _cities.empty() || _cars.empty() ? 0 : 1; }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }
    bool takesKey(const Event& ev) const override;
    bool wantsToQuit() const override { return _quit; }

private:
    enum class State { kMenu, kCountdown, kRacing, kFinished };
    struct Racer {
        std::unique_ptr<CarActor> car;
        PathDriver driver; // opponents only
        std::string name;
        size_t next = 0;           // index into targets_ of the next checkpoint (in any order: how many cleared)
        std::vector<bool> cleared; // in a race in any order, those cleared
        size_t lastCleared = 0;
        float finishTime = -1;
        bool player = false;
        uint8_t peer = kNoPlayer; // another player's car (multiplayer), driven by what its driver sends
        uint8_t botId = kNoPlayer; // a computer player the host drives (multiplayer)
        GoldBot bot;               // and its mind, outside the races
        // An opponent: where along its path it's nearest each checkpoint
        // (its path doesn't always go through them all).
        std::vector<size_t> gatePoints;
        float throttle = 0;        // as last driven, for its sound on the others' machines
        uint8_t resets = 0;        // times put back (fallen out, stuck), counting on
        float putBackAt = -100;    // the race clock when it last was
        // Another player's car on this machine: its engine and its damage,
        // as its own machine says them.
        std::unique_ptr<EngineSound> engine;
        std::unique_ptr<CarWear> wear;
        uint8_t dentsSeen = 0;
        bool dentsKnown = false;
        float damageShown = 0;
        // The host's own account of another player's race: where its car
        // last was, and how many times it had been put back then.
        Vec3 seenAt{};
        bool seen = false;
        uint8_t seenResets = 0;
    };

    void loadCity();
    void startRace();
    void endRace();
    void stepRace(float dt);
    float progressOf(const Racer& r) const;
    void drawCheckpoints();
    void drawArrow();
    void drawOpponentMarkers();
    void drawMap(float dt);
    void drawMenu();
    void drawRaceHud();
    void judgeLesson(float dt, bool checkpoint, float impact);
    // The race's message `offset` into its event's group of mmlang.dll's
    // strings, or `fallback`.
    std::string raceMessage(int offset, const std::string& fallback) const;
    void drawCentred(const std::string& text, int y, Color color, int scale);
    bool lesson() const { return _race.kind == RaceKind::kCrashCourse; }
    // The race menu's Cruise, and the stunt under it (race_cruise.cpp).
    bool cruising() const { return _race.kind == RaceKind::kCruise && _race.index != kStuntRace; }
    bool stunt() const { return _race.kind == RaceKind::kCruise && _race.index == kStuntRace; }
    void startCruise();
    void stepCruise(float dt);
    void drawCruiseHud();
    float _cruiseHold = 0;      // seconds busted or wrecked shows before the car's put back
    float _stuntMessageFor = 0; // "Put the pedal to the metal!" after the start
    uint32_t _stuntMessage = 0;
    Vec3 _pierEnd{}; // where the stunt's pier ends (the ground stops)
    const Waypoint& targetPoint(size_t k) const { return _race.waypoints[_targets[k]]; }
    bool anyOrder() const;
    // The checkpoint the racer is making for: its next, or (the player in
    // any order) the arrow's.
    size_t targetOf(const Racer& r) const;
    // A racer's checkpoints still to clear, in order (the player's by default).
    std::vector<size_t> remaining(const Racer& r) const;
    std::vector<size_t> remaining() const { return remaining(_racers.front()); }
    void clearCheckpoint(Racer& r, size_t k);
    void pickCheckpoint(int step);
    void drawCheckpointMarkers();

    std::vector<CityInfo> _cities;
    std::vector<CarInfo> _cars;
    gfx::Renderer3D* _renderer = nullptr;
    std::unique_ptr<CityScene> _scene;
    CollisionWorld _world;
    RoadGraph _roads;
    RoadGraph _botRoads; // the road map's (city/<map>.bai), for computer players
    CityLife _life;
    std::unique_ptr<FlareDrawer> _flares; // with the scene's textures
    std::unique_ptr<SmokePuffs> _smoke;   // a damaged car's
    CarWear _wear;                        // the player's car's damage
    bool _fill = false;
    Surface _overlay;
    std::string _error;
    int _loadedCity = -1;

    // The menu's choices.
    int _field = 0, _city = 0, _kind = 0, _raceIndex = 0, _level = 0, _car = 0;
    std::vector<std::string> _raceNames;

    State _state = State::kMenu;
    RaceDefinition _race;
    std::vector<size_t> _targets; // the waypoints to reach, in order (laps unrolled)
    std::vector<Racer> _racers;   // the player first
    float _clock = 0;             // seconds since the start (negative counting down)
    std::string _outcome;
    EngineSound _engine;
    CarSounds _sounds;
    GameMusic _music;
    Dashboard _dash; // for the in-car camera
    HudMap _hudMap;
    HudPictures _pictures;
    int _shift = 0;
    bool _manual = false;
    std::vector<CarCamera> _cameras;
    int _camera = 0;
    CameraFollow _cameraPos{};
    CameraBlend _blend;      // a change of camera, blended
    ViewControls _view;      // the game's view controls (Thrill Cam, looking round...)
    PadDrive _pad;           // a game controller's sticks and triggers
    CdPlayer _cd;            // the CD player
    float _airFor = 0;       // seconds the player's car has been off the ground
    bool _airPlayed = false; // the big-air motif, this jump
    CameraView _lastView;    // the last frame's
    bool _cameraReset = true;
    uint32_t _keys = 0;
    uint64_t _lastMs = 0;
    float _orbit = 0; // the menu's camera, round the start
    GameStrings _strings;
    LessonJudge _judge;
    std::string _beatenAll; // "You've beaten all the Blitz races!!", once they are
    float _finalLapFor = 0; // seconds "Final lap!" has left
    float _sunkFor = 0;
    float _resultsTime = 0; // seconds the results have been up     // and "Sleep with the fishes!", after a car's sunk
    int _lastLap = 0;
    std::vector<float> _lapEnds;     // the clock at each of the player's laps' ends
    float _splitFor = 0, _split = 0; // the time shown after a checkpoint, and for how long yet
    bool _splitIsLap = false;
    float _targetStill = 0;     // seconds the car to destroy has stood still
    std::string _unlockMessage; // the results screen's: what the race opened
    bool _hudShown = true;      // the game's HUD Toggle
    bool _mirror = false;       // the rear-view mirror (Rear View Mirror)
    // A Checkpoint or Blitz race alone: the player's checkpoints in any
    // order, the finish once they're all cleared (the game's help); the
    // arrow at the nearest, or the one picked (Next/Prev. Checkpoint).
    int _picked = -1;
    gfx::Mat4 _viewProj{}; // this frame's, for the opponents' markers
    PlayerRecord _record; // the current driver's
    // The race's conditions as the menus changed them (else its own).
    FrontEnd::Choice::Conditions _conditions;
    bool _conditionsChanged = false;
    DriverRoster _roster;
    void switchDriver();
    bool _openAll = false;  // U: every race and car open, whatever the record says
    bool _recorded = false; // this race's result is in the record

    std::unique_ptr<FrontEnd> _front;   // the game's menus, when their files are there
    // The opening movie, logos.avi, before the menus: tried once.
    std::unique_ptr<video::Movie> _movie;
    bool _movieTried = false;
    double _movieTime = 0;
    audio::SoundHandle _movieSound = audio::kNoSound;
    image::Image _movieFrame;
    bool playMovie(float dt); // false once it's over (or there's none)
    void endMovie();
    std::unique_ptr<CarActor> _showCar; // the garage's car
    int _showCarIndex = -1, _showPaint = -1, _paint = 0;

    bool raceIsOpen(int index) const;
    bool carIsOpen(int car) const;
    void recordResult();
    void menuEvent(const Event& ev);
    void drawGarageCar(float dt);
    // The HUD arrow over the car pointing to `to`; `little`: the Opponent
    // Position's, smaller, lower and always green.
    void drawArrowTo(const Vec3& to, bool blitz, bool little = false);

    // Replays (race_replay.cpp): the race being driven is recorded; a
    // replay runs its race again from what was recorded.
    Replay _recording;
    Replay _playback;
    bool _replaying = false;
    bool _replayOfThisRace = false; // the instant replay of the race just driven (else one loaded)
    bool _replaySaved = false;
    bool _replayQuitting = false; // asked to save on the way out
    size_t _replayFrame = 0;
    bool _replayPaused = false;
    double _replayAccum = 0;
    int _replayCam = 0;   // the car's cameras, then (the last) the roadside one
    int _replayFocus = 0; // the replay bar's button
    Vec3 _tvAt{};
    bool _tvSet = false;
    float _tvSide = 1;
    std::string _replayNote;
    void recordFrame(float dt);
    void markRecording();
    void startReplay(Replay replay);
    void playFrame();
    void stepReplay(float dt);
    void endReplay();
    bool replayEvent(const Event& ev);
    std::string replayWord(int button) const; // the bar's word for it
    void replayButton(int which);
    CameraView replayCamera(float dt, float& fov);
    void drawReplayBar();
    void gameMenuEvent(const Event& ev);
    // Where quitting a replay goes back to: the menus (a replay loaded),
    // the results, or the race paused (it's run on to where it was).
    enum class ReplayReturn { kMenu, kResults, kRace };
    ReplayReturn _replayReturn = ReplayReturn::kMenu;

    // The race's own menus (race_menus.cpp): paused (Esc), and the
    // game's results screens; the menus' music and voices.
    bool _paused = false, _results = false;
    int _pauseFocus = 0, _resultsFocus = 0;
    std::string _lastFocus; // the menu control last spoken for
    std::unique_ptr<MenuPictures> _menuPictures;
    void pauseEvent(const Event& ev);
    void resultsEvent(const Event& ev);
    void drawPauseMenu();
    void drawResults();
    void menuSounds();
    void playVoice(const std::string& clip);
    bool nextRaceOpen() const;
    void instantReplay(ReplayReturn back);
    std::string replayDefaultName() const;

    // Multiplayer (race_net.cpp): the game's multiplayer menus, then the
    // session's game, each car driven on its own machine.
    std::unique_ptr<GameMenus> _game; // the Options and the main menu's dialogs
    std::unique_ptr<HelpViewer> _help; // the navigation bar's Help
    bool _quit = false;
    std::unique_ptr<NetMenus> _net;
    bool _netMenus = false;     // the multiplayer menus are up
    bool _netGame = false;      // playing a multiplayer game
    bool _garageForNet = false; // the garage's open for the lobby's car
    uint64_t _nowMs = 0;
    GoldGame _gold;
    uint32_t _goldSeen = 0;                                  // the last gold event said
    std::map<std::string, std::vector<std::string>> _speech; // the announcer's Cops & Robbers lines
    std::string _netMessage;
    float _netMessageFor = 0;
    int _minutesSaid = 0;   // the last "N minutes remaining"
    bool _chatting = false; // typing a line to the others
    std::string _chatLine;
    void netUpdate(uint64_t nowMs);
    void startNetGame();
    void endNetGame();
    void stepNet(float dt);
    void driveRemote(Racer& r, float dt);
    bool netEvent(const Event& ev);
    void drawNetHud();
    void drawNetLines();
    void drawGold();
    void netSay(const std::string& text);
    void announce(const GoldEvent& e);
    Vec3 gridSlot(const Vec3& centre, float yaw, size_t slot) const;
    std::vector<Waypoint> _places; // the city's Cops & Robbers places (computer players cruise between them)
    uint8_t netIdOf(const Racer& r) const;
    std::vector<GoldGame::Car> netCars() const;
    void syncNetRacers();                                      // the cars as the session's players come and go
    void remoteLook(Racer& r, const NetCarState& s, float dt); // another's engine and damage
    void checkProgress(Racer& r, const NetCarState& s);        // the host's count of its checkpoints
    std::set<uint8_t> _unloadable;                             // players whose cars couldn't be loaded
    std::map<uint8_t, std::pair<size_t, float>> _progress;     // the host's count of each: next checkpoint, finish
    NetCarState carStateOf(const Racer& r) const;
    void planBot(Racer& r, float dt, const std::vector<GoldGame::Car>& cars);
};

} // namespace reluva::age
