// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/pages/sp_play_viewer.h"
#include "engines/idtech3/shader.h"

#include "common/font.h"
#include "common/image.h"
#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace reluva::idtech3 {

namespace {

constexpr float kServerBudgetMs = 40; // of the game's frames, each page frame

bool badSaveName(const std::string& name); // below

// Whether a shader the game registers can be drawn: a script of that name,
// or an image (its own file type, or .tga or .jpg in its place).
bool shaderFound(const ShaderLibrary& shaders, const SearchSet& files, const std::string& name) {
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

SpPlayViewer::SpPlayViewer(ViewerContext& ctx) : Page(ctx) {
    // A game with its single-player game code beside the game folder.
    if (ctx.game.code != GameCode::EliteForce || ctx.game.spGameDll.empty() || ctx.singlePlayerPath.empty())
        return;
    // (By name in any case: copied from a CD, they may be in capitals.)
    const auto dll = fs::findFile(ctx.singlePlayerPath, ctx.game.spGameDll);
    std::ifstream in(dll ? *dll : std::filesystem::path(), std::ios::binary);
    if (!dll || !in)
        return;
    dll_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    // Its menus, if they're there too.
    if (const auto uiPath = fs::findFile(ctx.singlePlayerPath, ctx.game.spUiDll); uiPath && !ctx.game.spUiDll.empty())
        if (std::ifstream ui{*uiPath, std::ios::binary}; ui)
            uiDll_.assign(std::istreambuf_iterator<char>(ui), std::istreambuf_iterator<char>());
    // Its maps: all but multiplayer's.
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) != 0 || key.size() <= 4 || key.compare(key.size() - 4, 4, ".bsp") != 0)
            continue;
        if (std::none_of(ctx.game.multiplayerMapPrefixes.begin(), ctx.game.multiplayerMapPrefixes.end(),
                         [&](const std::string& p) { return key.compare(5, p.size(), p) == 0; }))
            names_.push_back(key);
    }
    std::sort(names_.begin(), names_.end());
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == "maps/" + ctx.game.firstMap + ".bsp")
            index_ = int(i);
}

SpPlayViewer::~SpPlayViewer() { deactivate(); }

