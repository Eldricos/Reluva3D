// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/system.h"
#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/types.h"
#include "engines/newworlds/briefing.h"
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/fif.h"
#include "engines/newworlds/game_ui.h"
#include "engines/newworlds/colony.h"
#include "engines/newworlds/economy.h"
#include "engines/newworlds/model_pose.h"
#include "engines/newworlds/model_render.h"
#include "engines/newworlds/star_system.h"
#include "engines/newworlds/world.h"
#include "engines/newworlds/geo.h"
#include "engines/newworlds/gmf.h"
#include "engines/newworlds/levels.h"
#include "engines/newworlds/mdl.h"
#include "engines/newworlds/mission_script.h"
#include "engines/newworlds/model_textures.h"
#include "engines/newworlds/soft3d.h"
#include "engines/newworlds/string_table.h"
#include "engines/newworlds/texture.h"

#include <filesystem>
#include <functional>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Data viewers used by the New Worlds engine until the game itself runs.
// Each draws into the engine's 640x480 RGBA32 screen and reports its state in
// the window title.

namespace reluva::newworlds {

// Browses every texture.
//   Left/Right: previous/next   PageUp/PageDown: jump 10   Home/End: first/last
//   F: filter (all, failed, colour-keyed, with alpha)
class TextureViewer {
public:
    TextureViewer(System &sys, Surface &screen, const Archives &archives);
    size_t count() const { return _textures.size(); }
    void activate(); // redraw and retitle after switching to this viewer
    void handleKey(uint32_t key);

private:
    struct TextureRef {
        EntryRef ref;
        bool ok = false;
        bool colourKeyed = false;
        bool alpha = false;
    };

    void classify();
    void applyFilter(int filter);
    void show(int index);
    void fill(int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void drawCheckerboard();
    void drawTexture(const Texture &tex);

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    std::vector<TextureRef> _textures;
    std::vector<size_t> _visible; // indices into textures_ that pass the filter
    bool _classified = false;
    int _filter = 0;
    int _current = 0;
};

// Shows every model with its textures. A model's textures are the ones named
// after it (k_bop: k_bop_a, k_bop_b, k_bop_g), in name order; each face picks
// one with the last value of its last corner. See docs/engines/newworlds/geo.md.
//   T: textures on/off   G: alpha layers (as glow, ignored, as transparency)
//   Left/Right: previous/next   PageUp/PageDown: jump 10   Home/End: first/last
//   Mouse drag or W/A/S/D: rotate   wheel or +/-: zoom   R: reset the view
//   Space: spin   P: show one part at a time   U: texture-coordinate checks
//   C: colour by texture number instead of texturing   H: smooth or flat shading
//   M: the model's points (from its .mdl) on/off   N: select the next point
//   K: play the next animation that fits the model (.arf frames), then none
//   L: outlines (solid, solid + outlines, outlines only)
//   B: back faces (hidden, shown, only back faces)   F: filter (all, multi-part, failed)
class ModelViewer {
public:
    ModelViewer(System &sys, Surface &screen, const Archives &archives);
    size_t count() const { return _models.size(); }
    void activate();
    void handleEvent(const Event &ev);
    void update(uint64_t nowMs); // redraws when something changed

private:
    void classify();
    void applyFilter(int filter);
    void show(int index);
    void resetView();
    void render();
    void updateTitle();
    void drawLine(float x0, float y0, float x1, float y1, const uint8_t *colour = nullptr);
    void loadTextures(const std::string &modelName);
    void loadMetamodel(const std::string &modelName);

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    Rasterizer _raster;
    std::vector<EntryRef> _models;
    std::vector<uint32_t> _partCounts; // per model; 0 when it doesn't decode
    std::vector<size_t> _visible;
    std::vector<TextureFile> _textureFiles; // every full-size texture
    std::vector<Texture> _modelTextures;    // the current model's, in face order
    std::vector<std::string> _modelTextureNames;
    bool _classified = false;
    int _filter = 0;
    int _current = 0;

