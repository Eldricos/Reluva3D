// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/legoisland/formats/si.h"
#include "engines/legoisland/formats/wdb.h"
#include "engines/legoisland/pages/scene.h"
#include "graphics/gfx3d.h"
#include "image/image.h"
#include "video/flic.h"
#include "video/smacker.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// LEGO Island's data viewer, until the game itself runs. Its pages draw
// into a 1024x768 RGBA32 screen, larger than the game's 640x480 so a
// picture fits beside the list, or into 3D frames of that size. Tab goes
// to the next page.

namespace reluva::legoisland {

/** The viewer's screen, in pixels. */
///@{
constexpr int kViewerW = 1024;
constexpr int kViewerH = 768;
///@}

/** A page of the viewer: what it shows, and the keys and mouse on it. */
class ViewerPage {
public:
    virtual ~ViewerPage() = default;
    /** How many things it shows; the viewer skips a page with none. */
    virtual size_t count() const = 0;
    /** Draws the page as it's shown; a 2D page's screen is set up for it, a 3D page sets up its own. */
    virtual void activate() = 0;
    /** Called as another page is shown: stops what's playing, and lets go of what the renderer holds. */
    virtual void deactivate() = 0;
    /** A key pressed or the mouse used on the page. */
    virtual void handleEvent(const Event &ev) = 0;
    /** Called every frame; `nowMs` is the system's clock. */
    virtual void update(uint64_t nowMs) = 0;
    /** Whether it draws its own 3D frames rather than the screen's surface. */
    virtual bool uses3D() const { return false; }
};

/**
 * The SI files' objects, as a tree, and the selected one's details and
 * medium: a still drawn, an animation played, a sound played; a 3D
 * object's contents in a line, a texture drawn.
 *   Up/Down: previous/next object   PageUp/PageDown: jump 20
 *   Home/End: first/last   Left/Right: previous/next SI file
 *   M: media only, or every object   Space: play or stop
 *   A: play each sound or animation as it's selected
 */
class SiViewer : public ViewerPage {
public:
    /** `files` are the SI files to show; `mixer` may be null (no audio device): sounds aren't played. */
    SiViewer(System &sys, Surface &screen, std::vector<std::filesystem::path> files, audio::Mixer *mixer);
    size_t count() const override { return _files.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override {
        if (ev.type == EventType::kKeyDown)
            handleKey(ev.key);
    }
    /** Moves an animation on, and draws again, while one plays. */
    void update(uint64_t nowMs) override;

private:
    void handleKey(uint32_t key);

    struct Row {
        const SiObject *object;
        int depth;
    };
    void openFile(size_t index);
    void buildRows();
    void select(size_t row);
    void loadMedium();
    void loadObject(const SiObject &object, const std::vector<uint8_t> &data);
    void play();
    void stop();
    void redraw();
    void drawList(int x, int y, int w, int h);
    void drawDetails(int x, int y, int w);
    void drawPicture(int x, int y, int w, int h);

    System &_sys;
    Surface &_screen;
    std::vector<std::filesystem::path> _files;
    audio::Mixer *_mixer;
    size_t _file = 0;
    SiFile _si;
    std::string _fileError;
    std::vector<Row> _rows;
    size_t _current = 0;
    bool _mediaOnly = false;
    bool _autoPlay = false;

    // The selected medium, rebuilt as its file.
    std::string _mediumInfo;
    std::string _mediumError;
    size_t _pieces = 0;
    image::Image _still;
    std::unique_ptr<flic::Decoder> _flic;
    std::unique_ptr<smacker::Decoder> _smk;
    std::shared_ptr<audio::Sound> _sound;
    image::Image _frame; // an animation's last frame, in colour
    bool _animating = false;
    uint64_t _nextFrameMs = 0;
    audio::SoundHandle _playing = audio::kNoSound;
    uint64_t _lastDrawMs = 0;
};

/**
 * WORLD.WDB's textures, each drawn as large as fits, with its palette.
 *   Up/Down: previous/next texture   PageUp/PageDown: jump 20   Home/End: first/last
 */
class WdbTexturePage : public ViewerPage {
public:
    /** Reads every part's and model's textures from `wdb`, which must outlive the page. */
    WdbTexturePage(Surface &screen, const WdbFile &wdb);
    size_t count() const override { return _rows.size(); }
    void activate() override { redraw(); }
    void deactivate() override {}
    void handleEvent(const Event &ev) override;
    void update(uint64_t) override {}

private:
    struct Row {
        std::string owner; // the part or model it's in, or "shared"
        const WdbTexture *texture;
        size_t image;
    };
    void add(const std::string &owner, const std::vector<WdbTexture> &textures);
    void redraw();

