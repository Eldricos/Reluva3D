// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"
#include "engines/idtech3/pages/menu_layer.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/audio/sounds.h"

#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3 {

class PlayViewer : public Page {
public:
    explicit PlayViewer(ViewerContext &ctx);
    ~PlayViewer() override;
    const char *name() const override { return "Play"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return _renderer != nullptr; }
    bool takesKey(const Event &ev) const override;

private:
    void start(int index);
    void stop(const std::string &why);
    void reset();
    // The level's drawing (and its light grid) from a map file; false
    // (stopped, with why) if it can't be loaded.
    bool loadLevel(const std::string &path, std::shared_ptr<Bsp> &bsp);
    // The player's client game, with the server's configstrings, the
    // message its game state came in and the command sequence it starts at.
    Error startClientGame(const std::map<int32_t, std::string> &configstrings, int32_t serverMessage,
                          int32_t commandSequence);
    // The player's settings as the server is told them (name, model...).
    std::string userinfo() const;
    // Opening the level to players on other machines (patch 1.2's game only).
    void startHosting();
    // Joining a server on another machine instead of running one.
    void connect(const std::string &address);
    // Plays demos/<name> (.efdemo added if not given).
    void playDemo(const std::string &name);
    bool playingDemo() const;
    void endDemo();
    // The paks the server's level uses that this player is missing:
    // whether they're being downloaded; and one that has finished
    // (false: stopped).
    bool startDownloads();
    bool finishDownload(const std::string &name, const std::vector<uint8_t> &data);
    std::vector<std::pair<std::string, int32_t>> _downloads; // name, checksum
    void runGame(uint64_t nowMs);
    bool runLocal(uint64_t nowMs, int32_t dt);
    void runRemote(uint64_t nowMs, int32_t dt);
    eliteforce::GameHost::UserCommand buildCommand();
    void drawLoadingScreen();
    bool _loadingFailed = false; // the client game couldn't draw its loading screen
    void sendServerCommands();
    eliteforce::GameHost::Snapshot localSnapshot();
    // On one's own server, what the client game has been sent: the
    // configstrings as last sent, and the commands (configstring changes
    // and the game's), by sequence from 1.
    std::map<int32_t, std::string> _localConfigstrings;
    std::vector<std::string> _localCommands;
    size_t _localCommandsSeen = 0; // of the game's serverCommands()
    int _localRestartsSeen = 0;    // of the game's map_restarts
    // Loading the level the game asked for (false: this frame is over).
    bool changeLevel(const std::string &map);
    void consoleCommand(const std::string &text);
    void setFromConsole(const std::string &name, const std::string &value);
    void runClientGameCommands();
    int _execDepth = 0;
    // The game's menus over the match (the original's in-game menu): Esc
    // opens them; they close themselves.
    void openMenu();
    // Sets up the renderer and what draws through it at the video
    // settings; false (shown) if there's no 3D rendering.
    bool setUpGraphics();
    // As the original's vid_restart during a match: the renderer started
    // again at the new settings, the level's graphics and the client game
    // loaded again; the server game and the connection go on.
    void restartVideo();
    void closeMenu();
    void runMenuCommands();
    std::string matchConfigstring(int32_t index) const;
    void runBinding(const std::string &line, int32_t key, bool down);
    void stopSounds();
    void drawOverlay(uint64_t nowMs, bool haveView);
    // What the games print: kept for the console, and the client game's
    // (chat, kills, joins) shown at the top for a few seconds, as the
    // original's console does with its last lines.
    void addPrint(const std::string &text, bool notify);
    std::deque<std::pair<uint64_t, std::string>> _notify;
    std::string _printPartial[2]; // a line not yet ended, per game
    std::deque<std::string> _consoleLines;
    // Typing: the console (~) or a chat line (messagemode, T).
    enum class Typing { kNone, kConsole, kSay, kSayTeam, kTell };
    Typing _typing = Typing::kNone;
    std::string _field;
    size_t _cursor = 0;
    int32_t _tellClient = -1;
    uint32_t _swallowChar = 0;    // the character of the key that opened the typing
    uint32_t _bindingKeyChar = 0; // the character of the key whose binding is running
    int _consoleScroll = 0;       // lines back from the newest
    std::vector<std::string> _history;
    size_t _historyPos = 0;
    void openTyping(Typing t, int32_t client = -1, uint32_t key = 0);
    void closeTyping();
    void typingEvent(const Event &ev);
    void submitTyping();
    void drawColoured(int x, int y, const std::string &line);

    std::vector<std::string> _names; // maps
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::unique_ptr<ShaderCache> _shaders;
    int _frameW = 960, _frameH = 720; // r_mode
    std::unique_ptr<WorldRenderer> _world;
    std::unique_ptr<ModelCache> _models;
    LightGrid _grid;
    std::unique_ptr<SceneDrawer> _scene; // the client game's scenes and pictures
    // What the player's view drew this frame, for the info line.
    struct Shown {
        size_t models = 0, effects = 0, lights = 0;
        int other = 0; // entities not drawn (kinds not done yet)
        std::set<std::string> unloadable;
        std::string effectShaders; // what the effects are drawn with
    };
    Shown _shown;
    struct Timing {
        float server = 0, client = 0, draw = 0; // ms, the last frame's
    };
    Timing _timing;
    struct FrameNote {
        uint64_t ms = 0;
        int dt = 0;
        bool command = false;
        int snapshots = 0;
        int32_t events = 0;
    };
    std::deque<FrameNote> _recentFrames; // the last two seconds'
    SoundLibrary _soundLibrary{_ctx.files};
    std::unique_ptr<SoundSystem> _soundSystem; // while a level runs, with audio output
    CvarStore &_cvars = _ctx.cvars;
    MenuLayer _menus{_ctx, _ctx.cvars}; // the in-game menu, kept for the page's life
    bool _menuOpen = false;
    bool _videoRestart = false;   // vid_restart asked for: done before the next frame
    bool _menuCaught = false;     // they've taken the keys (and will let go to close)
    int32_t _playerNumber = 0;    // the player's client number in the match
    std::string _lastMenuCommand; // what the in-game menu last ran, shown a while
    uint64_t _lastMenuCommandMs = 0;
    std::unique_ptr<eliteforce::GameHost> _game;
    std::unique_ptr<eliteforce::NetServer> _server; // others joining the level run here
    std::unique_ptr<eliteforce::NetClient> _client; // or: playing on another machine's server
    std::string _serverAddress;
    uint64_t _joinStartMs = 0;
    eliteforce::NetClient::State _joinState = eliteforce::NetClient::State::kDisconnected;
    std::unique_ptr<eliteforce::CgameHost> _cgame;
    Surface _overlay;
    std::string _error, _notice;
    // The screenshot command waiting for its frame: whether to say so, and
    // the file's name if given.
    struct Screenshot {
        bool wanted = false, silent = false;
        std::string name;
    };
    Screenshot _screenshot;
    void saveScreenshot();
    bool _showInfo = true;
    uint64_t _shownMs = 0, _lastMs = 0;
    int32_t _clientTime = 0; // the player's time in the level, ms
    // The player's view angles in degrees (pitch counts downwards), and
    // what they hold.
    PlayerInput _input;
    JoystickInput _joystick;
    void pollJoystick();
    gfx::Vec3 _eye{};
};

} // namespace reluva::idtech3