    GeoModel _model;
    std::string _status; // decode error, if any
    Metamodels _metamodels;
    MdlModel _mdl;
    bool _hasMdl = false;
    bool _markers = true;
    bool _turret = false;
    int _selectedPoint = -1; // -1: none
    std::vector<Animation> _animations;
    // -1: the stored pose; animations_.size(): the turret sweep
    int _animation = -1;
    int _animFrame = 0;
    uint64_t _animMs = 0;
    static constexpr uint64_t kFrameMs = 100;
    float _centre[3] = {0, 0, 0};
    float _size = 1; // distance from the centre to the farthest vertex

    float _yaw = 0;
    float _pitch = 0;
    float _zoom = 1;
    int _soloPart = -1; // -1: all parts
    bool _spin = false;
    bool _uvCheck = false;
    bool _colourByTail = false;
    bool _textured = true;
    bool _smooth = true;
    // faces_: 0 front only, 1 both sides, 2 back only
    int _outlines = 0;
    int _faces = 0;
    int _alphaMode = 0; // see kAlphaNames
    bool _dragging = false;
    int _dragX = 0;
    int _dragY = 0;
    bool _dirty = true;
    uint64_t _lastMs = 0;
};

// Shows the planet maps (.gmf), built from their ground tiles: a map is a
// grid of blocks, a block 2 x 2 tiles, and a tile one of the planet's ground
// models. See docs/engines/newworlds/gmf.md.
//   Left/Right: previous/next map   Mouse drag: rotate   wheel or +/-: zoom
//   W/A/S/D: move   Q/E: turn   R: reset the view   L: outlines   K: walls
//   O: placed objects (trees, rocks) on/off   H: smooth or flat shading
//   T: textures on/off   G: colouring (the models' own textures; the map's
//   terrain codes in its colour table; textures tinted by resource deposit)
class MapViewer {
public:
    MapViewer(System &sys, Surface &screen, const Archives &archives);
    // level.dat's missions, to name each map's mission in the title.
    void setLevels(std::vector<Level> levels) { _levels = std::move(levels); }
    size_t count() const { return _maps.size(); }
    void activate();
    void handleEvent(const Event &ev);
    void update(uint64_t nowMs); // redraws when something changed

