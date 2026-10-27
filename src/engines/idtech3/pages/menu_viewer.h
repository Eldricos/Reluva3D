// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"

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
    void stop(const Error& err);
    void pinCursor();
    void syncCursor();
    void findCursor();
    void playSound(const std::string& name, int channel);
    void checkCommands();

    bool available_ = false;
    gfx::Renderer3D* renderer_ = nullptr;
    int frameW_ = 960, frameH_ = 720; // r_mode
    bool restart_ = false;            // the menus ran vid_restart
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<ModelCache> models_;   // the models the menus show
    std::unique_ptr<SceneDrawer> scene_;   // their pictures and scenes
    CvarStore& cvars_ = ctx_.cvars;
    std::unique_ptr<ServerBrowser> browser_;  // the server list's, once the menus start
    std::unique_ptr<eliteforce::UiHost> ui_; // kept while other pages are shown
    size_t commandsSeen_ = 0;                // of ui_->commands()
    std::string lastCommand_;                // shown for a while, to see what buttons do
    uint64_t lastCommandMs_ = 0;
    SoundLibrary sounds_{ctx_.files};
    std::map<int, audio::SoundHandle> channels_; // the sound on each of the menus' channels
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
    float measuredX_ = 0, measuredY_ = 0; // where its picture last put it
    float pendingX_ = 0, pendingY_ = 0;   // the steps sent since, in pixels expected
    float gainX_ = 1, gainY_ = 1;         // pixels moved per pixel expected
};

} // namespace reluva::idtech3
