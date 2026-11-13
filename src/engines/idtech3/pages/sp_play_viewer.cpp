// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/pages/sp_play_viewer.h"
#include "engines/idtech3/formats/shader.h"

#include "graphics/font.h"
#include "image/image.h"
#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <tuple>

namespace reluva::idtech3 {

namespace {

constexpr float kServerBudgetMs = 40; // of the game's frames, each page frame

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

SpPlayViewer::SpPlayViewer(ViewerContext &ctx) : Page(ctx) {
    // A game with its single-player game code beside the game folder.
    if (ctx.game.code != GameCode::kEliteForce || ctx.game.spGameDll.empty() || ctx.singlePlayerPath.empty())
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

SpPlayViewer::~SpPlayViewer() {
    deactivate();
}

void SpPlayViewer::activate() {
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
    if (_ctx.mixer)
        _soundSystem = std::make_unique<SoundSystem>(*_ctx.mixer, _soundLibrary);
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
    if (!_uiDll.empty())
        startMenus();
    if (!_ui) {
        start(_index);
    } else if (!_introShown) {
        // The logo, as the original's engine plays at start-up.
        _introShown = true;
        startCinematic("eflogo", false);
    }
}

void SpPlayViewer::startCinematic(const std::string &name, bool inGame) {
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
    if (!err)
        err = c->movie.open(std::move(data));
    if (err) {
        _lastCommand = "Cinematic " + base + ": " + err.message;
        _lastCommandMs = _nowMs;
        return;
    }
    c->inGame = inGame;
    if (!inGame) {
        // The game ends, the menus close.
        endGame();
        if (_ui) {
            if (Error e = _ui->setActiveMenu(""))
                menusStopped(e);
            else
                _ui->setCatcher(_ui->catcher() & ~eliteforce::SpUiHost::kCatchUi);
        }
    }
    // Its sound whole, to play alongside.
    std::vector<int16_t> sound;
    if (!c->movie.decodeAllAudio(sound) && !sound.empty() && _ctx.mixer) {
        auto s = std::make_shared<audio::Sound>();
        s->rate = c->movie.audioRate();
        s->channels = c->movie.audioChannels();
        s->format = 1;
        s->samples = std::move(sound);
        c->sound = _ctx.mixer->play(s, audio::SoundType::Effects, audio::kMaxVolume);
    }
    _cinematic = std::move(c);
}

void SpPlayViewer::endCinematic() {
    if (!_cinematic)
        return;
    if (_cinematic->sound != audio::kNoSound && _ctx.mixer)
        _ctx.mixer->stop(_cinematic->sound);
    const bool inGame = _cinematic->inGame;
    _cinematic.reset();
    _lastMs = 0;
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

void SpPlayViewer::runCinematic(uint64_t nowMs) {
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
                _lastCommand = "Cinematic: " + err.message;
            return endCinematic();
        }
        decoded = true;
    }
    if (decoded)
        c.movie.toRgba(c.rgba);
}

void SpPlayViewer::startMenus() {
    _menuError.clear();
    _ui = std::make_unique<eliteforce::SpUiHost>(_ctx.files, _cvars);
    _ui->print = [](const std::string &text) { std::fputs(text.c_str(), stdout); };
    _ui->shaderExists = [this](const std::string &name) { return shaderFound(_ctx.shaders, _ctx.files, name); };
    _ui->bindings = &_ctx.bindings;
    _ui->startLocalSound = [this](const std::string &sound, int channel) {
        if (_soundSystem)
            _soundSystem->startLocalSound(sound, channel);
    };
    _ui->configString = [this](int32_t index, std::string &out) {
        if (!_game)
            return false;
        const auto it = _game->configstrings().find(index);
        if (it == _game->configstrings().end())
            return false;
        out = it->second;
        return true;
    };
    _ui->saveDir = _saveDir;
    _ui->modInstall = _ctx.installPath;
    _ui->modBaseDir = _ctx.game.baseDir;
    _ui->readSave = [this](const std::string &name, std::vector<uint8_t> &file) { return readSave(name, file); };
    _ui->listSave = [this](const std::string &name) {
        std::vector<uint8_t> file;
        return readSave(name, file) && saveFits(file);
    };
    _ui->askGame = [this](uint32_t flag) { return _game ? _game->allowedToSaveHere(flag) : flag; };
    if (Error err = _ui->load(_uiDll))
        return menusStopped(err);
    if (Error err = _ui->setActiveMenu("main"))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayViewer::menusStopped(const Error &err) {
    // The game goes on without them (Esc then leaves the page).
    _menuError = _ctx.game.spUiDll + ": " + err.message;
    _ui.reset();
}

bool SpPlayViewer::menusHaveKeys() const {
    return _ui && (_ui->catcher() & eliteforce::SpUiHost::kCatchUi);
}

void SpPlayViewer::menuKey(int32_t key) {
    if (!_ui)
        return;
    if (Error err = _ui->keyEvent(key))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayViewer::runMenuCommands() {
    if (!_ui)
        return;
    for (const std::string &line : _ui->takeCommands())
        runCommand(line);
}

void SpPlayViewer::runCommand(const std::string &line, bool fromGame) {
    // What the menus and the game run, as the original's console would.
    const std::vector<std::string> w = splitCommand(line);
    if (w.empty())
        return;
    _lastCommand = line;
    _lastCommandMs = _nowMs;
    const std::string cmd = fs::toLower(w[0]);
    if (cmd == "map" || cmd == "devmap" || cmd == "spmap" || cmd == "maptransition") {
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
        std::string map = fs::toLower(w[1]);
        if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
            map.resize(map.size() - 4);
        const auto it = std::find(_names.begin(), _names.end(), "maps/" + map + ".bsp");
        if (it == _names.end()) {
            _error = "There's no map " + map;
            return;
        }
        _nextSpawnTarget = cmd == "maptransition" && w.size() >= 3 ? w[2] : std::string();
        _nextLoadTransition = cmd == "maptransition";
        start(int(it - _names.begin()));
    } else if (cmd == "disconnect") {
        endGame();
        if (_ui)
            if (Error err = _ui->setActiveMenu("main"))
                menusStopped(err);
    } else if (cmd == "quit") {
        endGame();
        _ctx.requestPage = "Start";
    } else if (cmd == "turboliftmenu") {
        if (_ui && _game)
            if (Error err = _ui->setActiveMenu("turbolift"))
                menusStopped(err);
    } else if (cmd == "cinematic" || cmd == "ingamecinematic") {
        if (w.size() >= 2)
            startCinematic(w[1], cmd == "ingamecinematic");
    } else if (cmd == "music") {
        if (_soundSystem && w.size() >= 2)
            _soundSystem->startBackgroundTrack(w[1], w.size() >= 3 ? w[2] : w[1]);
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
            _lastCommand = "USAGE: save <filename>";
    } else if (cmd == "load") {
        if (w.size() == 2)
            loadGame(w[1]);
        else
            _lastCommand = "USAGE: load <filename>";
    } else if (cmd == "wipe") {
        const std::string name = w.size() == 2 ? fs::toLower(w[1]) : std::string();
        if (name.empty() || name == "auto" || name == "exitholodeck" || _saveDir.empty()) {
            _lastCommand = "Can't wipe 'auto' or 'exitholodeck'";
        } else if (badSaveName(w[1])) {
            _lastCommand = "Bad savegame name.";
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

void SpPlayViewer::deactivate() {
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
}

void SpPlayViewer::stop(const std::string &why) {
    _error = why;
    endGame();
}

void SpPlayViewer::endGame() {
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

void SpPlayViewer::start(int index, int32_t loadMode, raven::SaveReader *save) {
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
            _ui->setCatcher(_ui->catcher() & ~eliteforce::SpUiHost::kCatchUi);
    }
    _scene->reset();
    _models->clear();
    _world->clear();
    _shaders->clear();
    _input.yaw = _input.pitch = 0;
    _clientTime = 0;
    _lastMs = 0;
    const std::string &path = _names[size_t(index)];
    std::string map = path.substr(5, path.size() - 9);
    showLoadingScreen(map);
    _map = map;
    _gameCommandsRun = 0;
    _pendingMenu.clear();
    _capturing = false;
    setTitle(map);

    // The level: its drawing and light grid, as the Play page's.
    auto bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = _ctx.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = _world->load(*bsp, _ctx.shaders);
    if (err)
        return stop("Couldn't load " + path + ": " + err.message);
    _grid = LightGrid{};
    float cell[3] = {64, 64, 128};
    std::vector<Entity> entities;
    if (!parseEntities(bsp->entities, entities) && !entities.empty())
        entities[0].vector("gridsize", cell);
    _grid.build(*bsp, {cell[0], cell[1], cell[2]});

    // The game: the server half started on the level, the player joined,
    // then the client half.
    _game = std::make_unique<eliteforce::SpGameHost>(_ctx.files, _cvars);
    _game->print = [](const std::string &text) { std::fputs(text.c_str(), stdout); };
    _game->modInstall = _ctx.installPath;
    _game->modBaseDir = _ctx.game.baseDir;
    _game->soundSystem = _soundSystem.get();
    _game->shaderExists = [this](const std::string &name) { return shaderFound(_ctx.shaders, _ctx.files, name); };
    // What the client game shows where the level has no picture of its
    // own: the saved game's, the last level's last frame, or nothing.
    _game->screenShot = std::move(_nextScreenShot);
    _nextScreenShot.clear();
    if ((err = _game->load(_dll)))
        return stop(_ctx.game.spGameDll + ": " + err.message);
    // A saved game's chunks are read on as the original's loader does:
    // the level started, then (but for an autosave) its time and
    // configstrings, then the game's own.
    _game->setSaveStream(nullptr, save);
    _game->spawnTarget = std::exchange(_nextSpawnTarget, std::string());
    _game->loadTransition = std::exchange(_nextLoadTransition, false);
    if ((err = _game->startLevel(map, bsp, eliteforce::SpGameHost::mapChecksum(data), 0, loadMode)))
        return stop("The game couldn't start the level: " + err.message);
    if (save)
        if ((err = eliteforce::readSaveLevel(*save, *_game, loadMode == 2)))
            return stop("The saved game couldn't be read: " + err.message);
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
    if (loadMode == 0 && !map.empty() && map[0] != '_')
        saveGame("auto", true);
}

bool SpPlayViewer::readSave(const std::string &name, std::vector<uint8_t> &file) const {
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

bool SpPlayViewer::saveFits(const std::vector<uint8_t> &file) const {
    const auto kind = eliteforce::saveKind(file);
    return !kind || *kind == (_expansion ? eliteforce::SaveKind::kExpansion : eliteforce::SaveKind::kOriginal);
}

void SpPlayViewer::sortSaves() const {
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

std::vector<uint8_t> SpPlayViewer::levelShot(const std::string &map) const {
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
    std::vector<uint8_t> out(eliteforce::SpUiHost::kShotSize, 0);
    const bool same = img.w == 256 && img.h == 256;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            uint8_t *px = &out[size_t((255 - y) * 256 + x) * 4];
            if (same) {
                std::memcpy(px, img.pixel(x, y), 4);
                continue;
            }
            const int x0 = x * img.w / 256;
            const int x1 = std::max(x0 + 1, (x + 1) * img.w / 256);
            const int y0 = y * img.h / 256;
            const int y1 = std::max(y0 + 1, (y + 1) * img.h / 256);
            int sum[3] = {};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += img.pixel(sx, sy)[c];
            const int n = (x1 - x0) * (y1 - y0);
            for (int c = 0; c < 3; ++c)
                px[c] = uint8_t(sum[c] / n);
            px[3] = 255;
        }
    return out;
}

namespace {

bool badSaveName(const std::string &name) {
    return name.empty() || fs::toLower(name) == "current" || name.find("..") != std::string::npos ||
           name.find_first_of("/\\:") != std::string::npos;
}

} // namespace

void SpPlayViewer::saveGame(const std::string &name, bool autosave) {
    // As the original's save command: the engine's chunks, then the
    // game's (WriteLevel), to saves/<name>.sav.
    using raven::chunkId;
    if (!_game || !_game->clientGameRunning()) {
        _lastCommand = "You must be in a game to save.";
        return;
    }
    if (!autosave) {
        if (!_map.empty() && _map[0] == '_') {
            _lastCommand = "Cannot save on holodeck or brig.";
            return;
        }
        if (_game->playerHealth() <= 0) {
            _lastCommand = "Can't savegame while dead!";
            return;
        }
        if (badSaveName(name)) {
            _lastCommand = "Bad savegame name.";
            return;
        }
    }
    if (_saveDir.empty()) {
        _lastCommand = "There's nowhere to save games.";
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
    if (autosave && header.shot.empty() && _game->screenShot.size() == eliteforce::SpUiHost::kShotSize)
        header.shot = _game->screenShot;
    if (header.shot.empty())
        header.shot = _ui ? _ui->capture() : std::vector<uint8_t>(eliteforce::SpUiHost::kShotSize, 0);
    header.autosave = autosave;
    eliteforce::writeSaveHeader(w, _cvars, header);
    if (Error err = eliteforce::writeSaveLevel(w, *_game, autosave))
        return stop("The game couldn't save: " + err.message);
    // Written as "current", then renamed, as the original's.
    std::error_code ec;
    std::filesystem::create_directories(_saveDir, ec);
    const std::filesystem::path file = _saveDir / (name + ".sav");
    const std::filesystem::path temp = _saveDir / "current.sav";
    {
        std::ofstream out(temp, std::ios::binary);
        out.write(reinterpret_cast<const char *>(w.bytes().data()), std::streamsize(w.bytes().size()));
        if (!out) {
            _lastCommand = "Failed to create savegame " + temp.string();
            return;
        }
    }
    std::filesystem::rename(temp, file, ec); // replaces the old one, which is kept if this fails
    if (ec) {
        _lastCommand = "Couldn't write " + file.string() + ": " + ec.message();
        return;
    }
    if (!autosave)
        _lastCommand = "Saved " + name;
}

void SpPlayViewer::loadGame(std::string name) {
    // As the original's load command: the engine's chunks, then the level
    // started for them and the game's own (start()).
    using raven::chunkId;
    if (name == "*respawn") {
        // After dying: the saved game last loaded, if it's of this level;
        // else the level's start.
        std::string loadedMap;
        std::vector<uint8_t> file;
        if (!_lastLoaded.empty() && readSave(_lastLoaded, file)) {
            raven::SaveReader r(std::move(file));
            std::vector<uint8_t> skip;
            if (!r.next(chunkId("COMM"), skip) && !r.next(chunkId("SHOT"), skip))
                r.nextText(chunkId("MPCM"), loadedMap);
        }
        name = !_lastLoaded.empty() && loadedMap == _map ? _lastLoaded : "auto";
    }
    if (badSaveName(name)) {
        _lastCommand = "Bad loadgame name.";
        return;
    }
    std::vector<uint8_t> file;
    if (!readSave(name, file)) {
        _lastCommand = "Failed to open savegame " + name;
        return;
    }
    if (!saveFits(file)) {
        _lastCommand = name + (_expansion ? " was saved without the Expansion Pack, and can't be loaded with it on"
                                          : " was saved with the Expansion Pack, and can't be loaded with it off");
        return;
    }
    raven::SaveReader r(std::move(file));
    eliteforce::SaveHeader header;
    if (Error err = eliteforce::readSaveHeader(r, _cvars, header)) {
        _lastCommand = "Couldn't read " + name + ": " + err.message;
        return;
    }
    const std::string &map = header.map;
    const bool autosave = header.autosave;
    std::vector<uint8_t> &shot = header.shot;
    const auto it = std::find(_names.begin(), _names.end(), "maps/" + fs::toLower(map) + ".bsp");
    if (it == _names.end()) {
        _lastCommand = name + " is of a map that isn't here: " + map;
        return;
    }
    _lastLoaded = name;
    _lastCommand = "Loaded " + name;
    if (_ui)
        _ui->setCapture(shot); // the loading screen shows it
    _nextScreenShot = shot;    // and so does the client game's
    start(int(it - _names.begin()), autosave ? 2 : 1, &r);
    if (_game && !_saveDir.empty()) {
        _recording.open(_saveDir / (name + ".input"), std::ios::trunc);
        _recording << "# reluva input recording: serverTime buttons weapon pitch yaw roll forward right up\n";
    }
}

void SpPlayViewer::runPendingTransition() {
    const std::string line = std::move(_pendingTransition);
    _pendingTransition.clear();
    _runningTransition = true;
    runCommand(line, true);
    _runningTransition = false;
}

void SpPlayViewer::runGameCommands() {
    // What the game sent to the console (a level change, a load after
    // dying...), run as the original's console would.
    if (!_game)
        return;
    const auto &all = _game->consoleCommands();
    if (_gameCommandsRun >= all.size())
        return;
    std::vector<std::string> lines;
    for (size_t i = _gameCommandsRun; i < all.size(); ++i)
        for (const std::string &line : splitCommands(all[i]))
            lines.push_back(line);
    _gameCommandsRun = all.size();
    for (const std::string &line : lines)
        runCommand(line, true);
}

void SpPlayViewer::runBinding(const std::string &line, int32_t key, bool down) {
    _input.runBinding(line, key, down, [this](const std::string &text) {
        if (_game)
            if (Error err = _game->clientConsoleCommand(text))
                stop("The game stopped: " + err.message);
    });
}

bool SpPlayViewer::takesKey(const Event &ev) const {
    return ev.type == EventType::KeyDown && ev.key == key::Escape && !(ev.mods & keymod::Ctrl);
}

void SpPlayViewer::handleEvent(const Event &ev) {
    if (!_renderer)
        return;
    // The joystick, read once a frame (pollJoystick).
    if (ev.type == EventType::JoyButtonDown || ev.type == EventType::JoyButtonUp || ev.type == EventType::JoyAxis ||
        ev.type == EventType::JoyHat || ev.type == EventType::JoyRemoved) {
        _joystick.event(ev);
        return;
    }
    if (_cinematic) {
        // A typed key (Esc too) or a click skips it, as the original's;
        // nothing else gets through meanwhile.
        if (ev.type == EventType::KeyDown && !ev.repeat && !(ev.mods & keymod::Ctrl)) {
            const int32_t k = toQ3Key(ev.key);
            if (k > 0 && k < 128)
                endCinematic();
        } else if (ev.type == EventType::MouseDown && toQ3Button(ev.button) == q3key::kMouse1) {
            endCinematic();
        }
        return;
    }
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        const bool down = ev.type == EventType::KeyDown;
        if (down && (ev.mods & keymod::Ctrl) && !_names.empty()) {
            // Reluva's own: restart, another map (without the menus), the
            // info line.
            const int n = int(_names.size());
            if (ev.key == 'r') {
                if (!_game && !_uiDll.empty()) {
                    _ui.reset();
                    _error.clear();
                    return startMenus();
                }
                return start(_index);
            }
            if (ev.key == key::PageUp || ev.key == key::PageDown)
                return start((_index + (ev.key == key::PageUp ? n - 1 : 1)) % n);
            if (ev.key == 'i') {
                _showInfo = !_showInfo;
                return;
            }
        }
        const int32_t k = toQ3Key(ev.key);
        if (_ui && k) {
            if (down)
                _ui->keysDown.insert(k);
            else
                _ui->keysDown.erase(k);
        }
        if (down && ev.key == key::Escape) {
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
            if (menusHaveKeys())
                return menuKey(k);
            if (!ev.repeat)
                runBinding(_ctx.bindings.get(k), k, true);
        } else {
            runBinding(_ctx.bindings.get(k), k, false);
        }
        break;
    }
    case EventType::TextInput:
        // Characters for the menus' text fields, as key presses with the
        // character flag (Latin-1).
        if (menusHaveKeys())
            for (uint32_t cp : decodeUtf8(ev.text))
                if (cp >= 32 && cp < 256 && cp != 127 && _ui)
                    menuKey(int32_t(cp) | q3key::kCharFlag);
        break;
    case EventType::MouseDown:
    case EventType::MouseUp: {
        const int32_t k = toQ3Button(ev.button);
        if (!k)
            break;
        const bool down = ev.type == EventType::MouseDown;
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
    case EventType::MouseWheel:
        if (ev.wheelY && menusHaveKeys())
            menuKey(ev.wheelY > 0 ? q3key::kWheelUp : q3key::kWheelDown);
        break;
    case EventType::MouseMove:
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

void SpPlayViewer::runGame(uint64_t nowMs) {
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
    if (_soundSystem)
        _soundSystem->setTime(nowMs);
    if (Error err = _game->drawClientFrame(_clientTime))
        return stop("The client game stopped: " + err.message);
    _timing.client = ms(clientStart);
    if (_soundSystem)
        _soundSystem->endFrame();
}

void SpPlayViewer::pollJoystick() {
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

void SpPlayViewer::update(uint64_t nowMs) {
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
        if (_cinematic) {
            _renderer->setGamma(brightness(_ctx));
            _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
            if (!_cinematic->rgba.empty())
                _scene->drawImage(_cinematic->rgba.data(), _cinematic->movie.width(), _cinematic->movie.height(), 0, 0,
                                  640, 480, 640, 480);
            std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
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
            shot.resize(eliteforce::SpUiHost::kShotSize);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    std::memcpy(&shot[size_t((255 - y) * 256 + x) * 4],
                                &rgba[(size_t(y * h / 256) * size_t(w) + size_t(x * w / 256)) * 4], 4);
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
    const bool paused = menusHaveKeys();
    if (paused != _textInput) {
        _textInput = paused;
        _ctx.sys.setTextInput(paused);
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
        size_t drawn = 0;
        size_t shots = 0;
        bool first = true;
        for (const auto &v : scene.views) {
            drawGamePictures(drawn, shots, v.picturesBefore, time);
            _scene->drawView(scene, v, 640, 480, time, first, _world.get(), &_grid, dynamicLights);
            first = false;
        }
        drawGamePictures(drawn, shots, pictures.size(), time);
    }
    if (_ui)
        drawMenus(float(nowMs - _startMs) / 1000.0f);
    _timing.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
    drawOverlay();
    if ((!_pendingMenu.empty() || !_pendingTransition.empty()) && !_capturing) {
        _renderer->captureNextFrame();
        _capturing = true;
    }
    _renderer->endFrame(&_overlay);
}

void SpPlayViewer::drawMenus(float time) {
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

void SpPlayViewer::showLoadingScreen(const std::string &map) {
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

void SpPlayViewer::showClientLoading() {
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

void SpPlayViewer::drawGamePictures(size_t &drawn, size_t &shotsDrawn, size_t to, float time) {
    // The client game's pictures up to `to`, with the screen capture where
    // it drew that between them.
    const auto &pictures = _game->pictures();
    const auto &shots = _game->screenShotDraws();
    for (; shotsDrawn < shots.size() && shots[shotsDrawn].picturesBefore <= to; ++shotsDrawn) {
        const auto &s = shots[shotsDrawn];
        _scene->drawPictures(pictures, drawn, s.picturesBefore, 640, 480, time);
        drawn = std::max(drawn, s.picturesBefore);
        if (_game->screenShot.size() == eliteforce::SpUiHost::kShotSize)
            _scene->drawImage(_game->screenShot.data(), 256, 256, s.x, s.y, s.w, s.h, 640, 480);
    }
    _scene->drawPictures(pictures, drawn, to, 640, 480, time);
    drawn = std::max(drawn, to);
}

void SpPlayViewer::drawOverlay() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
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
        line(y, "Ctrl+R: try again   Ctrl+PgUp/PgDn: another map   Esc: menu", {200, 200, 200});
        y += font::kLineHeight;
    }
    if (!_menuError.empty()) {
        line(y, "The game's menus stopped: " + _menuError + " (Ctrl+R without a game: start them again)",
             {255, 120, 120});
        y += font::kLineHeight;
    }
    std::vector<std::string> bottom;
    if (!_lastCommand.empty() && _nowMs - _lastCommandMs < 4000)
        bottom.push_back("The menus ran: " + _lastCommand);
    if (_ui && !_ui->unhandled().empty()) {
        std::string calls = "Menu functions not answered yet:";
        for (const auto &[slot, count] : _ui->unhandled())
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
        bottom.push_back("Esc: the game's menu   Ctrl+Esc: leave   Ctrl+PgUp/PgDn: a map without the menus");
    for (size_t i = 0; i < bottom.size(); ++i)
        line(_frameH - int(bottom.size() - i) * font::kLineHeight - 4, bottom[i], {220, 220, 220});
}

} // namespace reluva::idtech3
