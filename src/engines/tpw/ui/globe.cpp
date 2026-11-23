// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How the globe turns and where the camera goes are measured on a
// recording: docs/engines/tpw/games/simcoaster/systems/globe.md.

#include "engines/tpw/ui/globe.h"

#include "engines/tpw/ui/opening.h"

#include "common/events.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr float kPi = 3.14159265f;

// The recording's timings: the camera reaches the close view about 4
// seconds after the path ends; the globe turns 120 degrees in 4 seconds,
// faster, about 75 degrees a second, to bring a chosen world round; holds
// it 0.8 seconds; flies to its gate in 1.4; "- PLEASE WAIT -" shows 0.3
// seconds before the loading screen.
constexpr float kApproachMillis = 4000;
constexpr float kTurnPerSecond = 2 * kPi / 3 / 4;
constexpr float kFacePerSecond = 1.3f;
constexpr uint32_t kHoldMillis = 800;
constexpr float kFlightMillis = 1400;
constexpr uint32_t kWaitMillis = 300;
constexpr uint32_t kFrameMillis = 40;

// "- PLEASE WAIT -": the game's program draws it, not a string of its
// files; in MENUBIG, its letters 9 pixels further apart, its top at 224.
constexpr const char *kPleaseWait = "-PLEASE WAIT-";
constexpr const char *kPleaseWaitFont = "MENUBIG";
constexpr int kPleaseWaitSpacing = 9;
constexpr int kPleaseWaitY = 224;

// The close view: from above the hall's side of the globe, so many of
// its radii away, looking down at it.
constexpr float kClosePitch = 0.95f;
constexpr float kCloseDistance = 1.6f;
constexpr float kCloseFocal = 0.7f; // in screen heights: a wider view than the path's
// How far below the globe's middle it aims, in radii: the recording has
// the globe a little above the screen's middle.
constexpr float kCloseAimDown = 0.3f;
constexpr float kNear = 0.5f;

// The gate's view at the end of the flight down to it: from the camera's
// side and above, in the globe's radii.
constexpr float kGateBack = 0.55f;
constexpr float kGateUp = 0.45f;

std::array<float, 3> horizontal(const gfx::Vec3 &from, const gfx::Vec3 &to) {
    return {to[0] - from[0], 0, to[2] - from[2]};
}

float angleOf(const gfx::Vec3 &from, const gfx::Vec3 &to) {
    return std::atan2(to[2] - from[2], to[0] - from[0]);
}

// A camera at `eye` looking at `at`, both in the viewer's world (z up).
soft3d::OrbitCamera lookAt(soft3d::Vec eye, soft3d::Vec at, int w, int h) {
    soft3d::OrbitCamera camera({0, 0, 0}, 0, 0, 1, w, h);
    camera.eye = eye;
    camera.forward = soft3d::normalize(at - eye);
    camera.right = soft3d::normalize(soft3d::cross(camera.forward, soft3d::Vec{0, 0, 1}));
    camera.up = soft3d::cross(camera.right, camera.forward);
    camera.focal = float(h);
    camera.nearZ = kNear;
    return camera;
}

