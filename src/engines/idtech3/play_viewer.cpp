// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Play page: a Holomatch level run by Elite Force's own game code, as
// a listen server would. The server game (vm/qagame.qvm) runs the level
// with one player, whose commands come from the keyboard and mouse; the
// client game (vm/cgame.qvm) gets a snapshot after each server frame and
// says what to draw: the level from the player's eye, then the HUD.
// Entities the client game adds as models are drawn with their skins,
// lit by the light grid; sprites, quads and lines (beams) with their
// shaders and colours. Other kinds aren't drawn yet.

#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
// The command's button bits: attack 1 (Quake III's) and alt attack 32
// (Elite Force's own), found with reluva-tool game-run -buttons: holding
// either changes the same player state words (weapon time, ammunition,
// events). See eliteforce-syscalls.md.
constexpr int32_t kButtonAttack = 1, kButtonAltAttack = 32;
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
// Elite Force's effect types, read from what its client game adds when
// firing (see eliteforce-syscalls.md): a sprite facing the camera (1:
// radius at 136, rotation in degrees at 132; its axes are left as they
// were cleared), a quad in the entity's own axes (2: left and up, radius
// at 136; the first axis faces out of what it lies on), a line from the
// origin to the old origin (width at 132, the texture repeated along it as often as 140
// says), and one tapering from the width at 132 to the one at 136.
// More, from the other weapons: a cubic Bezier curve from the origin
// through control points at 136 and 148 to the old origin (12), a ring
// or cone round the first axis (13: radii at 132 and 136, height at 140,
// 144 the texture's repeats round it), and electricity from the origin to
// the old origin (14: width 132, jitter 136, texture scale 140).
constexpr int32_t TypeSprite = 1, TypeQuad = 2, TypeLine = 9, TypeTaperedLine = 11, TypeCurve = 12,
                  TypeCylinder = 13, TypeBolt = 14;
constexpr size_t OldOrigin = 84, CustomShader = 112, Rgba = 116, ShaderTime = 128, Rotation = 132, Radius = 136,
                 Width = 132, EndWidth = 136, Repeat = 140, Control1 = 136, Control2 = 148, Jitter = 136,
                 CylinderRepeat = 144;
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
    // A level the menus asked for.
    if (!ctx_.requestMap.empty()) {
        const std::string wanted = normalizeArchiveName("maps/" + ctx_.requestMap + ".bsp");
        for (size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == wanted)
                index_ = int(i);
        ctx_.requestMap.clear();
    }
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
    effectMaterials_.clear();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    buttons_.clear();
}