    // For the missions page, which shows its maps through this viewer:
    // shows the map with that name ("map/f1.gmf", in any archive, ignoring
    // case); false if there's none.
    bool showMap(std::string_view name);
    // The archive the shown map is in ("icewrld.cat").
    std::string mapArchive() const;
    // Called after each redraw, to draw over the map.
    void setOverlay(std::function<void()> overlay) { _overlay = std::move(overlay); }
    // Given the title this viewer would set, returns the one to set instead.
    void setTitleFilter(std::function<std::string(const std::string &)> filter) { _titleFilter = std::move(filter); }
    void redraw() { _dirty = true; }
    // How the ground is coloured (0 textures, 1 terrain codes, 2 deposits).
    int colouring() const { return _colouring; }
    // Points the camera at a map position as the game gives them (eighths
    // of a block, row first; see docs/engines/newworlds/gmf.md), close and
    // low for a ground view or high above for an overview; a < 0 for the
    // whole map. turn() turns it about its target.
    void lookAt(int a, int b, bool ground);
    void turn(float radians) {
        _yaw += radians;
        _dirty = true;
    }
    void resetCamera() { resetView(); }
    // For the game interface: the map drawn small by its terrain codes'
    // colours, with the camera's target marked; the target as a map
    // position (eighths of a block, row first); the camera's heading in
    // degrees (0 when unturned, clockwise).
    void drawMinimap(Surface &s, int x, int y, int w, int h) const;
    // A world point's pixel on a minimap drawn at x, y, w, h.
    void minimapPoint(float wx, float wy, int x, int y, int w, int h, int &px, int &py) const;
    void cameraPosition(int &a, int &b) const;
    float heading() const;
    void setColouring(int colouring) {
        _colouring = colouring;
        _dirty = true;
    }
    // Buildings to stand on the shown map (the missions page's starting
    // colony): a model by name ("geo/fed/fch.geo", in any archive) at a map
    // position, turned anticlockwise by quarter turns. Cleared by showMap.
    struct Building {
        std::string model;
        int a = 0;
        int b = 0;
        int turn = 0;
        float progress = 1;  // under construction: 0 just begun, 1 built
        std::string builder; // the construction bee working on it ("geo/fed/fconbee.geo"), or none
    };
    void setBuildings(const std::vector<Building> &buildings);
    // A building's construction moved on (by its index in setBuildings').
    // The recordings show an apron first, then the building rising out of
    // it (play-tutorials.md): here its model flat, then growing upwards.
    void setBuildingProgress(size_t i, float progress);
    // The hologram of a building being placed (play): its model, tinted
    // green where it can go and red where it can't; model empty for none.
    void setGhost(const std::string &model, int a, int b, bool valid);
    // Tiles (x east, y north) drawn with the planet's concrete texture
    // instead of their own, as under a starting colony. Cleared by showMap.
    void setPaved(const std::vector<std::pair<int, int>> &tiles);
    // The shown map's size in blocks, a block's resource deposit (0 for
    // none), and the ground's height at a map position (NaN off the map
    // or where no tile is).
    int blocksWide() const { return _map.width; }
    int blocksHigh() const { return _map.height; }
    int depositAt(int bx, int by) const;
    // The cell value's low 12 bits at a block (50-300; how much a deposit
    // holds, presumably; gmf.md).
    int depositAmount(int bx, int by) const;
    float groundAt(int a, int b);
    // A building model's size across x and y (0 if it isn't there).
    std::pair<float, float> modelSize(const std::string &name);

    // For play (the missions page): units to draw this frame, by model, at
    // world positions (x east, y north), standing on the ground; the
    // selected ones with the recordings' row of orange marks above them.
    struct UnitDraw {
        std::string model;
        // heading: radians anticlockwise from east
        float x = 0;
        float y = 0;
        float heading = 0;
        bool selected = false;
        // Its turret turned from straight ahead (anticlockwise) and its gun
        // raised, in radians, for a model with one.
        float turretYaw = 0;
        float gunPitch = 0;
    };
    void setUnits(std::vector<UnitDraw> units) {
        _units = std::move(units);
        _dirty = true;
    }
    // The ground's height at a world point (0 off the map); the map's size
    // in world units.
    float groundAtWorld(float x, float y) { return groundHeight(x, y); }
    float worldWidth() const { return float(_map.width) * kBlockUnits; }
    // The ground for units: each tile's movement class (its flags' bits
    // 16-17) and the ground's height (gmf.md); empty with no map shown.
    Terrain terrain();
    float worldHeight() const { return float(_map.height) * kBlockUnits; }
    // The world point under a screen pixel (false if the ray misses the
    // map), and a world point's pixel (false if behind the camera).
    bool screenToGround(int sx, int sy, float &x, float &y);
    bool worldToScreen(float x, float y, float z, int &sx, int &sy) const;

