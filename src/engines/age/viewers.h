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
#include "common/audio/mixer.h"
#include "common/system.h"
#include "common/gfx3d.h"
#include "engines/age/car.h"
#include "engines/age/car_actor.h"
#include "engines/age/city_life.h"
#include "engines/age/city_scene.h"
#include "engines/age/cops_robbers.h"
#include "engines/age/damage.h"
#include "engines/age/dashboard.h"
#include "engines/age/front_end.h"
#include "engines/age/game_menus.h"
#include "engines/age/help_viewer.h"
#include "engines/age/hud_map.h"
#include "engines/age/hud_pictures.h"
#include "engines/age/lang.h"
#include "engines/age/lesson.h"
#include "engines/age/record.h"
#include "engines/age/engine_sound.h"
#include "engines/age/music.h"
#include "engines/age/net_menus.h"
#include "engines/age/opponent.h"
#include "engines/age/options.h"
#include "engines/age/race.h"
#include "engines/age/replay.h"

#include "engines/age/road_graph.h"
#include "engines/age/games.h"
#include "engines/age/pkg.h"
#include "engines/age/render_cache.h"
#include "engines/age/texture.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
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
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
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

    ViewerContext& ctx_;
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
    size_t count() const override { return files_.size(); }
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

    std::vector<Row> files_;
    std::vector<std::string> folders_; // top-level folders, for Left/Right
    int folder_ = -1;                  // index into folders_, -1: all
    std::vector<size_t> shown_;        // files_ indices in the folder
    int selected_ = 0, top_ = 0;

    // The open file, if any.
    bool viewing_ = false, isText_ = false;
    std::string viewName_;
    std::vector<std::string> lines_;
    int lineTop_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void show(int index);
    void draw();

    std::vector<std::string> names_;
    int index_ = 0;
    int zoom_ = 0; // 0: fit the screen, else that many times the size
    bool alpha_ = true;
    Tex tex_;
    std::string error_;
};

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return names_.size(); }
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

    std::vector<std::string> names_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<TextureCache> textures_;
    Surface overlay_;

    Pkg pkg_;
    std::string error_;
    std::vector<std::string> levels_; // the model's levels of detail, finest first
    int level_ = 0;
    uint32_t paintJob_ = 0;
    ModelMesh mesh_;

    Vec3 center_{};
    float radius_ = 1;
    float yaw_ = 0, pitch_ = 0, distance_ = 1;
    bool dragging_ = false, showInfo_ = true, showLights_ = false;
    int lastX_ = 0, lastY_ = 0;
};

// A city flown through: its streets and buildings from the PSDL, the
// models placed in it, its sky, lit as at one of its 16 times and
// weathers. Keys in the header's comment.
class WorldViewer : public Page {
public:
    explicit WorldViewer(ViewerContext& ctx);
    ~WorldViewer() override;
    const char* name() const override { return "World"; }
    size_t count() const override { return cities_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    void load(int index);
    void resetView();
    void drawInfo(const std::string& stats);

    std::vector<CityInfo> cities_;
    int index_ = 0;
    int lighting_ = 4;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<CityScene> scene_;
    Surface overlay_;
    std::string error_;

    gfx::Vec3 eye_{};
    float yaw_ = 0, pitch_ = 0;
    bool dragging_ = false, showInfo_ = true, showModels_ = true;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0;
    uint32_t keys_ = 0; // movement keys held, bit per kMove*
};

// A car driven round a city: Reluva's simulation of it (car.h) against
// the city's collisions, seen from the car's own cameras, with its engine
// sound and a HUD. Keys in the header's comment.
class DriveViewer : public Page {
public:
    explicit DriveViewer(ViewerContext& ctx);
    ~DriveViewer() override;
    const char* name() const override { return "Drive"; }
    size_t count() const override { return cities_.empty() ? 0 : cars_.size(); }
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

    std::vector<CityInfo> cities_;
    std::vector<CarInfo> cars_;
    int city_ = 0, carIndex_ = 0;
    uint32_t paintJob_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<CityScene> scene_;
    CollisionWorld world_;
    size_t boundsUsed_ = 0;
    Surface overlay_;
    std::string error_;

    CarActor car_;
    EngineSound engine_;
    CarSounds sounds_;
    GameMusic music_;
    Dashboard dash_; // for the in-car camera
    HudMap hudMap_;
    HudPictures pictures_;
    int shift_ = 0;       // a gear change asked for (manual gears)
    bool manual_ = false; // manual gears (M)
    std::vector<CarCamera> cameras_;
    int camera_ = 0;
    Vec3 cameraPos_{};
    bool cameraReset_ = true;
    int lighting_ = 4;
    bool showInfo_ = true;

