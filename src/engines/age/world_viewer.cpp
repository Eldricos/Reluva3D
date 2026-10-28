// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's World page: a city's streets and buildings generated
// from its PSDL, the models its .inst file places, its sky dome, lit as
// one of its .lt00-.lt15 files says, flown through freely.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "common/fs.h"
#include "engines/age/city_mesh.h"
#include "engines/age/instances.h"
#include "engines/age/psdl.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kFov = 1.0f;          // radians, vertically
constexpr float kFar = 4000;          // metres
constexpr float kModelDistance = 900; // placed models further than this aren't drawn
constexpr float kSpeed = 40;          // metres a second, four times with Shift

enum : uint32_t { kMoveForward = 1, kMoveBack = 2, kMoveLeft = 4, kMoveRight = 8, kMoveUp = 16, kMoveDown = 32,
                  kMoveFast = 64 };

const char* const kTimes[] = {"morning", "noon", "evening", "night"};
const char* const kWeathers[] = {"clear", "cloudy", "foggy", "rainy"};

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

// "Key=Value" lines (.cinfo).
std::string iniValue(const std::string& text, const std::string& key) {
    size_t at = 0;
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
            return line.substr(key.size() + 1);
    }
    return {};
}

gfx::Mat4 matrixOf(const std::array<float, 12>& m) {
    return {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0, m[9], m[10], m[11], 1};
}

// The direction a light's heading and pitch (radians) point to it from.
gfx::Vec3 towards(float heading, float pitch) {
    return {std::cos(pitch) * std::sin(heading), -std::sin(pitch), std::cos(pitch) * std::cos(heading)};
}

} // namespace

WorldViewer::WorldViewer(ViewerContext& ctx) : Page(ctx) {
    // The cities the menus offer (tune/*.cinfo), if their PSDL is there.
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("tune/", 0) != 0 || key.size() < 6 || key.compare(key.size() - 6, 6, ".cinfo") != 0)
            continue;
        std::vector<uint8_t> data;
        if (ctx.files.read(n, data))
            continue;
        const std::string text = textOf(data);
        City c{iniValue(text, "MapName"), iniValue(text, "LocalizedName")};
        if (!c.map.empty() && ctx.files.has("city/" + c.map + ".psdl"))
            cities_.push_back(c);
    }
}

WorldViewer::~WorldViewer() { deactivate(); }

void WorldViewer::deactivate() {
    unload();
    models_.reset();
    textures_.reset();
    renderer_ = nullptr;
}

void WorldViewer::unload() {
    if (renderer_ && cityMesh_)
        renderer_->destroyMesh(cityMesh_);
    if (renderer_)
        destroyModelMesh(*renderer_, skyMesh_);
    cityMesh_ = 0;
    batches_.clear();
    placed_.clear();
    if (models_)
        models_->clear();
    if (textures_)
        textures_->clear();
}

void WorldViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The World page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    textures_ = std::make_unique<TextureCache>(*renderer_, ctx_.files);
    models_ = std::make_unique<ModelCache>(*renderer_, *textures_, ctx_.files);
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    load(index_);
}

