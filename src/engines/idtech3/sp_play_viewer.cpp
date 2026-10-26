// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kButtonAttack = 1, kButtonAltAttack = 32;

int32_t toShort(float degrees) { return int32_t(std::lround(degrees * 65536.0f / 360.0f)) & 0xffff; }

bool isButton(const std::string& name) {
    static const char* const kButtons[] = {"forward", "back", "moveleft", "moveright", "moveup", "movedown",
                                           "left", "right", "lookup", "lookdown", "speed", "strafe",
                                           "attack", "altattack", "mlook", "klook"};
    for (const char* b : kButtons)
        if (name == b)
            return true;
    return name.rfind("button", 0) == 0 && name.size() > 6 && name.size() <= 8 &&
           std::isdigit(static_cast<unsigned char>(name[6]));
}

} // namespace

SpPlayViewer::SpPlayViewer(ViewerContext& ctx) : Page(ctx) {
    // Elite Force with its single-player game code beside the game folder.
    if (std::strcmp(ctx.game.gameId, "stvef") != 0 || ctx.installPath.empty())
        return;
    const std::filesystem::path dll = ctx.installPath / "efgamex86.dll";
    std::ifstream in(dll, std::ios::binary);
    if (!in)
        return;
    dll_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    // Its menus, if they're there too.
    if (std::ifstream ui{ctx.installPath / "efuix86.dll", std::ios::binary})
        uiDll_.assign(std::istreambuf_iterator<char>(ui), std::istreambuf_iterator<char>());
    // Its maps: all but Holomatch's.
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0 &&
            key.rfind("maps/hm_", 0) != 0 && key.rfind("maps/ctf_", 0) != 0)
            names_.push_back(key);
    }
    std::sort(names_.begin(), names_.end());
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == "maps/borg1.bsp")
            index_ = int(i);
}

SpPlayViewer::~SpPlayViewer() { deactivate(); }

void SpPlayViewer::activate() {
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
    scene_ = std::make_unique<SceneDrawer>(*renderer_, *shaders_, *models_);
    ctx_.sys.setRelativeMouse(true);
    if (ctx_.mixer)
        soundSystem_ = std::make_unique<SoundSystem>(*ctx_.mixer, soundLibrary_);
    startMs_ = 0;
    if (!uiDll_.empty())
        startMenus();
    if (!ui_)
        start(index_);
}

void SpPlayViewer::startMenus() {
    menuError_.clear();
    ui_ = std::make_unique<eliteforce::SpUiHost>(ctx_.files, cvars_);
    ui_->print = [](const std::string& text) { std::fputs(text.c_str(), stdout); };
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
    ui_->askGame = [this](uint32_t flag) { return game_ ? game_->allowedToSaveHere(flag) : flag; };
    if (Error err = ui_->load(uiDll_))
        return menusStopped(err);
    if (Error err = ui_->setActiveMenu("main"))
        return menusStopped(err);
    runMenuCommands();
}

