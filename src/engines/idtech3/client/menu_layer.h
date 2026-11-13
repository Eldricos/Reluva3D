// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/audio/sounds.h"

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
    static constexpr int32_t kMainMenu = 1;
    static constexpr int32_t kInGameMenu = 2;
    // Their key catcher's bit while they take the keys (Quake III's).
    static constexpr int32_t kCatchUi = 2;

    MenuLayer(PageContext &ctx, CvarStore &cvars) : _ctx(ctx), _cvars(cvars) {}
    ~MenuLayer() { detach(); }

    // Drawing into `renderer`'s frames of w x h pixels (detach() before
    // the renderer goes); the menus keep running across.
    void attach(gfx::Renderer3D *renderer, int w, int h);
    void detach();

    // Loads and starts the menus on `menu` (they're kept: start them
    // again with reset() first); false if they stopped (error()).
    bool start(int32_t menu);
    // Shows `menu` on menus already running.
    void show(int32_t menu);
    void reset();
    bool running() const { return _ui != nullptr; }
    eliteforce::UiHost *ui() { return _ui.get(); }
    // Whether they're taking the keys (shown, not closed by the player).
    bool open() const { return _ui && (_ui->catcher() & kCatchUi); }

    // Why they stopped, and where the details were written.
    const std::string &error() const { return _error; }
    const std::string &reportPath() const { return _reportPath; }
    void clearError() {
        _error.clear();
        _reportPath.clear();
    }

    void handleEvent(const Event &ev);
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
    ServerBrowser *browser = nullptr;
    std::function<void(eliteforce::UiHost &)> configure;

private:
    void stop(const Error &err);
    void pinCursor();
    void syncCursor();
    void findCursor();
    void playSound(const std::string &name, int channel);

    PageContext &_ctx;
    CvarStore &_cvars;
    gfx::Renderer3D *_renderer = nullptr;
    int _frameW = 960;
    int _frameH = 720;
    std::unique_ptr<ShaderCache> _shaders;
    std::unique_ptr<ModelCache> _models; // the models the menus show
    std::unique_ptr<SceneDrawer> _scene; // their pictures and scenes
    std::unique_ptr<eliteforce::UiHost> _ui;
    size_t _commandsSeen = 0; // of ui_->commands()
    std::vector<std::string> _commands;
    SoundLibrary _sounds{_ctx.files};
    std::map<int, audio::SoundHandle> _channels; // the sound on each of the menus' channels
    std::string _error;
    std::string _reportPath;
    uint64_t _startMs = 0;
    // The mouse, and the menus' own pointer as last drawn (both in frame
    // pixels); the pointer's picture is drawn offset from its point.
    bool _haveMouse = false;
    bool _haveCursor = false;
    bool _calibrate = false;
    int _mouseX = 0;
    int _mouseY = 0;
    float _cursorX = 0;
    float _cursorY = 0;
    float _cursorOffsetX = 0;
    float _cursorOffsetY = 0;
    // where its picture last put it
    float _measuredX = 0;
    float _measuredY = 0;
    // the steps sent since, in pixels expected
    float _pendingX = 0;
    float _pendingY = 0;
    // pixels moved per pixel expected
    float _gainX = 1;
    float _gainY = 1;
};

} // namespace reluva::idtech3
