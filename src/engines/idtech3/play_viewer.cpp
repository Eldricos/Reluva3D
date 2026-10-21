// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Play page: a Holomatch level run by Elite Force's own game code, as
// a listen server would. The server game (vm/qagame.qvm) runs the level
// with one player, whose commands come from the keyboard and mouse; the
// client game (vm/cgame.qvm) gets a snapshot after each server frame and
// says what to draw: the level from the player's eye, then the HUD.
// Entities the client game adds as models are drawn with their skins,
// lit by the light grid; other kinds (sprites, beams...) not yet.

#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr uint64_t kHintMs = 8000;
constexpr float kPi = 3.14159265358979f;
// Degrees turned per unit the mouse moves: the original's defaults
// (sensitivity 5, m_yaw and m_pitch 0.022).
constexpr float kMouseDegrees = 5 * 0.022f;
// Quake III's BUTTON_ATTACK, taken to be Elite Force's too.
constexpr int32_t kButtonAttack = 1;
// refdef_t's flag for views without the level (the HUD's heads).
constexpr int32_t kNoWorldModel = 1;
// refEntity_t: Quake III's 140 bytes, then 20 of Elite Force's own (see
// eliteforce-syscalls.md). Its type (0: a model), render flags, model,
// lighting origin, axes (forward, left, up), origin, frame, old frame
// and how far back towards it, custom skin.
namespace refent {
constexpr size_t Type = 0, Flags = 4, Model = 8, LightingOrigin = 12, Axes = 28, Origin = 68, Frame = 80,
                 OldFrame = 96, BackLerp = 100, CustomSkin = 108;
constexpr int32_t TypeModel = 0;
// Render flags (Quake III's): drawn only in mirrors and portals (the
// player's own body); lit from the lighting origin.
constexpr int32_t ThirdPerson = 2, UseLightingOrigin = 0x80;
} // namespace refent

int32_t readInt(const std::vector<uint8_t>& bytes, size_t at) {
    int32_t v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, &bytes[at], 4);
    return v;
}

float readFloat(const std::vector<uint8_t>& bytes, size_t at) { return Qvm::asFloat(readInt(bytes, at)); }

gfx::Vec3 readVec(const std::vector<uint8_t>& bytes, size_t at) {
    return {readFloat(bytes, at), readFloat(bytes, at + 4), readFloat(bytes, at + 8)};
}

// An angle in degrees as usercmd_t carries it: 65536 to a full turn.
int32_t toShort(float degrees) { return int32_t(std::lround(degrees * 65536.0f / 360.0f)) & 0xffff; }

} // namespace

PlayViewer::PlayViewer(ViewerContext& ctx) : Page(ctx) {
    if (std::strcmp(ctx.game.gameId, "stvef") != 0 || !ctx.files.has("vm/qagame.qvm") || !ctx.files.has("vm/cgame.qvm"))
        return;
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            names_.push_back(key);
    }
    std::sort(names_.begin(), names_.end());
    // Holomatch maps first.
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i].rfind("maps/hm_", 0) == 0) {
            index_ = int(i);
            break;
        }
}

PlayViewer::~PlayViewer() { deactivate(); }

void PlayViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Play page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    world_ = std::make_unique<WorldRenderer>(*renderer_, *shaders_);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    shownMs_ = 0;
    ctx_.sys.setRelativeMouse(true);
    start(index_);
}

void PlayViewer::deactivate() {
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    // The level stops with the page; it starts again when shown.
    cgame_.reset();
    game_.reset();
    materials_.clear();
    modelIds_.clear();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    held_ = {};
}