void SpPlayViewer::activate() {
    JoystickInput::registerCvars(cvars_);
    const VideoSettings video = videoSettings(ctx_.cvars);
    frameW_ = video.width;
    frameH_ = video.height;
    ctx_.sys.setVideoMode(frameW_, frameH_, video.fullscreen);
    if (Error err = ctx_.sys.init3D(frameW_, frameH_, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "Single player needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(frameW_, frameH_, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    world_ = std::make_unique<WorldRenderer>(*renderer_, *shaders_);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    applyVideoSettings(video, *shaders_, world_.get(), models_.get());
    scene_ = std::make_unique<SceneDrawer>(*renderer_, *shaders_, *models_, ctx_.game.spRefEntity);
    ctx_.sys.setRelativeMouse(true);
    if (ctx_.mixer)
        soundSystem_ = std::make_unique<SoundSystem>(*ctx_.mixer, soundLibrary_);
    startMs_ = 0;
    saveDir_ = ctx_.savePath.empty() ? std::filesystem::path() : ctx_.savePath / "saves";
    // The player's userinfo, as the original's client registers it.
    cvars_.create("name", ctx_.game.spPlayerName, CvarStore::Archive | CvarStore::UserInfo);
    cvars_.create("snaps", "20", CvarStore::Archive | CvarStore::UserInfo);
    cvars_.create("headModel", ctx_.game.malePlayer.head, CvarStore::UserInfo);
    cvars_.create("torsoModel", ctx_.game.malePlayer.torso, CvarStore::UserInfo);
    cvars_.create("legsModel", ctx_.game.malePlayer.legs, CvarStore::UserInfo);
    cvars_.create("sex", "male", CvarStore::Archive | CvarStore::UserInfo);
    cvars_.create("handicap", "100", CvarStore::Archive | CvarStore::UserInfo);
    if (!uiDll_.empty())
        startMenus();
    if (!ui_) {
        start(index_);
    } else if (!introShown_) {
        // The logo, as the original's engine plays at start-up.
        introShown_ = true;
        startCinematic("eflogo", false);
    }
}

void SpPlayViewer::startCinematic(const std::string& name, bool inGame) {
    if (inGame && !(game_ && game_->clientGameRunning()))
        return;
    std::string base = fs::toLower(name);
    // (Asked for with or without a file type, which may be another's: the
    // game's own is played.)
    if (const size_t dot = base.rfind('.'); dot != std::string::npos && base.size() - dot == 4)
        base.resize(dot);
    std::vector<uint8_t> data;
    auto c = std::make_unique<Cinematic>();
    Error err = ctx_.files.read("video/" + base + ctx_.game.cinematicExtension, data);
    if (!err)
        err = c->movie.open(std::move(data));
    if (err) {
        lastCommand_ = "Cinematic " + base + ": " + err.message;
        lastCommandMs_ = nowMs_;
        return;
    }
    c->inGame = inGame;
    if (!inGame) {
        // The game ends, the menus close.
        endGame();
        if (ui_) {
            if (Error e = ui_->setActiveMenu(""))
                menusStopped(e);
            else
                ui_->setCatcher(ui_->catcher() & ~eliteforce::SpUiHost::kCatchUi);
        }
    }
    // Its sound whole, to play alongside.
    std::vector<int16_t> sound;
    if (!c->movie.decodeAllAudio(sound) && !sound.empty() && ctx_.mixer) {
        auto s = std::make_shared<audio::Sound>();
        s->rate = c->movie.audioRate();
        s->channels = c->movie.audioChannels();
        s->format = 1;
        s->samples = std::move(sound);
        c->sound = ctx_.mixer->play(s, audio::SoundType::Effects, audio::kMaxVolume);
    }
    cinematic_ = std::move(c);
}

void SpPlayViewer::endCinematic() {
    if (!cinematic_)
        return;
    if (cinematic_->sound != audio::kNoSound && ctx_.mixer)
        ctx_.mixer->stop(cinematic_->sound);
    const bool inGame = cinematic_->inGame;
    cinematic_.reset();
    lastMs_ = 0;
    // As the original's: then the nextmap cvar's command, once.
    if (!inGame) {
        const std::string next = cvars_.string("nextmap");
        if (!next.empty()) {
            cvars_.set("nextmap", "");
            for (const std::string& line : splitCommands(next))
                runCommand(line);
        }
    }
}

void SpPlayViewer::runCinematic(uint64_t nowMs) {
    // The frame due now, at the movie's rate from when it started.
    Cinematic& c = *cinematic_;
    if (!c.startMs)
        c.startMs = nowMs;
    const int64_t due = int64_t((nowMs - c.startMs) * c.movie.rateNumerator() /
                                (uint64_t(c.movie.rateDenominator()) * 1000));
    bool decoded = false;
    while (c.movie.frameNumber() < due) {
        bool more = false;
        if (Error err = c.movie.nextFrame(more) ; err || !more) {
            if (err)
                lastCommand_ = "Cinematic: " + err.message;
            return endCinematic();
        }
        decoded = true;
    }
    if (decoded)
        c.movie.toRgba(c.rgba);
}

void SpPlayViewer::startMenus() {
    menuError_.clear();
    ui_ = std::make_unique<eliteforce::SpUiHost>(ctx_.files, cvars_);
    ui_->print = [](const std::string& text) { std::fputs(text.c_str(), stdout); };
    ui_->shaderExists = [this](const std::string& name) { return shaderFound(ctx_.shaders, ctx_.files, name); };
    ui_->bindings = &ctx_.bindings;
    ui_->startLocalSound = [this](const std::string& sound, int channel) {
        if (soundSystem_)
            soundSystem_->startLocalSound(sound, channel);
    };
    ui_->configString = [this](int32_t index, std::string& out) {
        if (!game_)
            return false;
        const auto it = game_->configstrings().find(index);
        if (it == game_->configstrings().end())
            return false;
        out = it->second;
        return true;
    };
    ui_->saveDir = saveDir_;
    ui_->readSave = [this](const std::string& name, std::vector<uint8_t>& file) { return readSave(name, file); };
    ui_->askGame = [this](uint32_t flag) { return game_ ? game_->allowedToSaveHere(flag) : flag; };
    if (Error err = ui_->load(uiDll_))
        return menusStopped(err);
    if (Error err = ui_->setActiveMenu("main"))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayViewer::menusStopped(const Error& err) {
    // The game goes on without them (Esc then leaves the page).
    menuError_ = ctx_.game.spUiDll + ": " + err.message;
    ui_.reset();
}

bool SpPlayViewer::menusHaveKeys() const { return ui_ && (ui_->catcher() & eliteforce::SpUiHost::kCatchUi); }

void SpPlayViewer::menuKey(int32_t key) {
    if (!ui_)
        return;
    if (Error err = ui_->keyEvent(key))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayViewer::runMenuCommands() {
    if (!ui_)
        return;
    for (const std::string& line : ui_->takeCommands())
        runCommand(line);
}

void SpPlayViewer::runCommand(const std::string& line, bool fromGame) {
    // What the menus and the game run, as the original's console would.
    const std::vector<std::string> w = splitCommand(line);
    if (w.empty())
        return;
    lastCommand_ = line;
    lastCommandMs_ = nowMs_;
    const std::string cmd = fs::toLower(w[0]);
    if (cmd == "map" || cmd == "devmap" || cmd == "spmap" || cmd == "maptransition") {
        if (w.size() < 2)
            return;
        if (cmd == "maptransition" && game_ && !runningTransition_) {
            // The level's last frame first, for the next one's loading
            // screen (taken as this frame is drawn; see update()).
            pendingTransition_ = line;
            return;
        }
        // A level change carries the player over (maptransition); a new
        // start doesn't, and forgets the last level's autosave.
        if (ui_)
            ui_->setCapture({}); // the loading screen shows the level's picture
        if (cmd != "maptransition")
            nextScreenShot_.clear(); // and the client game's, nothing in its place
        if (cmd == "maptransition" && game_) {
            game_->savePlayer();
        } else {
            cvars_.set("playersave", "");
            if (w[1][0] != '_' && !saveDir_.empty()) {
                std::error_code ec;
                std::filesystem::remove(saveDir_ / "auto.sav", ec);
            }
        }
        std::string map = fs::toLower(w[1]);
        if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
            map.resize(map.size() - 4);
        const auto it = std::find(names_.begin(), names_.end(), "maps/" + map + ".bsp");
        if (it == names_.end()) {
            error_ = "There's no map " + map;
            return;
        }
        nextSpawnTarget_ = cmd == "maptransition" && w.size() >= 3 ? w[2] : std::string();
        nextLoadTransition_ = cmd == "maptransition";
        start(int(it - names_.begin()));
    } else if (cmd == "disconnect") {
        endGame();
        if (ui_)
            if (Error err = ui_->setActiveMenu("main"))
                menusStopped(err);
    } else if (cmd == "quit") {
        endGame();
        ctx_.requestPage = "Start";
    } else if (cmd == "turboliftmenu") {
        if (ui_ && game_)
            if (Error err = ui_->setActiveMenu("turbolift"))
                menusStopped(err);
    } else if (cmd == "cinematic" || cmd == "ingamecinematic") {
        if (w.size() >= 2)
            startCinematic(w[1], cmd == "ingamecinematic");
    } else if (cmd == "music") {
        if (soundSystem_ && w.size() >= 2)
            soundSystem_->startBackgroundTrack(w[1], w.size() >= 3 ? w[2] : w[1]);
    } else if (cmd == "bind" || cmd == "unbind" || cmd == "unbindall" || cmd == "set" || cmd == "seta" ||
               cmd == "sets" || cmd == "setu" || cmd == "exec") {
        ctx_.bindings.execute(line, cvars_, nullptr, [this](const std::string& name, std::string& text) {
            std::vector<uint8_t> data;
            if (ctx_.files.read(name, data))
                return false;
            text.assign(data.begin(), data.end());
            return true;
        });
    } else if (cmd == "save") {
        if (w.size() == 2)
            saveGame(w[1], false);
        else
            lastCommand_ = "USAGE: save <filename>";
    } else if (cmd == "load") {
        if (w.size() == 2)
            loadGame(w[1]);
        else
            lastCommand_ = "USAGE: load <filename>";
    } else if (cmd == "wipe") {
        const std::string name = w.size() == 2 ? fs::toLower(w[1]) : std::string();
        if (name.empty() || name == "auto" || name == "exitholodeck" || saveDir_.empty()) {
            lastCommand_ = "Can't wipe 'auto' or 'exitholodeck'";
        } else if (badSaveName(w[1])) {
            lastCommand_ = "Bad savegame name.";
        } else {
            std::error_code ec;
            std::filesystem::remove(saveDir_ / (w[1] + ".sav"), ec);
        }
    } else if (cmd == "cvar_restart" || cmd == "snd_restart" || cmd == "in_restart" || cmd == "vid_restart" ||
               cmd == "s_enable_a3d" || cmd == "s_disable_a3d") {
        // Nothing to restart here.
    } else {
        // The menus' own commands (ui_turbolift...), then the game's.
        bool handled = false;
        if (ui_)
            if (Error err = ui_->consoleCommand(line, &handled))
                menusStopped(err);
        if (!handled && game_ && !fromGame)
            if (Error err = game_->clientConsoleCommand(line))
                stop("The game stopped: " + err.message);
    }
}

void SpPlayViewer::deactivate() {
    if (cinematic_ && cinematic_->sound != audio::kNoSound && ctx_.mixer)
        ctx_.mixer->stop(cinematic_->sound);
    cinematic_.reset();
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    if (textInput_)
        ctx_.sys.setTextInput(false);
    textInput_ = false;
    game_.reset();
    ui_.reset();
    soundSystem_.reset();
    scene_.reset();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    input_.releaseAll();
}

void SpPlayViewer::stop(const std::string& why) {
    error_ = why;
    endGame();
}

void SpPlayViewer::endGame() {
    game_.reset();
    recording_.close();
    // As the original's disconnect: no scene left half skipped.
    cvars_.set("timescale", "1", true);
    cvars_.set("skippingCinematic", "0", true);
    if (scene_)
        scene_->reset(); // its handles are gone
    if (soundSystem_)
        soundSystem_->stopAll();
    input_.releaseAll();
}

void SpPlayViewer::start(int index, int32_t loadMode, raven::SaveReader* save) {
    if (names_.empty() || !renderer_)
        return;
    index_ = index;
    error_.clear();
    endGame();
    // The menus close as a level loads (SetActiveMenu(0)).
    if (ui_) {
        if (Error err = ui_->setActiveMenu(""))
            menusStopped(err);
        else
            ui_->setCatcher(ui_->catcher() & ~eliteforce::SpUiHost::kCatchUi);
    }
    scene_->reset();
    models_->clear();
    world_->clear();
    shaders_->clear();
    input_.yaw = input_.pitch = 0;
    clientTime_ = 0;
    lastMs_ = 0;
    const std::string& path = names_[size_t(index)];
    std::string map = path.substr(5, path.size() - 9);
    showLoadingScreen(map);
    map_ = map;
    gameCommandsRun_ = 0;
    pendingMenu_.clear();
    capturing_ = false;
    setTitle(map);

    // The level: its drawing and light grid, as the Play page's.
    auto bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = ctx_.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = world_->load(*bsp, ctx_.shaders);
    if (err)
        return stop("Couldn't load " + path + ": " + err.message);
    grid_ = LightGrid{};
    float cell[3] = {64, 64, 128};
    std::vector<Entity> entities;
    if (!parseEntities(bsp->entities, entities) && !entities.empty())
        entities[0].vector("gridsize", cell);
    grid_.build(*bsp, {cell[0], cell[1], cell[2]});

    // The game: the server half started on the level, the player joined,
    // then the client half.
    game_ = std::make_unique<eliteforce::SpGameHost>(ctx_.files, cvars_);
    game_->print = [](const std::string& text) { std::fputs(text.c_str(), stdout); };
    game_->soundSystem = soundSystem_.get();
    game_->shaderExists = [this](const std::string& name) { return shaderFound(ctx_.shaders, ctx_.files, name); };
    // What the client game shows where the level has no picture of its
    // own: the saved game's, the last level's last frame, or nothing.
    game_->screenShot = std::move(nextScreenShot_);
    nextScreenShot_.clear();
    if ((err = game_->load(dll_)))
        return stop(ctx_.game.spGameDll + ": " + err.message);
    // A saved game's chunks are read on as the original's loader does:
    // the level started, then (but for an autosave) its time and
    // configstrings, then the game's own.
    game_->setSaveStream(nullptr, save);
    game_->spawnTarget = std::exchange(nextSpawnTarget_, std::string());
    game_->loadTransition = std::exchange(nextLoadTransition_, false);
    if ((err = game_->startLevel(map, bsp, eliteforce::SpGameHost::mapChecksum(data), 0, loadMode)))
        return stop("The game couldn't start the level: " + err.message);
    if (save)
        if ((err = eliteforce::readSaveLevel(*save, *game_, loadMode == 2)))
            return stop("The saved game couldn't be read: " + err.message);
    game_->setSaveStream(nullptr, nullptr);
    std::string refused;
    // The player as the original's engine describes them: the userinfo
    // cvars, with the models for the sex chosen.
    const bool male = fs::toLower(cvars_.string("sex")).rfind('m', 0) == 0;
    const PlayerModels& player = male ? ctx_.game.malePlayer : ctx_.game.femalePlayer;
    cvars_.set("legsmodel", player.legs);
    cvars_.set("torsomodel", player.torso);
    cvars_.set("headmodel", player.head);
    std::string userinfo;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if (cv.flags & CvarStore::UserInfo)
            userinfo += "\\" + cv.name + "\\" + cv.value;
    if ((err = game_->connectPlayer(userinfo, refused, loadMode)))
        return stop("The player couldn't join: " + err.message);
    if (!refused.empty())
        return stop("The game refused the player: " + refused);
    scene_->setHandles({&game_->models(), &game_->skins(), &game_->shaders()});
    // As it loads, the client game asks for the screen: its loading screen
    // (the level's picture, what's loading) under the menus' text.
    game_->updateScreen = [this] { showClientLoading(); };
    err = game_->startClientGame();
    if (game_)
        game_->updateScreen = nullptr;
    if (err)
        return stop("The client game couldn't start: " + err.message);
    // The player's time from where the level is (it has settled for a few
    // frames before they joined).
    clientTime_ = game_->levelTime();
    // A new level (not one loaded) is saved as "auto" as the player joins,
    // as the original's (but the holodeck's and the brig's, "_" maps).
    if (loadMode == 0 && !map.empty() && map[0] != '_')
        saveGame("auto", true);
}

bool SpPlayViewer::readSave(const std::string& name, std::vector<uint8_t>& file) const {
    // Those written here first, then the game folder's own.
    if (!saveDir_.empty()) {
        std::ifstream in(saveDir_ / (name + ".sav"), std::ios::binary);
        if (in) {
            file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }
    }
    return !ctx_.files.read("saves/" + name + ".sav", file);
}

std::vector<uint8_t> SpPlayViewer::levelShot(const std::string& map) const {
    // levelshots/<map>.tga at 256 x 256, as the original's engine makes an
    // autosave's picture (renderer entry 23, eliteforce-sp-interface.md):
    // as it is if it's that size, otherwise each pixel the average of the
    // ones it covers, opaque; rows bottom first. Empty if there's none.
    // Its image loader takes the .jpg when there's no .tga, as the
    // renderer's always do: the game's levelshots are all .jpg.
    std::vector<uint8_t> data;
    image::Image img;
    const std::string file = findImage(ctx_.files, "levelshots/" + map + ".tga");
    if (file.empty() || ctx_.files.read(file, data) || image::decode(data, img) || img.w <= 0 || img.h <= 0)
        return {};
    std::vector<uint8_t> out(eliteforce::SpUiHost::kShotSize, 0);
    const bool same = img.w == 256 && img.h == 256;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            uint8_t* px = &out[size_t((255 - y) * 256 + x) * 4];
            if (same) {
                std::memcpy(px, img.pixel(x, y), 4);
                continue;
            }
            const int x0 = x * img.w / 256, x1 = std::max(x0 + 1, (x + 1) * img.w / 256);
            const int y0 = y * img.h / 256, y1 = std::max(y0 + 1, (y + 1) * img.h / 256);
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

bool badSaveName(const std::string& name) {
    return name.empty() || fs::toLower(name) == "current" || name.find("..") != std::string::npos ||
           name.find_first_of("/\\:") != std::string::npos;
}

} // namespace

void SpPlayViewer::saveGame(const std::string& name, bool autosave) {
    // As the original's save command: the engine's chunks, then the
    // game's (WriteLevel), to saves/<name>.sav.
    using raven::chunkId;
    if (!game_ || !game_->clientGameRunning()) {
        lastCommand_ = "You must be in a game to save.";
        return;
    }
    if (!autosave) {
        if (!map_.empty() && map_[0] == '_') {
            lastCommand_ = "Cannot save on holodeck or brig.";
            return;
        }
        if (game_->playerHealth() <= 0) {
            lastCommand_ = "Can't savegame while dead!";
            return;
        }
        if (badSaveName(name)) {
            lastCommand_ = "Bad savegame name.";
            return;
        }
    }
    if (saveDir_.empty()) {
        lastCommand_ = "There's nowhere to save games.";
        return;
    }
    if (autosave)
        game_->savePlayer();
    raven::SaveWriter w;
    // Its description: the save menu's, else when and where.
    std::string comment;
    if (autosave) {
        comment = "-------> " + map_;
    } else if (ui_ && !ui_->saveComment.empty()) {
        comment = ui_->saveComment;
    } else {
        char when[64] = {};
        const std::time_t t = std::time(nullptr);
        if (const std::tm* tm = std::localtime(&t))
            std::strftime(when, sizeof when, "%m/%d/%y %H:%M", tm);
        comment = std::string(when) + "  " + map_;
    }
    eliteforce::SaveHeader header;
    header.comment = comment;
    header.map = map_;
    // An autosave's picture is the level's own, as the original's (the
    // screen as captured if it has none); a save the player makes, the
    // screen.
    if (autosave || !ui_)
        header.shot = levelShot(map_);
    // Without one, the screen as the last level ended (the capture the
    // engine holds for the loading screen).
    if (autosave && header.shot.empty() && game_->screenShot.size() == eliteforce::SpUiHost::kShotSize)
        header.shot = game_->screenShot;
    if (header.shot.empty())
        header.shot = ui_ ? ui_->capture() : std::vector<uint8_t>(eliteforce::SpUiHost::kShotSize, 0);
    header.autosave = autosave;
    eliteforce::writeSaveHeader(w, cvars_, header);
    if (Error err = eliteforce::writeSaveLevel(w, *game_, autosave))
        return stop("The game couldn't save: " + err.message);
    // Written as "current", then renamed, as the original's.
    std::error_code ec;
    std::filesystem::create_directories(saveDir_, ec);
    const std::filesystem::path file = saveDir_ / (name + ".sav"), temp = saveDir_ / "current.sav";
    {
        std::ofstream out(temp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(w.bytes().data()), std::streamsize(w.bytes().size()));
        if (!out) {
            lastCommand_ = "Failed to create savegame " + temp.string();
            return;
        }
    }
    std::filesystem::rename(temp, file, ec); // replaces the old one, which is kept if this fails
    if (ec) {
        lastCommand_ = "Couldn't write " + file.string() + ": " + ec.message();
        return;
    }
    if (!autosave)
        lastCommand_ = "Saved " + name;
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
        if (!lastLoaded_.empty() && readSave(lastLoaded_, file)) {
            raven::SaveReader r(std::move(file));
            std::vector<uint8_t> skip;
            if (!r.next(chunkId("COMM"), skip) && !r.next(chunkId("SHOT"), skip))
                r.nextText(chunkId("MPCM"), loadedMap);
        }
        name = !lastLoaded_.empty() && loadedMap == map_ ? lastLoaded_ : "auto";
    }
    if (badSaveName(name)) {
        lastCommand_ = "Bad loadgame name.";
        return;
    }
    std::vector<uint8_t> file;
    if (!readSave(name, file)) {
        lastCommand_ = "Failed to open savegame " + name;
        return;
    }
    raven::SaveReader r(std::move(file));
    eliteforce::SaveHeader header;
    if (Error err = eliteforce::readSaveHeader(r, cvars_, header)) {
        lastCommand_ = "Couldn't read " + name + ": " + err.message;
        return;
    }
    const std::string& map = header.map;
    const bool autosave = header.autosave;
    std::vector<uint8_t>& shot = header.shot;
    const auto it = std::find(names_.begin(), names_.end(), "maps/" + fs::toLower(map) + ".bsp");
    if (it == names_.end()) {
        lastCommand_ = name + " is of a map that isn't here: " + map;
        return;
    }
    lastLoaded_ = name;
    lastCommand_ = "Loaded " + name;
    if (ui_)
        ui_->setCapture(shot); // the loading screen shows it
    nextScreenShot_ = shot;   // and so does the client game's
    start(int(it - names_.begin()), autosave ? 2 : 1, &r);
    if (game_ && !saveDir_.empty()) {
        recording_.open(saveDir_ / (name + ".input"), std::ios::trunc);
        recording_ << "# reluva input recording: serverTime buttons weapon pitch yaw roll forward right up\n";
    }
}

void SpPlayViewer::runPendingTransition() {
    const std::string line = std::move(pendingTransition_);
    pendingTransition_.clear();
    runningTransition_ = true;
    runCommand(line, true);
    runningTransition_ = false;
}

void SpPlayViewer::runGameCommands() {
    // What the game sent to the console (a level change, a load after
    // dying...), run as the original's console would.
    if (!game_)
        return;
    const auto& all = game_->consoleCommands();
    if (gameCommandsRun_ >= all.size())
        return;
    std::vector<std::string> lines;
    for (size_t i = gameCommandsRun_; i < all.size(); ++i)
        for (const std::string& line : splitCommands(all[i]))
            lines.push_back(line);
    gameCommandsRun_ = all.size();
    for (const std::string& line : lines)
        runCommand(line, true);
}

void SpPlayViewer::runBinding(const std::string& line, int32_t key, bool down) {
    input_.runBinding(line, key, down, [this](const std::string& text) {
        if (game_)
            if (Error err = game_->clientConsoleCommand(text))
                stop("The game stopped: " + err.message);
    });
}

bool SpPlayViewer::takesKey(const Event& ev) const {
    return ev.type == EventType::KeyDown && ev.key == key::Escape && !(ev.mods & keymod::Ctrl);
}

void SpPlayViewer::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
    // The joystick, read once a frame (pollJoystick).
    if (ev.type == EventType::JoyButtonDown || ev.type == EventType::JoyButtonUp || ev.type == EventType::JoyAxis ||
        ev.type == EventType::JoyHat || ev.type == EventType::JoyRemoved) {
        joystick_.event(ev);
        return;
    }
    if (cinematic_) {
        // A typed key (Esc too) or a click skips it, as the original's;
        // nothing else gets through meanwhile.
        if (ev.type == EventType::KeyDown && !ev.repeat && !(ev.mods & keymod::Ctrl)) {
            const int32_t k = toQ3Key(ev.key);
            if (k > 0 && k < 128)
                endCinematic();
        } else if (ev.type == EventType::MouseDown && toQ3Button(ev.button) == q3key::Mouse1) {
            endCinematic();
        }
        return;
    }
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        const bool down = ev.type == EventType::KeyDown;
        if (down && (ev.mods & keymod::Ctrl) && !names_.empty()) {
            // Reluva's own: restart, another map (without the menus), the
            // info line.
            const int n = int(names_.size());
            if (ev.key == 'r') {
                if (!game_ && !uiDll_.empty()) {
                    ui_.reset();
                    error_.clear();
                    return startMenus();
                }
                return start(index_);
            }
            if (ev.key == key::PageUp || ev.key == key::PageDown)
                return start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
            if (ev.key == 'i') {
                showInfo_ = !showInfo_;
                return;
            }
        }
        const int32_t k = toQ3Key(ev.key);
        if (ui_ && k) {
            if (down)
                ui_->keysDown.insert(k);
            else
                ui_->keysDown.erase(k);
        }
        if (down && ev.key == key::Escape) {
            // As the original's engine: to the menus if they have the
            // keys, else the in-game menu (the main one without a game).
            if (!ui_) {
                ctx_.requestPage = "Start";
                return;
            }
            if (menusHaveKeys())
                return menuKey(q3key::Escape);
            if (game_ && game_->clientGameRunning()) {
                // The game is captured first (the next frame), for the
                // save menu's picture.
                pendingMenu_ = "ingame";
                return;
            }
            if (Error err = ui_->setActiveMenu("main"))
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
                runBinding(ctx_.bindings.get(k), k, true);
        } else {
            runBinding(ctx_.bindings.get(k), k, false);
        }
        break;
    }
    case EventType::TextInput:
        // Characters for the menus' text fields, as key presses with the
        // character flag (Latin-1).
        if (menusHaveKeys())
            for (uint32_t cp : decodeUtf8(ev.text))
                if (cp >= 32 && cp < 256 && cp != 127 && ui_)
                    menuKey(int32_t(cp) | q3key::CharFlag);
        break;
    case EventType::MouseDown:
    case EventType::MouseUp: {
        const int32_t k = toQ3Button(ev.button);
        if (!k)
            break;
        const bool down = ev.type == EventType::MouseDown;
        if (ui_) {
            if (down)
                ui_->keysDown.insert(k);
            else
                ui_->keysDown.erase(k);
        }
        if (down && menusHaveKeys())
            return menuKey(k);
        runBinding(ctx_.bindings.get(k), k, down);
        break;
    }
    case EventType::MouseWheel:
        if (ev.wheelY && menusHaveKeys())
            menuKey(ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown);
        break;
    case EventType::MouseMove:
        if (menusHaveKeys()) {
            // In the menus' 640 x 480, as the window's pixels are to the
            // frame (the fractions kept for the next move).
            menuMouseX_ += float(ev.moveX) * 640.0f / float(std::max(frameW_, 1));
            menuMouseY_ += float(ev.moveY) * 480.0f / float(std::max(frameH_, 1));
            const auto dx = int32_t(menuMouseX_), dy = int32_t(menuMouseY_);
            menuMouseX_ -= float(dx);
            menuMouseY_ -= float(dy);
            if (dx || dy)
                if (Error err = ui_->mouseEvent(dx, dy))
                    menusStopped(err);
        } else {
            input_.addMouse(ev.moveX, ev.moveY);
        }
        break;
    default: break;
    }
}

