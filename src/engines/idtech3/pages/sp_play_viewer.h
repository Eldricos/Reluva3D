// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/sp_ui_host.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"

#include "common/video/bink.h"

#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// Elite Force's single player: the retail game code (efgamex86.dll, run in
// the x86 interpreter) on its maps, drawn as the Play page draws Holomatch.
class SpPlayViewer : public Page {
public:
    explicit SpPlayViewer(ViewerContext& ctx);
    ~SpPlayViewer() override;
    const char* name() const override { return "Single player"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return renderer_ != nullptr; }
    bool takesKey(const Event& ev) const override;

private:
    // A level: new (loadMode 0), or a saved game's (1, or 2 for an
    // autosave) read on from `save` after its header.
    void start(int index, int32_t loadMode = 0, raven::SaveReader* save = nullptr);
    void stop(const std::string& why);
    // Saved games (saves/<name>.sav), as the original's save, load and
    // wipe commands; an autosave ("auto") as each new level starts.
    void saveGame(const std::string& name, bool autosave);
    void loadGame(std::string name);
    bool readSave(const std::string& name, std::vector<uint8_t>& file) const;
    std::vector<uint8_t> levelShot(const std::string& map) const;
    void runGameCommands();
    // Cinematics (video/<name>.bik): full screen, the game ended first
    // ("cinematic", then the nextmap cvar's command) or paused under it
    // ("ingamecinematic"). Any typed key or a click skips one.
    void startCinematic(const std::string& name, bool inGame);
    void endCinematic();
    void runCinematic(uint64_t nowMs);
    struct Cinematic {
        bink::Movie movie;
        std::vector<uint8_t> rgba;
        uint64_t startMs = 0;
        bool inGame = false;
        audio::SoundHandle sound = audio::kNoSound;
    };
    std::unique_ptr<Cinematic> cinematic_;
    bool introShown_ = false;
    void endGame();
    void runGame(uint64_t nowMs);
    void runBinding(const std::string& line, int32_t key, bool down);
    void drawOverlay();
    void drawMenus(float time);
    void showLoadingScreen(const std::string& map);
    void showClientLoading();
    // The game's menus (efuix86.dll), when it's there: started at the main
    // menu; Esc opens the in-game one. While a menu has the keys, the game
    // waits, as the original's single player.
    void startMenus();
    bool menusHaveKeys() const;
    void menuKey(int32_t key);
    void runMenuCommands();
    void runCommand(const std::string& line, bool fromGame = false);
    void menusStopped(const Error& err);

    std::vector<std::string> names_; // the single-player maps
    std::string map_;                // the level running
    std::string lastLoaded_;         // the saved game last loaded, for "load *respawn"
    // The player's commands since a saved game was loaded, written beside
    // it (<name>.input) for reluva-tool sp-run -load <save> -replay to
    // play back; closed as the game ends.
    std::ofstream recording_;
    std::filesystem::path saveDir_;  // where saved games are written
    size_t gameCommandsRun_ = 0;     // of the game's console commands
    // The in-game menu waits a frame while the game is captured for the
    // save menu's picture.
    std::string pendingMenu_;
    bool capturing_ = false;
    int index_ = 0;
    std::vector<uint8_t> dll_;       // efgamex86.dll
    std::vector<uint8_t> uiDll_;     // efuix86.dll
    gfx::Renderer3D* renderer_ = nullptr;
    std::unique_ptr<ShaderCache> shaders_;
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<ModelCache> models_;
    std::unique_ptr<SceneDrawer> scene_;
    LightGrid grid_;
    int frameW_ = 960, frameH_ = 720;
    CvarStore& cvars_ = ctx_.cvars;
    std::unique_ptr<eliteforce::SpGameHost> game_;
    std::unique_ptr<eliteforce::SpUiHost> ui_;
    SoundLibrary soundLibrary_{ctx_.files};
    std::unique_ptr<SoundSystem> soundSystem_; // with audio output
    Surface overlay_;
    std::string error_, menuError_, lastCommand_;
    uint64_t lastCommandMs_ = 0, startMs_ = 0, nowMs_ = 0;
    bool showInfo_ = true;
    uint64_t lastMs_ = 0;
    int32_t clientTime_ = 0;
    PlayerInput input_;
    float menuMouseX_ = 0, menuMouseY_ = 0;
    bool textInput_ = false;
    struct Timing {
        float server = 0, client = 0, draw = 0; // ms, the last frame's
    };
    Timing timing_;
};

} // namespace reluva::idtech3
