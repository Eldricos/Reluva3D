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
    start(index_);
}

void SpPlayViewer::deactivate() {
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    game_.reset();
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
    game_.reset();
    soundSystem_.reset();
}

void SpPlayViewer::start(int index) {
    if (names_.empty() || !renderer_)
        return;
    index_ = index;
    error_.clear();
    game_.reset();
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
    soundSystem_.reset();
    if (ctx_.mixer) {
        soundSystem_ = std::make_unique<SoundSystem>(*ctx_.mixer, soundLibrary_);
        game_->soundSystem = soundSystem_.get();
    }
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
    if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
        ctx_.requestPage = "Menu";
        return;
    }
    if (!renderer_ || names_.empty())
        return;
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        const bool down = ev.type == EventType::KeyDown;
        if (down && (ev.mods & keymod::Ctrl)) {
            // Reluva's own: restart, another map, the info line.
            const int n = int(names_.size());
            if (ev.key == 'r')
                return start(index_);
            if (ev.key == key::PageUp || ev.key == key::PageDown)
                return start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
            if (ev.key == 'i') {
                showInfo_ = !showInfo_;
                return;
            }
        }
        if (down && ev.repeat)
            return;
        if (const int32_t k = toQ3Key(ev.key))
            runBinding(ctx_.bindings.get(k), k, down);
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (const int32_t k = toQ3Button(ev.button))
            runBinding(ctx_.bindings.get(k), k, ev.type == EventType::MouseDown);
        break;
    case EventType::MouseMove:
        mouseX_ += ev.moveX;
        mouseY_ += ev.moveY;
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
    if (soundSystem_) {
        // The original's volumes unless the menus set them.
        soundSystem_->setVolumes(setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
        soundSystem_->endFrame();
    }
}

void SpPlayViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    runGame(nowMs);
    renderer_->setGamma(brightness(ctx_));
    const float time = float(clientTime_) / 1000.0f;
    const auto drawStart = std::chrono::steady_clock::now();
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    if (game_ && game_->clientGameRunning()) {
        // Each scene over the pictures drawn before it, the rest (the HUD)
        // over them all, as the Play page draws.
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
    timing_.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
    drawOverlay();
    renderer_->endFrame(&overlay_);
}

void SpPlayViewer::drawOverlay() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    auto line = [&](int y, const std::string& s, Color c) {
        // Text with a dark band behind it, for reading over the level.
        const int w = int(s.size()) * font::kAdvance + 8;
        for (int yy = y; yy < std::min(y + font::kLineHeight, frameH_); ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < std::min(w, frameW_); ++x)
                p[x * 4 + 3] = 160;
        }
        font::drawText(overlay_, 4, y, s, c);
    };
    if (!error_.empty()) {
        line(4, error_, {255, 120, 120});
        line(4 + font::kLineHeight, "Ctrl+R: try again   Ctrl+PgUp/PgDn: another map   Esc: menu", {200, 200, 200});
        return;
    }
    if (!showInfo_ || !game_)
        return;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s  server %.1f ms  client %.1f ms  draw %.1f ms  (Ctrl+I hides this)",
                  names_[size_t(index_)].c_str(), timing_.server, timing_.client, timing_.draw);
    line(frameH_ - font::kLineHeight - 4, buf, {220, 220, 220});
}

} // namespace reluva::idtech3