    // The game's camera (play-tutorials.md): the bird's-eye view, F2's sky
    // view, F1's first-person view; arrows move, numpad 4/6 turn, 8/2 zoom
    // towards the pointer, 7/1 raise and lower, 9/3 tilt. A key it doesn't
    // use returns false. mouseX/Y: where the pointer is, for the zoom.
    enum class View { kBirdsEye, kSky, kFirstPerson };
    bool gameKey(uint32_t key, int mouseX, int mouseY);
    void setView(View v);
    View view() const { return _view; }
    // Ctrl+click: zoom in on the pointed spot, or out.
    void zoomAt(int sx, int sy, bool in);
    // Centres the view on a world point, keeping the view's height.
    void centreOn(float x, float y);
    // Double-click on a building: the camera closes in and orbits it (4/6
    // turn about it) until unlocked.
    void lockOn(float x, float y);
    void unlock() { _locked = false; }
    bool locked() const { return _locked; }
    static constexpr float kBlockUnits = 16384; // a block's size in model units
    size_t buildingsShown() const { return _buildings.size(); }

private:
    using Vec3 = GeoModel::Vec3;
    struct TileModel {
        std::string name;  // e.g. "geo/map/ice/ice_07.geo"
        std::string error; // when it doesn't decode
        GeoModel geo;
        std::vector<Vec3> faceNormals; // per face, unit length
        std::vector<Texture> textures; // in the order the faces number them
        std::vector<std::string> textureNames;
    };

    // A tile's edge heights after its rotation, at 5 points along each side
    // (east, north, west, south); a side without vertices on it is missing.
    using TileEdges = TileSides;

    // A placed object, ready to draw.
    struct Placed {
        Vec3 position;
        float angle; // anticlockwise, in radians
        const TileModel *model;
        float progress = 1;
        std::string builder;
    };
    // A model's moving parts: its metamodel, animations and turret.
    struct Motion {
        MdlModel mdl;
        std::vector<Animation> animations;
        bool turret = false;
    };
    const Motion &motionOf(const std::string &model);

    void show(int index);
    void resetView();
    void render();
    void updateTitle();
    // An entry of the map's archive; ground tiles get textures by role, other
    // models those named after them.
    void loadModel(size_t entry, TileModel &m, bool ground);
    // The rest of loading a model, once read; buildings get their doors'
    // and aprons' textures too.
    void prepareModel(const std::vector<uint8_t> &data, TileModel &m, bool ground, bool building);
    // A model by name, loaded once; buildings get their doors' and aprons'
    // textures.
    const TileModel &namedModel(const std::string &name, bool building = true);
    const TileModel &tileModel(uint32_t index);
    float groundHeight(float x, float y);
    void placeScenery();
    void makeUnderlay();
    const TileEdges &tileEdges(uint32_t tile);
    int tileAt(int tx, int ty) const; // tile record at a tile position, or -1
    static unsigned terrainCode(const GmfMap::Tile &tile, int cx, int cy);

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    Rasterizer _raster;
    std::vector<EntryRef> _maps;
    std::vector<Level> _levels;
    std::vector<TextureFile> _textureFiles;
    bool _shown = false;
    int _current = 0;

    GmfMap _map;
    std::string _status;                // decode error, if any
    std::vector<size_t> _archiveModels; // entry indices of the map archive's .geo files, by model number
    std::map<uint32_t, TileModel> _tileModels;
    std::map<uint32_t, TileEdges> _tileEdges;
    Texture _wallTexture;                    // the map archive's last cliff texture, if any
    std::map<int, TileModel> _sceneryModels; // by object type
    std::vector<Placed> _placed;
    std::map<std::string, TileModel> _buildingModels; // by name, across maps
    std::vector<Placed> _buildings;
    std::vector<bool> _hidden; // placed_ objects under a building or on paved_
    std::vector<UnitDraw> _units;
    Metamodels _metamodels;
    std::map<std::string, Motion> _motions;
    uint64_t _animClockMs = 0;
    std::optional<Placed> _ghost;
    bool _ghostValid = false;
    View _view = View::kBirdsEye;
    bool _locked = false;
    std::vector<std::array<float, 4>> _buildingAreas; // middle x, y and half sizes, in model units
    void updateHidden();
    std::set<std::pair<int, int>> _paved;
    std::map<uint32_t, TileModel> _pavedModels; // tile models with the concrete texture throughout
    const TileModel &pavedModel(uint32_t index);
    Texture _underlay; // ground showing through tiles with GmfMap::Tile::underlay

