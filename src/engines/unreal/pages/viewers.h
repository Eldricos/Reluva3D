// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Unreal Engine 1 data viewer: pages showing what a game's packages
// hold, while the renderer and the game's script are written. Tab switches
// pages, Esc returns to the launcher.
//
// Maps:     a level's BSP drawn by the GPU. Left/Right pick a map; drag to
//           look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift to
//           go faster, R back to the player start, I to show or hide the
//           info, T for textures or plain shading, L for the level's
//           lightmaps or shading by facing, [ and ] to dim or brighten
//           the lightmaps, - and = to change the display's contrast (its
//           gamma; to compare with the original), O to move the movers
//           (doors, lifts) on to their next keyframe, K to show or hide
//           the sky, N to play or stop the meshes' animations.
//           Enter plays the level: its scripts run live with the player
//           in it, seen from the game's own camera. W/S move, A/D step
//           aside, Left/Right turn, Up/Down look, drag to turn and look,
//           Space jumps, Ctrl or F fires; V lets the camera loose (to
//           fly round the level as it plays) and back; Enter stops.
// Textures: Left/Right step through a package's textures, PageUp/PageDown
//           pick the package (.utx, then maps' own, then .u).
// Meshes:   every Mesh and LodMesh. Left/Right step, PageUp/PageDown pick
//           the package; drag to turn, the wheel zooms, A plays or stops
//           the animation, [ and ] pick it, R resets the view, I shows or
//           hides the info.
// Sounds:   Left/Right step, PageUp/PageDown pick the package, Space plays.
// Movies:   the game's movies (its movies/*.avi; the intro plays first
//           when the viewer starts). Left/Right pick one, Space or Enter
//           plays it; Space, Enter or Esc stops it.
// Packages: every package on the search path. Left/Right pick one,
//           Up/Down move through its exports (PageUp/PageDown, Home/End),
//           Enter shows the selected export's properties.

#include "engines/unreal/render/level_view.h"
#include "engines/unreal/render/view.h"
#include "engines/unreal/ui/movie.h"

namespace reluva::unreal {

/** The Packages page: every package's exports, and their properties. */
class PackageViewer : public Page {
public:
    /** Lists every package in the set. */
    explicit PackageViewer(ViewContext &ctx);
    const char *name() const override { return "Packages"; }
    size_t count() const override { return _names.size(); }
    void activate() override { draw(); }
    void handleEvent(const Event &ev) override;

private:
    void draw();

    std::vector<std::string> _names;
    int _index = 0;
    int _selected = 0;
    int _top = 0;
    bool _showProps = false;
};

/** Steps through the exports of some classes, package by package. */
class ObjectBrowser {
public:
    /**
     * Over the named packages, in order; `wanted` says by its class name
     * whether an export is one to step through. Nothing is loaded until
     * the first step.
     */
    ObjectBrowser(const PackageSet &packages, std::vector<std::string> names, bool (*wanted)(std::string_view));
    /** How many packages it was given, with wanted exports or not. */
    size_t packageCount() const { return _names.size(); }
    /**
     * Moves within the package (by objects) or between packages (skipping
     * those with none); false if there's nothing.
     */
    ///@{
    bool step(int objects);
    bool stepPackage(int packages);
    ///@}
    /** Where it is: the package and the export; nullptr and -1 at none. */
    ///@{
    const Package *package() const;
    int exportIndex() const;
    ///@}
    /** "3/40 in Package (2/12)". */
    std::string where() const;

private:
    bool load(int package);

    const PackageSet &_packages;
    bool (*_wanted)(std::string_view);
    std::vector<std::string> _names;
    int _package = -1;
    std::vector<int> _objects;
    int _object = 0;
};

/** The Textures page: each texture, its animation played if it has one. */
class TextureViewer : public Page {
public:
    /** Over the .utx packages, then the maps', then the .u ones. */
    explicit TextureViewer(ViewContext &ctx);
    const char *name() const override { return "Textures"; }
    size_t count() const override { return _browser.packageCount(); }
    void activate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    void select();
    void draw();
    ObjectBrowser _browser;
    bool _started = false;
    image::Image _img;
    TextureInfo _info;
    std::string _error;
    std::unique_ptr<AnimatedTexture> _anim; // the texture's animation, if it has one
    bool _paused = false;
    uint64_t _lastMs = 0;
};

/** The Sounds page: each Sound object, played through the mixer. */
class SoundViewer : public Page {
public:
    /** Over the .uax packages, then the .umx ones, then the maps and the .u ones. */
    explicit SoundViewer(ViewContext &ctx);
    ~SoundViewer() override;
    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _browser.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;

private:
    void draw();
    void play();
    ObjectBrowser _browser;
    bool _started = false;
    std::string _status;
    audio::SoundHandle _playing = audio::kNoSound;
};

/** The Meshes page: each Mesh and LodMesh, drawn by the GPU and animated. */
class MeshViewer : public Page {
public:
    /** Over the .u packages first (where meshes usually are), then the maps, then the .utx ones. */
    explicit MeshViewer(ViewContext &ctx);
    ~MeshViewer() override;
    const char *name() const override { return "Meshes"; }
    size_t count() const override { return _browser.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }

private:
    void show();
    void unload();
    void resetView();

    ObjectBrowser _browser;
    bool _started = false;
    gfx::Renderer3D *_renderer = nullptr;
    Mesh _mesh;
    GpuMesh _gpu;
    std::vector<gfx::TextureId> _textures;
    std::string _error;
    std::vector<std::string> _info;
    Surface _overlay;
    int _anim = -1; // into _mesh.anims
    // The animations to step through: the mesh's own, then (built on bones)
    // those of the shared sets its list names, borrowed when picked.
    std::vector<std::string> _choices;
    int _choice = -1;
    size_t _ownAnims = 0; // of _choices, the mesh's own
    size_t _ownAnimCount = 0;
    int32_t _ownFrames = 0;
    void pick(int choice);
    bool _playing = true;
    bool _showInfo = true;
    bool _dragging = false;
    float _time = 0;
    float _yaw = 0;
    float _pitch = 0.3f;
    float _distance = 200;
    int _lastX = 0;
    int _lastY = 0;
    uint64_t _lastMs = 0;
};

} // namespace reluva::unreal
