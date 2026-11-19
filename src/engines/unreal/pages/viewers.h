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

class PackageViewer : public Page {
public:
    explicit PackageViewer(ViewContext &ctx);
    const char *name() const override { return "Packages"; }
    size_t count() const override { return names_.size(); }
    void activate() override { draw(); }
    void handleEvent(const Event &ev) override;

private:
    void draw();

    std::vector<std::string> names_;
    int index_ = 0;
    int selected_ = 0, top_ = 0;
    bool showProps_ = false;
};

// Steps through the exports of some classes, package by package.
class ObjectBrowser {
public:
    // Over the named packages, in order.
    ObjectBrowser(const PackageSet &packages, std::vector<std::string> names, bool (*wanted)(std::string_view));
    size_t packageCount() const { return names_.size(); }
    // Moves within the package (by objects) or between packages (skipping
    // those with none); false if there's nothing.
    bool step(int objects);
    bool stepPackage(int packages);
    const Package *package() const;
    int exportIndex() const;
    // "3/40 in Package (2/12)".
    std::string where() const;

private:
    bool load(int package);

    const PackageSet &packages_;
    bool (*wanted_)(std::string_view);
    std::vector<std::string> names_;
    int package_ = -1;
    std::vector<int> objects_;
    int object_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewContext &ctx);
    const char *name() const override { return "Textures"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    void select();
    void draw();
    ObjectBrowser browser_;
    bool started_ = false;
    image::Image img_;
    TextureInfo info_;
    std::string error_;
    std::unique_ptr<AnimatedTexture> anim_; // the texture's animation, if it has one
    bool paused_ = false;
    uint64_t lastMs_ = 0;
};

class SoundViewer : public Page {
public:
    explicit SoundViewer(ViewContext &ctx);
    ~SoundViewer() override;
    const char *name() const override { return "Sounds"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;

private:
    void draw();
    void play();
    ObjectBrowser browser_;
    bool started_ = false;
    std::string status_;
    audio::SoundHandle playing_ = audio::kNoSound;
};

class MeshViewer : public Page {
public:
    explicit MeshViewer(ViewContext &ctx);
    ~MeshViewer() override;
    const char *name() const override { return "Meshes"; }
    size_t count() const override { return browser_.packageCount(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    void show();
    void unload();
    void resetView();

    ObjectBrowser browser_;
    bool started_ = false;
    gfx::Renderer3D *renderer_ = nullptr;
    Mesh mesh_;
    GpuMesh gpu_;
    std::vector<gfx::TextureId> textures_;
    std::string error_;
    std::vector<std::string> info_;
    Surface overlay_;
    int anim_ = -1; // into mesh_.anims
    // The animations to step through: the mesh's own, then (built on bones)
    // those of the shared sets its list names, borrowed when picked.
    std::vector<std::string> choices_;
    int choice_ = -1;
    size_t ownAnims_ = 0; // of choices_, the mesh's own
    size_t ownAnimCount_ = 0;
    int32_t ownFrames_ = 0;
    void pick(int choice);
    bool playing_ = true, showInfo_ = true, dragging_ = false;
    float time_ = 0, yaw_ = 0, pitch_ = 0.3f, distance_ = 200;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0;
};

} // namespace reluva::unreal