void WorldViewer::load(int index) {
    unload();
    index_ = index;
    error_.clear();
    notes_.clear();
    const City& city = cities_[size_t(index)];
    setTitle(city.name + " (" + city.map + ")");

    std::vector<uint8_t> data;
    Psdl psdl;
    if (Error err = ctx_.files.read("city/" + city.map + ".psdl", data)) {
        error_ = err.message;
        return;
    }
    if (Error err = readPsdl(data, psdl)) {
        error_ = "city/" + city.map + ".psdl: " + err.message;
        return;
    }
    // Start on the road nearest the city's middle, a car's height above
    // it, facing along it.
    start_ = {(psdl.min[0] + psdl.max[0]) / 2, psdl.max[1] + 40, (psdl.min[2] + psdl.max[2]) / 2};
    startYaw_ = 0;
    float nearest = -1;
    for (const PsdlBlock& block : psdl.blocks)
        for (const PsdlAttribute& a : block.attributes) {
            if (a.type != psdlattr::RoadStrip || a.words.size() < 8)
                continue;
            auto middle = [&](size_t section) {
                const Vec3 l = psdl.vertices[std::min<size_t>(a.words[section * 4 + 1], psdl.vertices.size() - 1)];
                const Vec3 r = psdl.vertices[std::min<size_t>(a.words[section * 4 + 2], psdl.vertices.size() - 1)];
                return Vec3{(l[0] + r[0]) / 2, (l[1] + r[1]) / 2, (l[2] + r[2]) / 2};
            };
            const Vec3 m = middle(0), next = middle(1);
            const float dx = m[0] - psdl.center[0], dz = m[2] - psdl.center[2];
            if (nearest < 0 || dx * dx + dz * dz < nearest) {
                nearest = dx * dx + dz * dz;
                start_ = {m[0], m[1] + 2.5f, m[2]};
                startYaw_ = std::atan2(-(next[0] - m[0]), -(next[2] - m[2]));
            }
        }
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    std::vector<uint32_t> indices;
    for (const CityMesh::Batch& b : mesh.batches) {
        Batch out{uint32_t(indices.size()), uint32_t(b.indices.size()), 0, false};
        if (!b.texture.empty())
            out.texture = textures_->get(b.texture, out.translucent);
        indices.insert(indices.end(), b.indices.begin(), b.indices.end());
        batches_.push_back(out);
    }
    if (!mesh.vertices.empty() && renderer_->createMesh(mesh.vertices, indices, cityMesh_))
        cityMesh_ = 0;
    char note[160];
    std::snprintf(note, sizeof note, "%zu blocks, %zu triangles in %zu textures; %zu tunnels not drawn yet",
                  psdl.blocks.size() - 1, indices.size() / 3, batches_.size(), mesh.skipped);
    notes_.push_back(note);

    // The models placed in it.
    std::vector<Instance> instances;
    if (!ctx_.files.read("city/" + city.map + ".inst", data)) {
        if (Error err = readInstances(data, instances))
            notes_.push_back("city/" + city.map + ".inst: " + err.message);
    }
    size_t missing = 0;
    for (const Instance& inst : instances) {
        const ModelCache::Model& m = models_->get(inst.model);
        if (!m.ok) {
            ++missing;
            continue;
        }
        Placed p{&m, matrixOf(inst.matrix), gfx::transformPoint(matrixOf(inst.matrix), m.center), m.radius};
        // Scaled instances' bounds grow with their largest axis.
        float scale = 1;
        for (int a = 0; a < 3; ++a)
            scale = std::max(scale, std::sqrt(inst.matrix[size_t(3 * a)] * inst.matrix[size_t(3 * a)] +
                                              inst.matrix[size_t(3 * a + 1)] * inst.matrix[size_t(3 * a + 1)] +
                                              inst.matrix[size_t(3 * a + 2)] * inst.matrix[size_t(3 * a + 2)]));
        p.radius *= scale;
        placed_.push_back(p);
    }
    std::snprintf(note, sizeof note, "%zu placed models (%zu not loaded)", placed_.size(), missing);
    notes_.push_back(note);

    // The sky: city/<map>.sky names its dome model.
    sky_.clear();
    if (!ctx_.files.read("city/" + city.map + ".sky", data)) {
        const std::string text = textOf(data);
        sky_ = text.substr(0, text.find_first_of(" \t\r\n"));
    }
    applyLighting();
    resetView();
}