// Between two cameras, `t` 0 to 1 of the way.
soft3d::OrbitCamera blend(const soft3d::OrbitCamera &a, const soft3d::OrbitCamera &b, float t, int w, int h) {
    const soft3d::Vec eye = a.eye + (b.eye - a.eye) * t;
    const soft3d::Vec forward = soft3d::normalize(a.forward + (b.forward - a.forward) * t);
    return lookAt(eye, eye + forward, w, h);
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

const std::array<Globe::World, Globe::kWorlds> &Globe::worlds() {
    // The pieces' names are Theme Park World's for the worlds SimCoaster
    // turned into these: New Science, Ice World, Arabian.
    static const std::array<World, kWorlds> kWorldList{{
        {0, "levels/science/", "ns"},
        {1, "levels/water/", "iw"},
        {3, "levels/arabian/", "an"},
    }};
    return kWorldList;
}

Error Globe::load(const FrontEndScene &scene) {
    _scene = &scene;
    const Model &model = scene.model().model;
    const auto &nodes = model.nodes();
    _rest = scene.meshTransforms(scene.frames());

    auto underGlobe = [&](size_t node) {
        for (int n = int(node); n >= 0; n = nodes[size_t(n)].parent)
            if (nodes[size_t(n)].name == "baseobject")
                return true;
        return false;
    };
    // Each mesh's middle where the path leaves it.
    std::vector<gfx::Vec3> middles(model.meshes().size());
    gfx::Vec3 low{1e9f, 1e9f, 1e9f};
    gfx::Vec3 high{-1e9f, -1e9f, -1e9f};
    std::vector<bool> onGlobe(model.meshes().size(), false);
    std::array<bool, kWorlds> found{};
    for (size_t m = 0; m < model.meshes().size(); ++m) {
        const Model::Mesh &mesh = model.meshes()[m];
        gfx::Vec3 a{1e9f, 1e9f, 1e9f};
        gfx::Vec3 b{-1e9f, -1e9f, -1e9f};
        for (const gfx::Vec3 &p : mesh.positions) {
            const gfx::Vec3 at = gfx::transformPoint(_rest[m], p);
            for (size_t i = 0; i < 3; ++i) {
                a[i] = std::min(a[i], at[i]);
                b[i] = std::max(b[i], at[i]);
            }
        }
        for (size_t i = 0; i < 3; ++i)
            middles[m][i] = (a[i] + b[i]) / 2;
        if (mesh.positions.empty() || !underGlobe(mesh.node))
            continue;
        onGlobe[m] = true;
        for (size_t i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], a[i]);
            high[i] = std::max(high[i], b[i]);
        }
        for (size_t w = 0; w < kWorlds; ++w)
            if (mesh.name.rfind(worlds()[w].prefix, 0) == 0 && mesh.name.find("(gate)") != std::string::npos) {
                _gates[w] = middles[m];
                found[w] = true;
            }
    }
    for (size_t w = 0; w < kWorlds; ++w)
        if (!found[w])
            return Error::fail(std::string(FrontEndScene::kModel) + " has no gate for the world " + worlds()[w].prefix);
    for (size_t i = 0; i < 3; ++i)
        _centre[i] = (low[i] + high[i]) / 2;
    _radius = std::max(high[0] - low[0], high[2] - low[2]) / 2;

    // Each piece belongs to the world whose gate's direction from the axis
    // is nearest its own.
    _worldOf.assign(model.meshes().size(), -1);
    for (size_t m = 0; m < model.meshes().size(); ++m) {
        if (!onGlobe[m])
            continue;
        const float a = angleOf(_centre, middles[m]);
        float best = 1e9f;
        for (int w = 0; w < kWorlds; ++w) {
            const float d = std::fabs(std::remainder(a - angleOf(_centre, _gates[size_t(w)]), 2 * kPi));
            if (d < best) {
                best = d;
                _worldOf[m] = w;
            }
        }
    }
    return Error::success();
}

float Globe::facing(int world) const {
    // Turned by θ, a direction's angle about the axis goes down by θ; the
    // close view looks from the hall's side, −z.
    const float camera = std::atan2(-1.0f, 0.0f);
    return angleOf(_centre, _gates[size_t(world)]) - camera;
}

std::vector<gfx::Mat4> Globe::turned(float turn) const {
    // About the upright axis through the middle: x' = x cos + z sin,
    // z' = −x sin + z cos.
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    gfx::Mat4 r = gfx::identity();
    r[0] = c;
    r[2] = -s;
    r[8] = s;
    r[10] = c;
    r[12] = _centre[0] - (c * _centre[0] + s * _centre[2]);
    r[14] = _centre[2] - (-s * _centre[0] + c * _centre[2]);
    std::vector<gfx::Mat4> out = _rest;
    for (size_t m = 0; m < out.size(); ++m)
        if (_worldOf[m] >= 0)
            out[m] = gfx::multiply(r, _rest[m]);
    return out;
}

soft3d::OrbitCamera Globe::camera(float approach, int w, int h) const {
    const soft3d::Vec aim = toViewerWorld(_centre) - soft3d::Vec{0, 0, _radius * kCloseAimDown};
    soft3d::OrbitCamera close(aim, 0, kClosePitch, _radius * kCloseDistance, w, h);
    close.focal = float(h) * kCloseFocal;
    close.nearZ = kNear;
    if (approach >= 1)
        return close;
    return blend(_scene->camera(_scene->frames(), w, h), close, smooth(approach), w, h);
}

soft3d::OrbitCamera Globe::cameraToGate(int world, float turn, float t, int w, int h) const {
    // The gate where the turn takes it.
    const gfx::Vec3 &g = _gates[size_t(world)];
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    const gfx::Vec3 gate{_centre[0] + c * (g[0] - _centre[0]) + s * (g[2] - _centre[2]), g[1],
                         _centre[2] - s * (g[0] - _centre[0]) + c * (g[2] - _centre[2])};
    const soft3d::OrbitCamera from = camera(1, w, h);
    const std::array<float, 3> out = horizontal(_centre, gate);
    const float length = std::max(1e-3f, std::hypot(out[0], out[2]));
    const gfx::Vec3 eye{gate[0] + out[0] / length * _radius * kGateBack, gate[1] + _radius * kGateUp,
                        gate[2] + out[2] / length * _radius * kGateBack};
    const soft3d::OrbitCamera to = lookAt(toViewerWorld(eye), toViewerWorld(gate), w, h);
    return blend(from, to, smooth(t), w, h);
}

void Globe::draw(Surface &screen, soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &camera, float turn) const {
    _scene->draw(screen, rasterizer, camera, turned(turn));
}

