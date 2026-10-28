// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_scene.h"

#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr float kModelDistance = 900; // placed models further than this aren't drawn

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

// The direction a light's heading and pitch (radians) point to it from.
gfx::Vec3 towards(float heading, float pitch) {
    return {std::cos(pitch) * std::sin(heading), -std::sin(pitch), std::cos(pitch) * std::cos(heading)};
}

} // namespace

gfx::Mat4 instanceMatrix(const std::array<float, 12>& m) {
    return {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0, m[9], m[10], m[11], 1};
}

std::vector<CityInfo> findCities(const SearchSet& files) {
    std::vector<CityInfo> cities;
    for (const std::string& n : files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("tune/", 0) != 0 || key.size() < 6 || key.compare(key.size() - 6, 6, ".cinfo") != 0)
            continue;
        std::vector<uint8_t> data;
        if (files.read(n, data))
            continue;
        const std::string text = textOf(data);
        CityInfo c{iniValue(text, "MapName"), iniValue(text, "LocalizedName")};
        if (!c.map.empty() && files.has("city/" + c.map + ".psdl"))
            cities.push_back(c);
    }
    return cities;
}

CityScene::CityScene(gfx::Renderer3D& renderer, const SearchSet& files) : renderer_(renderer), files_(files) {
    textures_ = std::make_unique<TextureCache>(renderer_, files_);
    models_ = std::make_unique<ModelCache>(renderer_, *textures_, files_);
}

CityScene::~CityScene() {
    unload();
    models_.reset();
    textures_.reset();
}

void CityScene::unload() {
    if (cityMesh_)
        renderer_.destroyMesh(cityMesh_);
    destroyModelMesh(renderer_, skyMesh_);
    cityMesh_ = 0;
    batches_.clear();
    placed_.clear();
    instances_.clear();
    notes_.clear();
    psdl_ = Psdl{};
    mesh_ = CityMesh{};
    models_->clear();
    textures_->clear();
}

Error CityScene::load(const std::string& map) {
    unload();
    map_ = map;
    std::vector<uint8_t> data;
    if (Error err = files_.read("city/" + map + ".psdl", data))
        return err;
    if (Error err = readPsdl(data, psdl_))
        return Error::fail("city/" + map + ".psdl: " + err.message);

    // Start on the road nearest the city's middle, facing along it.
    start = {(psdl_.min[0] + psdl_.max[0]) / 2, psdl_.max[1] + 40, (psdl_.min[2] + psdl_.max[2]) / 2};
    startYaw = 0;
    float nearest = -1;
    const size_t last = psdl_.vertices.empty() ? 0 : psdl_.vertices.size() - 1;
    for (const PsdlBlock& block : psdl_.blocks)
        for (const PsdlAttribute& a : block.attributes) {
            if (a.type != psdlattr::RoadStrip || a.words.size() < 8 || psdl_.vertices.empty())
                continue;
            auto middle = [&](size_t section) {
                const Vec3 l = psdl_.vertices[std::min<size_t>(a.words[section * 4 + 1], last)];
                const Vec3 r = psdl_.vertices[std::min<size_t>(a.words[section * 4 + 2], last)];
                return Vec3{(l[0] + r[0]) / 2, (l[1] + r[1]) / 2, (l[2] + r[2]) / 2};
            };
            const Vec3 m = middle(0), next = middle(1);
            const float dx = m[0] - psdl_.center[0], dz = m[2] - psdl_.center[2];
            if (nearest < 0 || dx * dx + dz * dz < nearest) {
                nearest = dx * dx + dz * dz;
                start = m;
                startYaw = std::atan2(-(next[0] - m[0]), -(next[2] - m[2]));
            }
        }

    buildCityMesh(psdl_, mesh_);
    std::vector<uint32_t> indices;
    for (const CityMesh::Batch& b : mesh_.batches) {
        Batch out{uint32_t(indices.size()), uint32_t(b.indices.size()), 0, false};
        if (!b.texture.empty())
            out.texture = textures_->get(b.texture, out.translucent);
        indices.insert(indices.end(), b.indices.begin(), b.indices.end());
        batches_.push_back(out);
    }
    if (!mesh_.vertices.empty() && renderer_.createMesh(mesh_.vertices, indices, cityMesh_))
        cityMesh_ = 0;
    char note[160];
    std::snprintf(note, sizeof note, "%zu blocks, %zu triangles in %zu textures; %zu tunnels not drawn yet",
                  psdl_.blocks.size() - 1, indices.size() / 3, batches_.size(), mesh_.skipped);
    notes_.push_back(note);

    // The models placed in it.
    if (!files_.read("city/" + map + ".inst", data))
        if (Error err = readInstances(data, instances_))
            notes_.push_back("city/" + map + ".inst: " + err.message);
    size_t missing = 0;
    for (const Instance& inst : instances_) {
        const ModelCache::Model& m = models_->get(inst.model);
        if (!m.ok) {
            ++missing;
            continue;
        }
        const gfx::Mat4 matrix = instanceMatrix(inst.matrix);
        Placed p{&m, matrix, gfx::transformPoint(matrix, m.center), m.radius};
        // Scaled instances' bounds grow with their largest axis.
        float scale = 1;
        for (size_t a = 0; a < 3; ++a)
            scale = std::max(scale, std::sqrt(inst.matrix[3 * a] * inst.matrix[3 * a] +
                                              inst.matrix[3 * a + 1] * inst.matrix[3 * a + 1] +
                                              inst.matrix[3 * a + 2] * inst.matrix[3 * a + 2]));
        p.radius *= scale;
        placed_.push_back(p);
    }
    std::snprintf(note, sizeof note, "%zu placed models (%zu not loaded)", placed_.size(), missing);
    notes_.push_back(note);

    // The sky: city/<map>.sky names its dome model.
    sky_.clear();
    if (!files_.read("city/" + map + ".sky", data)) {
        const std::string text = textOf(data);
        sky_ = text.substr(0, text.find_first_of(" \t\r\n"));
    }
    setLighting(lighting_);
    return Error::success();
}

