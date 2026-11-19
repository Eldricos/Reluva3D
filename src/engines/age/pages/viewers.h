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

#include "engines/age/ui/screen.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * The first part of a name's file name, for jumping between kinds:
 * "geometry/vpbug.pkg" -> "vpbug", "texture/sw_stc_tan.tex" -> "sw".
 */
std::string kindOf(const std::string &name);

/**
 * A file's contents as lines to show: its text (tabs expanded, CR LF and
 * LF ends) if it looks like text, else a hex dump of 16 bytes a line.
 */
std::vector<std::string> fileLines(const std::vector<uint8_t> &data, bool &isText);

class FileViewer : public Page {
public:
    explicit FileViewer(ScreenContext &ctx);
    const char *name() const override { return "Files"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;
    bool takesKey(const Event &ev) const override;

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
    int _selected = 0;
    int _top = 0;

    // The open file, if any.
    bool _viewing = false;
    bool _isText = false;
    std::string _viewName;
    std::vector<std::string> _lines;
    int _lineTop = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ScreenContext &ctx);
    const char *name() const override { return "Textures"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

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
    explicit ModelViewer(ScreenContext &ctx);
    ~ModelViewer() override;
    const char *name() const override { return "Models"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; } // without one, it says so, paced as 2D

private:
    void show(int index);
    void build(); // the current level of detail and paint job, uploaded
    void resetView();
    void drawInfo();

    std::vector<std::string> _names;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
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
    float _yaw = 0;
    float _pitch = 0;
    float _distance = 1;
    bool _dragging = false;
    bool _showInfo = true;
    bool _showLights = false;
    int _lastX = 0;
    int _lastY = 0;
};

/**
 * A city flown through: its streets and buildings from the PSDL, the
 * models placed in it, its sky, lit as at one of its 16 times and
 * weathers. Keys in the header's comment.
 */
class WorldViewer : public Page {
public:
    explicit WorldViewer(ScreenContext &ctx);
    ~WorldViewer() override;
    const char *name() const override { return "World"; }
    size_t count() const override { return _cities.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; } // without one, it says so, paced as 2D

private:
    void load(int index);
    void resetView();
    void drawInfo(const std::string &stats);

    std::vector<CityInfo> _cities;
    int _index = 0;
    int _lighting = 4;
    gfx::Renderer3D *_renderer = nullptr;
    std::unique_ptr<CityScene> _scene;
    Surface _overlay;
    std::string _error;

    gfx::Vec3 _eye{};
    float _yaw = 0;
    float _pitch = 0;
    bool _dragging = false;
    bool _showInfo = true;
    bool _showModels = true;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
    uint32_t _keys = 0; // movement keys held, bit per kMove*
};

/**
 * A car driven round a city: Reluva's simulation of it (car.h) against
 * the city's collisions, seen from the car's own cameras, with its engine
 * sound and a HUD. Keys in the header's comment.
 */
class DriveViewer : public Page {
public:
    explicit DriveViewer(ScreenContext &ctx);
    ~DriveViewer() override;
    const char *name() const override { return "Drive"; }
    size_t count() const override { return _cities.empty() ? 0 : _cars.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; } // without one, it says so, paced as 2D

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
    int _city = 0;
    int _carIndex = 0;
    uint32_t _paintJob = 0;
    gfx::Renderer3D *_renderer = nullptr;
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
    PadShaker _shaker;       // a knock felt through it
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
    bool _fill = true; // place the traffic anywhere round the car next update
    SirenSound _siren; // the player's, in a police car
    bool _sirenOn = false;
    float _clock = 0;
    float _busted = 0;  // seconds the "busted" message has left
    float _wrecked = 0; // seconds the "wrecked" message has left
    uint32_t _keys = 0; // driving keys held: DriveKeys
    uint64_t _lastMs = 0;
};

} // namespace reluva::age