    Surface &_screen;
    std::vector<WdbPart> _parts;
    std::vector<WdbModel> _models;
    std::vector<Row> _rows;
    std::vector<std::string> _errors;
    size_t _current = 0;
};

/**
 * WORLD.WDB's models and parts, drawn in 3D: a model's ROIs placed by its
 * animation's first keys, those taken from parts with the parts' LODs.
 *   Up/Down: previous/next   PageUp/PageDown: jump 20   Home/End: first/last
 *   L: a less detailed LOD (round to the finest)   R: the view reset
 *   I: the details shown or hidden   drag: turn   wheel or +/-: nearer, further
 */
class WdbModelPage : public ViewerPage {
public:
    /** `screen` is drawn on only to say why, if 3D rendering isn't available. */
    WdbModelPage(System &sys, Surface &screen, const WdbFile &wdb);
    ~WdbModelPage() override;
    size_t count() const override { return _items.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }

private:
    struct Item {
        std::string name;
        int model = -1;     // into WdbFile::allModels(), or
        int part = -1;      // into WdbFile::allParts(), or -1 with
        int sharedRoi = -1; // into the shared part's ROIs
    };
    void show(size_t index);
    void build();
    void drawInfo();

    System &_sys;
    Surface &_screen;
    const WdbFile &_wdb;
    WdbLibrary _lib;
    std::vector<WdbModelRef> _modelRefs;
    std::vector<WdbPartRef> _partRefs;
    std::vector<Item> _items;
    size_t _current = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::unique_ptr<RoiScene> _scene;
    Surface _overlay;

    WdbModel _model;
    std::string _error;
    int _lod = 0;                                      // how many levels less detailed than the finest
    std::vector<std::pair<size_t, gfx::Mat4>> _placed; // each piece, where it goes
    Orbit _orbit;
    bool _showInfo = true;
};

/**
 * The SI files' 3D models and animations, drawn: a model as WORLD.WDB's
 * are, an animation's actors (models, parts, characters made of the
 * shared part's pieces) placed by its nodes as it plays, round and round.
 *   Left/Right: previous/next SI file   Up/Down: previous/next object
 *   PageUp/PageDown: jump 20   Home/End: first/last   Space: pause or play
 *   C: through the animation's camera, if it moves one   L: less detail
 *   R: the view reset   I: the details shown or hidden   drag, wheel, +/-
 */
class SiScenePage : public ViewerPage {
public:
    /** `files` are the SI files; `screen` is drawn on only to say why, if 3D rendering isn't available. */
    SiScenePage(System &sys, Surface &screen, const WdbFile &wdb, std::vector<std::filesystem::path> files);
    ~SiScenePage() override;
    size_t count() const override { return _files.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }

private:
    // A piece drawn where a node is: the node's own place, or another's
    // with an offset from it.
    struct Binding {
        size_t piece = 0;
        std::string node;
        std::string parent;
        gfx::Mat4 offset = gfx::identity();
    };
    void openFile(size_t index);
    void show(size_t index);
    void build();
    void bindModel(const WdbModel &model, const std::string &actorNode);
    void bindCharacter(const WdbAnimNode &node);
    bool findModel(const std::string &name, WdbModel &out) const;
    gfx::Mat4 placeOf(const Binding &b, const NodePlaces &places) const;
    void drawInfo();

    System &_sys;
    Surface &_screen;
    WdbLibrary _lib;
    std::vector<std::filesystem::path> _files;
    size_t _file = 0;
    SiFile _si;
    std::string _fileError;
    std::vector<const SiObject *> _objects; // the open file's animations and models
    size_t _current = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::unique_ptr<RoiScene> _scene;
    Surface _overlay;

    std::string _error;
    bool _isAnimation = false;
    WdbAnimation _animation; // a model's own animation, for a model
    WdbModel _model;
    std::vector<std::unique_ptr<WdbModel>> _actorModels; // the bindings' ROIs point into them
    std::vector<Binding> _bindings;
    std::vector<std::string> _drawnAs; // what each actor was drawn as
    int _lod = 0;
    bool _paused = false;
    bool _cameraView = false;
    uint32_t _time = 0; // in milliseconds
    uint64_t _lastMs = 0;
    Orbit _orbit;
    bool _showInfo = true;
};

} // namespace reluva::legoisland