    float _targetX = 0;
    float _targetY = 0;
    float _targetZ = 0;
    float _yaw = 0;
    float _pitch = 0;
    float _distance = 1;
    bool _textured = true;
    bool _outlines = false;
    bool _walls = true;
    bool _scenery = true;
    bool _smooth = true;
    int _colouring = 0; // see kColouringNames
    bool _dragging = false;
    int _dragX = 0;
    int _dragY = 0;
    bool _dirty = true;
    unsigned _drawnTiles = 0;
    unsigned _drawnFaces = 0;
    unsigned _drawnObjects = 0;
    std::function<void()> _overlay;
    std::function<std::string(const std::string &)> _titleFilter;
};

// Plays the sounds (.wav) and shows their waveforms.
//   Left/Right: previous/next   PageUp/PageDown: jump 10   Home/End: first/last
//   Space or Enter: play/stop   A: play each sound on selecting it
class SoundViewer {
public:
    // gameDir: the game folder, whose loose .mp3 and .wav files (Media/) are
    // listed after the archives' sounds; empty for none. mixer: the engine's,
    // playing into the System's output; nullptr when there's no output.
    SoundViewer(System &sys, Surface &screen, const Archives &archives, audio::Mixer *mixer,
                const std::filesystem::path &gameDir = {});
    size_t count() const { return _sounds.size(); }
    void activate();
    void deactivate(); // stops playing when switching to another viewer
    void handleKey(uint32_t key);
    void update(uint64_t nowMs);

private:
    void show(int index);
    void play();
    void stop();
    bool playing() const;
    void draw();
    void updateTitle();
    const std::string &nameOf(size_t sound) const;
    std::string archiveOf(size_t sound) const; // its archive's file name

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    // A sound in an archive, or a loose file in the game folder.
    struct Item {
        EntryRef ref{0, 0};
        std::filesystem::path file; // empty for an archive's
        std::string name;           // the archive's name for it, or the path in the game folder
    };
    Error read(size_t sound, std::vector<uint8_t> &data) const;
    std::vector<Item> _sounds;
    // The archives store some sounds many times over (kliactor has kneg1.wav
    // 24 times, in 4 slots of each vehicle's set), sometimes under other
    // names: each sound's first copy, and the sounds listed (all, or only
    // the first of each set of copies).
    std::vector<size_t> _firstCopy;
    std::vector<size_t> _shownSounds;
    bool _showCopies = false;
    bool _shown = false;
    bool _autoPlay = false;
    bool _wasPlaying = false;
    int _current = 0;
    std::string _status;
    std::shared_ptr<const audio::Sound> _sound; // the one shown
    audio::Mixer *_mixer;                       // one sound at a time here
    audio::SoundHandle _playing = audio::kNoSound;
};

// Shows the string tables (.stb), word-wrapped, with their ids.
class StringViewer {
public:
    StringViewer(System &sys, Surface &screen, const Archives &archives);
    size_t count() const { return _tables.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    struct Span {
        std::string text;
        Color color;
    };
    struct Line {
        std::string id; // on a string's first line
        std::vector<Span> spans;
    };

    void show(int index);
    void layout(); // wraps the table into lines_ for the screen's width
    void draw();
    void updateTitle();
    int rows() const; // lines that fit on the screen
    // Text in the current font: Reluva's own, or one of the game's.
    int measure(std::string_view text) const;
    int lineHeight() const;
    int drawSpan(int x, int y, std::string_view text, Color color);

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    std::vector<EntryRef> _tables;
    std::vector<GameFont> _fonts;
    int _font = -1; // index in fonts_, -1 for the built-in font
    bool _shown = false;
    int _current = 0;
    int _top = 0;
    int _scale = 1;
    StringTable _table;
    std::string _status;
    std::vector<Line> _lines;
};

// Shows the game's fonts: each one's characters and some sample text.
class FontViewer {
public:
    FontViewer(System &sys, Surface &screen, const Archives &archives);
    size_t count() const { return _fonts.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void draw();
    void updateTitle();

    System &_sys;
    Surface &_screen;
    std::vector<GameFont> _fonts;
    int _current = 0;
    int _scale = 2;
    bool _boxes = false; // outline each glyph's box
};

// Shows the missions level.dat lists: each one's map (through a MapViewer,
// so its keys move the view), its briefing pages from the side's string
// table, its voice clips (Media/FedVO, KlingVO, RomVO) and its planet's
// music. The maps hold no units, buildings or colonies (see
// docs/engines/newworlds/gmf.md); where the missions put them isn't known.
//   Left/Right: previous/next mission   Up/Down: briefing page
//   B: briefing on/off   J: side (joint missions)
//   Space: play/stop the voice clip   N/P: next/previous clip (and play it)
//   M: the planet's music on/off   and the map viewer's keys
//   Enter: play the briefing (its pages, views and clips in turn, as
//   recordings of play showed them), or skip to its next step
//   Backspace: stop the briefing
//   X: play the mission's script a step at a time, in the order the
//   recording showed its events (the messages and objectives as they
//   change); Z: stop it
//   I: the game's interface over the map (its panels in the side's style,
//   with the script's messages and objectives; not playable)
//   C: the colony on/off (its hub where the recordings put it, and the
//   buildings the script builds; see colony.h)   V: look at the colony
//   F3: play on the map (the game's camera and controls, the colony's
//   starting units to select and move; world.h): Escape or F3 leaves
class MissionViewer {
public:
    MissionViewer(System &sys, Surface &screen, const Archives &archives, std::vector<Level> levels,
                  audio::Mixer *mixer, const std::filesystem::path &gameDir);
    size_t count() const { return _missions.size(); }
    void activate();
    void deactivate(); // stops the sound when switching to another viewer
    void handleEvent(const Event &ev);
    void update(uint64_t nowMs);
    // Playing: Tab and Escape are the game's, not the data viewer's.
    bool playing() const { return _playing; }

private:
    struct Page {
        std::string heading; // without the ~s; empty for the title page
        std::string text;
        uint32_t id = 0; // the string's (the first one's, for plain strings run together)
    };