void SpPlayViewer::runGame(uint64_t nowMs) {
    if (!game_ || !game_->clientGameRunning())
        return;
    // At most 200 ms a frame, as the original's client takes, so a slow
    // frame doesn't leave the game's clock behind for long.
    const int32_t dt = lastMs_ ? int32_t(std::min<uint64_t>(nowMs - lastMs_, 200)) : 0;
    lastMs_ = nowMs;
    auto setting = [&](const char* name, float fallback) { return cvars_.find(name) ? cvars_.value(name) : fallback; };
    // Looking: the mouse and keys, as the Play page.
    input_.turn(cvars_, dt);

    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) { return std::chrono::duration<float, std::milli>(Clock::now() - from).count(); };
    // timescale speeds the game's clock, as the original's: the game sets
    // it to 100 to fast-forward a scene the player skips.
    const float timescale = std::clamp(setting("timescale", 1), 0.0f, 100.0f);
    if (dt > 0)
        clientTime_ += int32_t(std::lround(float(dt) * timescale));
    // The server kept a frame ahead of the player's time, then the
    // player's command, then the client game's frame.
    const auto serverStart = Clock::now();
    // At most so much of the game's time a frame (the emulated game can't
    // run a skipped scene's timescale 100 as the original could): what
    // doesn't fit is let go rather than owed, so the page keeps up.
    // One snapshot after the frames run, as the original's server sends.
    bool ran = false;
    while (game_->levelTime() <= clientTime_) {
        if (Error err = game_->runFrame(false))
            return stop("The game stopped: " + err.message);
        ran = true;
        if (ms(serverStart) > kServerBudgetMs && game_->levelTime() <= clientTime_) {
            clientTime_ = game_->levelTime() - 1;
            break;
        }
    }
    if (ran)
        game_->sendSnapshot();
    if (dt > 0) {
        eliteforce::SpGameHost::UserCommand cmd;
        cmd.serverTime = clientTime_;
        const PlayerInput::Command c = input_.command(cvars_, ctx_.game);
        cmd.angles = c.angles;
        cmd.buttons = c.buttons;
        cmd.forward = c.forward;
        cmd.right = c.right;
        cmd.up = c.up;
        if (Error err = game_->clientThink(cmd))
            return stop("The game stopped: " + err.message);
        // (The weapon as the game was given it: the client game's choice.)
        if (recording_.is_open())
            recording_ << cmd.serverTime << ' ' << cmd.buttons << ' ' << int(game_->weaponSelect()) << ' '
                       << cmd.angles[0] << ' ' << cmd.angles[1] << ' ' << cmd.angles[2] << ' ' << int(cmd.forward)
                       << ' ' << int(cmd.right) << ' ' << int(cmd.up) << std::endl; // flushed: kept if closed
    }
    timing_.server = ms(serverStart);
    const auto clientStart = Clock::now();
    if (soundSystem_)
        soundSystem_->setTime(nowMs);
    if (Error err = game_->drawClientFrame(clientTime_))
        return stop("The client game stopped: " + err.message);
    timing_.client = ms(clientStart);
    if (soundSystem_)
        soundSystem_->endFrame();
}