void WorldViewer::applyLighting() {
    const City& city = cities_[size_t(index_)];
    char name[64];
    std::snprintf(name, sizeof name, "city/%s.lt%02d", city.map.c_str(), lighting_);
    lightingName_ = std::string(kWeathers[lighting_ % 4]) + " " + kTimes[lighting_ / 4];
    light_ = SceneLight{};
    std::vector<uint8_t> data;
    TuneNode root;
    if (!ctx_.files.read(name, data) && !parseTune(textOf(data), root) && !root.children.empty()) {
        const TuneNode& l = root.children.front();
        // Reluva's approximation of the original's lighting (how it lights
        // the city isn't known yet): the key light, three quarters as
        // strong; the two fills, which come from the side, as a quarter
        // each of extra ambient light.
        light_.direction = towards(l.number("KeyHeading"), l.number("KeyPitch"));
        const uint32_t ambient = uint32_t(int32_t(l.number("Ambient")));
        for (size_t c = 0; c < 3; ++c) {
            light_.directed[c] = 0.75f * l.number("KeyColor", c);
            light_.ambient[c] = float(ambient >> (16 - 8 * c) & 0xff) / 255 +
                                0.25f * (l.number("Fill1Color", c) + l.number("Fill2Color", c));
        }
    }
    // The sky dome: one paint job for each time and weather, in the same order.
    if (renderer_) {
        destroyModelMesh(*renderer_, skyMesh_);
        std::vector<uint8_t> pkgData;
        Pkg pkg;
        const std::string file = "geometry/" + sky_ + ".pkg";
        if (!sky_.empty() && !ctx_.files.read(file, pkgData) && !readPkg(pkgData, pkg)) {
            const auto levels = modelLevels(pkg);
            buildModelMesh(*renderer_, *textures_, ctx_.files, file, pkg, levels.empty() ? "" : levels.front(),
                           pkg.paintJobs ? uint32_t(lighting_) % pkg.paintJobs : 0, false, skyMesh_);
        }
    }
}

void WorldViewer::resetView() {
    eye_ = start_;
    yaw_ = startYaw_;
    pitch_ = -0.05f;
}

