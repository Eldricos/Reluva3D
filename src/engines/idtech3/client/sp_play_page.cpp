// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/jk2/sp_menus.h"
#include "engines/idtech3/jk2/sp_savegame.h"
#include "engines/idtech3/client/sp_play_page.h"
#include "engines/idtech3/formats/shader.h"

#include "graphics/font.h"
#include "image/image.h"
#include "common/developer.h"
#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <tuple>

namespace reluva::idtech3 {

namespace {

constexpr float kServerBudgetMs = 40; // of the game's frames, each page frame
// Jedi Outcast's configstring of the music's state, which the game sets
// and the engine reads each frame (0x428290).
constexpr int32_t kMusicStateConfigString = 907;
// 1.03's, as its configstrings from 522 on moved 96 for force
// feedback's effects (0x428b50).
constexpr int32_t kMusicStateConfigString103 = 1003;

bool badSaveName(const std::string &name); // below

// Whether a shader the game registers can be drawn: a script of that name,
// or an image (its own file type, or .tga or .jpg in its place).
bool shaderFound(const ShaderLibrary &shaders, const SearchSet &files, const std::string &name) {
    if (name.empty())
        return false;
    if (shaders.find(name) || files.has(name))
        return true;
    std::string base = name;
    if (const size_t dot = base.rfind('.'); dot != std::string::npos && base.find('/', dot) == std::string::npos)
        base.resize(dot);
    return files.has(base + ".tga") || files.has(base + ".jpg");
}

} // namespace

SpPlayPage::SpPlayPage(PageContext &ctx) : Page(ctx) {
    // A game with its single-player game code beside the game folder.
    if ((ctx.game.code != GameCode::kEliteForce && ctx.game.code != GameCode::kJediOutcast) ||
        ctx.game.spGameDll.empty() || ctx.singlePlayerPath.empty())
        return;
    // (By name in any case: copied from a CD, they may be in capitals.)
    const auto dll = fs::findFile(ctx.singlePlayerPath, ctx.game.spGameDll);
    std::ifstream in(dll ? *dll : std::filesystem::path(), std::ios::binary);
    if (!dll || !in)
        return;
    _dll.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    _expansion = isExpansionDll(*dll);
    // Its menus, if they're there too.
    if (const auto uiPath = fs::findFile(ctx.singlePlayerPath, ctx.game.spUiDll); uiPath && !ctx.game.spUiDll.empty())
        if (std::ifstream ui{*uiPath, std::ios::binary}; ui)
            _uiDll.assign(std::istreambuf_iterator<char>(ui), std::istreambuf_iterator<char>());
    // Its maps: all but multiplayer's.
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) != 0 || key.size() <= 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        if (std::none_of(ctx.game.multiplayerMapPrefixes.begin(), ctx.game.multiplayerMapPrefixes.end(),
                         [&](const std::string &p) { return key.compare(5, p.size(), p) == 0; }))
            _names.push_back(key);
    }
    std::sort(_names.begin(), _names.end());
    for (size_t i = 0; i < _names.size(); ++i)
        if (_names[i] == "maps/" + ctx.game.firstMap + ".bsp")
            _index = int(i);
}

SpPlayPage::~SpPlayPage() {
    deactivate();
}

void SpPlayPage::swapSettings() {
    std::swap(_ctx.cvars, _otherCvars);
    std::swap(_ctx.bindings, _otherBindings);
    _ownSettings = !_ownSettings;
}