void PlayViewer::start(int index) {
    index_ = index;
    error_.clear();
    cgame_.reset();
    game_.reset();
    materials_.clear();
    modelIds_.clear();
    effectMaterials_.clear();
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
    // As the original's client: commands it knows itself, then the client
    // game's, and anything else goes to the server, whose game answers
    // (an unknown command it prints as such).
    const std::vector<std::string> w = splitCommand(text);
    if (w.empty() || !cgame_ || !game_)
        return;
    const std::string verb = fs::toLower(w[0]);
    if (verb == "centerview") {
        pitch_ = 0;
        return;
    }
    if (verb == "vstr" && w.size() >= 2) {
        runBinding(cvars_.string(w[1]), 0, true);
        return;
    }
    if (verb == "togglemenu") {
        ctx_.requestPage = "Menu";
        return;
    }
    if (verb == "toggleconsole" || verb == "screenshot" || verb == "screenshotjpeg" || verb.rfind("messagemode", 0) == 0) {
        notice_ = "\"" + text + "\" isn't done yet";
        return;
    }
    bool handled = false;
    if (Error err = cgame_->consoleCommand(text, handled)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    if (Error err = game_->clientCommand(0, text))
        stop("The server game stopped: " + err.message);
}

bool PlayViewer::isButton(const std::string& name) {
    static const char* const kButtons[] = {"forward", "back", "moveleft", "moveright", "moveup", "movedown",
                                           "left", "right", "lookup", "lookdown", "speed", "strafe",
                                           "attack", "altattack", "mlook", "klook"};
    for (const char* b : kButtons)
        if (name == b)
            return true;
    return name.rfind("button", 0) == 0 && name.size() > 6 && name.size() <= 8 &&
           std::isdigit(static_cast<unsigned char>(name[6]));
}

void PlayViewer::runBinding(const std::string& line, int32_t key, bool down) {
    // A key's command: "+name" is held while the key is (the engine's own
    // buttons, or the client game's +/- pair); others run on the press.
    for (const std::string& command : splitCommands(line)) {
        const std::vector<std::string> w = splitCommand(command);
        if (w.empty())
            continue;
        if (w[0][0] == '+' || w[0][0] == '-') {
            const std::string name = fs::toLower(w[0].substr(1));
            if (w[0][0] == '-')
                continue; // a key bound to "-x" alone does nothing useful
            if (isButton(name)) {
                if (down)
                    buttons_[name].insert(key);
                else
                    buttons_[name].erase(key);
            } else {
                std::string text = (down ? "+" : "-") + name;
                for (size_t i = 1; i < w.size(); ++i)
                    text += " " + w[i];
                consoleCommand(text);
            }
            continue;
        }
        if (down)
            consoleCommand(command);
    }
}

bool PlayViewer::held(const char* button) const {
    const auto it = buttons_.find(button);
    return it != buttons_.end() && !it->second.empty();
}

bool PlayViewer::takesKey(const Event& ev) const {
    // Esc goes back to the menus, as in the game; with Ctrl it leaves.
    return ev.type == EventType::KeyDown && ev.key == key::Escape && !(ev.mods & keymod::Ctrl);
}

void PlayViewer::handleEvent(const Event& ev) {
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
            // Reluva's own: restart, another map, the info.
            const int n = int(names_.size());
            if (ev.key == 'r') {
                start(index_);
                return;
            }
            if (ev.key == key::PageUp || ev.key == key::PageDown) {
                start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
                return;
            }
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
    case EventType::MouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown;
            runBinding(ctx_.bindings.get(k), k, true);
            runBinding(ctx_.bindings.get(k), k, false);
        }
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
    // Turning and looking by keys, as the original: 140 degrees a second
    // (cl_yawspeed, cl_pitchspeed), half as fast again with +speed; with
    // +strafe, left and right step instead.
    const float turn = 140.0f * (held("speed") ? 1.5f : 1.0f) * float(dt) / 1000.0f;
    const bool strafe = held("strafe");
    if (!strafe)
        yaw_ += float(held("left") - held("right")) * turn;
    pitch_ = std::clamp(pitch_ + float(held("lookdown") - held("lookup")) * turn, -89.0f, 89.0f);
    if (dt > 0) {
        // The player's command for this moment, as a client sends one each
        // frame: where they look, how they move, what they press. The game
        // moves the player by it at once, as the original server does.
        clientTime_ += dt;
        eliteforce::GameHost::UserCommand cmd;
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
        cmd.weapon = uint8_t(cgame_->weaponSelect());
        // Running unless +speed is held (cl_run 1, the default), or the reverse.
        const int speed = (cvars_.find("cl_run") ? cvars_.integer("cl_run") != 0 : true) != held("speed") ? 127 : 64;
        int side = held("moveright") - held("moveleft");
        if (strafe)
            side += held("right") - held("left");
        cmd.forward = int8_t(std::clamp(speed * (held("forward") - held("back")), -127, 127));
        cmd.right = int8_t(std::clamp(speed * side, -127, 127));
        cmd.up = int8_t(std::clamp(speed * (held("moveup") - held("movedown")), -127, 127));
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
            models_->draw(p.model, p.frame, p.transform, p.light, time - p.shaderTime, p.skin, 0, kLastOpaque,
                          p.shader, &p.color, p.oldFrame, p.backLerp);
        world_->drawSurfaces(time, kLastOpaque + 0.5f);
        for (const Placed& p : placed_)
            models_->draw(p.model, p.frame, p.transform, p.light, time - p.shaderTime, p.skin, kLastOpaque + 0.5f,
                          1e9f, p.shader, &p.color, p.oldFrame, p.backLerp);
        drawEffects(time);
    } else {
        placed_.clear();
        effects_.clear();
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

std::array<float, 4> PlayViewer::entityColor(const std::vector<uint8_t>& e) {
    // refEntity_t's colour: bytes r, g, b, a.
    std::array<float, 4> c{};
    for (size_t i = 0; i < 4; ++i)
        c[i] = (refent::Rgba + i < e.size() ? e[refent::Rgba + i] : 255) / 255.0f;
    return c;
}

float PlayViewer::shaderTime(const std::vector<uint8_t>& e) const {
    // When the entity's shader animations start, in seconds of the level
    // (Quake III's shaderTime): an explosion's from its first frame. Only
    // a time that has come is taken.
    const float t = readFloat(e, refent::ShaderTime);
    return t > 0 && t <= float(clientTime_) / 1000.0f + 1 ? t : 0;
}

void PlayViewer::addEffect(const std::vector<uint8_t>& e, const gfx::Vec3& left, const gfx::Vec3& up) {
    const int32_t type = readInt(e, refent::Type);
    const int32_t shader = readInt(e, refent::CustomShader);
    if (shader <= 0)
        return;
    using V = gfx::Vec3;
    auto add = [](const V& a, const V& b, float s) { return V{a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s}; };
    auto sub = [](const V& a, const V& b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto cross = [](const V& a, const V& b) {
        return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto normalized = [](V v) {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len > 1e-6f)
            for (float& c : v)
                c /= len;
        return v;
    };
    const auto first = uint32_t(effectIndices_.size());
    const int32_t rgba = readInt(e, refent::Rgba);
    auto vertex = [&](const V& pos, float s, float t) {
        gfx::Vertex v{};
        for (size_t k = 0; k < 3; ++k)
            v.pos[k] = pos[k];
        v.uv[0] = s;
        v.uv[1] = t;
        v.normal[2] = 1;
        std::memcpy(v.color, &rgba, 4); // bytes r, g, b, a
        effectVertices_.push_back(v);
        return uint32_t(effectVertices_.size() - 1);
    };
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        for (uint32_t i : {a, b, c, a, c, d})
            effectIndices_.push_back(i);
    };
    // A ribbon through `points`, facing the eye, as wide as `widths`
    // says at each; the texture across it in s and along it in t, to
    // `repeat` at the end.
    auto ribbon = [&](const std::vector<V>& points, const std::vector<float>& widths, float repeat) {
        uint32_t prevA = 0, prevB = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            const V along = sub(points[std::min(i + 1, points.size() - 1)], points[i ? i - 1 : 0]);
            const V side = normalized(cross(along, sub(eye_, points[i])));
            const float t = repeat * float(i) / float(points.size() - 1);
            const uint32_t a = vertex(add(points[i], side, widths[i] / 2), 0, t);
            const uint32_t b = vertex(add(points[i], side, -widths[i] / 2), 1, t);
            if (i)
                quad(prevA, a, b, prevB);
            prevA = a;
            prevB = b;
        }
    };
    const V origin = readVec(e, refent::Origin);
    if (type == refent::TypeSprite || type == refent::TypeQuad) {
        const float r = readFloat(e, refent::Radius);
        V l = left, u = up;
        if (type == refent::TypeQuad) {
            l = readVec(e, refent::Axes + 12);
            u = readVec(e, refent::Axes + 24);
        } else if (const float deg = readFloat(e, refent::Rotation); deg != 0) {
            const float c = std::cos(deg * kPi / 180), sn = std::sin(deg * kPi / 180);
            for (size_t i = 0; i < 3; ++i) {
                l[i] = left[i] * c + up[i] * sn;
                u[i] = up[i] * c - left[i] * sn;
            }
        }
        quad(vertex(add(add(origin, l, r), u, r), 0, 0), vertex(add(add(origin, l, -r), u, r), 1, 0),
             vertex(add(add(origin, l, -r), u, -r), 1, 1), vertex(add(add(origin, l, r), u, -r), 0, 1));
    } else if (type == refent::TypeLine || type == refent::TypeTaperedLine) {
        const float w0 = readFloat(e, refent::Width);
        const float w1 = type == refent::TypeTaperedLine ? readFloat(e, refent::EndWidth) : w0;
        const float repeat = readFloat(e, refent::Repeat) > 0 ? readFloat(e, refent::Repeat) : 1.0f;
        ribbon({origin, readVec(e, refent::OldOrigin)}, {w0, w1}, repeat);
    } else if (type == refent::TypeCurve) {
        // A cubic Bezier curve from the origin through two control points
        // to the old origin.
        const V p0 = origin, p1 = readVec(e, refent::Control1), p2 = readVec(e, refent::Control2),
                p3 = readVec(e, refent::OldOrigin);
        std::vector<V> points;
        constexpr int kSteps = 16;
        for (int i = 0; i <= kSteps; ++i) {
            const float t = float(i) / kSteps, m = 1 - t;
            V p{};
            for (size_t k = 0; k < 3; ++k)
                p[k] = m * m * m * p0[k] + 3 * m * m * t * p1[k] + 3 * m * t * t * p2[k] + t * t * t * p3[k];
            points.push_back(p);
        }
        ribbon(points, std::vector<float>(points.size(), readFloat(e, refent::Width)), 1);
    } else if (type == refent::TypeBolt) {
        // Electricity: a line broken into steps, each point pushed aside at
        // random by the jitter times the step's length; new each frame.
        const V end = readVec(e, refent::OldOrigin);
        const V along = sub(end, origin);
        const float len = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        const V dir = normalized(along);
        V side1 = normalized(cross(dir, V{0, 0, 1}));
        if (std::fabs(dir[2]) > 0.99f)
            side1 = V{1, 0, 0};
        const V side2 = cross(dir, side1);
        const int steps = std::clamp(int(len / 16), 2, 32);
        const float jitter = readFloat(e, refent::Jitter) * len / float(steps);
        std::vector<V> points;
        for (int i = 0; i <= steps; ++i) {
            V p = add(origin, along, float(i) / float(steps));
            if (i > 0 && i < steps) {
                const float r1 = float(effectRandom_() % 2001) / 1000.0f - 1;
                const float r2 = float(effectRandom_() % 2001) / 1000.0f - 1;
                p = add(add(p, side1, r1 * jitter), side2, r2 * jitter);
            }
            points.push_back(p);
        }
        const float scale = readFloat(e, refent::Repeat);
        ribbon(points, std::vector<float>(points.size(), readFloat(e, refent::Width)),
               scale > 0 ? len * scale / 16 : 1);
    } else if (type == refent::TypeCylinder) {
        // A ring or cone around the first axis: the radius at 132 at the
        // origin, the one at 136 a height (140) along the axis; the texture
        // round it (repeated as 144 says) and up it.
        const V axis = normalized(readVec(e, refent::Axes));
        const V a1 = normalized(readVec(e, refent::Axes + 12)), a2 = normalized(cross(axis, a1));
        const float r0 = readFloat(e, refent::Width), r1 = readFloat(e, refent::EndWidth);
        const float height = readFloat(e, refent::Repeat);
        const float around = readFloat(e, refent::CylinderRepeat) > 0 ? readFloat(e, refent::CylinderRepeat) : 1.0f;
        const V top = add(origin, axis, height);
        constexpr int kSides = 24;
        uint32_t prevA = 0, prevB = 0;
        for (int i = 0; i <= kSides; ++i) {
            const float ang = 2 * kPi * float(i) / kSides;
            const V radial =
                add(V{a1[0] * std::cos(ang), a1[1] * std::cos(ang), a1[2] * std::cos(ang)}, a2, std::sin(ang));
            const float s = around * float(i) / kSides;
            const uint32_t a = vertex(add(origin, radial, r0), s, 1), b = vertex(add(top, radial, r1), s, 0);
            if (i)
                quad(prevA, a, b, prevB);
            prevA = a;
            prevB = b;
        }
    } else {
        ++otherEntities_;
        return;
    }
    effects_.push_back({shader, first, uint32_t(effectIndices_.size()) - first, entityColor(e), shaderTime(e)});
}

void PlayViewer::drawEffects(float time) {
    // This frame's effects, in the order the client game added them, over
    // everything else (they blend, as their shaders say).
    if (effects_.empty())
        return;
    gfx::MeshId mesh = 0;
    if (renderer_->createMesh(effectVertices_, effectIndices_, mesh) || !mesh)
        return;
    for (const Effect& fx : effects_) {
        if (size_t(fx.shader) >= effectMaterials_.size())
            effectMaterials_.resize(size_t(fx.shader) + 1, -2);
        int& material = effectMaterials_[size_t(fx.shader)];
        if (material == -2)
            material = shaders_->material(cgame_->shaders().name(fx.shader), Lighting::Effect);
        if (material < 0 || shaders_->get(material).skip)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh;
        call.firstIndex = fx.firstIndex;
        call.indexCount = fx.indexCount;
        shaders_->draw(material, call, 0, time - fx.shaderTime, false, &fx.color);
    }
    renderer_->destroyMesh(mesh);
}

void PlayViewer::placeEntities(const eliteforce::CgameHost::View& view) {
    // The view's entities: models where the client game put them, and
    // effects as quads facing the eye (refdef_t's axes: left at 48, up at 60).
    placed_.clear();
    effects_.clear();
    effectVertices_.clear();
    effectIndices_.clear();
    otherEntities_ = 0;
    unloadable_.clear();
    const gfx::Vec3 left = readVec(view.refdef, 48), up = readVec(view.refdef, 60);
    const auto& entities = cgame_->scene().entities;
    for (size_t i = view.firstEntity; i < view.firstEntity + view.entityCount && i < entities.size(); ++i) {
        const std::vector<uint8_t>& e = entities[i].bytes;
        if (readInt(e, refent::Type) != refent::TypeModel) {
            addEffect(e, left, up);
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
        // Between two frames, as far back towards the old one as it says.
        p.frame = readInt(e, refent::Frame);
        p.oldFrame = readInt(e, refent::OldFrame);
        p.backLerp = readFloat(e, refent::BackLerp);
        p.light = grid_.sample(flags & refent::UseLightingOrigin ? readVec(e, refent::LightingOrigin) : origin);
        if (const int32_t skin = readInt(e, refent::CustomSkin); skin > 0 && !cgame_->skins().name(skin).empty())
            p.skin = &models_->skin(cgame_->skins().name(skin));
        // A shader in place of the model's own (explosions, shells), its
        // colour fading as the client game says.
        if (const int32_t shader = readInt(e, refent::CustomShader); shader > 0)
            p.shader = cgame_->shaders().name(shader);
        p.color = entityColor(e);
        p.shaderTime = shaderTime(e);
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
            std::snprintf(line, sizeof line, "time %.1f s, eye at %.0f %.0f %.0f%s; %zu models and %zu effects drawn, %d other entities not yet",
                          float(clientTime_) / 1000.0f, eye_[0], eye_[1], eye_[2],
                          haveView ? "" : " (no view yet)", placed_.size(), effects_.size(), otherEntities_);
            lines.push_back(line);
            // What the player's commands carry, to see that keys arrive.
            std::string held;
            for (const auto& [name, keys] : buttons_)
                if (!keys.empty())
                    held += " +" + name;
            std::snprintf(line, sizeof line, "weapon chosen by the client game: %d; held:%s",
                          cgame_ ? cgame_->weaponSelect() : 0, held.empty() ? " nothing" : held.c_str());
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
        lines.push_back("Keys do what they're bound to (set in the menus' Controls). The mouse looks.");
        lines.push_back("Ctrl+PageUp/PageDown: another map, Ctrl+I: info, Ctrl+R: restart, Esc: the menus, Ctrl+Esc: launcher.");
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
