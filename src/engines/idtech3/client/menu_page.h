// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/menu_layer.h"
#include "engines/idtech3/client/server_browser.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class MenuPage : public Page {
public:
    explicit MenuPage(PageContext &ctx);
    ~MenuPage() override;
    const char *name() const override { return "Menu"; }
    size_t count() const override { return _available ? 1 : 0; }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }
    bool takesKey(const Event &ev) const override;

private:
    void start();
    void checkCommands();

    bool _available = false;
    gfx::Renderer3D *_renderer = nullptr;
    // r_mode
    int _frameW = 960;
    int _frameH = 720;
    bool _restart = false; // the menus ran vid_restart
    CvarStore &_cvars = _ctx.cvars;
    std::unique_ptr<ServerBrowser> _browser; // the server list's, once the menus start
    MenuLayer _menus{_ctx, _ctx.cvars};      // kept running while other pages are shown
    JoystickInput _joystick;                 // its keys (buttons, the arrows its axes give) for the menus
    std::string _lastCommand;                // shown for a while, to see what buttons do
    uint64_t _lastCommandMs = 0;
    Surface _overlay;
    uint64_t _shownMs = 0;
};

} // namespace reluva::idtech3
