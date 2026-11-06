// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/sounds.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// The game's own menus (vm/ui.qvm) as a layer a page shows: started on a
// menu, given the keys and the mouse, run each frame and drawn into the
// page's frame. The Menu page shows them alone; the Play page over a
// match, as the original's in-game menu.
class MenuLayer {
public:
    // The menus' own numbers for the menu to start on (Quake III's).
    static constexpr int32_t kMainMenu = 1, kInGameMenu = 2;
    // Their key catcher's bit while they take the keys (Quake III's).
    static constexpr int32_t kCatchUi = 2;

    MenuLayer(ViewerContext& ctx, CvarStore& cvars) : ctx_(ctx), cvars_(cvars) {}
    ~MenuLayer() { detach(); }

    // Drawing into `renderer`'s frames of w x h pixels (detach() before
    // the renderer goes); the menus keep running across.
    void attach(gfx::Renderer3D* renderer, int w, int h);
    void detach();

    // Loads and starts the menus on `menu` (they're kept: start them
    // again with reset() first); false if they stopped (error()).
    bool start(int32_t menu);
    // Shows `menu` on menus already running.
    void show(int32_t menu);
    void reset();
    bool running() const { return ui_ != nullptr; }
    eliteforce::UiHost* ui() { return ui_.get(); }
    // Whether they're taking the keys (shown, not closed by the player).
    bool open() const { return ui_ && (ui_->catcher() & kCatchUi); }

    // Why they stopped, and where the details were written.
    const std::string& error() const { return error_; }
    const std::string& reportPath() const { return reportPath_; }
    void clearError() {
        error_.clear();
        reportPath_.clear();
    }

    void handleEvent(const Event& ev);
    // A key by its Quake III number (the joystick's: JOY1..., the arrows
    // its axes give), pressed or released; the menus take presses only.
    void key(int32_t q3key, bool down);
    // A frame: the menus run (time in ms since they started) and their
    // pictures and 3D scenes drawn, after beginFrame.
    void frame(uint64_t nowMs);
    // The commands the menus have run since the last call, one each.
    std::vector<std::string> takeCommands();

    // Set before start(): the server list's (the Menu page's), and what
    // the menus are told of the connection and its configstrings.
    ServerBrowser* browser = nullptr;
    std::function<void(eliteforce::UiHost&)> configure;

private:
    void stop(const Error& err);
    void pinCursor();
    void syncCursor();
    void findCursor();
    void playSound(const std::string& name, int channel);

    ViewerContext& ctx_;
    CvarStore& cvars_;
    gfx::Renderer3D* renderer_ = nullptr;
    int frameW_ = 960, frameH_ = 720;
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_; // the models the menus show
    std::unique_ptr<SceneDrawer> scene_; // their pictures and scenes
    std::unique_ptr<eliteforce::UiHost> ui_;
    size_t commandsSeen_ = 0; // of ui_->commands()
    std::vector<std::string> commands_;
    SoundLibrary sounds_{ctx_.files};
    std::map<int, audio::SoundHandle> channels_; // the sound on each of the menus' channels
    std::string error_, reportPath_;
    uint64_t startMs_ = 0;
    // The mouse, and the menus' own pointer as last drawn (both in frame
    // pixels); the pointer's picture is drawn offset from its point.
    bool haveMouse_ = false, haveCursor_ = false, calibrate_ = false;
    int mouseX_ = 0, mouseY_ = 0;
    float cursorX_ = 0, cursorY_ = 0;
    float cursorOffsetX_ = 0, cursorOffsetY_ = 0;
    float measuredX_ = 0, measuredY_ = 0; // where its picture last put it
    float pendingX_ = 0, pendingY_ = 0;   // the steps sent since, in pixels expected
    float gainX_ = 1, gainY_ = 1;         // pixels moved per pixel expected
};

} // namespace reluva::idtech3