void SpPlayViewer::pollJoystick() {
    // As the Play page's: the joystick's keys as keys (to the menus while
    // they have the keys), its analog axes moving and turning the player.
    const JoystickInput::Frame f = joystick_.frame(cvars_);
    const bool menus = menusHaveKeys();
    input_.setJoystick(menus || cinematic_ ? 0 : f.x, menus || cinematic_ ? 0 : f.y);
    for (const auto& [k, down] : f.keys) {
        if (cinematic_)
            continue; // only typed keys and clicks skip one
        if (ui_) {
            if (down)
                ui_->keysDown.insert(k);
            else
                ui_->keysDown.erase(k);
        }
        if (down && menusHaveKeys()) {
            menuKey(k);
            continue;
        }
        runBinding(ctx_.bindings.get(k), k, down);
        if (!renderer_)
            return;
    }
}

void SpPlayViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    pollJoystick();
    if (!renderer_)
        return;
    nowMs_ = nowMs;
    if (!startMs_)
        startMs_ = nowMs;
    scene_->newFrame();
    if (cinematic_) {
        // A cinematic has the screen (the game waits under one in game).
        runCinematic(nowMs);
        if (cinematic_) {
            renderer_->setGamma(brightness(ctx_));
            renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
            if (!cinematic_->rgba.empty())
                scene_->drawImage(cinematic_->rgba.data(), cinematic_->movie.width(), cinematic_->movie.height(), 0,
                                  0, 640, 480, 640, 480);
            std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
            renderer_->endFrame(&overlay_);
            return;
        }
    }
    if (capturing_) {
        // The last frame was kept: the save menu's picture, at 256 x 256,
        // then the menu the player asked for; or the next level's loading
        // screen's, then the level change.
        capturing_ = false;
        std::vector<uint8_t> rgba, shot;
        const int w = renderer_->width(), h = renderer_->height();
        if (renderer_->takeCapture(rgba) && w > 0 && h > 0 && rgba.size() >= size_t(w) * size_t(h) * 4) {
            shot.resize(eliteforce::SpUiHost::kShotSize);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    std::memcpy(&shot[size_t((255 - y) * 256 + x) * 4],
                                &rgba[(size_t(y * h / 256) * size_t(w) + size_t(x * w / 256)) * 4], 4);
        }
        if (!pendingTransition_.empty()) {
            nextScreenShot_ = shot;
            runPendingTransition();
        }
        if (ui_ && !pendingMenu_.empty()) {
            ui_->setCapture(shot);
            if (Error err = ui_->setActiveMenu(pendingMenu_))
                menusStopped(err);
            else
                runMenuCommands();
        }
        pendingMenu_.clear();
    } else if (!pendingTransition_.empty()) {
        runPendingTransition(); // no frame was drawn to take it from
    }
    // The game waits while a menu has the keys, as the original's single
    // player pauses (cl_paused, which the in-game menu sets, back to 0
    // after); typed text goes to them meanwhile.
    const bool paused = menusHaveKeys();
    if (paused != textInput_) {
        textInput_ = paused;
        ctx_.sys.setTextInput(paused);
    }
    if (paused) {
        lastMs_ = nowMs;
    } else {
        if (cvars_.integer("cl_paused") != 0)
            cvars_.set("cl_paused", "0");
        runGame(nowMs);
    }
    runGameCommands();
    if (soundSystem_) {
        auto setting = [&](const char* name, float fallback) { return cvars_.find(name) ? cvars_.value(name) : fallback; };
        // Effects silent while a scene is skipped (fast-forwarded, its
        // lines would pile up); those playing when the skip starts are
        // stopped, as the original's client does (S_StopSounds), so they
        // don't carry on after it.
        const bool skipping = game_ && cvars_.integer("skippingCinematic") != 0;
        if (skipping && !wasSkipping_)
            soundSystem_->stopSounds();
        wasSkipping_ = skipping;
        soundSystem_->setVolumes(skipping ? 0.0f : setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
    }

    // The menus: the main one whenever there's no game and none is open,
    // as the original's engine; whether they cover the game; their frame.
    bool fullscreen = false;
    if (ui_) {
        ui_->connectionState = game_ && game_->clientGameRunning() ? 7 : 1;
        Error err;
        if (!game_ && !menusHaveKeys())
            err = ui_->setActiveMenu("main");
        if (!err)
            fullscreen = ui_->fullscreen();
        if (!err)
            err = ui_->refresh(int32_t(nowMs - startMs_));
        if (err)
            menusStopped(err);
        else
            runMenuCommands();
    }

    renderer_->setGamma(brightness(ctx_));
    const float time = float(clientTime_) / 1000.0f;
    const auto drawStart = std::chrono::steady_clock::now();
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    if (game_ && game_->clientGameRunning() && !fullscreen) {
        // Each scene over the pictures drawn before it, the rest (the HUD)
        // over them all, as the Play page draws.
        scene_->setHandles({&game_->models(), &game_->skins(), &game_->shaders()});
        const Scene& scene = game_->scene();
        const auto& pictures = game_->pictures();
        const bool dynamicLights = !cvars_.find("r_dynamiclight") || cvars_.integer("r_dynamiclight") != 0;
        size_t drawn = 0, shots = 0;
        bool first = true;
        for (const auto& v : scene.views) {
            drawGamePictures(drawn, shots, v.picturesBefore, time);
            scene_->drawView(scene, v, 640, 480, time, first, world_.get(), &grid_, dynamicLights);
            first = false;
        }
        drawGamePictures(drawn, shots, pictures.size(), time);
    }
    if (ui_)
        drawMenus(float(nowMs - startMs_) / 1000.0f);
    timing_.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
    drawOverlay();
    if ((!pendingMenu_.empty() || !pendingTransition_.empty()) && !capturing_) {
        renderer_->captureNextFrame();
        capturing_ = true;
    }
    renderer_->endFrame(&overlay_);
}

