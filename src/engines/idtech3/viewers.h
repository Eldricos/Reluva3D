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
// Models:   every .md3; players (lower, upper and head in one folder) put
//           together. Left/Right step, PageUp/PageDown jump 10; drag to
//           turn, wheel or +/- to zoom, A to animate, ,/. to step frames,
//           R to reset the view, I to show or hide the info.
// Files:    Up/Down, PageUp/PageDown, Home/End scroll the search path's files.
// Textures: Left/Right step, PageUp/PageDown jump 10, Home/End.
// Menu:     the game's own menus (Elite Force: vm/ui.qvm) running; keys,
//           typing and the mouse go to them (their own pointer follows
//           the mouse, the system's is hidden), Ctrl+Tab and Ctrl+Esc
//           leave, Ctrl+R starts them again.
// Maps:     Left/Right pick a map; drag (or arrow keys with Shift) to rotate,
//           wheel or +/- to zoom, W/A/S/D/Q/E to move, R to reset the view,
//           P to show or hide patches, I to show or hide the info.

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/shader_cache.h"
#include "engines/idtech3/world_renderer.h"

#include "common/archive.h"
#include "common/events.h"
#include "common/image.h"
#include "common/system.h"

#include <array>
#include <cstdint>
#include <filesystem>
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
    std::filesystem::path savePath;           // where this game's own files go (GameTarget::savePath)
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
    // Whether Esc and Tab (which otherwise leave and switch pages) go to
    // the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }

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
    std::unique_ptr<ShaderCache> shaders_;  // the next three draw through it
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<ModelCache> models_;
    LightGrid grid_;
    // Models the map's entities name, lit by the light grid where they stand.
    struct Placed {
        int model = -1;
        gfx::Mat4 transform{};
        PointLight light;
    };
    std::vector<Placed> placed_;
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
    uint64_t loadedMs_ = 0; // shader animations run from when the map was loaded
    uint64_t fpsStart_ = 0;
    int frames_ = 0;
    float fps_ = 0;
    // Per second: average milliseconds per frame spent issuing draws, and
    // presenting (which includes waiting for vsync).
    double drawSum_ = 0, presentSum_ = 0;
    float drawMs_ = 0, presentMs_ = 0;
    std::string device_;
};

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return entries_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }

private:
    struct Entry {
        std::string name; // the .md3, or the player's folder
        bool player = false;
    };
    void show(int index);
    void resetView();

    std::vector<Entry> entries_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_;
    Surface overlay_;
    std::vector<std::string> info_;
    std::string error_;
    int model_ = -1;                     // a single model
    int lower_ = -1, upper_ = -1, head_ = -1; // a player's parts
    const Skin* skins_[3] = {};
    std::array<float, 3> center_{};
    float radius_ = 32;
    float yaw_ = 0, pitch_ = 0, distance_ = 100;
    bool dragging_ = false, animate_ = false, showInfo_ = true;
    int lastX_ = 0, lastY_ = 0;
    int frame_ = 0; // when not animating
    uint64_t startMs_ = 0;
};

class MenuViewer : public Page {
public:
    explicit MenuViewer(ViewerContext& ctx);
    ~MenuViewer() override;
    const char* name() const override { return "Menu"; }
    size_t count() const override { return available_ ? 1 : 0; }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }
    bool takesKey(const Event& ev) const override;

private:
    void start();
    void stop(const Error& err);
    void pinCursor();
    void syncCursor();
    void findCursor();

    bool available_ = false;
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    std::vector<int> materials_; // by the menus' shader handle; -2 not made yet
    CvarStore cvars_;
    std::unique_ptr<eliteforce::UiHost> ui_; // kept while other pages are shown
    Surface overlay_;
    std::string error_;
    std::string reportPath_; // where the details of the last stop were written
    uint64_t startMs_ = 0, shownMs_ = 0;
    // The mouse, and the menus' own pointer as last drawn (both in frame
    // pixels); the pointer's picture is drawn offset from its point.
    bool haveMouse_ = false, haveCursor_ = false, calibrate_ = false;
    int mouseX_ = 0, mouseY_ = 0;
    float cursorX_ = 0, cursorY_ = 0;
    float cursorOffsetX_ = 0, cursorOffsetY_ = 0;
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
