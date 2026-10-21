// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The id Tech 3 engine's data viewer: pages showing what a game's files
// hold, while the renderer and game code are written. Tab switches pages,
// Esc returns to the launcher.
//
// World:    the map drawn by the GPU renderer. Left/Right pick a map; drag
//           to look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift
//           to go faster, R back to the start point, I to show or hide
//           the info.
// Files:    Up/Down, PageUp/PageDown, Home/End scroll the search path's files.
// Textures: Left/Right step, PageUp/PageDown jump 10, Home/End.
// Maps:     Left/Right pick a map; drag (or arrow keys with Shift) to rotate,
//           wheel or +/- to zoom, W/A/S/D/Q/E to move, R to reset the view,
//           P to show or hide patches, I to show or hide the info.

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/world_renderer.h"

#include "common/archive.h"
#include "common/events.h"
#include "common/image.h"
#include "common/system.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const ShaderLibrary& shaders;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0;   // pages with nothing to show are skipped
    virtual void activate() = 0;        // redraw and retitle after switching here
    virtual void deactivate() {}        // before switching away
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    // Pages that draw with System::init3D; the engine puts the 2D
    // framebuffer back when leaving one.
    virtual bool uses3D() const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();

    struct Row {
        std::string name;
        std::string source; // the archive's file name
    };
    std::vector<Row> files_;
    int top_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return names_.size(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;

private:
    void show(int index);

    std::vector<std::string> names_;
    int index_ = 0;
};

class WorldViewer : public Page {
public:
    explicit WorldViewer(ViewerContext& ctx);
    ~WorldViewer() override;
    const char* name() const override { return "World"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    void load(int index);
    void toStart();
    void drawOverlay(uint64_t nowMs);

    std::vector<std::string> names_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr; // valid while this page is shown
    std::unique_ptr<WorldRenderer> world_;
    Surface overlay_;
    std::string error_;
    std::vector<std::string> info_;
    std::array<float, 3> start_{};
    float startYaw_ = 0;

    // Fly camera, in the map's units (Z up); yaw 0 looks along +X.
    std::array<float, 3> pos_{};
    float yaw_ = 0, pitch_ = 0;
    bool held_[8] = {}; // forward, back, left, right, up, down, fast, (unused)
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    bool showInfo_ = true;
    uint64_t lastMs_ = 0;
    uint64_t fpsStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
};

class MapViewer : public Page {
public:
    explicit MapViewer(ViewerContext& ctx);
    const char* name() const override { return "Maps"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Edge {
        std::array<float, 3> a, b;
        Color color;
        bool patch;
    };
    // The orbit camera's basis, rebuilt by render().
    struct Camera {
        std::array<float, 3> pos{}, right{}, up{}, forward{};
        float cx = 0, cy = 0, focal = 0;
    };

    void load(int index);
    void resetView();
    void render();
    void drawLine3d(const std::array<float, 3>& a, const std::array<float, 3>& b, Color c);
    void drawLine2d(float x0, float y0, float x1, float y1, Color c);

    std::vector<std::string> names_;
    int index_ = 0;
    Bsp bsp_;
    std::string error_; // why the map couldn't be shown
    std::vector<std::string> info_;
    std::vector<Edge> edges_;
    std::array<float, 3> mins_{}, maxs_{};

    // Orbit camera around `target_`.
    std::array<float, 3> target_{};
    float yaw_ = 0, pitch_ = 0, distance_ = 0;
    bool showPatches_ = true, showInfo_ = true;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    bool dirty_ = true;
    Camera cam_;
};

} // namespace reluva::idtech3