void PlayViewer::start(int index) {
    index_ = index;
    error_.clear();
    cgame_.reset();
    game_.reset();
    materials_.clear();
    modelIds_.clear();
    models_->clear();
    world_->clear();
    shaders_->clear();
    yaw_ = pitch_ = 0;
    const std::string& path = names_[size_t(index)];
    std::string map = path.substr(5, path.size() - 9); // without maps/ and .bsp
    setTitle(map + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");

    auto bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = ctx_.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = world_->load(*bsp, ctx_.shaders);
    if (err) {
        stop("Couldn't load " + path + ": " + err.message);
        return;
    }
    // The light grid lights models, with the worldspawn's cell size if it
    // gives one.
    grid_ = LightGrid{};
    {
        float cell[3] = {64, 64, 128};
        std::vector<Entity> entities;
        if (!parseEntities(bsp->entities, entities) && !entities.empty())
            entities[0].vector("gridsize", cell);
        grid_.build(*bsp, {cell[0], cell[1], cell[2]});
    }

    // The server: the level, and the player joining it. The holodeck
    // intro, which holds new players in place, is off until there's a
    // way to watch it.
    cvars_.set("g_holoIntro", "0");
    game_ = std::make_unique<eliteforce::GameHost>(ctx_.files, cvars_);
    err = game_->load();
    if (!err)
        err = game_->startLevel(map, bsp, int32_t(ctx_.sys.getMillis()));
    std::string refused;
    if (!err)
        err = game_->connectClient(0,
                                   "\\name\\Reluva\\model\\munro\\handicap\\100\\sex\\male\\color\\4"
                                   "\\snaps\\20\\rate\\25000\\ip\\localhost",
                                   refused);
    if (!err && !refused.empty())
        err = Error::fail("the game refused the player: " + refused);
    if (!err)
        err = game_->beginClient(0);
    if (err) {
        stop("The server game stopped: " + err.message);
        return;
    }

    // The player's client game, given what the server has set.
    cgame_ = std::make_unique<eliteforce::CgameHost>(ctx_.files, cvars_);
    err = cgame_->load();
    if (!err) {
        cgame_->setConfigstrings(game_->configstrings());
        sendServerCommands();
        err = cgame_->init(0, 0, 0);
    }
    if (err) {
        stop("The client game stopped: " + err.message);
        return;
    }
    clientTime_ = game_->levelTime();
    lastMs_ = 0;
    cgame_->newSnapshot(game_->snapshot(0));
}

void PlayViewer::stop(const std::string& why) {
    error_ = why;
    const std::string log = cgame_ ? cgame_->log() : game_ ? game_->log() : std::string();
    if (!log.empty()) {
        std::string last = log;
        while (!last.empty() && last.back() == '\n')
            last.pop_back();
        error_ += " (last printed: " + last.substr(last.rfind('\n') == std::string::npos ? 0 : last.rfind('\n') + 1) + ")";
    }
    cgame_.reset();
    game_.reset();
}

void PlayViewer::sendServerCommands() {
    std::vector<std::string> commands;
    for (const auto& [to, text] : game_->serverCommands())
        if (to == 0 || to == -1)
            commands.push_back(text);
    cgame_->setServerCommands(std::move(commands));
}

void PlayViewer::consoleCommand(const std::string& text) {
    if (!cgame_)
        return;
    bool handled = false;
    if (Error err = cgame_->consoleCommand(text, handled))
        stop("The client game stopped: " + err.message);
    else if (!handled)
        notice_ = "The client game doesn't know \"" + text + "\"";
}

void PlayViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    auto hold = [&](uint32_t k, bool down) {
        switch (k) {
        case 'w': case key::Up: held_[Forward] = down; return true;
        case 's': case key::Down: held_[Back] = down; return true;
        case 'a': held_[Left] = down; return true;
        case 'd': held_[Right] = down; return true;
        case key::Space: held_[Jump] = down; return true;
        case 'c': case key::LCtrl: held_[Crouch] = down; return true;
        case key::Left: held_[TurnLeft] = down; return true;
        case key::Right: held_[TurnRight] = down; return true;
        default: return false;
        }
    };
    switch (ev.type) {
    case EventType::KeyUp: hold(ev.key, false); break;
    case EventType::KeyDown: {
        if (ev.key == 'r' && (ev.mods & keymod::Ctrl)) {
            start(index_);
            return;
        }
        if (hold(ev.key, true) || ev.repeat)
            return;
        const int n = int(names_.size());
        if (ev.key == key::PageUp || ev.key == key::PageDown)
            start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
        else if (ev.key == 'i')
            showInfo_ = !showInfo_;
        else if (ev.key >= '1' && ev.key <= '9')
            consoleCommand("weapon " + std::string(1, char(ev.key)));
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (ev.button == MouseButton::Left)
            attack_ = ev.type == EventType::MouseDown;
        break;
    case EventType::MouseWheel:
        if (ev.wheelY)
            consoleCommand(ev.wheelY > 0 ? "weapprev" : "weapnext");
        break;
    case EventType::MouseMove:
        // Moving the mouse up looks up: pitch counts downwards.
        yaw_ -= ev.moveX * kMouseDegrees;
        pitch_ = std::clamp(pitch_ + ev.moveY * kMouseDegrees, -89.0f, 89.0f);
        break;
    default: break;
    }
}

void PlayViewer::runGame(uint64_t nowMs) {
    if (!game_ || !cgame_)
        return;
    const int32_t dt = lastMs_ ? int32_t(std::min<uint64_t>(nowMs - lastMs_, 100)) : 0;
    lastMs_ = nowMs;
    yaw_ += float(held_[TurnLeft] - held_[TurnRight]) * 140.0f * float(dt) / 1000.0f;
    if (dt > 0) {
        // The player's command for this moment, as a client sends one each
        // frame: where they look, how they move, what they press. The game
        // moves the player by it at once, as the original server does.
        clientTime_ += dt;
        eliteforce::GameHost::UserCommand cmd;
        cmd.serverTime = clientTime_;
        cmd.angles = {toShort(pitch_), toShort(yaw_), 0};
        cmd.buttons = attack_ ? kButtonAttack : 0;
        cmd.weapon = uint8_t(cgame_->weaponSelect());
        cmd.forward = int8_t(127 * (held_[Forward] - held_[Back]));
        cmd.right = int8_t(127 * (held_[Right] - held_[Left]));
        cmd.up = int8_t(127 * (held_[Jump] - held_[Crouch]));
        if (Error err = game_->clientThink(0, cmd)) {
            stop("The server game stopped: " + err.message);
            return;
        }
        cgame_->addUserCommand(cmd);
    }
    // The server keeps a frame ahead of the player's time, so the client
    // game always has the snapshot after it to move things towards.
    while (game_->levelTime() <= clientTime_) {
        if (Error err = game_->runFrame()) {
            stop("The server game stopped: " + err.message);
            return;
        }
        sendServerCommands();
        cgame_->setConfigstrings(game_->configstrings());
        cgame_->newSnapshot(game_->snapshot(0));
    }
    cgame_->clearScene();
    cgame_->clearPictures();
    int32_t ignored = 0;
    if (Error err = cgame_->call(eliteforce::CgameHost::DrawActiveFrame, {clientTime_, 0, 0}, ignored))
        stop("The client game stopped: " + err.message);
}

void PlayViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    if (!shownMs_)
        shownMs_ = nowMs;
    runGame(nowMs);
    const float time = float(clientTime_) / 1000.0f;

    // The player's view: the first the client game rendered with the
    // level in it (refdef_t: fov y at 20, the eye at 24, the axes from 36:
    // forward, left, up).
    const std::vector<uint8_t>* view = nullptr;
    const eliteforce::CgameHost::View* sceneView = nullptr;
    if (cgame_)
        for (const auto& v : cgame_->scene().views)
            if (!(readInt(v.refdef, 76) & kNoWorldModel)) {
                view = &v.refdef;
                sceneView = &v;
                break;
            }
    const float aspect = float(kFrameW) / float(kFrameH);
    if (view) {
        eye_ = readVec(*view, 24);
        const gfx::Vec3 forward = readVec(*view, 36), up = readVec(*view, 60);
        const float fovY = std::clamp(readFloat(*view, 20), 1.0f, 170.0f) * kPi / 180.0f;
        const gfx::Mat4 viewProj =
            gfx::multiply(gfx::perspective(fovY, aspect, 4, 32768), gfx::lookAlong(eye_, forward, up));
        renderer_->beginFrame(viewProj, eye_, {0, 0, 0});
        constexpr float kLastOpaque = 5;
        placeEntities(*sceneView);
        world_->drawSky(time);
        world_->drawSurfaces(time, 0, kLastOpaque);
        for (const Placed& p : placed_)
            models_->draw(p.model, p.frame, p.transform, p.light, time, p.skin, 0, kLastOpaque);
        world_->drawSurfaces(time, kLastOpaque + 0.5f);
        for (const Placed& p : placed_)
            models_->draw(p.model, p.frame, p.transform, p.light, time, p.skin, kLastOpaque + 0.5f);
    } else {
        placed_.clear();
        renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    }

    // The HUD over it, in the client game's 640x480 screen.
    std::vector<eliteforce::CgameHost::Picture> pictures;
    if (cgame_)
        pictures = cgame_->pictures();
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(pictures.size() * 4);
    indices.reserve(pictures.size() * 6);
    for (const auto& p : pictures) {
        const auto base = uint32_t(vertices.size());
        uint8_t rgba[4];
        for (int i = 0; i < 4; ++i)
            rgba[i] = uint8_t(std::clamp(p.color[size_t(i)], 0.0f, 1.0f) * 255.0f + 0.5f);
        const float xs[4] = {p.x, p.x + p.w, p.x + p.w, p.x};
        const float ys[4] = {p.y, p.y, p.y + p.h, p.y + p.h};
        const float ss[4] = {p.s1, p.s2, p.s2, p.s1};
        const float ts[4] = {p.t1, p.t1, p.t2, p.t2};
        for (int i = 0; i < 4; ++i) {
            gfx::Vertex v{};
            v.pos[0] = xs[i];
            v.pos[1] = ys[i];
            v.uv[0] = ss[i];
            v.uv[1] = ts[i];
            v.normal[2] = 1;
            std::memcpy(v.color, rgba, 4);
            vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(base + i);
    }
    gfx::MeshId mesh = 0;
    if (!vertices.empty())
        renderer_->createMesh(vertices, indices, mesh);
    if (mesh) {
        gfx::Mat4 ortho{};
        ortho[0] = 2.0f / 640.0f;
        ortho[5] = -2.0f / 480.0f;
        ortho[10] = 1;
        ortho[12] = -1;
        ortho[13] = 1;
        ortho[15] = 1;
        renderer_->setView(ortho, {0, 0, 0});
        for (size_t i = 0; i < pictures.size(); ++i) {
            const int32_t handle = pictures[i].shader;
            if (handle <= 0)
                continue;
            if (size_t(handle) >= materials_.size())
                materials_.resize(size_t(handle) + 1, -2);
            int& material = materials_[size_t(handle)];
            if (material == -2)
                material = shaders_->material(cgame_->shaders().name(handle), Lighting::TwoD);
            if (material < 0 || shaders_->get(material).skip)
                continue;
            gfx::DrawCall call;
            call.mesh = mesh;
            call.firstIndex = uint32_t(i * 6);
            call.indexCount = 6;
            shaders_->draw(material, call, 0, time);
        }
    }

    drawOverlay(nowMs, view != nullptr);
    renderer_->endFrame(&overlay_);
    if (mesh)
        renderer_->destroyMesh(mesh);
}

void PlayViewer::placeEntities(const eliteforce::CgameHost::View& view) {
    // The view's entities that are models, where the client game put them.
    placed_.clear();
    otherEntities_ = 0;
    unloadable_.clear();
    const auto& entities = cgame_->scene().entities;
    for (size_t i = view.firstEntity; i < view.firstEntity + view.entityCount && i < entities.size(); ++i) {
        const std::vector<uint8_t>& e = entities[i].bytes;
        if (readInt(e, refent::Type) != refent::TypeModel) {
            ++otherEntities_;
            continue;
        }
        const int32_t flags = readInt(e, refent::Flags);
        if (flags & refent::ThirdPerson)
            continue;
        const int model = modelId(readInt(e, refent::Model));
        if (model < 0)
            continue;
        Placed p;
        p.model = model;
        const gfx::Vec3 origin = readVec(e, refent::Origin);
        p.transform = placement(origin, {readVec(e, refent::Axes), readVec(e, refent::Axes + 12),
                                         readVec(e, refent::Axes + 24)});
        // The nearer of the two frames it's between.
        p.frame = readFloat(e, refent::BackLerp) < 0.5f ? readInt(e, refent::Frame) : readInt(e, refent::OldFrame);
        p.light = grid_.sample(flags & refent::UseLightingOrigin ? readVec(e, refent::LightingOrigin) : origin);
        if (const int32_t skin = readInt(e, refent::CustomSkin); skin > 0 && !cgame_->skins().name(skin).empty())
            p.skin = &models_->skin(cgame_->skins().name(skin));
        placed_.push_back(p);
    }
}

int PlayViewer::modelId(int32_t handle) {
    // The client game's model handle to a loaded model, loaded on first
    // use; -1 if it can't be.
    if (handle <= 0)
        return -1;
    if (const auto it = modelIds_.find(handle); it != modelIds_.end()) {
        if (it->second < 0)
            unloadable_.insert(cgame_->models().name(handle));
        return it->second;
    }
    const std::string name = cgame_->models().name(handle);
    int id = -1;
    if (name.size() > 4 && fs::toLower(name.substr(name.size() - 4)) == ".md3")
        id = models_->load(name);
    modelIds_[handle] = id;
    if (id < 0)
        unloadable_.insert(name);
    return id;
}

void PlayViewer::drawOverlay(uint64_t nowMs, bool haveView) {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!error_.empty()) {
        lines.push_back(error_);
        lines.push_back("Ctrl+R: start the level again, PageUp/PageDown: another map.");
    }
    if (showInfo_) {
        char line[200];
        if (game_) {
            std::snprintf(line, sizeof line, "time %.1f s, eye at %.0f %.0f %.0f%s; %zu models drawn, %d other entities not yet",
                          float(clientTime_) / 1000.0f, eye_[0], eye_[1], eye_[2],
                          haveView ? "" : " (no view yet)", placed_.size(), otherEntities_);
            lines.push_back(line);
            if (!unloadable_.empty()) {
                std::string names = "Models not drawn (only .md3 so far):";
                for (const std::string& n : unloadable_)
                    names += " " + n;
                lines.push_back(names.size() > 150 ? names.substr(0, 150) + "..." : names);
            }
        }
        for (const auto* host : {"server", "client"}) {
            const std::map<int, int>* unhandled = nullptr;
            if (host[0] == 's' && game_)
                unhandled = &game_->unhandled();
            else if (host[0] == 'c' && cgame_)
                unhandled = &cgame_->unhandled();
            if (!unhandled || unhandled->empty())
                continue;
            std::string calls = std::string("The ") + host + " game's system calls not implemented yet:";
            for (const auto& [number, count] : *unhandled)
                calls += " " + std::to_string(number);
            lines.push_back(calls);
        }
        if (!notice_.empty())
            lines.push_back(notice_);
    }
    if (nowMs - shownMs_ < kHintMs) {
        lines.push_back("W/A/S/D move, the mouse looks, Space jumps, C crouches, click fires, wheel or 1-9 picks a weapon.");
        lines.push_back("PageUp/PageDown: another map, I: info, Ctrl+R: restart, Tab: next page, Esc: launcher.");
    }
    const int lh = font::kLineHeight;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = kFrameH - int(lines.size() - i) * lh - 4;
        const int w = std::min(kFrameW, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(overlay_, 4, y, lines[i], i == 0 && !error_.empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::idtech3