void SpPlayPage::activate() {
    if (!_ctx.game.spDefaultConfig.empty() && !_ownSettings) {
        saveConfig(_ctx);
        const std::string gameDir = _ctx.cvars.string("fs_game");
        swapSettings();
        if (!std::exchange(_ownSettingsRead, true)) {
            loadConfig(_ctx, true);
            _ctx.cvars.create("fs_game", gameDir, CvarStore::kSystemInfo | CvarStore::kInit);
            _ctx.cvars.set("fs_game", gameDir, true);
        }
    }
    JoystickInput::registerCvars(_cvars);
    const VideoSettings video = videoSettings(_ctx.cvars);
    _frameW = video.width;
    _frameH = video.height;
    _ctx.sys.setVideoMode(_frameW, _frameH, video.fullscreen);
    if (Error err = _ctx.sys.init3D(_frameW, _frameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "Single player needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(_frameW, _frameH, PixelFormat::kRGBA32);
    _shaders = std::make_unique<ShaderCache>(*_renderer, _ctx.files, _ctx.shaders);
    _world = std::make_unique<WorldRenderer>(*_renderer, *_shaders);
    _models = std::make_unique<ModelCache>(*_renderer, *_shaders, _ctx.files);
    applyVideoSettings(video, *_shaders, _world.get(), _models.get());
    _scene = std::make_unique<SceneDrawer>(*_renderer, *_shaders, *_models, _ctx.game.spRefEntity);
    _ctx.sys.setRelativeMouse(true);
    if (_ctx.mixer) {
        _soundSystem = std::make_unique<SoundSystem>(*_ctx.mixer, _soundLibrary);
        _soundSystem->print = [this](const std::string &text) { consolePrint(text); };
    }
    // Jedi Outcast's dynamic music sets.
    if (_soundSystem && jedi()) {
        std::vector<uint8_t> dms;
        if (!_ctx.files.read("ext_data/dms.dat", dms))
            _soundSystem->setMusicData(std::string(dms.begin(), dms.end()));
    }
    _startMs = 0;
    // Each version's saved games in a folder of its own (sortSaves).
    _saveDir = _ctx.savePath.empty() ? std::filesystem::path()
               : _expansion          ? _ctx.savePath / "expansion" / "saves"
                                     : _ctx.savePath / "saves";
    sortSaves();
    // The player's userinfo, as the original's client registers it.
    _cvars.create("name", _ctx.game.spPlayerName, CvarStore::kArchive | CvarStore::kUserInfo);
    _cvars.create("snaps", "20", CvarStore::kArchive | CvarStore::kUserInfo);
    _cvars.create("headModel", _ctx.game.malePlayer.head, CvarStore::kUserInfo);
    _cvars.create("torsoModel", _ctx.game.malePlayer.torso, CvarStore::kUserInfo);
    _cvars.create("legsModel", _ctx.game.malePlayer.legs, CvarStore::kUserInfo);
    _cvars.create("sex", "male", CvarStore::kArchive | CvarStore::kUserInfo);
    _cvars.create("handicap", "100", CvarStore::kArchive | CvarStore::kUserInfo);
    if (hasMenus())
        startMenus();
    if (!_ui) {
        start(_index);
    } else if (!_introShown) {
        // The logos, as the original's engine plays at start-up (Jedi
        // Outcast's: 0x4d8f8c).
        _introShown = true;
        startCinematic(jedi() ? "openinglogos" : "eflogo", false);
    }
}

void SpPlayPage::startCinematic(const std::string &name, bool inGame, const std::string &how) {
    if (inGame && !(_game && _game->clientGameRunning()))
        return;
    std::string base = fs::toLower(name);
    // (Asked for with or without a file type, which may be another's: the
    // game's own is played.)
    if (const size_t dot = base.rfind('.'); dot != std::string::npos && base.size() - dot == 4)
        base.resize(dot);
    std::vector<uint8_t> data;
    auto c = std::make_unique<Cinematic>();
    Error err = _ctx.files.read("video/" + base + _ctx.game.cinematicExtension, data);
    c->loop = !how.empty() && how[0] == '2';
    if (c->loop)
        c->file = data;
    if (!err)
        err = c->movie.open(std::move(data), _ctx.game.cinematicExtension);
    if (err) {
        report("Cinematic " + base + ": " + err.message);
        return;
    }
    c->inGame = inGame;
    // Jedi Outcast's opening crawl: its movie held at its end, the crawl
    // over it and its music (jk2sp.exe, 0x404ad5).
    const GameInfo::TextCrawl &crawl = _ctx.game.textCrawl;
    c->crawl = !crawl.movie.empty() && base == crawl.movie;
    c->hold = c->crawl || (!how.empty() && how[0] == '1') ||
              std::find(_ctx.game.heldMovies.begin(), _ctx.game.heldMovies.end(), base) != _ctx.game.heldMovies.end();
    // Its speech a sound of its own outside English (0x404a8f on).
    std::string speech;
    const std::string language = fs::toLower(_cvars.string("s_language"));
    if (const auto it = _ctx.game.separateSpeech.find(base);
        it != _ctx.game.separateSpeech.end() && !language.empty() && language != "english")
        speech = it->second;
    if (c->crawl) {
        const int language = _cvars.find("sp_language") ? _cvars.integer("sp_language") : 0;
        _crawlShaders = HandleTable{};
        _crawlShaders.add(crawl.picture + std::to_string(language));
    }
    if (!inGame) {
        // The game ends, the console and the menus close (0x403c74).
        endGame();
        if (_consoleOpen)
            closeConsole();
        if (_ui) {
            if (Error e = _ui->setActiveMenu(""))
                menusStopped(e);
            else
                _ui->setCatcher(_ui->catcher() & ~SpMenuHost::kCatchUi);
        }
    }
    // Its sound whole, to play alongside.
    std::vector<int16_t> sound;
    int rate = 0;
    int channels = 0;
    if (c->crawl) {
        if (auto music = _soundLibrary.find(crawl.music); music && _ctx.mixer)
            c->sound = _ctx.mixer->play(music, audio::SoundType::kMusic, audio::kMaxVolume);
    } else if (!speech.empty()) {
        if (auto spoken = _soundLibrary.find(speech); spoken && _ctx.mixer)
            c->sound = _ctx.mixer->play(spoken, audio::SoundType::kEffects, audio::kMaxVolume);
    } else if (!c->movie.decodeAllAudio(sound, rate, channels) && !sound.empty() && _ctx.mixer) {
        auto s = std::make_shared<audio::Sound>();
        s->rate = rate;
        s->channels = channels;
        s->format = 1;
        s->samples = std::move(sound);
        c->sound = _ctx.mixer->play(s, audio::SoundType::kEffects, audio::kMaxVolume);
        if (c->loop)
            c->soundData = s;
    }
    _cinematic = std::move(c);
}

void SpPlayPage::endCinematic() {
    if (!_cinematic)
        return;
    if (_cinematic->sound != audio::kNoSound && _ctx.mixer)
        _ctx.mixer->stop(_cinematic->sound);
    const bool inGame = _cinematic->inGame;
    _cinematic.reset();
    _lastMs = 0;
    // Jedi Outcast's music starts again in a level (0x405122).
    if (_soundSystem && _game && jedi())
        _soundSystem->restartMusic();
    // As the original's: then the nextmap cvar's command, once.
    if (!inGame) {
        const std::string next = _cvars.string("nextmap");
        if (!next.empty()) {
            _cvars.set("nextmap", "");
            for (const std::string &line : splitCommands(next))
                runCommand(line);
        }
    }
}

void SpPlayPage::runCinematic(uint64_t nowMs) {
    // The frame due now, at the movie's rate from when it started.
    Cinematic &c = *_cinematic;
    if (!c.startMs)
        c.startMs = nowMs;
    const int64_t due =
        int64_t((nowMs - c.startMs) * c.movie.rateNumerator() / (uint64_t(c.movie.rateDenominator()) * 1000));
    bool decoded = false;
    while (c.movie.frameNumber() < due) {
        bool more = false;
        if (Error err = c.movie.nextFrame(more); err || !more) {
            if (err)
                report("Cinematic: " + err.message);
            if (!err && c.hold)
                break;
            if (!err && c.loop && !c.movie.open(c.file, _ctx.game.cinematicExtension)) {
                // From its start again, its sound with it.
                c.startMs = nowMs;
                if (c.soundData && _ctx.mixer) {
                    _ctx.mixer->stop(c.sound);
                    c.sound = _ctx.mixer->play(c.soundData, audio::SoundType::kEffects, audio::kMaxVolume);
                }
                break;
            }
            return endCinematic();
        }
        decoded = true;
    }
    if (decoded)
        c.movie.toRgba(c.rgba);
}

void SpPlayPage::drawTextCrawl(uint64_t elapsedMs) {
    // As jk2sp.exe draws it (0x404cc0): from 9 s, a view with no world as
    // wide as the screen and twice as high, 50 pixels above it, 130
    // degrees across and up, looking along x at one slanted polygon (its
    // near edge at x 90, y -250 to 250, its far edge at x 715, 1100 up).
    // Its picture scrolls 0.000015 of itself a millisecond; its colour
    // (255, 235, 127), a tenth of that at the far edge, fades out from
    // 78.5 s until it ends.
    constexpr uint64_t kStartMs = 9000;
    constexpr uint64_t kFadeMs = 78500;
    if (elapsedMs < kStartMs)
        return;
    const float fade =
        elapsedMs < kFadeMs ? 1.0f : std::clamp(float(kCrawlEndMs) - float(elapsedMs), 0.0f, 2480.0f) / 2480.0f;
    const float scroll = float(elapsedMs - kStartMs) * 0.000015f;
    struct Corner {
        float x;
        float y;
        float z;
        float s;
        float t;
        bool far;
    };
    const Corner corners[4] = {{90, -250, 0, 1, scroll, false},
                               {90, 250, 0, 0, scroll, false},
                               {715, 250, 1100, 0, scroll - 1, true},
                               {715, -250, 1100, 1, scroll - 1, true}};
    std::vector<uint8_t> vertices(4 * 24);
    for (size_t i = 0; i < 4; ++i) {
        const Corner &c = corners[i];
        const float xyzst[5] = {c.x, c.y, c.z, c.s, c.t};
        std::memcpy(&vertices[i * 24], xyzst, sizeof xyzst);
        const float shade = c.far ? 0.1f : 1.0f;
        vertices[i * 24 + 20] = uint8_t(255 * fade * shade);
        vertices[i * 24 + 21] = uint8_t(235 * fade * shade);
        vertices[i * 24 + 22] = uint8_t(127 * fade * shade);
        vertices[i * 24 + 23] = uint8_t(255 * fade);
    }
    // refdef_t: the rectangle in the 640 x 480 screen (the 50 pixels the
    // original's are in the screen's own), fov across and up, the eye at
    // the origin with Quake's axes, no world.
    std::array<uint8_t, kRefdefSize> refdef{};
    auto put = [&refdef](size_t at, auto value) { std::memcpy(refdef.data() + at, &value, sizeof value); };
    put(0, int32_t(0));
    put(4, int32_t(-50 * 480 / std::max(_frameH, 1)));
    put(8, int32_t(640));
    put(12, int32_t(960));
    put(16, 130.0f);
    put(20, 130.0f);
    put(36, 1.0f);
    put(52, 1.0f);
    put(68, 1.0f);
    put(76, int32_t(1));
    Scene scene;
    scene.addPoly(1, 4, vertices.data());
    scene.render(refdef.data(), 0);
    _scene->setHandles({nullptr, nullptr, &_crawlShaders});
    _scene->drawView(scene, scene.views[0], 640, 480, float(elapsedMs) / 1000.0f, false, nullptr, nullptr, false);
}

void SpPlayPage::startMenus() {
    _menuError.clear();
    std::unique_ptr<eliteforce::SpUiHost> efMenus;
    std::unique_ptr<jk2::SpMenus> jediMenus;
    if (jedi()) {
        jediMenus = std::make_unique<jk2::SpMenus>(_ctx.files, _cvars);
    } else {
        efMenus = std::make_unique<eliteforce::SpUiHost>(_ctx.files, _cvars);
        efMenus->modInstall = _ctx.installPath;
        efMenus->modBaseDir = _ctx.game.baseDir;
    }
    SpMenuHost &ui = jediMenus ? static_cast<SpMenuHost &>(*jediMenus) : *efMenus;
    ui.print = [this](const std::string &text) { consolePrint(text); };
    ui.shaderExists = [this](const std::string &name) { return shaderFound(_ctx.shaders, _ctx.files, name); };
    ui.bindings = &_ctx.bindings;
    ui.startLocalSound = [this](const std::string &sound, int channel) {
        if (_soundSystem)
            _soundSystem->startLocalSound(sound, channel);
    };
    ui.configString = [this](int32_t index, std::string &out) {
        if (!_game)
            return false;
        const auto it = _game->configstrings().find(index);
        if (it == _game->configstrings().end())
            return false;
        out = it->second;
        return true;
    };
    ui.saveDir = _saveDir;
    ui.readSave = [this](const std::string &name, std::vector<uint8_t> &file) { return readSave(name, file); };
    ui.listSave = [this](const std::string &name) {
        std::vector<uint8_t> file;
        return readSave(name, file) && saveFits(file);
    };
    ui.askGame = [this](uint32_t flag) { return _game ? _game->allowedToSaveHere(flag) : flag; };
    if (jediMenus && _renderer)
        jediMenus->rendererName = _renderer->deviceName();
    if (jediMenus)
        jediMenus->drawDataPad = [this](int32_t command, std::vector<Picture> &out) -> const HandleTable * {
            if (!_game || !_game->clientGameRunning())
                return nullptr;
            if (Error err = _game->drawDataPad(command, out)) {
                stop("The game stopped: " + err.message);
                return nullptr;
            }
            return &_game->shaders();
        };
    Error err = jediMenus ? jediMenus->load() : efMenus->load(_uiDll);
    if (jediMenus)
        _ui = std::move(jediMenus);
    else
        _ui = std::move(efMenus);
    if (err)
        return menusStopped(err);
    if ((err = _ui->setActiveMenu("main")))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayPage::menusStopped(const Error &err) {
    // The game goes on without them (Esc then leaves the page).
    _menuError = (jedi() ? std::string("The menus") : _ctx.game.spUiDll) + ": " + err.message;
    _ui.reset();
}

bool SpPlayPage::menusHaveKeys() const {
    return _ui && (_ui->catcher() & SpMenuHost::kCatchUi);
}

void SpPlayPage::menuKey(int32_t key) {
    if (!_ui)
        return;
    if (Error err = _ui->keyEvent(key))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayPage::runMenuCommands() {
    if (!_ui)
        return;
    for (const std::string &line : _ui->takeCommands())
        runCommand(line);
}

void SpPlayPage::runCommand(const std::string &line, bool fromGame) {
    // What the menus and the game run, as the original's console would.
    const std::vector<std::string> w = splitCommand(line);
    if (w.empty())
        return;
    const std::string cmd = fs::toLower(w[0]);
    if (cmd == "loadtransition" && jedi()) {
        if (w.size() < 2)
            return;
        if (_game && !_runningTransition) {
            _pendingTransition = line; // the screen first, as below
            return;
        }
        loadTransition(w);
    } else if (cmd == "map" || cmd == "devmap" || cmd == "spmap" || cmd == "maptransition") {
        if (w.size() < 2)
            return;
        if (cmd == "maptransition" && _game && !_runningTransition) {
            // The level's last frame first, for the next one's loading
            // screen (taken as this frame is drawn; see update()).
            _pendingTransition = line;
            return;
        }
        // A level change carries the player over (maptransition); a new
        // start doesn't, and forgets the last level's autosave.
        if (_ui)
            _ui->setCapture({}); // the loading screen shows the level's picture
        if (cmd != "maptransition")
            _nextScreenShot.clear(); // and the client game's, nothing in its place
        if (cmd == "maptransition" && _game) {
            _game->savePlayer();
        } else {
            _cvars.set("playersave", "");
            if (w[1][0] != '_' && !_saveDir.empty()) {
                std::error_code ec;
                std::filesystem::remove(_saveDir / "auto.sav", ec);
            }
        }
        const int index = mapIndex(w[1]);
        if (index < 0) {
            _error = "There's no map " + w[1];
            return;
        }
        _nextSpawnTarget = cmd == "maptransition" && w.size() >= 3 ? w[2] : std::string();
        // Jedi Outcast's game is told of a level change only by
        // loadtransition (0x435293).
        _nextLoadTransition = cmd == "maptransition" && !jedi();
        start(index);
    } else if (cmd == "disconnect") {
        endGame();
        if (_ui)
            if (Error err = _ui->setActiveMenu("main"))
                menusStopped(err);
    } else if (cmd == "echo") {
        std::string text;
        for (size_t i = 1; i < w.size(); ++i)
            text += (i > 1 ? " " : "") + w[i];
        report(text);
    } else if (cmd == "clear") {
        _console.clear();
    } else if (cmd == "toggleconsole") {
        if (_consoleOpen)
            closeConsole();
        else
            openConsole(0);
    } else if (cmd == "quit") {
        endGame();
        _ctx.requestPage = "Start";
    } else if (cmd == "turboliftmenu") {
        if (_ui && _game)
            if (Error err = _ui->setActiveMenu("turbolift"))
                menusStopped(err);
    } else if (cmd == "cinematic" || cmd == "ingamecinematic") {
        if (w.size() >= 2)
            startCinematic(w[1], cmd == "ingamecinematic", w.size() >= 3 ? w[2] : std::string());
    } else if (cmd == "music") {
        if (_soundSystem && w.size() >= 2 && jedi())
            _soundSystem->startMusic(w[1], w.size() >= 3 ? w[2] : w[1]);
        else if (_soundSystem && w.size() >= 2)
            _soundSystem->startBackgroundTrack(w[1], w.size() >= 3 ? w[2] : w[1]);
    } else if (cmd == "s_dynamic" && jedi()) {
        if (_soundSystem)
            _soundSystem->dynamicCommand({w.begin() + 1, w.end()});
    } else if (cmd == "bind" || cmd == "unbind" || cmd == "unbindall" || cmd == "set" || cmd == "seta" ||
               cmd == "sets" || cmd == "setu" || cmd == "exec") {
        _ctx.bindings.execute(line, _cvars, nullptr, [this](const std::string &name, std::string &text) {
            std::vector<uint8_t> data;
            if (_ctx.files.read(name, data))
                return false;
            text.assign(data.begin(), data.end());
            return true;
        });
    } else if (cmd == "save") {
        if (w.size() == 2)
            saveGame(w[1], false);
        else
            report("USAGE: save <filename>");
    } else if (cmd == "load") {
        if (w.size() == 2)
            loadGame(w[1]);
        else
            report("USAGE: load <filename>");
    } else if (cmd == "wipe") {
        const std::string name = w.size() == 2 ? fs::toLower(w[1]) : std::string();
        if (name.empty() || name == "auto" || name == "exitholodeck" || _saveDir.empty()) {
            report("Can't wipe 'auto' or 'exitholodeck'");
        } else if (badSaveName(w[1])) {
            report("Bad savegame name.");
        } else {
            std::error_code ec;
            std::filesystem::remove(_saveDir / (w[1] + ".sav"), ec);
        }
    } else if (cmd == "vid_restart") {
        // The game's files start again if a mod was chosen (fs_game, the
        // Mods screen). The original's single player only reads fs_game as
        // it starts; Reluva switches at once, as its Holomatch does.
        _ctx.requestRestart = true;
    } else if (cmd == "cvar_restart" || cmd == "snd_restart" || cmd == "in_restart" || cmd == "s_enable_a3d" ||
               cmd == "s_disable_a3d") {
        // Nothing to restart here.
    } else {
        // The menus' own commands (ui_turbolift...), then the game's.
        bool handled = false;
        if (_ui)
            if (Error err = _ui->consoleCommand(line, &handled))
                menusStopped(err);
        if (!handled && _game && !fromGame)
            if (Error err = _game->clientConsoleCommand(line))
                stop("The game stopped: " + err.message);
    }
}

void SpPlayPage::deactivate() {
    if (_cinematic && _cinematic->sound != audio::kNoSound && _ctx.mixer)
        _ctx.mixer->stop(_cinematic->sound);
    _cinematic.reset();
    if (_renderer)
        _ctx.sys.setRelativeMouse(false);
    if (_textInput)
        _ctx.sys.setTextInput(false);
    _textInput = false;
    _game.reset();
    _ui.reset();
    _soundSystem.reset();
    _scene.reset();
    _models.reset();
    _world.reset();
    _shaders.reset();
    _renderer = nullptr;
    _input.releaseAll();
    if (_ownSettings) {
        saveConfig(_ctx, true);
        swapSettings();
    }
}

void SpPlayPage::stop(const std::string &why) {
    _error = why;
    endGame();
}

void SpPlayPage::endGame() {
    _game.reset();
    _recording.close();
    // As the original's disconnect: no scene left half skipped.
    _cvars.set("timescale", "1", true);
    _cvars.set("skippingCinematic", "0", true);
    if (_scene)
        _scene->reset(); // its handles are gone
    if (_soundSystem)
        _soundSystem->stopAll();
    _input.releaseAll();
}

void SpPlayPage::start(int index, int32_t loadMode, raven::SaveReader *save) {
    if (_names.empty() || !_renderer)
        return;
    _index = index;
    _error.clear();
    endGame();
    // The menus close as a level loads (SetActiveMenu(0)).
    if (_ui) {
        if (Error err = _ui->setActiveMenu(""))
            menusStopped(err);
        else
            _ui->setCatcher(_ui->catcher() & ~SpMenuHost::kCatchUi);
    }
    _scene->reset();
    _models->clear();
    _world->clear();
    _shaders->clear();
    _consoleShadersLoaded = false;
    // A level loading shuts the console (0x40aa6a).
    if (_consoleOpen)
        closeConsole();
    _weatherDrawing.reset();
    _weather.clear();
    _weather.contents = [this](const std::array<float, 3> &p) {
        return _game ? _game->worldContents({p[0], p[1], p[2]}) : 0;
    };
    _weather.loadPicture = [this](const std::string &name, WeatherPicture &out) {
        return loadWeatherPicture(_ctx.files, name, out);
    };
    _weatherTime = -1;
    _weatherError.clear();
    _input.yaw = _input.pitch = 0;
    _clientTime = 0;
    _lastMs = 0;
    const std::string &path = _names[size_t(index)];
    std::string map = path.substr(5, path.size() - 9);
    // Its dynamic music set unless the level names another (0x4371dc).
    if (_soundSystem) {
        _soundSystem->setMapName(map);
        _soundSystem->setDebugDynamic(_cvars.find("s_debugdynamic") ? int(_cvars.value("s_debugdynamic")) : 0);
    }
    showLoadingScreen(map);
    _map = map;
    _gameCommandsRun = 0;
    _gameLines.clear();
    _pendingMenu.clear();
    _pendingSave.clear();
    _waitFrames = 0;
    _capturing = false;
    setTitle(map);

    // The level: its drawing and light grid, as the Play page's.
    auto bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = _ctx.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    // Jedi Outcast's light is doubled only by its fullscreen gamma ramp.
    const bool overbright = !_ctx.game.overbrightFullscreenOnly ||
                            (videoSettings(_cvars).fullscreen && _cvars.integer("r_overBrightBits") > 0);
    _world->setOverbright(overbright);
    if (!err)
        err = _world->load(*bsp, _ctx.shaders);
    if (err)
        return stop("Couldn't load " + path + ": " + err.message);
    _grid = LightGrid{};
    float cell[3] = {64, 64, 128};
    std::vector<Entity> entities;
    if (!parseEntities(bsp->entities, entities) && !entities.empty())
        entities[0].vector("gridsize", cell);
    _grid.build(*bsp, {cell[0], cell[1], cell[2]}, overbright);

    // The game: the server half started on the level, the player joined,
    // then the client half.
    _game = std::make_unique<eliteforce::SpGameHost>(_ctx.files, _cvars);
    _game->print = [this](const std::string &text) { consolePrint(text); };
    _game->worldEffectCommand = [this](const std::string &line) {
        WorldEffects::Outcome o = _weather.command(line);
        if (!o.error.empty() && _weatherError.empty())
            _weatherError = o.error;
        else if (!o.understood)
            consolePrint("World effect command not done: " + line + "\n");
    };
    _game->modInstall = _ctx.installPath;
    _game->modBaseDir = _ctx.game.baseDir;
    _game->soundSystem = _soundSystem.get();
    _game->shaderExists = [this](const std::string &name) { return shaderFound(_ctx.shaders, _ctx.files, name); };
    // What the client game shows where the level has no picture of its
    // own: the saved game's, the last level's last frame, or nothing.
    _game->screenShot = std::move(_nextScreenShot);
    _nextScreenShot.clear();
    if ((err = _game->load(_dll, _ctx.game.code == GameCode::kJediOutcast)))
        return stop(_ctx.game.spGameDll + ": " + err.message);
    // A saved game's chunks are read on as the original's loader does:
    // the level started, then (but for an autosave) its time and
    // configstrings, then the game's own.
    _game->setSaveStream(nullptr, save);
    _game->spawnTarget = std::exchange(_nextSpawnTarget, std::string());
    _game->loadTransition = std::exchange(_nextLoadTransition, false);
    if ((err = _game->startLevel(map, bsp, eliteforce::SpGameHost::mapChecksum(data), 0, loadMode)))
        return stop("The game couldn't start the level: " + err.message);
    if (save) {
        err = _game->jedi() ? jk2::readSaveLevel(*save, *_game, loadMode == 2)
                            : eliteforce::readSaveLevel(*save, *_game, loadMode == 2);
        if (err)
            return stop("The saved game couldn't be read: " + err.message);
    }
    _game->setSaveStream(nullptr, nullptr);
    std::string refused;
    // The player as the original's engine describes them: the userinfo
    // cvars, with the models for the sex chosen.
    const bool male = fs::toLower(_cvars.string("sex")).rfind('m', 0) == 0;
    const PlayerModels &player = male ? _ctx.game.malePlayer : _ctx.game.femalePlayer;
    _cvars.set("legsmodel", player.legs);
    _cvars.set("torsomodel", player.torso);
    _cvars.set("headmodel", player.head);
    std::string userinfo;
    for (const CvarStore::Cvar &cv : _cvars.all())
        if (cv.flags & CvarStore::kUserInfo)
            userinfo += "\\" + cv.name + "\\" + cv.value;
    if ((err = _game->connectPlayer(userinfo, refused, loadMode)))
        return stop("The player couldn't join: " + err.message);
    if (!refused.empty())
        return stop("The game refused the player: " + refused);
    _scene->setHandles({&_game->models(), &_game->skins(), &_game->shaders()});
    // Jedi Outcast's Ghoul2 models, posed as its records in the game's
    // memory say.
    _scene->ghoul2Poses = [this](int32_t instance, int32_t time, std::vector<Ghoul2Pose> &out) {
        return _game && _game->ghoul2Drawn(uint32_t(instance), time, out);
    };
    // As it loads, the client game asks for the screen: its loading screen
    // (the level's picture, what's loading) under the menus' text.
    _game->updateScreen = [this] { showClientLoading(); };
    err = _game->startClientGame();
    if (_game)
        _game->updateScreen = nullptr;
    if (err)
        return stop("The client game couldn't start: " + err.message);
    // The player's time from where the level is (it has settled for a few
    // frames before they joined).
    _clientTime = _game->levelTime();
    // A new level (not one loaded) is saved as "auto" as the player joins,
    // as the original's (but the holodeck's and the brig's, "_" maps).
    if (loadMode == 0 && !map.empty() && map[0] != '_') {
        saveGame("auto", true);
        // Jedi Outcast's also as auto_<map> (0x43621e).
        if (_game && _game->jedi())
            saveGame("auto_" + map, true);
    } else if (_game && _game->jedi() && _game->loadTransition && !map.empty() && map[0] != '_') {
        // One loadtransition loaded from its hub save is saved again as
        // hub/<map>, then as auto, both as saves the player made (0x436283).
        saveJediGame("hub/" + map, false, _hubShot, false);
        if (_game)
            saveJediGame("auto", false, _hubShot, false);
    }
    _hubShot.clear();
}

bool SpPlayPage::readSave(const std::string &name, std::vector<uint8_t> &file) const {
    // Those written here first, then the game folder's own.
    if (!_saveDir.empty()) {
        std::ifstream in(_saveDir / (name + ".sav"), std::ios::binary);
        if (in) {
            file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }
    }
    return !_ctx.files.read("saves/" + name + ".sav", file);
}

bool SpPlayPage::saveFits(const std::vector<uint8_t> &file) const {
    const auto kind = eliteforce::saveKind(file);
    return !kind || *kind == (_expansion ? eliteforce::SaveKind::kExpansion : eliteforce::SaveKind::kOriginal);
}

void SpPlayPage::sortSaves() const {
    if (_ctx.savePath.empty())
        return;
    const std::filesystem::path original = _ctx.savePath / "saves";
    const std::filesystem::path expansion = _ctx.savePath / "expansion" / "saves";
    for (const auto &[from, to, wrong] : {std::tuple{original, expansion, eliteforce::SaveKind::kExpansion},
                                          std::tuple{expansion, original, eliteforce::SaveKind::kOriginal}}) {
        std::error_code ec;
        std::vector<std::filesystem::path> moving;
        for (std::filesystem::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
            const std::filesystem::path &p = it->path();
            if (fs::toLower(p.extension().string()) != ".sav" || !it->is_regular_file(ec))
                continue;
            std::ifstream in(p, std::ios::binary);
            if (eliteforce::saveKind(in) == wrong)
                moving.push_back(p);
        }
        for (const std::filesystem::path &p : moving) {
            std::filesystem::create_directories(to, ec);
            // With its recording of the player's commands, if there's one;
            // never over a save of the same name there.
            if (std::filesystem::exists(to / p.filename(), ec))
                continue;
            std::filesystem::rename(p, to / p.filename(), ec);
            std::filesystem::path input = p;
            input.replace_extension(".input");
            if (!ec && std::filesystem::exists(input, ec) && !std::filesystem::exists(to / input.filename(), ec))
                std::filesystem::rename(input, to / input.filename(), ec);
        }
    }
}

namespace {

// The picture at size x size, each pixel the average of those it covers,
// opaque (as it is if it's that size already).
image::Image boxScaled(const image::Image &img, int size) {
    if (img.w == size && img.h == size)
        return img;
    image::Image out;
    out.w = size;
    out.h = size;
    out.rgba.resize(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int x0 = x * img.w / size;
            const int x1 = std::max(x0 + 1, (x + 1) * img.w / size);
            const int y0 = y * img.h / size;
            const int y1 = std::max(y0 + 1, (y + 1) * img.h / size);
            int sum[3] = {};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += img.pixel(sx, sy)[c];
            const int n = (x1 - x0) * (y1 - y0);
            uint8_t *px = out.pixel(x, y);
            for (int c = 0; c < 3; ++c)
                px[c] = uint8_t(sum[c] / n);
            px[3] = 255;
        }
    return out;
}

// A Jedi Outcast saved game's picture (a JPEG) as the loading screens
// here take one: 256 x 256, rows bottom first. Empty if it won't decode.
std::vector<uint8_t> loadingShot(const std::vector<uint8_t> &jpeg) {
    image::Image img;
    if (jpeg.empty() || image::decode(jpeg, img) || img.w <= 0 || img.h <= 0)
        return {};
    const image::Image small = boxScaled(img, 256);
    std::vector<uint8_t> out(SpMenuHost::kShotSize);
    for (int y = 0; y < 256; ++y)
        std::memcpy(&out[size_t(255 - y) * 256 * 4], small.pixel(0, y), 256 * 4);
    return out;
}

bool badSaveName(const std::string &name) {
    return name.empty() || fs::toLower(name) == "current" || name.find("..") != std::string::npos ||
           name.find_first_of("/\\:") != std::string::npos;
}

} // namespace

std::vector<uint8_t> SpPlayPage::levelShot(const std::string &map) const {
    // levelshots/<map>.tga at 256 x 256, as the original's engine makes an
    // autosave's picture (renderer entry 23, docs/engines/idtech3/games/stvef/systems/sp-interface.md):
    // as it is if it's that size, otherwise each pixel the average of the
    // ones it covers, opaque; rows bottom first. Empty if there's none.
    // Its image loader takes the .jpg when there's no .tga, as the
    // renderer's always do: the game's levelshots are all .jpg.
    std::vector<uint8_t> data;
    image::Image img;
    const std::string file = findImage(_ctx.files, "levelshots/" + map + ".tga");
    if (file.empty() || _ctx.files.read(file, data) || image::decode(data, img) || img.w <= 0 || img.h <= 0)
        return {};
    // Jedi Outcast's: 512 x 512, as a JPEG.
    if (jedi())
        return image::encodeJpeg(boxScaled(img, jk2::kShotSize), jk2::kShotQuality);
    const image::Image small = boxScaled(img, 256);
    std::vector<uint8_t> out(SpMenuHost::kShotSize, 0);
    for (int y = 0; y < 256; ++y)
        std::memcpy(&out[size_t(255 - y) * 256 * 4], small.pixel(0, y), 256 * 4);
    return out;
}

void SpPlayPage::saveGame(const std::string &name, bool autosave, const std::vector<uint8_t> &picture) {
    // As the original's save command: the engine's chunks, then the
    // game's (WriteLevel), to saves/<name>.sav.
    using raven::chunkId;
    if (!_game || !_game->clientGameRunning()) {
        report("You must be in a game to save.");
        return;
    }
    if (_game->jedi()) {
        saveJediGame(name, autosave, picture);
        return;
    }
    if (!autosave) {
        if (!_map.empty() && _map[0] == '_') {
            report("Cannot save on holodeck or brig.");
            return;
        }
        if (_game->playerHealth() <= 0) {
            report("Can't savegame while dead!");
            return;
        }
        if (badSaveName(name)) {
            report("Bad savegame name.");
            return;
        }
    }
    if (_saveDir.empty()) {
        report("There's nowhere to save games.");
        return;
    }
    if (autosave)
        _game->savePlayer();
    raven::SaveWriter w;
    // Its description: the save menu's, else when and where.
    std::string comment;
    if (autosave) {
        comment = "-------> " + _map;
    } else if (_ui && !_ui->saveComment.empty()) {
        comment = _ui->saveComment;
    } else {
        char when[64] = {};
        const std::time_t t = std::time(nullptr);
        if (const std::tm *tm = std::localtime(&t))
            std::strftime(when, sizeof when, "%m/%d/%y %H:%M", tm);
        comment = std::string(when) + "  " + _map;
    }
    eliteforce::SaveHeader header;
    header.comment = comment;
    header.map = _map;
    // An autosave's picture is the level's own, as the original's (the
    // screen as captured if it has none); a save the player makes, the
    // screen.
    if (autosave || !_ui)
        header.shot = levelShot(_map);
    // Without one, the screen as the last level ended (the capture the
    // engine holds for the loading screen).
    if (autosave && header.shot.empty() && _game->screenShot.size() == SpMenuHost::kShotSize)
        header.shot = _game->screenShot;
    if (header.shot.empty())
        header.shot = _ui ? _ui->capture() : std::vector<uint8_t>(SpMenuHost::kShotSize, 0);
    header.autosave = autosave;
    eliteforce::writeSaveHeader(w, _cvars, header);
    if (Error err = eliteforce::writeSaveLevel(w, *_game, autosave))
        return stop("The game couldn't save: " + err.message);
    if (writeSaveFile(name, w.bytes()) && !autosave)
        report("Saved " + name);
}

bool SpPlayPage::writeSaveFile(const std::string &name, const std::vector<uint8_t> &bytes) {
    // Written as "current", then renamed, as the original's.
    std::error_code ec;
    const std::filesystem::path file = _saveDir / (name + ".sav");
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = _saveDir / "current.sav";
    {
        std::ofstream out(temp, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) {
            report("Failed to create savegame " + temp.string());
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec); // replaces the old one, which is kept if this fails
    if (ec) {
        report("Couldn't write " + file.string() + ": " + ec.message());
        return false;
    }
    return true;
}

void SpPlayPage::saveJediGame(std::string name, bool autosave, const std::vector<uint8_t> &picture, bool command) {
    // As jk2sp.exe's save command (0x438390) and its saver (0x438aa0).
    if (!autosave && command) {
        if (_game->playerHealth() <= 0) {
            report("Can't savegame while dead!");
            return;
        }
        if (fs::toLower(name) == "current") {
            report("Can't save to 'current'");
            return;
        }
        if (badSaveName(name)) {
            report("Bad savegame name.");
            return;
        }
    }
    if (!autosave) {
        // The saver's own checks (0x438a20), silent: no cinematic playing
        // (one in game by its flag, 0x4051e0; any other takes the client
        // out of the level), not on a "_" map, the player alive, and where
        // the game allows it.
        if (_cinematic || (!_map.empty() && _map[0] == '_') || _game->playerHealth() <= 0 ||
            !_game->allowedToSaveHere(0))
            return;
    }
    if (!autosave && command) {
        // The menus' quick save and autosave names, "quik*" and "auto*",
        // save as quik and auto.
        if (const std::string lower = fs::toLower(name); lower == "quik*" || lower == "auto*")
            name.resize(4);
    }
    if (_saveDir.empty()) {
        report("There's nowhere to save games.");
        return;
    }
    // The picture: an autosave's is the level's own, else the screen,
    // which is taken as the next frame is drawn (see update()).
    std::vector<uint8_t> shot = picture;
    if (shot.empty() && (autosave || !command))
        shot = levelShot(_map);
    if (shot.empty() && !autosave && menusHaveKeys())
        shot = _menuShot;
    if (shot.empty() && !autosave && command) {
        _pendingSave = name;
        return;
    }
    if (shot.empty()) {
        image::Image black;
        black.w = jk2::kShotSize;
        black.h = jk2::kShotSize;
        black.rgba.assign(size_t(black.w) * size_t(black.h) * 4, 0);
        shot = image::encodeJpeg(black, jk2::kShotQuality);
    }
    if (autosave)
        _game->savePlayer();
    raven::SaveWriter w;
    jk2::SaveHeader header;
    header.comment = _ui && !autosave ? _ui->saveComment : std::string();
    if (header.comment.empty())
        header.comment = "---> " + _map;
    header.time = int32_t(std::time(nullptr));
    header.map = _map;
    header.shot = std::move(shot);
    header.autosave = autosave;
    jk2::writeSaveHeader(w, _cvars, header);
    if (Error err = jk2::writeSaveLevel(w, *_game, autosave))
        return stop("The game couldn't save: " + err.message);
    if (!writeSaveFile(name, w.bytes()))
        return;
    if (!autosave && command)
        report("Saving game \"" + name + "\"... Done.");
}

std::string SpPlayPage::savedMap(const std::string &name) const {
    std::vector<uint8_t> file;
    if (!readSave(name, file))
        return {};
    raven::SaveReader r(std::move(file));
    if (jedi()) {
        CvarStore ignored;
        jk2::SaveHeader header;
        return jk2::readSaveHeader(r, ignored, header) ? std::string() : header.map;
    }
    using raven::chunkId;
    std::vector<uint8_t> skip;
    std::string map;
    if (!r.next(chunkId("COMM"), skip) && !r.next(chunkId("SHOT"), skip))
        r.nextText(chunkId("MPCM"), map);
    return map;
}

bool SpPlayPage::loadGame(std::string name, bool command) {
    // As the original's load command: the engine's chunks, then the level
    // started for them and the game's own (start()).
    using raven::chunkId;
    if (command && name == "*respawn" && jedi()) {
        // After dying (0x438230): the level's autosave if it's of this
        // level or nothing was loaded; else the saved game last loaded.
        // In the brig, the autosave if it can be read at all.
        const std::string autoMap = savedMap("auto");
        if (_lastLoaded.empty() || (_map == "_brig" ? !autoMap.empty() : autoMap == _map))
            name = "auto";
        else
            name = _lastLoaded;
    } else if (command && name == "*respawn") {
        // After dying: the saved game last loaded, if it's of this level;
        // else the level's start.
        name = !_lastLoaded.empty() && savedMap(_lastLoaded) == _map ? _lastLoaded : "auto";
    }
    if (command && badSaveName(name)) {
        report("Bad loadgame name.");
        return false;
    }
    std::vector<uint8_t> file;
    if (!readSave(name, file)) {
        report("Failed to open savegame " + name);
        return false;
    }
    if (!saveFits(file)) {
        report(name + (_expansion ? " was saved without the Expansion Pack, and can't be loaded with it on"
                                  : " was saved with the Expansion Pack, and can't be loaded with it off"));
        return false;
    }
    raven::SaveReader r(std::move(file));
    std::string map;
    bool autosave = false;
    std::vector<uint8_t> shot;
    Error err;
    if (jedi()) {
        jk2::SaveHeader header;
        err = jk2::readSaveHeader(r, _cvars, header);
        map = header.map;
        autosave = header.autosave;
        shot = loadingShot(header.shot);
    } else {
        eliteforce::SaveHeader header;
        err = eliteforce::readSaveHeader(r, _cvars, header);
        map = header.map;
        autosave = header.autosave;
        shot = std::move(header.shot);
    }
    if (err) {
        report("Couldn't read " + name + ": " + err.message);
        return false;
    }
    const auto it = std::find(_names.begin(), _names.end(), "maps/" + fs::toLower(map) + ".bsp");
    if (it == _names.end()) {
        report(name + " is of a map that isn't here: " + map);
        return false;
    }
    _lastLoaded = name;
    report("Loaded " + name);
    if (_ui)
        _ui->setCapture(shot); // the loading screen shows it
    _nextScreenShot = shot;    // and so does the client game's
    start(int(it - _names.begin()), autosave ? 2 : 1, &r);
    if (_game && !_saveDir.empty()) {
        _recording.open(_saveDir / (name + ".input"), std::ios::trunc);
        _recording << "# reluva input recording: serverTime buttons weapon pitch yaw roll forward right up\n";
    }
    return true;
}

void SpPlayPage::loadTransition(const std::vector<std::string> &w) {
    // As jk2sp.exe's loadtransition (0x435280): the player carried over,
    // the level left saved as hub/<map>, then the next map's hub save
    // loaded, or if there's none the map started as by maptransition; the
    // game told of a level change either way.
    if (_game) {
        _game->savePlayer();
        saveJediGame("hub/" + _map, false, _hubShot, false);
    }
    _nextSpawnTarget = w.size() >= 3 ? w[2] : std::string();
    _nextLoadTransition = true;
    if (!loadGame("hub/" + fs::toLower(w[1]), false)) {
        const int index = mapIndex(w[1]);
        if (index < 0) {
            _error = "There's no map " + w[1];
        } else {
            if (_ui)
                _ui->setCapture({});
            if (w[1][0] != '_' && !_saveDir.empty()) {
                std::error_code ec;
                std::filesystem::remove(_saveDir / "auto.sav", ec);
            }
            start(index);
        }
    }
    _nextSpawnTarget.clear();
    _nextLoadTransition = false;
}

int SpPlayPage::mapIndex(std::string map) const {
    map = fs::toLower(map);
    if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
        map.resize(map.size() - 4);
    const auto it = std::find(_names.begin(), _names.end(), "maps/" + map + ".bsp");
    return it == _names.end() ? -1 : int(it - _names.begin());
}

void SpPlayPage::runPendingTransition() {
    const std::string line = std::move(_pendingTransition);
    _pendingTransition.clear();
    _runningTransition = true;
    runCommand(line, true);
    _runningTransition = false;
}

void SpPlayPage::runGameCommands() {
    // What the game sent to the console (a level change, a load after
    // dying...), run as the original's console would.
    if (!_game)
        return;
    const auto &all = _game->consoleCommands();
    for (size_t i = _gameCommandsRun; i < all.size(); ++i)
        for (const std::string &line : splitCommands(all[i]))
            _gameLines.push_back(line);
    _gameCommandsRun = all.size();
    // A cinematic started holds the rest, as the original's game is paused
    // under it (cl_paused) before it sends any more; "wait n" holds them n
    // frames (one if no number), as the console's command buffer does.
    if (_waitFrames > 0)
        --_waitFrames;
    size_t run = 0;
    while (run < _gameLines.size() && !_cinematic && _waitFrames == 0) {
        const std::vector<std::string> words = splitCommand(_gameLines[run]);
        if (!words.empty() && fs::toLower(words[0]) == "wait") {
            _waitFrames = words.size() >= 2 ? std::max(1, std::atoi(words[1].c_str())) : 1;
            ++run;
            continue;
        }
        runCommand(_gameLines[run++], true);
    }
    _gameLines.erase(_gameLines.begin(), _gameLines.begin() + std::ptrdiff_t(run));
}

void SpPlayPage::runBinding(const std::string &line, int32_t key, bool down) {
    // As typed at the original's console: the engine's own commands (save,
    // load...) first, then the menus' and the game's.
    _input.runBinding(line, key, down, [this](const std::string &text) { runCommand(text); });
}

bool SpPlayPage::takesKey(const Event &ev) const {
    return ev.type == EventType::kKeyDown && ev.key == key::kEscape && !(ev.mods & keymod::kCtrl);
}

void SpPlayPage::handleEvent(const Event &ev) {
    if (!_renderer)
        return;
    // The joystick, read once a frame (pollJoystick).
    if (ev.type == EventType::kJoyButtonDown || ev.type == EventType::kJoyButtonUp || ev.type == EventType::kJoyAxis ||
        ev.type == EventType::kJoyHat || ev.type == EventType::kJoyRemoved) {
        _joystick.event(ev);
        return;
    }
    if (_consoleOpen) {
        // The console has the keys: typing, the history, scrolling; Esc
        // or the console key shut it.
        switch (_console.handle(ev, true)) {
        case ConsoleState::Action::kClose:
            closeConsole();
            break;
        case ConsoleState::Action::kSubmit:
            if (std::string line = _console.submit(true); !line.empty()) {
                // Every line a command, a leading / or \ left out.
                if (line[0] == '/' || line[0] == '\\')
                    line.erase(0, 1);
                runCommand(line);
            }
            break;
        default:
            break;
        }
        return;
    }
    if (ev.type == EventType::kKeyDown && ev.key == '`' && !ev.repeat && !(ev.mods & keymod::kCtrl)) {
        // The console key, whatever it's bound to (0x405200: toggleconsole).
        openConsole('`');
        return;
    }
    if (_cinematic) {
        // A typed key (Esc too) or a click skips it, as the original's;
        // nothing else gets through meanwhile.
        if (ev.type == EventType::kKeyDown && !ev.repeat && !(ev.mods & keymod::kCtrl)) {
            const int32_t k = toQ3Key(ev.key);
            if (k > 0 && k < 128)
                endCinematic();
        } else if (ev.type == EventType::kMouseDown && toQ3Button(ev.button) == q3key::kMouse1) {
            endCinematic();
        }
        return;
    }
    switch (ev.type) {
    case EventType::kKeyDown:
    case EventType::kKeyUp: {
        const bool down = ev.type == EventType::kKeyDown;
        if (kDeveloperKeys && down && (ev.mods & keymod::kCtrl) && !_names.empty()) {
            // Reluva's own, for working on the engine: restart, another map
            // (without the menus), the info line.
            const int n = int(_names.size());
            if (ev.key == 'r') {
                if (!_game && hasMenus()) {
                    _ui.reset();
                    _error.clear();
                    return startMenus();
                }
                return start(_index);
            }
            if (ev.key == key::kPageUp || ev.key == key::kPageDown)
                return start((_index + (ev.key == key::kPageUp ? n - 1 : 1)) % n);
            if (ev.key == 'i') {
                _showInfo = !_showInfo;
                return;
            }
        }
        const int32_t k = toQ3Key(ev);
        if (_ui && k) {
            if (down)
                _ui->keysDown.insert(k);
            else
                _ui->keysDown.erase(k);
        }
        if (down && ev.key == key::kEscape) {
            // As the original's engine: to the menus if they have the
            // keys, else the in-game menu (the main one without a game).
            if (!_ui) {
                _ctx.requestPage = "Start";
                return;
            }
            if (menusHaveKeys())
                return menuKey(q3key::kEscape);
            if (_game && _game->clientGameRunning()) {
                // The game is captured first (the next frame), for the
                // save menu's picture.
                _pendingMenu = "ingame";
                return;
            }
            if (Error err = _ui->setActiveMenu("main"))
                return menusStopped(err);
            return runMenuCommands();
        }
        if (!k)
            return;
        if (down) {
            // Presses go to the menus while they have the keys (repeats
            // too, for scrolling); releases always end what a key held.
            if (menusHaveKeys()) {
                // (Not Reluva's own numbers, which they don't know.)
                if (k < q3key::kFirstOwn)
                    menuKey(k);
                return;
            }
            if (!ev.repeat)
                runBinding(_ctx.bindings.get(k), k, true);
        } else {
            runBinding(_ctx.bindings.get(k), k, false);
        }
        break;
    }
    case EventType::kTextInput:
        // Characters for the menus' text fields, as key presses with the
        // character flag (Latin-1).
        if (menusHaveKeys())
            for (uint32_t cp : decodeUtf8(ev.text))
                if (cp >= 32 && cp < 256 && cp != 127 && _ui)
                    menuKey(int32_t(cp) | q3key::kCharFlag);
        break;
    case EventType::kMouseDown:
    case EventType::kMouseUp: {
        const int32_t k = toQ3Button(ev.button);
        if (!k)
            break;
        const bool down = ev.type == EventType::kMouseDown;
        if (_ui) {
            if (down)
                _ui->keysDown.insert(k);
            else
                _ui->keysDown.erase(k);
        }
        if (down && menusHaveKeys())
            return menuKey(k);
        runBinding(_ctx.bindings.get(k), k, down);
        break;
    }
    case EventType::kMouseWheel:
        if (ev.wheelY && menusHaveKeys())
            menuKey(ev.wheelY > 0 ? q3key::kWheelUp : q3key::kWheelDown);
        break;
    case EventType::kMouseMove:
        if (menusHaveKeys()) {
            // In the menus' 640 x 480, as the window's pixels are to the
            // frame (the fractions kept for the next move).
            _menuMouseX += float(ev.moveX) * 640.0f / float(std::max(_frameW, 1));
            _menuMouseY += float(ev.moveY) * 480.0f / float(std::max(_frameH, 1));
            const auto dx = int32_t(_menuMouseX);
            const auto dy = int32_t(_menuMouseY);
            _menuMouseX -= float(dx);
            _menuMouseY -= float(dy);
            if (dx || dy)
                if (Error err = _ui->mouseEvent(dx, dy))
                    menusStopped(err);
        } else {
            _input.addMouse(ev.moveX, ev.moveY);
        }
        break;
    default:
        break;
    }
}

void SpPlayPage::runGame(uint64_t nowMs) {
    if (!_game || !_game->clientGameRunning())
        return;
    // At most 200 ms a frame, as the original's client takes, so a slow
    // frame doesn't leave the game's clock behind for long.
    const int32_t dt = _lastMs ? int32_t(std::min<uint64_t>(nowMs - _lastMs, 200)) : 0;
    _lastMs = nowMs;
    auto setting = [&](const char *name, float fallback) { return _cvars.find(name) ? _cvars.value(name) : fallback; };
    // Looking: the mouse and keys, as the Play page.
    _input.turn(_cvars, dt);

    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };
    // timescale speeds the game's clock, as the original's: the game sets
    // it to 100 to fast-forward a scene the player skips.
    const float timescale = std::clamp(setting("timescale", 1), 0.0f, 100.0f);
    if (dt > 0)
        _clientTime += int32_t(std::lround(float(dt) * timescale));
    // The server kept a frame ahead of the player's time, then the
    // player's command, then the client game's frame.
    const auto serverStart = Clock::now();
    // At most so much of the game's time a frame (the emulated game can't
    // run a skipped scene's timescale 100 as the original could): what
    // doesn't fit is let go rather than owed, so the page keeps up.
    // One snapshot after the frames run, as the original's server sends.
    bool ran = false;
    while (_game->levelTime() <= _clientTime) {
        if (Error err = _game->runFrame(false))
            return stop("The game stopped: " + err.message);
        ran = true;
        if (ms(serverStart) > kServerBudgetMs && _game->levelTime() <= _clientTime) {
            _clientTime = _game->levelTime() - 1;
            break;
        }
    }
    if (ran)
        _game->sendSnapshot();
    if (dt > 0) {
        eliteforce::SpGameHost::UserCommand cmd;
        cmd.serverTime = _clientTime;
        const PlayerInput::Command c = _input.command(_cvars, _ctx.game);
        cmd.angles = c.angles;
        cmd.buttons = c.buttons;
        cmd.forward = c.forward;
        cmd.right = c.right;
        cmd.up = c.up;
        if (Error err = _game->clientThink(cmd))
            return stop("The game stopped: " + err.message);
        // (The weapon as the game was given it: the client game's choice.)
        if (_recording.is_open())
            _recording << cmd.serverTime << ' ' << cmd.buttons << ' ' << int(_game->weaponSelect()) << ' '
                       << cmd.angles[0] << ' ' << cmd.angles[1] << ' ' << cmd.angles[2] << ' ' << int(cmd.forward)
                       << ' ' << int(cmd.right) << ' ' << int(cmd.up) << std::endl; // flushed: kept if closed
    }
    _timing.server = ms(serverStart);
    const auto clientStart = Clock::now();
    if (_soundSystem) {
        _soundSystem->setTime(nowMs);
        // (Before the client game, which starts the music as it begins.)
        _soundSystem->setDebugDynamic(int(setting("s_debugdynamic", 0)));
    }
    if (Error err = _game->drawClientFrame(_clientTime))
        return stop("The client game stopped: " + err.message);
    _timing.client = ms(clientStart);
    if (_soundSystem && jedi()) {
        // The music's state as the game asks for it (configstring 907).
        const auto &strings = _game->configstrings();
        const auto it =
            strings.find(_game->apiVersion() == eliteforce::SpGameHost::kJedi103ApiVersion ? kMusicStateConfigString103
                                                                                           : kMusicStateConfigString);
        _soundSystem->setMusicState(it != strings.end() ? &it->second : nullptr);
        _soundSystem->setAllowDynamicMusic(setting("s_allowDynamicMusic", 1) != 0);
    }
    if (_soundSystem)
        _soundSystem->endFrame();
}

void SpPlayPage::pollJoystick() {
    // As the Play page's: the joystick's keys as keys (to the menus while
    // they have the keys), its analog axes moving and turning the player.
    const JoystickInput::Frame f = _joystick.frame(_cvars);
    const bool menus = menusHaveKeys();
    _input.setJoystick(menus || _cinematic ? 0 : f.x, menus || _cinematic ? 0 : f.y);
    for (const auto &[k, down] : f.keys) {
        if (_cinematic)
            continue; // only typed keys and clicks skip one
        if (_ui) {
            if (down)
                _ui->keysDown.insert(k);
            else
                _ui->keysDown.erase(k);
        }
        if (down && menusHaveKeys()) {
            menuKey(k);
            continue;
        }
        runBinding(_ctx.bindings.get(k), k, down);
        if (!_renderer)
            return;
    }
}

void SpPlayPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    pollJoystick();
    if (!_renderer)
        return;
    _nowMs = nowMs;
    if (!_startMs)
        _startMs = nowMs;
    _scene->newFrame();
    if (_cinematic) {
        // A cinematic has the screen (the game waits under one in game).
        runCinematic(nowMs);
        if (_cinematic && _cinematic->crawl && nowMs - _cinematic->startMs >= kCrawlEndMs)
            endCinematic();
        if (_cinematic) {
            _renderer->setGamma(brightness(_ctx));
            _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
            if (!_cinematic->rgba.empty())
                _scene->drawImage(_cinematic->rgba.data(), _cinematic->movie.width(), _cinematic->movie.height(), 0, 0,
                                  640, 480, 640, 480);
            if (_cinematic->crawl)
                drawTextCrawl(nowMs - _cinematic->startMs);
            // The console over it, if it's open (one in a level leaves it).
            if (!_ctx.game.spConsoleVersion.empty())
                drawConsole(nowMs, float(nowMs - _startMs) / 1000.0f);
            std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
            if (_ctx.game.spConsoleVersion.empty() && _consoleOpen)
                drawOwnConsole(_overlay, _console, (nowMs / 250) % 2 == 0);
            _renderer->endFrame(&_overlay);
            return;
        }
    }
    if (_capturing) {
        // The last frame was kept: the save menu's picture, at 256 x 256,
        // then the menu the player asked for; or the next level's loading
        // screen's, then the level change.
        _capturing = false;
        std::vector<uint8_t> rgba;
        std::vector<uint8_t> shot;
        const int w = _renderer->width();
        const int h = _renderer->height();
        if (_renderer->takeCapture(rgba) && w > 0 && h > 0 && rgba.size() >= size_t(w) * size_t(h) * 4) {
            shot.resize(SpMenuHost::kShotSize);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    std::memcpy(&shot[size_t((255 - y) * 256 + x) * 4],
                                &rgba[(size_t(y * h / 256) * size_t(w) + size_t(x * w / 256)) * 4], 4);
        }
        const bool hub = !_pendingTransition.empty() && jedi() &&
                         fs::toLower(splitCommand(_pendingTransition).front()) == "loadtransition";
        if (!_pendingSave.empty() || (jedi() && (!_pendingMenu.empty() || hub))) {
            // Jedi Outcast's picture: 512 x 512, as a JPEG. The game as
            // the in-game menu opens is kept for a save made from it.
            std::vector<uint8_t> jpeg;
            if (!shot.empty()) {
                image::Image frame;
                frame.w = w;
                frame.h = h;
                frame.rgba = std::move(rgba);
                jpeg = image::encodeJpeg(boxScaled(frame, jk2::kShotSize), jk2::kShotQuality);
            }
            if (hub)
                _hubShot = jpeg;
            const std::string name = std::exchange(_pendingSave, std::string());
            if (name.empty() && !_pendingMenu.empty())
                _menuShot = std::move(jpeg);
            else if (!jpeg.empty())
                saveGame(name, false, jpeg);
            else
                report("Failed to save " + name + ": the screen couldn't be taken");
        }
        if (!_pendingTransition.empty()) {
            _nextScreenShot = shot;
            runPendingTransition();
        }
        if (_ui && !_pendingMenu.empty()) {
            _ui->setCapture(shot);
            if (Error err = _ui->setActiveMenu(_pendingMenu))
                menusStopped(err);
            else
                runMenuCommands();
        }
        _pendingMenu.clear();
    } else if (!_pendingTransition.empty()) {
        runPendingTransition(); // no frame was drawn to take it from
    }
    // The game waits while a menu has the keys, as the original's single
    // player pauses (cl_paused, which the in-game menu sets, back to 0
    // after); typed text goes to them meanwhile.
    // (The console doesn't pause it: jk2sp.exe's only takes the keys.)
    const bool paused = menusHaveKeys();
    if ((paused || _consoleOpen) != _textInput) {
        _textInput = paused || _consoleOpen;
        _ctx.sys.setTextInput(_textInput);
    }
    if (paused) {
        _lastMs = nowMs;
    } else {
        if (_cvars.integer("cl_paused") != 0)
            _cvars.set("cl_paused", "0");
        runGame(nowMs);
    }
    runGameCommands();
    if (_soundSystem) {
        auto setting = [&](const char *name, float fallback) {
            return _cvars.find(name) ? _cvars.value(name) : fallback;
        };
        // Effects silent while a scene is skipped (fast-forwarded, its
        // lines would pile up); those playing when the skip starts are
        // stopped, as the original's client does (S_StopSounds), so they
        // don't carry on after it.
        const bool skipping = _game && _cvars.integer("skippingCinematic") != 0;
        if (skipping && !_wasSkipping)
            _soundSystem->stopSounds();
        _wasSkipping = skipping;
        _soundSystem->setVolumes(skipping ? 0.0f : setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
    }

    // The menus: the main one whenever there's no game and none is open,
    // as the original's engine; whether they cover the game; their frame.
    bool fullscreen = false;
    if (_ui) {
        _ui->connectionState = _game && _game->clientGameRunning() ? 7 : 1;
        Error err;
        if (!_game && !menusHaveKeys())
            err = _ui->setActiveMenu("main");
        if (!err)
            fullscreen = _ui->fullscreen();
        if (!err)
            err = _ui->refresh(int32_t(nowMs - _startMs));
        if (err)
            menusStopped(err);
        else
            runMenuCommands();
    }

    // A weather command that couldn't be done ends the level, as the
    // original's error does (0x4994cc).
    if (!_weatherError.empty())
        return stop(std::exchange(_weatherError, std::string()));
    _renderer->setGamma(brightness(_ctx));
    const float time = float(_clientTime) / 1000.0f;
    const auto drawStart = std::chrono::steady_clock::now();
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    if (_game && _game->clientGameRunning() && !fullscreen) {
        // Each scene over the pictures drawn before it, the rest (the HUD)
        // over them all, as the Play page draws.
        _scene->setHandles({&_game->models(), &_game->skins(), &_game->shaders()});
        const Scene &scene = _game->scene();
        const auto &pictures = _game->pictures();
        const bool dynamicLights = !_cvars.find("r_dynamiclight") || _cvars.integer("r_dynamiclight") != 0;
        if (_game->jedi()) {
            _world->setLightStyles(_game->lightStyles());
            _world->setSurfaceSpriteWeather(weatherSprites(_weather, _cvars));
            _grid.setStyles(_game->lightStyles());
        }
        size_t drawn = 0;
        size_t shots = 0;
        bool first = true;
        bool weather = _weather.raining() || _weather.snowing();
        for (const auto &v : scene.views) {
            drawGamePictures(drawn, shots, v.picturesBefore, time);
            // The weather with the first view of the level (refdef_t's
            // flags at 76: 1 no world).
            if (weather && v.refdef.size() >= 80 && !(v.refdef[76] & 1)) {
                prepareWeather(v);
                weather = false;
            }
            _scene->drawView(scene, v, 640, 480, time, first, _world.get(), &_grid, dynamicLights);
            first = false;
        }
        drawGamePictures(drawn, shots, pictures.size(), time);
    }
    if (_ui)
        drawMenus(float(nowMs - _startMs) / 1000.0f);
    if (!_ctx.game.spConsoleVersion.empty())
        drawConsole(nowMs, time);
    _timing.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
    drawOverlay();
    if ((!_pendingMenu.empty() || !_pendingTransition.empty() || !_pendingSave.empty()) && !_capturing) {
        _renderer->captureNextFrame();
        _capturing = true;
    }
    _renderer->endFrame(&_overlay);
}

void SpPlayPage::drawMenus(float time) {
    // The menus' last frame, with any models they show and raw pictures.
    _scene->setHandles({&_ui->models(), &_ui->skins(), &_ui->shaders()});
    const auto &pictures = _ui->pictures();
    size_t drawn = 0;
    for (const auto &v : _ui->scene().views) {
        _scene->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
        drawn = std::max(drawn, v.picturesBefore);
        _scene->drawView(_ui->scene(), v, 640, 480, time, false, nullptr, nullptr, true);
    }
    _scene->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    for (const auto &img : _ui->rawImages())
        _scene->drawImage(img.rgba.data(), img.cols, img.rows, img.x, img.y, img.w, img.h, 640, 480);
}

void SpPlayPage::showLoadingScreen(const std::string &map) {
    // As the original's engine while a level loads: the menus' loading
    // screen (a saved game's picture, or theirs), shown at once.
    if (!_ui || !_renderer)
        return;
    _ui->connectionState = 5;
    if (Error err = _ui->drawLoadingScreen(map, "")) {
        menusStopped(err);
        return;
    }
    _scene->newFrame();
    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    drawMenus(float(_nowMs - _startMs) / 1000.0f);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    _renderer->endFrame(&_overlay);
}

void SpPlayPage::showClientLoading() {
    // What the client game has registered so far is loaded now, as the
    // original's renderer loads it as it's registered: the loading
    // screen's stages then take as long as the loading does.
    if (!_game)
        return;
    _scene->setHandles({&_game->models(), &_game->skins(), &_game->shaders()});
    _scene->preload();
    if (_game->drawLoadingFrame())
        return;
    if (_ui)
        if (Error err = _ui->drawConnectText("localhost", ""))
            menusStopped(err); // the loading screen goes on without their text
    _scene->newFrame();
    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    _scene->setHandles({&_game->models(), &_game->skins(), &_game->shaders()});
    size_t drawn = 0;
    size_t shots = 0;
    for (const auto &v : _game->scene().views) {
        drawGamePictures(drawn, shots, v.picturesBefore, 0);
        _scene->drawView(_game->scene(), v, 640, 480, 0, true, nullptr, nullptr, false);
    }
    drawGamePictures(drawn, shots, _game->pictures().size(), 0);
    if (_ui)
        drawMenus(0);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    _renderer->endFrame(&_overlay);
}

void SpPlayPage::drawGamePictures(size_t &drawn, size_t &shotsDrawn, size_t to, float time) {
    // The client game's pictures up to `to`, with the screen capture where
    // it drew that between them.
    const auto &pictures = _game->pictures();
    const auto &shots = _game->screenShotDraws();
    for (; shotsDrawn < shots.size() && shots[shotsDrawn].picturesBefore <= to; ++shotsDrawn) {
        const auto &s = shots[shotsDrawn];
        _scene->drawPictures(pictures, drawn, s.picturesBefore, 640, 480, time);
        drawn = std::max(drawn, s.picturesBefore);
        if (_game->screenShot.size() == SpMenuHost::kShotSize)
            _scene->drawImage(_game->screenShot.data(), 256, 256, s.x, s.y, s.w, s.h, 640, 480);
    }
    _scene->drawPictures(pictures, drawn, to, 640, 480, time);
    drawn = std::max(drawn, to);
}

void SpPlayPage::prepareWeather(const View &view) {
    // As jk2sp.exe's world effects pass (0x48eca0): moved on by the frame's
    // time (none while the game waits), drawn round the eye.
    const float seconds = _weatherTime < 0 ? 0 : float(std::max(_clientTime - _weatherTime, 0)) / 1000.0f;
    _weatherTime = _clientTime;
    _weatherDrawing.frame(_weather, view.refdef, seconds, _frameH, *_shaders, _ctx.files, *_scene);
}

void SpPlayPage::consolePrint(const std::string &text) {
    // Into the console, each line also shown over the game a while, as
    // the original's prints (and on the terminal, for following a run).
    std::fputs(text.c_str(), stdout);
    _console.print(text, true, _nowMs);
}

void SpPlayPage::report(const std::string &text) {
    consolePrint(text + "\n");
}

void SpPlayPage::openConsole(uint32_t key) {
    // The keys held let go; the notify lines cleared, as the original's
    // toggle does (0x405200).
    _input.releaseAll();
    _console.beginTyping(key);
    _console.clearNotify();
    _consoleOpen = true;
}

void SpPlayPage::closeConsole() {
    _console.clearField();
    _console.clearNotify();
    _consoleOpen = false;
}

void SpPlayPage::drawConsole(uint64_t nowMs, float time) {
    // As jk2sp.exe's, which draws it as its multiplayer engine's does
    // (console.md): dropped down over everything, or shut, the recent
    // lines over the game while no menu has the keys.
    const float speed = _cvars.find("scr_conspeed") ? _cvars.value("scr_conspeed") : 3.0f;
    const float frac = _console.slide(nowMs, _consoleOpen, speed);
    if (!_consoleShadersLoaded) {
        _consoleShaders.charSet = _shaders->material("gfx/2d/charsgrid_med", Lighting::kTwoD);
        _consoleShaders.console = _shaders->material("console", Lighting::kTwoD);
        _consoleShaders.white = _shaders->material("white", Lighting::kTwoD);
        _consoleShadersLoaded = true;
    }
    ConsoleFrame f;
    f.screenW = _frameW;
    f.screenH = _frameH;
    f.frac = frac;
    f.version = _ctx.game.spConsoleVersion;
    f.lines = &_console.lines();
    f.scroll = _console.scroll();
    f.typing = _consoleOpen;
    f.input = _console.field();
    f.cursor = _console.cursor();
    f.fieldScroll = _console.fieldScroll();
    f.overstrike = _console.overstrike();
    f.cursorOn = ((nowMs >> 8) & 1) == 0;
    _console.setWrapWidth(_frameW / 8 - 2);
    if (!menusHaveKeys() && _game) {
        const float notifyMs = 1000.0f * (_cvars.find("con_notifytime") ? _cvars.value("con_notifytime") : 3.0f);
        f.notify = _console.notify(nowMs, notifyMs);
    }
    _scene->drawMaterialPictures(consolePictures(f, _consoleShaders), 640, 480, time);
}

void SpPlayPage::drawOverlay() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    if (_ctx.game.spConsoleVersion.empty()) {
        // Reluva's own console for a game whose original's isn't drawn:
        // open, over the top half; shut, the recent lines at the top.
        const bool blink = (_nowMs / 250) % 2 == 0;
        _console.expireNotify(_nowMs, 3000);
        if (_consoleOpen) {
            drawOwnConsole(_overlay, _console, blink);
        } else if (!menusHaveKeys()) {
            int notifyY = 4;
            for (const auto &[when, text] : _console.notifyLines()) {
                drawColouredText(_overlay, 4, notifyY, text);
                notifyY += font::kLineHeight;
            }
        }
    }
    auto line = [&](int y, const std::string &s, Color c) {
        // Text with a dark band behind it, for reading over the level.
        const int w = int(s.size()) * font::kAdvance + 8;
        for (int yy = std::max(y, 0); yy < std::min(y + font::kLineHeight, _frameH); ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int x = 0; x < std::min(w, _frameW); ++x)
                p[x * 4 + 3] = 160;
        }
        font::drawText(_overlay, 4, y, s, c);
    };
    int y = 4;
    if (!_error.empty()) {
        line(y, _error, {255, 120, 120});
        y += font::kLineHeight;
        line(y, kDeveloperKeys ? "Ctrl+R: try again   Ctrl+PgUp/PgDn: another map   Esc: menu" : "Esc: menu",
             {200, 200, 200});
        y += font::kLineHeight;
    }
    if (!_menuError.empty()) {
        line(y,
             "The game's menus stopped: " + _menuError +
                 (kDeveloperKeys ? " (Ctrl+R without a game: start them again)" : " (Ctrl+Esc: leave)"),
             {255, 120, 120});
        y += font::kLineHeight;
    }
    std::vector<std::string> bottom;
    if (const auto *ef = dynamic_cast<const eliteforce::SpUiHost *>(_ui.get()); ef && !ef->unhandled().empty()) {
        std::string calls = "Menu functions not answered yet:";
        for (const auto &[slot, count] : ef->unhandled())
            calls += " " + std::to_string(slot) + " (x" + std::to_string(count) + ")";
        bottom.push_back(calls);
    }
    if (_showInfo && _game && !_names.empty()) {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s  server %.1f ms  client %.1f ms  draw %.1f ms  (Ctrl+I hides this)",
                      _names[size_t(_index)].c_str(), _timing.server, _timing.client, _timing.draw);
        bottom.push_back(buf);
    }
    if (_game && _cvars.integer("skippingCinematic") != 0)
        bottom.push_back("Skipping the scene (the use button skips it, as in the original)");
    if (_startMs && _nowMs - _startMs < 6000)
        bottom.push_back(kDeveloperKeys
                             ? "Esc: the game's menu   Ctrl+Esc: leave   Ctrl+PgUp/PgDn: a map without the menus"
                             : "Esc: the game's menu   Ctrl+Esc: leave");
    for (size_t i = 0; i < bottom.size(); ++i)
        line(_frameH - int(bottom.size() - i) * font::kLineHeight - 4, bottom[i], {220, 220, 220});
}

} // namespace reluva::idtech3