void SpPlayViewer::menusStopped(const Error& err) {
    // The game goes on without them (Esc then leaves the page).
    menuError_ = "efuix86.dll: " + err.message;
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

void SpPlayViewer::runCommand(const std::string& line) {
    // What the menus run, as the original's console would.
    const std::vector<std::string> w = splitCommand(line);
    if (w.empty())
        return;
    lastCommand_ = line;
    lastCommandMs_ = nowMs_;
    const std::string cmd = fs::toLower(w[0]);
    if (cmd == "map" || cmd == "devmap" || cmd == "spmap") {
        if (w.size() < 2)
            return;
        std::string map = fs::toLower(w[1]);
        if (map.size() > 4 && map.compare(map.size() - 4, 4, ".bsp") == 0)
            map.resize(map.size() - 4);
        const auto it = std::find(names_.begin(), names_.end(), "maps/" + map + ".bsp");
        if (it == names_.end()) {
            error_ = "There's no map " + map;
            return;
        }
        start(int(it - names_.begin()));
    } else if (cmd == "disconnect") {
        endGame();
        if (ui_)
            if (Error err = ui_->setActiveMenu("main"))
                menusStopped(err);
    } else if (cmd == "quit") {
        endGame();
        ctx_.requestPage = "Menu";
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
    } else if (cmd == "load" || cmd == "save") {
        lastCommand_ = line + " (saved games aren't supported yet)";
    } else if (cmd == "cvar_restart" || cmd == "snd_restart" || cmd == "in_restart" || cmd == "vid_restart" ||
               cmd == "s_enable_a3d" || cmd == "s_disable_a3d") {
        // Nothing to restart here.
    } else if (game_) {
        if (Error err = game_->clientConsoleCommand(line))
            stop("The game stopped: " + err.message);
    }
}

void SpPlayViewer::deactivate() {
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    game_.reset();
    ui_.reset();
    soundSystem_.reset();
    scene_.reset();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    buttons_.clear();
}

void SpPlayViewer::stop(const std::string& why) {
    error_ = why;
    endGame();
}

void SpPlayViewer::endGame() {
    game_.reset();
    if (soundSystem_)
        soundSystem_->stopAll();
    buttons_.clear();
}

void SpPlayViewer::start(int index) {
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
    yaw_ = pitch_ = 0;
    clientTime_ = 0;
    lastMs_ = 0;
    const std::string& path = names_[size_t(index)];
    std::string map = path.substr(5, path.size() - 9);
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
    if ((err = game_->load(dll_)))
        return stop("efgamex86.dll: " + err.message);
    if ((err = game_->startLevel(map, bsp, eliteforce::SpGameHost::mapChecksum(data), 0)))
        return stop("The game couldn't start the level: " + err.message);
    std::string refused;
    if ((err = game_->connectPlayer("\\name\\Munro\\model\\munro/main", refused)))
        return stop("The player couldn't join: " + err.message);
    if (!refused.empty())
        return stop("The game refused the player: " + refused);
    scene_->setHandles({&game_->models(), &game_->skins(), &game_->shaders()});
    if ((err = game_->startClientGame()))
        return stop("The client game couldn't start: " + err.message);
    // The player's time from where the level is (it has settled for a few
    // frames before they joined).
    clientTime_ = game_->levelTime();
}

void SpPlayViewer::runBinding(const std::string& line, int32_t key, bool down) {
    // "+name" is held while the key is (the engine's buttons) or runs the
    // client game's +/- pair; other commands run on the press.
    for (const std::string& command : splitCommands(line)) {
        const std::vector<std::string> w = splitCommand(command);
        if (w.empty())
            continue;
        if (w[0][0] == '+' || w[0][0] == '-') {
            if (w[0][0] == '-')
                continue;
            const std::string name = fs::toLower(w[0].substr(1));
            if (isButton(name)) {
                if (down)
                    buttons_[name].insert(key);
                else
                    buttons_[name].erase(key);
            } else if (game_) {
                if (Error err = game_->clientConsoleCommand((down ? "+" : "-") + name))
                    stop("The game stopped: " + err.message);
            }
            continue;
        }
        if (down && game_)
            if (Error err = game_->clientConsoleCommand(command))
                stop("The game stopped: " + err.message);
    }
}

bool SpPlayViewer::held(const char* button) const {
    const auto it = buttons_.find(button);
    return it != buttons_.end() && !it->second.empty();
}

bool SpPlayViewer::takesKey(const Event& ev) const {
    return ev.type == EventType::KeyDown && ev.key == key::Escape && !(ev.mods & keymod::Ctrl);
}

void SpPlayViewer::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
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
                ctx_.requestPage = "Menu";
                return;
            }
            if (menusHaveKeys())
                return menuKey(q3key::Escape);
            if (Error err = ui_->setActiveMenu(game_ ? "ingame" : "main"))
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
            if (Error err = ui_->mouseEvent(int32_t(ev.moveX), int32_t(ev.moveY)))
                menusStopped(err);
        } else {
            mouseX_ += ev.moveX;
            mouseY_ += ev.moveY;
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
    const float sensitivity = setting("sensitivity", 5);
    yaw_ -= mouseX_ * sensitivity * setting("m_yaw", 0.022f);
    if (setting("cl_freelook", 1) != 0 || held("mlook"))
        pitch_ += mouseY_ * sensitivity * setting("m_pitch", 0.022f);
    mouseX_ = mouseY_ = 0;
    const float keyScale = (held("speed") ? setting("cl_anglespeedkey", 1.5f) : 1.0f) * float(dt) / 1000.0f;
    if (!held("strafe"))
        yaw_ += float(held("left") - held("right")) * setting("cl_yawspeed", 140) * keyScale;
    pitch_ += float(held("lookdown") - held("lookup")) * setting("cl_pitchspeed", 140) * keyScale;
    pitch_ = std::remainder(pitch_, 360.0f);

    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) { return std::chrono::duration<float, std::milli>(Clock::now() - from).count(); };
    if (dt > 0)
        clientTime_ += dt;
    // The server kept a frame ahead of the player's time, then the
    // player's command, then the client game's frame.
    const auto serverStart = Clock::now();
    while (game_->levelTime() <= clientTime_)
        if (Error err = game_->runFrame())
            return stop("The game stopped: " + err.message);
    if (dt > 0) {
        eliteforce::SpGameHost::UserCommand cmd;
        cmd.serverTime = clientTime_;
        cmd.angles = {toShort(pitch_), toShort(yaw_), 0};
        for (const auto& [name, keys] : buttons_) {
            if (keys.empty())
                continue;
            if (name == "attack")
                cmd.buttons |= kButtonAttack;
            else if (name == "altattack")
                cmd.buttons |= kButtonAltAttack;
            else if (name.rfind("button", 0) == 0)
                cmd.buttons |= 1 << std::atoi(name.c_str() + 6);
        }
        const int speed = (cvars_.find("cl_run") ? cvars_.integer("cl_run") != 0 : true) != held("speed") ? 127 : 64;
        int side = held("moveright") - held("moveleft");
        if (held("strafe"))
            side += held("right") - held("left");
        cmd.forward = int8_t(std::clamp(speed * (held("forward") - held("back")), -127, 127));
        cmd.right = int8_t(std::clamp(speed * side, -127, 127));
        cmd.up = int8_t(std::clamp(speed * (held("moveup") - held("movedown")), -127, 127));
        if (Error err = game_->clientThink(cmd))
            return stop("The game stopped: " + err.message);
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

void SpPlayViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    nowMs_ = nowMs;
    if (!startMs_)
        startMs_ = nowMs;
    // The game waits while a menu has the keys, as the original's single
    // player pauses.
    if (menusHaveKeys())
        lastMs_ = nowMs;
    else
        runGame(nowMs);
    if (soundSystem_) {
        auto setting = [&](const char* name, float fallback) { return cvars_.find(name) ? cvars_.value(name) : fallback; };
        soundSystem_->setVolumes(setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
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
        const eliteforce::Scene& scene = game_->scene();
        const auto& pictures = game_->pictures();
        const bool dynamicLights = !cvars_.find("r_dynamiclight") || cvars_.integer("r_dynamiclight") != 0;
        size_t drawn = 0;
        bool first = true;
        for (const auto& v : scene.views) {
            scene_->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
            drawn = std::max(drawn, v.picturesBefore);
            scene_->drawView(scene, v, 640, 480, time, first, world_.get(), &grid_, dynamicLights);
            first = false;
        }
        scene_->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    }
    if (ui_) {
        // The menus over it, with any models they show.
        const float menuTime = float(nowMs - startMs_) / 1000.0f;
        scene_->setHandles({&ui_->models(), &ui_->skins(), &ui_->shaders()});
        const auto& pictures = ui_->pictures();
        size_t drawn = 0;
        for (const auto& v : ui_->scene().views) {
            scene_->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, menuTime);
            drawn = std::max(drawn, v.picturesBefore);
            scene_->drawView(ui_->scene(), v, 640, 480, menuTime, false, nullptr, nullptr, true);
        }
        scene_->drawPictures(pictures, drawn, pictures.size(), 640, 480, menuTime);
    }
    timing_.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
    drawOverlay();
    renderer_->endFrame(&overlay_);
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
    if (startMs_ && nowMs_ - startMs_ < 6000)
        bottom.push_back("Esc: the game's menu   Ctrl+Esc: leave   Ctrl+PgUp/PgDn: a map without the menus");
    for (size_t i = 0; i < bottom.size(); ++i)
        line(frameH_ - int(bottom.size() - i) * font::kLineHeight - 4, bottom[i], {220, 220, 220});
}

} // namespace reluva::idtech3