    CityLife life_;
    std::unique_ptr<FlareDrawer> flares_; // with the scene's textures
    std::unique_ptr<SmokePuffs> smoke_;   // a damaged car's
    CarWear wear_;                        // the player's car's damage
    RoadGraph roads_;
    int trafficLevel_ = 2; // none, light, normal, heavy
    bool cops_ = true;
    bool fill_ = true;     // place the traffic anywhere round the car next update
    SirenSound siren_;     // the player's, in a police car
    bool sirenOn_ = false;
    float clock_ = 0;
    float busted_ = 0;     // seconds the "busted" message has left
    float wrecked_ = 0;    // seconds the "wrecked" message has left
    uint32_t keys_ = 0; // driving keys held: DriveKeys
    uint64_t lastMs_ = 0;
};

// A race (Blitz, Circuit, Checkpoint) from the game's race files, against
// opponents driving their recorded paths. Keys in the header's comment.
class RaceViewer : public Page {
public:
    explicit RaceViewer(ViewerContext& ctx);
    ~RaceViewer() override;
    const char* name() const override { return "Race"; }
    size_t count() const override { return cities_.empty() || cars_.empty() ? 0 : 1; }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }
    bool takesKey(const Event& ev) const override;
    bool wantsToQuit() const override { return quit_; }

private:
    enum class State { Menu, Countdown, Racing, Finished };
    struct Racer {
        std::unique_ptr<CarActor> car;
        PathDriver driver; // opponents only
        std::string name;
        size_t next = 0;      // index into targets_ of the next checkpoint
        float finishTime = -1;
        bool player = false;
        uint8_t peer = kNoPlayer; // another player's car (multiplayer), driven by what its driver sends
        uint8_t botId = kNoPlayer; // a computer player the host drives (multiplayer)
        GoldBot bot;               // and its mind, outside the races
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
    void drawMap(float dt);
    void drawMenu();
    void drawRaceHud();
    void judgeLesson(float dt, bool checkpoint, float impact);
    // The race's message `offset` into its event's group of mmlang.dll's
    // strings, or `fallback`.
    std::string raceMessage(int offset, const std::string& fallback) const;
    void drawCentred(const std::string& text, int y, Color color, int scale);
    bool lesson() const { return race_.kind == RaceKind::CrashCourse; }
    // The race menu's Cruise, and the stunt under it (race_cruise.cpp).
    bool cruising() const { return race_.kind == RaceKind::Cruise && race_.index != kStuntRace; }
    bool stunt() const { return race_.kind == RaceKind::Cruise && race_.index == kStuntRace; }
    void startCruise();
    void stepCruise(float dt);
    void drawCruiseHud();
    float cruiseHold_ = 0;      // seconds busted or wrecked shows before the car's put back
    float stuntMessageFor_ = 0; // "Put the pedal to the metal!" after the start
    uint32_t stuntMessage_ = 0;
    Vec3 pierEnd_{}; // where the stunt's pier ends (the ground stops)
    const Waypoint& targetPoint(size_t k) const { return race_.waypoints[targets_[k]]; }

    std::vector<CityInfo> cities_;
    std::vector<CarInfo> cars_;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<CityScene> scene_;
    CollisionWorld world_;
    RoadGraph roads_;
    RoadGraph botRoads_; // the road map's (city/<map>.bai), for computer players
    CityLife life_;
    std::unique_ptr<FlareDrawer> flares_; // with the scene's textures
    std::unique_ptr<SmokePuffs> smoke_;   // a damaged car's
    CarWear wear_;                        // the player's car's damage
    bool fill_ = false;
    Surface overlay_;
    std::string error_;
    int loadedCity_ = -1;

    // The menu's choices.
    int field_ = 0, city_ = 0, kind_ = 0, raceIndex_ = 0, level_ = 0, car_ = 0;
    std::vector<std::string> raceNames_;