void WorldViewer::handleEvent(const Event& ev) {
    if (!renderer_ || cities_.empty())
        return;
    auto moveKey = [](uint32_t k) -> uint32_t {
        switch (k) {
        case 'w': return kMoveForward;
        case 's': return kMoveBack;
        case 'a': return kMoveLeft;
        case 'd': return kMoveRight;
        case ' ':
        case 'e': return kMoveUp;
        case 'c':
        case 'q': return kMoveDown;
        case key::LShift:
        case key::RShift: return kMoveFast;
        default: return 0;
        }
    };
    if (ev.type == EventType::KeyDown) {
        if (const uint32_t m = moveKey(ev.key)) {
            keys_ |= m;
            return;
        }
        const int n = int(cities_.size());
        if (ev.key == key::Left || ev.key == key::Right) {
            load((index_ + (ev.key == key::Right ? 1 : n - 1)) % n);
        } else if (ev.key == 't') {
            lighting_ = (lighting_ + 1) % 16;
            applyLighting();
        } else if (ev.key == 'm') {
            showModels_ = !showModels_;
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        } else if (ev.key == 'r') {
            resetView();
        }
    } else if (ev.type == EventType::KeyUp) {
        keys_ &= ~moveKey(ev.key);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.005f;
        pitch_ = std::clamp(pitch_ - float(ev.mouseY - lastY_) * 0.005f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    }
}

void WorldViewer::drawInfo(const std::string& stats) {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    const City& city = cities_[size_t(index_)];
    font::drawText(overlay_, 4, 4,
                   city.name + "  (Left/Right: city; " + lightingName_ + ", T: time and weather)",
                   {150, 200, 255});
    int y = 4 + lh;
    if (!error_.empty()) {
        font::drawText(overlay_, 4, y, error_, {255, 120, 120});
        return;
    }
    if (!showInfo_)
        return;
    std::vector<std::string> lines = notes_;
    lines.push_back(stats);
    char pos[120];
    std::snprintf(pos, sizeof pos, "at %.0f %.0f %.0f; W/A/S/D, Space/C, Shift, drag to look; M models, R start",
                  eye_[0], eye_[1], eye_[2]);
    lines.push_back(pos);
    for (const std::string& m : textures_->missing())
        if (lines.size() < 12)
            lines.push_back("missing texture: " + m);
    for (const std::string& line : lines) {
        font::drawText(overlay_, 4, y, line, {230, 230, 230});
        y += lh;
    }
}

void WorldViewer::update(uint64_t nowMs) {
    if (!renderer_ || cities_.empty())
        return;
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000) : 0;
    lastMs_ = nowMs;
    const gfx::Vec3 forward{-std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), -std::cos(pitch_) * std::cos(yaw_)};
    const gfx::Vec3 right{std::cos(yaw_), 0, -std::sin(yaw_)};
    const float step = kSpeed * (keys_ & kMoveFast ? 4 : 1) * dt;
    for (size_t k = 0; k < 3; ++k) {
        eye_[k] += forward[k] * step * (float(bool(keys_ & kMoveForward)) - float(bool(keys_ & kMoveBack)));
        eye_[k] += right[k] * step * (float(bool(keys_ & kMoveRight)) - float(bool(keys_ & kMoveLeft)));
    }
    eye_[1] += step * (float(bool(keys_ & kMoveUp)) - float(bool(keys_ & kMoveDown)));

    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(kFov, float(kFrameW) / float(kFrameH), 0.5f, kFar),
                                             gfx::lookAlong(eye_, forward, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye_, {150, 170, 200});

    // The sky round the eye, behind everything.
    if (skyMesh_.mesh) {
        gfx::Mat4 at = gfx::identity();
        at[12] = eye_[0];
        at[13] = eye_[1];
        at[14] = eye_[2];
        for (const ModelDraw& d : skyMesh_.draws) {
            gfx::DrawCall call;
            call.mesh = skyMesh_.mesh;
            call.firstIndex = d.firstIndex;
            call.indexCount = d.indexCount;
            call.model = at;
            call.texture = d.texture;
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
            renderer_->draw(call);
        }
    }

    // The planes of the view, to leave out models outside it.
    const float* m = viewProj.data();
    auto row = [&](int r) { return std::array<float, 4>{m[r], m[4 + r], m[8 + r], m[12 + r]}; };
    std::array<std::array<float, 4>, 5> planes;
    const auto r0 = row(0), r1 = row(1), r3 = row(3);
    for (size_t k = 0; k < 4; ++k) {
        planes[0][k] = r3[k] + r0[k];
        planes[1][k] = r3[k] - r0[k];
        planes[2][k] = r3[k] + r1[k];
        planes[3][k] = r3[k] - r1[k];
        planes[4][k] = row(2)[k];
    }
    for (auto& p : planes) {
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& c : p)
            c /= len;
    }
    auto visible = [&](const gfx::Vec3& c, float radius) {
        for (const auto& p : planes)
            if (p[0] * c[0] + p[1] * c[1] + p[2] * c[2] + p[3] < -radius)
                return false;
        return true;
    };

    // The city, opaque then translucent, then the models.
    for (int pass = 0; pass < 2 && cityMesh_; ++pass)
        for (const Batch& b : batches_) {
            if (b.translucent != (pass == 1))
                continue;
            gfx::DrawCall call;
            call.mesh = cityMesh_;
            call.firstIndex = b.firstIndex;
            call.indexCount = b.indexCount;
            call.texture = b.texture;
            call.rgbSource = gfx::ColorSource::Lighting;
            call.ambientLight = light_.ambient;
            call.directedLight = light_.directed;
            call.lightDirection = light_.direction;
            call.state.cull = gfx::Cull::None;
            call.state.depthBias = pass == 1;
            if (pass == 1) {
                call.state.src = gfx::Blend::SrcAlpha;
                call.state.dst = gfx::Blend::OneMinusSrcAlpha;
                call.state.depthWrite = false;
                call.state.alphaTest = gfx::AlphaTest::Gt0;
            }
            renderer_->draw(call);
        }
    size_t drawn = 0;
    std::vector<std::pair<const Placed*, const ModelMesh*>> shown;
    if (showModels_)
        for (const Placed& p : placed_) {
            const float dx = p.center[0] - eye_[0], dy = p.center[1] - eye_[1], dz = p.center[2] - eye_[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance - p.radius > kModelDistance || !visible(p.center, p.radius))
                continue;
            // Finer levels nearer, by how large the model looks.
            const float size = distance / std::max(1.0f, p.radius);
            const size_t levels = p.model->levels.size();
            const size_t level = std::min<size_t>(levels - 1, size < 10 ? 0 : size < 25 ? 1 : size < 60 ? 2 : 3);
            shown.push_back({&p, &p.model->levels[level]});
        }
    for (const auto& [p, mesh] : shown) {
        drawModelMesh(*renderer_, *mesh, p->matrix, light_, false);
        ++drawn;
    }
    for (const auto& [p, mesh] : shown)
        drawModelMesh(*renderer_, *mesh, p->matrix, light_, true);

    char stats[80];
    std::snprintf(stats, sizeof stats, "%zu of %zu placed models drawn", drawn, placed_.size());
    drawInfo(stats);
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