    void show(int index);
    void loadSide(); // the briefing and clips for the mission's current side
    void drawBriefing();
    void drawNoMap();
    std::string title(const std::string &mapTitle) const;
    void updateTitle();
    void redraw();
    // The briefing player: steps run one after another, each as long as its
    // clip (a few seconds when it has none, or its clip is missing).
    void drawInterface();
    void stepScript();  // the next scripted event (X)
    void startColony(); // the hub alone, for the mission's side
    // Play: the game's controls over the map.
    void startPlay();
    void stopPlay();
    bool playEvent(const Event &ev); // true if play used it
    void showUnits();
    void drawPlay(); // the selection box, the full-screen Tricorder
    bool hubPosition(float &x, float &y) const;
    // Building in play (B): the menu's types for the side, the hologram
    // under the pointer, placing it, and its construction drawn.
    std::vector<const BuildingType *> buildMenu() const;
    void choose(const BuildingType &t);
    void updateGhost();
    void placeBuilding();
    void showProgress();
    void deliver(const ScriptEvent &fired, const std::vector<MissionRun::Output> &out);
    void showColony(); // hands the colony's buildings to the map
    void stopScript();
    void startBriefing();
    void stopBriefing();
    void startStep(int step);
    bool mapInView() const; // the map is drawn (not a briefing backdrop)
    void drawBackdrop(BriefingView view);
    // The game's own art for the briefing's scenes, when the archives have
    // it (glblgui.cat, glblgame.cat): each side's emblem screen, and the
    // Tabula Rasa galaxy model with its textures.
    void loadBriefingArt();
    bool drawEmblem(char side);
    bool drawGalaxy();
    void loadStarSystem(); // the one the mission's table names
    bool drawStarSystem();
    // A model turning on the grid, with its textures (loaded once).
    bool drawModel(const std::string &name);
    struct BriefingModel {
        TexturedModel model;
        bool ok = false;
    };
    std::map<std::string, BriefingModel> _models;
    std::vector<TextureFile> _textureFiles;
    void playClip(int index);
    void stopClip();
    void startMusic(); // the shown map's planet theme, looped
    void stopMusic();
    bool clipPlaying() const;
    // Reads and decodes one of the game folder's sound files.
    std::shared_ptr<const audio::Sound> loadSound(const std::string &stem);
    // Text in the briefing's font: the game's fe8pt if there, else Reluva's.
    int measure(std::string_view text) const;
    int lineHeight() const;
    void drawText(int x, int y, std::string_view text, Color color);