void SpPlayViewer::drawMenus(float time) {
    // The menus' last frame, with any models they show and raw pictures.
    scene_->setHandles({&ui_->models(), &ui_->skins(), &ui_->shaders()});
    const auto& pictures = ui_->pictures();
    size_t drawn = 0;
    for (const auto& v : ui_->scene().views) {
        scene_->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
        drawn = std::max(drawn, v.picturesBefore);
        scene_->drawView(ui_->scene(), v, 640, 480, time, false, nullptr, nullptr, true);
    }
    scene_->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    for (const auto& img : ui_->rawImages())
        scene_->drawImage(img.rgba.data(), img.cols, img.rows, img.x, img.y, img.w, img.h, 640, 480);
}

void SpPlayViewer::showLoadingScreen(const std::string& map) {
    // As the original's engine while a level loads: the menus' loading
    // screen (a saved game's picture, or theirs), shown at once.
    if (!ui_ || !renderer_)
        return;
    ui_->connectionState = 5;
    if (Error err = ui_->drawLoadingScreen(map, "")) {
        menusStopped(err);
        return;
    }
    scene_->newFrame();
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    drawMenus(float(nowMs_ - startMs_) / 1000.0f);
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    renderer_->endFrame(&overlay_);
}

void SpPlayViewer::showClientLoading() {
    // What the client game has registered so far is loaded now, as the
    // original's renderer loads it as it's registered: the loading
    // screen's stages then take as long as the loading does.
    if (!game_)
        return;
    scene_->setHandles({&game_->models(), &game_->skins(), &game_->shaders()});
    scene_->preload();
    if (game_->drawLoadingFrame())
        return;
    if (ui_)
        if (Error err = ui_->drawConnectText("localhost", ""))
            menusStopped(err); // the loading screen goes on without their text
    scene_->newFrame();
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    scene_->setHandles({&game_->models(), &game_->skins(), &game_->shaders()});
    size_t drawn = 0, shots = 0;
    for (const auto& v : game_->scene().views) {
        drawGamePictures(drawn, shots, v.picturesBefore, 0);
        scene_->drawView(game_->scene(), v, 640, 480, 0, true, nullptr, nullptr, false);
    }
    drawGamePictures(drawn, shots, game_->pictures().size(), 0);
    if (ui_)
        drawMenus(0);
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    renderer_->endFrame(&overlay_);
}