int Globe::worldAt(int x, int y, const soft3d::OrbitCamera &camera, float turn, int w, int h) const {
    // Each mesh drawn in its world's number, 0 off the globe; the number
    // nearest at the point is the world.
    Surface ids;
    ids.create(w, h, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    rasterizer.begin(ids);
    const std::vector<gfx::Mat4> transforms = turned(turn);
    const Model &model = _scene->model().model;
    for (size_t m = 0; m < model.meshes().size(); ++m) {
        const Model::Mesh &mesh = model.meshes()[m];
        const uint8_t id = uint8_t(_worldOf[m] + 1);
        for (const Model::Face &face : mesh.faces) {
            std::vector<soft3d::ViewVertex> polygon;
            for (size_t k = 0; k < 3; ++k)
                polygon.push_back(camera.toView(toViewerWorld(
                    gfx::transformPoint(transforms[m], mesh.positions[mesh.positionOf[face.vertex[k]]]))));
            polygon = soft3d::clipNear(polygon, camera.nearZ);
            for (size_t k = 2; k < polygon.size(); ++k)
                rasterizer.drawTriangle(camera.project(polygon[0]), camera.project(polygon[k - 1]),
                                        camera.project(polygon[k]), [id](float, float, uint8_t *rgba) {
                                            rgba[0] = id;
                                            return true;
                                        });
        }
    }
    if (x < 0 || y < 0 || x >= w || y >= h)
        return -1;
    return int(ids.row(y)[x * 4]) - 1;
}

float GlobeTurning::turn(uint64_t now) const {
    return float(now - std::min(now, _start)) / 1000 * kTurnPerSecond;
}

soft3d::OrbitCamera GlobeTurning::camera(uint64_t now, int w, int h) const {
    return _globe->camera(float(now - std::min(now, _start)) / kApproachMillis, w, h);
}

void GlobeTurning::draw(Surface &screen, soft3d::Rasterizer &rasterizer, uint64_t now) const {
    _globe->draw(screen, rasterizer, camera(now, screen.w, screen.h), turn(now));
}

namespace {

// Text with extra room between its characters, centred on the screen.
void drawSpaced(Surface &screen, const Font &font, const CharacterSet &characters, const std::string &text, int y) {
    int width = -kPleaseWaitSpacing;
    for (char c : text)
        width += gameTextWidth(font, characters, std::string(1, c)) + kPleaseWaitSpacing;
    int x = (screen.w - width) / 2;
    for (char c : text)
        x = drawGameText(screen, font, characters, x, y, std::string(1, c), Color{255, 255, 255}) + kPleaseWaitSpacing;
}

} // namespace

int chooseWorld(System &sys, const SearchSet &files, const GlobeTurning &turning,
                const std::array<bool, Globe::kWorlds> &open) {
    const Globe &globe = turning.globe();
    Font font;
    CharacterSet characters;
    const std::string language = languageOf(files);
    std::vector<uint8_t> data;
    const bool wording =
        !language.empty() && !files.read("Language/" + language + "/MBToUni.dat", data) && !characters.parse(data) &&
        !files.read("Language/" + language + "/" + kPleaseWaitFont + ".bf4", data) && !font.parse(data);

    Surface screen;
    screen.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    int chosen = -1;
    float from = 0; // the turn when the world was chosen
    float to = 0;   // and the turn that faces it
    uint64_t chosenAt = 0;
    for (;;) {
        const uint64_t now = sys.getMillis();
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return -1;
            if (chosen < 0 && ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
                const int w = globe.worldAt(ev.mouseX, ev.mouseY, turning.camera(now, screen.w, screen.h),
                                            turning.turn(now), screen.w, screen.h);
                if (w >= 0 && open[size_t(w)]) {
                    chosen = w;
                    chosenAt = now;
                    from = turning.turn(now);
                    // The shorter way round to the world facing the camera
                    // (the recording shows only a short turn, the way it was
                    // going).
                    to = from + std::remainder(globe.facing(w) - from, 2 * kPi);
                }
            }
        }
        if (chosen < 0) {
            turning.draw(screen, rasterizer, now);
        } else {
            const float since = float(now - chosenAt);
            const float turnMillis = std::fabs(to - from) / kFacePerSecond * 1000;
            const float turn = turnMillis > 0 ? from + (to - from) * std::min(1.0f, since / turnMillis) : to;
            const float flight = (since - turnMillis - float(kHoldMillis)) / kFlightMillis;
            if (flight < 0) {
                globe.draw(screen, rasterizer, globe.camera(1, screen.w, screen.h), turn);
            } else {
                globe.draw(screen, rasterizer, globe.cameraToGate(chosen, to, flight, screen.w, screen.h), to);
                if (flight >= 1 && wording)
                    drawSpaced(screen, font, characters, kPleaseWait, kPleaseWaitY);
                if (flight >= 1 + float(kWaitMillis) / kFlightMillis) {
                    sys.updateScreen(screen);
                    return chosen;
                }
            }
        }
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
