// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/pages/menu_layer.h"
#include "engines/idtech3/server_browser.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

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
    void checkCommands();

    bool available_ = false;
    gfx::Renderer3D* renderer_ = nullptr;
    int frameW_ = 960, frameH_ = 720; // r_mode
    bool restart_ = false;            // the menus ran vid_restart
    CvarStore& cvars_ = ctx_.cvars;
    std::unique_ptr<ServerBrowser> browser_; // the server list's, once the menus start
    MenuLayer menus_{ctx_, ctx_.cvars};       // kept running while other pages are shown
    JoystickInput joystick_;                  // its keys (buttons, the arrows its axes give) for the menus
    std::string lastCommand_;                // shown for a while, to see what buttons do
    uint64_t lastCommandMs_ = 0;
    Surface overlay_;
    uint64_t shownMs_ = 0;
};

} // namespace reluva::idtech3