void SpPlayViewer::drawGamePictures(size_t& drawn, size_t& shotsDrawn, size_t to, float time) {
    // The client game's pictures up to `to`, with the screen capture where
    // it drew that between them.
    const auto& pictures = game_->pictures();
    const auto& shots = game_->screenShotDraws();
    for (; shotsDrawn < shots.size() && shots[shotsDrawn].picturesBefore <= to; ++shotsDrawn) {
        const auto& s = shots[shotsDrawn];
        scene_->drawPictures(pictures, drawn, s.picturesBefore, 640, 480, time);
        drawn = std::max(drawn, s.picturesBefore);
        if (game_->screenShot.size() == eliteforce::SpUiHost::kShotSize)
            scene_->drawImage(game_->screenShot.data(), 256, 256, s.x, s.y, s.w, s.h, 640, 480);
    }
    scene_->drawPictures(pictures, drawn, to, 640, 480, time);
    drawn = std::max(drawn, to);
}

void SpPlayViewer::drawOverlay() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    auto line = [&](int y, const std::string& s, Color c) {
        // Text with a dark band behind it, for reading over the level.
        const int w = int(s.size()) * font::kAdvance + 8;
        for (int yy = std::max(y, 0); yy < std::min(y + font::kLineHeight, frameH_); ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < std::min(w, frameW_); ++x)
                p[x * 4 + 3] = 160;
        }
        font::drawText(overlay_, 4, y, s, c);
    };
    int y = 4;
    if (!error_.empty()) {
        line(y, error_, {255, 120, 120});
        y += font::kLineHeight;
        line(y, "Ctrl+R: try again   Ctrl+PgUp/PgDn: another map   Esc: menu", {200, 200, 200});
        y += font::kLineHeight;
    }
    if (!menuError_.empty()) {
        line(y, "The game's menus stopped: " + menuError_ + " (Ctrl+R without a game: start them again)",
             {255, 120, 120});
        y += font::kLineHeight;
    }
    std::vector<std::string> bottom;
    if (!lastCommand_.empty() && nowMs_ - lastCommandMs_ < 4000)
        bottom.push_back("The menus ran: " + lastCommand_);
    if (ui_ && !ui_->unhandled().empty()) {
        std::string calls = "Menu functions not answered yet:";
        for (const auto& [slot, count] : ui_->unhandled())
            calls += " " + std::to_string(slot) + " (x" + std::to_string(count) + ")";
        bottom.push_back(calls);
    }
    if (showInfo_ && game_ && !names_.empty()) {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s  server %.1f ms  client %.1f ms  draw %.1f ms  (Ctrl+I hides this)",
                      names_[size_t(index_)].c_str(), timing_.server, timing_.client, timing_.draw);
        bottom.push_back(buf);
    }
    if (game_ && cvars_.integer("skippingCinematic") != 0)
        bottom.push_back("Skipping the scene (the use button skips it, as in the original)");
    if (startMs_ && nowMs_ - startMs_ < 6000)
        bottom.push_back("Esc: the game's menu   Ctrl+Esc: leave   Ctrl+PgUp/PgDn: a map without the menus");
    for (size_t i = 0; i < bottom.size(); ++i)
        line(frameH_ - int(bottom.size() - i) * font::kLineHeight - 4, bottom[i], {220, 220, 220});
}

} // namespace reluva::idtech3