    State state_ = State::Menu;
    RaceDefinition race_;
    std::vector<size_t> targets_; // the waypoints to reach, in order (laps unrolled)
    std::vector<Racer> racers_;   // the player first
    float clock_ = 0;             // seconds since the start (negative counting down)
    std::string outcome_;
    EngineSound engine_;
    CarSounds sounds_;
    GameMusic music_;
    Dashboard dash_; // for the in-car camera
    HudMap hudMap_;
    HudPictures pictures_;
    int shift_ = 0;
    bool manual_ = false;
    std::vector<CarCamera> cameras_;
    int camera_ = 0;
    Vec3 cameraPos_{};
    bool cameraReset_ = true;
    uint32_t keys_ = 0;
    uint64_t lastMs_ = 0;
    float orbit_ = 0; // the menu's camera, round the start
    GameStrings strings_;
    LessonJudge judge_;
    std::string beatenAll_; // "You've beaten all the Blitz races!!", once they are
    float finalLapFor_ = 0; // seconds "Final lap!" has left
    int lastLap_ = 0;
    PlayerRecord record_; // the current driver's
    // The race's conditions as the menus changed them (else its own).
    FrontEnd::Choice::Conditions conditions_;
    bool conditionsChanged_ = false;
    DriverRoster roster_;
    void switchDriver();
    bool openAll_ = false;  // U: every race and car open, whatever the record says
    bool recorded_ = false; // this race's result is in the record

    std::unique_ptr<FrontEnd> front_;   // the game's menus, when their files are there
    std::unique_ptr<CarActor> showCar_; // the garage's car
    int showCarIndex_ = -1, showPaint_ = -1, paint_ = 0;

    bool raceIsOpen(int index) const;
    bool carIsOpen(int car) const;
    void recordResult();
    void menuEvent(const Event& ev);
    void drawGarageCar(float dt);
    void drawArrowTo(const Vec3& to, bool blitz);

    // Replays (race_replay.cpp): the race being driven is recorded; a
    // replay runs its race again from what was recorded.
    Replay recording_;
    Replay playback_;
    bool replaying_ = false;
    bool replayOfThisRace_ = false; // the instant replay of the race just driven (else one loaded)
    bool replaySaved_ = false;
    bool replayQuitting_ = false; // asked to save on the way out
    size_t replayFrame_ = 0;
    bool replayPaused_ = false;
    double replayAccum_ = 0;
    int replayCam_ = 0;   // the car's cameras, then (the last) the roadside one
    int replayFocus_ = 0; // the replay bar's button
    Vec3 tvAt_{};
    bool tvSet_ = false;
    float tvSide_ = 1;
    std::string replayNote_;
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
    enum class ReplayReturn { Menu, Results, Race };
    ReplayReturn replayReturn_ = ReplayReturn::Menu;

    // The race's own menus (race_menus.cpp): paused (Esc), and the
    // game's results screens; the menus' music and voices.
    bool paused_ = false, results_ = false;
    int pauseFocus_ = 0, resultsFocus_ = 0;
    std::string lastFocus_; // the menu control last spoken for
    std::unique_ptr<MenuPictures> menuPictures_;
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
    std::unique_ptr<GameMenus> game_; // the Options and the main menu's dialogs
    std::unique_ptr<HelpViewer> help_; // the navigation bar's Help
    bool quit_ = false;
    std::unique_ptr<NetMenus> net_;
    bool netMenus_ = false;     // the multiplayer menus are up
    bool netGame_ = false;      // playing a multiplayer game
    bool garageForNet_ = false; // the garage's open for the lobby's car
    uint64_t nowMs_ = 0;
    GoldGame gold_;
    uint32_t goldSeen_ = 0;                                  // the last gold event said
    std::map<std::string, std::vector<std::string>> speech_; // the announcer's Cops & Robbers lines
    std::string netMessage_;
    float netMessageFor_ = 0;
    int minutesSaid_ = 0;   // the last "N minutes remaining"
    bool chatting_ = false; // typing a line to the others
    std::string chatLine_;
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
    std::vector<Waypoint> places_; // the city's Cops & Robbers places (computer players cruise between them)
    uint8_t netIdOf(const Racer& r) const;
    std::vector<GoldGame::Car> netCars() const;
    void syncNetRacers();                                      // the cars as the session's players come and go
    void remoteLook(Racer& r, const NetCarState& s, float dt); // another's engine and damage
    void checkProgress(Racer& r, const NetCarState& s);        // the host's count of its checkpoints
    std::set<uint8_t> unloadable_;                             // players whose cars couldn't be loaded
    std::map<uint8_t, std::pair<size_t, float>> progress_;     // the host's count of each: next checkpoint, finish
    NetCarState carStateOf(const Racer& r) const;
    void planBot(Racer& r, float dt, const std::vector<GoldGame::Car>& cars);
};

} // namespace reluva::age