    System &_sys;
    Surface &_screen;
    const Archives &_archives;
    std::vector<Level> _missions;
    audio::Mixer *_mixer;
    MapViewer _map;
    std::vector<GameFont> _fonts;
    const GameFont *_font = nullptr;
    // The game folder's loose MP3s, by upper-case stem (F1005 -> its path).
    std::map<std::string, std::filesystem::path> _mediaFiles;

    bool _shown = false;
    bool _hasMap = false;
    bool _dirty = true;
    bool _briefing = true;
    bool _music = false;
    bool _wasPlaying = false;
    int _current = 0;
    int _side = 0;
    int _page = 0;
    int _clip = 0;
    std::string _mapName;
    std::string _mapNote;
    std::string _mapTitle;
    std::string _tableName;
    std::string _status;
    std::string _soundStatus;
    std::vector<Page> _pages;
    std::vector<std::string> _clips; // stems, in order
    StringTable _table;
    std::vector<BriefingStep> _script;
    std::map<char, Texture> _emblems; // 640 x 480, by side
    GeoModel _galaxy;
    std::vector<Texture> _galaxyTextures;  // in the order its faces number them
    std::vector<std::string> _systemNames; // geo/solsys/'s systems
    std::string _systemName;
    GeoModel _system;
    StarSystemLayout _systemLayout;
    std::vector<Texture> _systemTextures; // per texture number
    Rasterizer _raster;
    bool _running = false;
    bool _stepOnMap = false;
    // The mission's script, being walked through.
    std::unique_ptr<MissionRun> _run;
    std::vector<ScriptEvent> _walk;
    size_t _walkPos = 0;
    std::vector<std::pair<std::string, std::string>> _log; // sender, text
    std::string _walkNote;
    GameUi _ui;
    char _uiSide = 0;
    std::unique_ptr<ColonyLayout> _colony;
    std::unique_ptr<World> _world;
    std::unique_ptr<Economy> _economy;
    std::vector<int> _structureIds; // per colony building, its economy structure
    bool _buildMenu = false;
    bool _ghostOk = false;
    std::string _placing; // the type being placed, or empty
    // where its hologram stands
    int _ghostA = -1;
    int _ghostB = -1;
    bool _playing = false;
    bool _boxing = false;
    bool _tricorderFull = false;
    int _mouseX = 0;
    int _mouseY = 0;
    int _boxX = 0;
    int _boxY = 0;
    uint64_t _lastTickMs = 0;
    ColonySite _colonySite;
    bool _colonyShown = true;
    bool _interface = false;
    int _step = 0;
    int _savedColouring = 0;
    // stepEndMs_: when a step without a playing clip ends
    uint64_t _nowMs = 0;
    uint64_t _stepEndMs = 0;
    uint64_t _lastTurnMs = 0;
    audio::SoundHandle _clipHandle = audio::kNoSound;
    audio::SoundHandle _musicHandle = audio::kNoSound;
    std::string _musicName;
};

} // namespace reluva::newworlds