std::string CityScene::lightingName() const {
    return std::string(kWeathers[lighting_ % 4]) + " " + kTimes[lighting_ / 4];
}

void CityScene::setLighting(int index) {
    lighting_ = ((index % 16) + 16) % 16;
    char name[64];
    std::snprintf(name, sizeof name, "city/%s.lt%02d", map_.c_str(), lighting_);
    light_ = SceneLight{};
    std::vector<uint8_t> data;
    TuneNode root;
    if (!files_.read(name, data) && !parseTune(textOf(data), root) && !root.children.empty()) {
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
    destroyModelMesh(renderer_, skyMesh_);
    std::vector<uint8_t> pkgData;
    Pkg pkg;
    const std::string file = "geometry/" + sky_ + ".pkg";
    if (!sky_.empty() && !files_.read(file, pkgData) && !readPkg(pkgData, pkg)) {
        const auto levels = modelLevels(pkg);
        buildModelMesh(renderer_, *textures_, files_, file, pkg, levels.empty() ? "" : levels.front(),
                       pkg.paintJobs ? uint32_t(lighting_) % pkg.paintJobs : 0, false, skyMesh_);
    }
}

size_t CityScene::draw(const gfx::Mat4& viewProj, const gfx::Vec3& eye, bool models) {
    // The sky round the eye, behind everything.
    if (skyMesh_.mesh) {
        gfx::Mat4 at = gfx::identity();
        at[12] = eye[0];
        at[13] = eye[1];
        at[14] = eye[2];
        for (const ModelDraw& d : skyMesh_.draws) {
            gfx::DrawCall call;
            call.mesh = skyMesh_.mesh;
            call.firstIndex = d.firstIndex;
            call.indexCount = d.indexCount;
            call.model = at;
            call.texture = d.texture;
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
            renderer_.draw(call);
        }
    }

    // Models outside the view are left out.
    const ViewFrustum frustum(viewProj);
    auto visible = [&](const gfx::Vec3& c, float radius) { return frustum.visible(c, radius); };

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
            renderer_.draw(call);
        }
    if (!models)
        return 0;
    std::vector<std::pair<const Placed*, const ModelMesh*>> shown;
    for (const Placed& p : placed_) {
        const float dx = p.center[0] - eye[0], dy = p.center[1] - eye[1], dz = p.center[2] - eye[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance - p.radius > kModelDistance || !visible(p.center, p.radius))
            continue;
        // Finer levels nearer, by how large the model looks.
        const float size = distance / std::max(1.0f, p.radius);
        const size_t levels = p.model->levels.size();
        const size_t level = std::min<size_t>(levels - 1, size < 10 ? 0 : size < 25 ? 1 : size < 60 ? 2 : 3);
        shown.push_back({&p, &p.model->levels[level]});
    }
    for (const auto& [p, mesh] : shown)
        drawModelMesh(renderer_, *mesh, p->matrix, light_, false);
    for (const auto& [p, mesh] : shown)
        drawModelMesh(renderer_, *mesh, p->matrix, light_, true);
    return shown.size();
}

} // namespace reluva::age
