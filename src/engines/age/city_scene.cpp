// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/city_scene.h"

#include "common/fs.h"

#include "engines/age/pathset.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr float kModelDistance = 900; // placed models further than this aren't drawn
constexpr float kLowDetail = 150;     // roads in blocks further than this drawn in their low-detail texture
constexpr float kDetailStep = 60;     // metres the eye moves before that's worked out again

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
    beacons_.clear();
    pvs_ = Pvs{};
    blockBounds_.clear();
    viewBlock_ = blocksDrawn_ = 0;
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
    loadedAt_ = std::chrono::steady_clock::now();
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
    // The water under the bridges (city/<map>.water), in the first animated
    // water texture the city names (San Francisco's s_ocean-0007, London's
    // s_thames-0009).
    if (!files_.read("city/" + map + ".water", data)) {
        CityWater water;
        if (Error err = parseCityWater(textOf(data), water)) {
            notes_.push_back("city/" + map + ".water: " + err.message);
        } else {
            for (size_t t = 0; t < psdl_.textures.size(); ++t) {
                std::string base = psdl_.textures[t];
                if (const size_t dash = base.rfind('-'); dash != std::string::npos)
                    base.resize(dash);
                if (files_.has("tune/" + base + ".movie")) {
                    addCityWater(psdl_, water, uint32_t(t + 1), mesh_);
                    break;
                }
            }
        }
    }
    // The road markings (city/<map>/decals.pathset).
    if (!files_.read("city/" + map + "/decals.pathset", data)) {
        std::vector<PathsetPath> decals;
        readPathset(data, decals);
        addCityDecals(decals, mesh_);
    }
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
    std::snprintf(note, sizeof note, "%zu blocks, %zu triangles in %zu textures", psdl_.blocks.size() - 1,
                  indices.size() / 3, batches_.size());
    notes_.push_back(note);

    // Each block's outline, for finding the eye's; and what can be seen
    // from each.
    blockBounds_.resize(psdl_.blocks.size());
    for (size_t i = 1; i < psdl_.blocks.size(); ++i) {
        BlockBounds& bb = blockBounds_[i];
        bool any = false;
        for (const auto& [v, beyond] : psdl_.blocks[i].perimeter) {
            if (v >= psdl_.vertices.size())
                continue;
            const Vec3& p = psdl_.vertices[v];
            bb.outline.push_back({p[0], p[2]});
            bb.minX = any ? std::min(bb.minX, p[0]) : p[0];
            bb.maxX = any ? std::max(bb.maxX, p[0]) : p[0];
            bb.minZ = any ? std::min(bb.minZ, p[2]) : p[2];
            bb.maxZ = any ? std::max(bb.maxZ, p[2]) : p[2];
            bb.minY = any ? std::min(bb.minY, p[1]) : p[1];
            bb.maxY = any ? std::max(bb.maxY, p[1]) : p[1];
            any = true;
        }
    }
    if (!files_.read("city/" + map + ".cpvs", data)) {
        if (Error err = readCpvs(data, pvs_))
            notes_.push_back("city/" + map + ".cpvs: " + err.message);
        else if (pvs_.blocks != psdl_.blocks.size())
            pvs_ = Pvs{}, notes_.push_back("city/" + map + ".cpvs is for another city's blocks");
        else
            notes_.push_back("visibility from " + std::to_string(pvs_.rows.size()) + " blocks");
    }
    blocksDrawn_ = psdl_.blocks.size() - 1;

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
        Placed p{&m, matrix, gfx::transformPoint(matrix, m.center), m.radius, inst.block};
        // Scaled instances' bounds grow with their largest axis.
        float scale = 1;
        for (size_t a = 0; a < 3; ++a)
            scale = std::max(scale, std::sqrt(inst.matrix[3 * a] * inst.matrix[3 * a] +
                                              inst.matrix[3 * a + 1] * inst.matrix[3 * a + 1] +
                                              inst.matrix[3 * a + 2] * inst.matrix[3 * a + 2]));
        p.radius *= scale;
        placed_.push_back(p);
        // The models placed inside it (xrefs: a landmark's warning lights,
        // awnings, doors), in its own coordinates.
        for (const PkgXref& x : m.xrefs) {
            const gfx::Mat4 xMatrix = gfx::multiply(matrix, instanceMatrix(x.matrix));
            const std::string lower = fs::toLower(x.name);
            if (lower == "sp_light_red_f" || lower == "sp_light_white_f") {
                beacons_.push_back({gfx::transformPoint(xMatrix, {0, 0, 0}), lower == "sp_light_red_f"
                                                                                 ? std::array<float, 3>{1, 0.15f, 0.1f}
                                                                                 : std::array<float, 3>{1, 1, 0.9f}});
                continue;
            }
            const ModelCache::Model& xm = models_->get(x.name);
            if (!xm.ok)
                continue;
            placed_.push_back({&xm, xMatrix, gfx::transformPoint(xMatrix, xm.center), xm.radius * scale, inst.block});
        }
    }
    std::snprintf(note, sizeof note, "%zu placed models (%zu not loaded)", placed_.size(), missing);
    notes_.push_back(note);

    // The sky: city/<map>.sky names its dome model.
    sky_.clear();
    if (!files_.read("city/" + map + ".sky", data)) {
        const std::string text = textOf(data);
        sky_ = text.substr(0, text.find_first_of(" \t\r\n"));
    }
    // Fog for each lighting: colour (0-255), start, end, a description.
    fogs_.clear();
    if (!files_.read("city/" + map + "_fog.csv", data)) {
        const std::string text = textOf(data);
        size_t at = text.find('\n'); // the header
        while (at != std::string::npos && at + 1 < text.size()) {
            const size_t eol = text.find('\n', at + 1);
            const std::string line = text.substr(at + 1, eol == std::string::npos ? std::string::npos : eol - at - 1);
            at = eol;
            float v[5];
            if (std::sscanf(line.c_str(), "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5)
                fogs_.push_back({{v[0] / 255, v[1] / 255, v[2] / 255}, v[3], v[4]});
        }
    }
    setLighting(lighting_);
    return Error::success();
}

void CityScene::relightCity() {
    if (!cityMesh_)
        return;
    // Each vertex: the ambient colour and each of the three lights by how
    // squarely its face is turned to it, at most 1 (as Direct3D lights
    // vertices).
    for (gfx::Vertex& v : mesh_.vertices) {
        for (size_t c = 0; c < 3; ++c) {
            float sum = cityLights_.ambient[c];
            for (size_t k = 0; k < 3; ++k) {
                const gfx::Vec3& d = cityLights_.direction[k];
                const float facing = v.normal[0] * d[0] + v.normal[1] * d[1] + v.normal[2] * d[2];
                sum += cityLights_.color[k][c] * std::max(0.0f, facing);
            }
            v.color[c] = uint8_t(std::lround(std::clamp(sum, 0.0f, 1.0f) * 255));
        }
        v.color[3] = 255;
    }
    renderer_.destroyMesh(cityMesh_);
    cityMesh_ = 0;
    std::vector<uint32_t> indices;
    for (const CityMesh::Batch& b : mesh_.batches)
        indices.insert(indices.end(), b.indices.begin(), b.indices.end());
    if (renderer_.createMesh(mesh_.vertices, indices, cityMesh_))
        cityMesh_ = 0;
    rebuildIndices();
}

std::string CityScene::lightingName() const {
    return std::string(kWeathers[lighting_ % 4]) + " " + kTimes[lighting_ / 4];
}

void CityScene::setLighting(int index) {
    lighting_ = ((index % 16) + 16) % 16;
    char name[64];
    std::snprintf(name, sizeof name, "city/%s.lt%02d", map_.c_str(), lighting_);
    light_ = SceneLight{};
    cityLights_ = CityLights{};
    std::vector<uint8_t> data;
    TuneNode root;
    if (!files_.read(name, data) && !parseTune(textOf(data), root) && !root.children.empty()) {
        const TuneNode& l = root.children.front();
        // Models, cars and people are lit as the city's surfaces are
        // (relightCity): the ambient colour and the three lights, each by
        // how squarely a surface faces it, as Direct3D lights vertices.
        light_.direction = towards(l.number("KeyHeading"), l.number("KeyPitch"));
        light_.fillDirection = {towards(l.number("Fill1Heading"), l.number("Fill1Pitch")),
                                towards(l.number("Fill2Heading"), l.number("Fill2Pitch"))};
        const uint32_t ambient = uint32_t(int32_t(l.number("Ambient")));
        for (size_t c = 0; c < 3; ++c) {
            light_.directed[c] = l.number("KeyColor", c);
            light_.fill[0][c] = l.number("Fill1Color", c);
            light_.fill[1][c] = l.number("Fill2Color", c);
            light_.ambient[c] = float(ambient >> (16 - 8 * c) & 0xff) / 255;
        }
        // The city's own surfaces get the three lights as they are (see
        // relightCity).
        const char* const names[] = {"Key", "Fill1", "Fill2"};
        for (size_t k = 0; k < 3; ++k) {
            const std::string n = names[k];
            cityLights_.direction[k] = towards(l.number(n + "Heading"), l.number(n + "Pitch"));
            for (size_t c = 0; c < 3; ++c)
                cityLights_.color[k][c] = l.number(n + "Color", c);
        }
        for (size_t c = 0; c < 3; ++c)
            cityLights_.ambient[c] = float(ambient >> (16 - 8 * c) & 0xff) / 255;
    }
    // The fog (city/<map>_fog.csv), a line for each time and weather.
    if (size_t(lighting_) < fogs_.size()) {
        light_.fogColor = fogs_[size_t(lighting_)].color;
        light_.fogStart = fogs_[size_t(lighting_)].start;
        light_.fogEnd = fogs_[size_t(lighting_)].end;
    }
    relightCity();
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
    textures_->setTime(
        std::chrono::duration<float>(std::chrono::steady_clock::now() - loadedAt_).count());
    // What can be seen from the eye's block, again when it's in another.
    const size_t from = visibility() ? blockAt({eye[0], eye[1], eye[2]}) : 0;
    const float moved = std::hypot(eye[0] - detailFrom_[0], eye[1] - detailFrom_[1], eye[2] - detailFrom_[2]);
    if (from != viewBlock_ || moved > kDetailStep) {
        viewBlock_ = from;
        detailFrom_ = {eye[0], eye[1], eye[2]};
        rebuildIndices();
    }

    // The sky round the eye, behind everything.
    if (skyMesh_.mesh && skyShown_) {
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
            call.texture = textures_->frame(b.texture);
            call.rgbSource = gfx::ColorSource::Vertex; // lit by relightCity
            call.state.cull = gfx::Cull::None;
            call.state.depthBias = pass == 1;
            if (pass == 1) {
                call.state.src = gfx::Blend::SrcAlpha;
                call.state.dst = gfx::Blend::OneMinusSrcAlpha;
                call.state.depthWrite = false;
                call.state.alphaTest = gfx::AlphaTest::Gt0;
            }
            applyFog(call, light_);
            renderer_.draw(call);
        }
    if (!models)
        return 0;
    std::vector<std::pair<const Placed*, const ModelMesh*>> shown;
    for (const Placed& p : placed_) {
        if (viewBlock_ && !pvs_.sees(viewBlock_, p.block))
            continue;
        const float dx = p.center[0] - eye[0], dy = p.center[1] - eye[1], dz = p.center[2] - eye[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance - p.radius > std::min(kModelDistance, modelDistance_) || !visible(p.center, p.radius))
            continue;
        // Finer levels nearer, by how large the model looks.
        const float size = distance / std::max(1.0f, p.radius) / std::max(0.1f, detail_);
        const size_t levels = p.model->levels.size();
        const size_t level = std::min<size_t>(levels - 1, size < 10 ? 0 : size < 25 ? 1 : size < 60 ? 2 : 3);
        shown.push_back({&p, &p.model->levels[level]});
    }
    for (const auto& [p, mesh] : shown)
        drawModelMesh(renderer_, *mesh, p->matrix, light_, false, textures_.get());
    for (const auto& [p, mesh] : shown)
        drawModelMesh(renderer_, *mesh, p->matrix, light_, true, textures_.get());
    return shown.size();
}

void CityScene::drawDynamicLights(std::vector<gfx::DynamicLight> lights, const gfx::Vec3& eye) {
    if (lights.empty() || !cityMesh_)
        return;
    auto distance = [&](const gfx::DynamicLight& l) {
        const float dx = l.origin[0] - eye[0], dy = l.origin[1] - eye[1], dz = l.origin[2] - eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(lights.begin(), lights.end(),
              [&](const gfx::DynamicLight& a, const gfx::DynamicLight& b) { return distance(a) < distance(b); });
    if (lights.size() > size_t(gfx::kMaxDynamicLights))
        lights.resize(size_t(gfx::kMaxDynamicLights));
    renderer_.setDynamicLights(lights);
    for (const Batch& b : batches_) {
        if (b.translucent)
            continue;
        gfx::DrawCall call;
        call.mesh = cityMesh_;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.dynamicLights = true;
        // Over what's drawn: what's there times (1 + the light).
        call.state.src = gfx::Blend::DstColor;
        call.state.dst = gfx::Blend::One;
        call.state.depthEqual = true;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::None;
        renderer_.draw(call);
    }
}

void CityScene::hide(const std::function<bool(const Vec3& a, const Vec3& b, const Vec3& c)>& drop) {
    if (!cityMesh_)
        return;
    for (CityMesh::Batch& b : mesh_.batches) {
        std::vector<uint32_t> kept;
        std::vector<uint16_t> keptBlocks;
        for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
            const auto& p0 = mesh_.vertices[b.indices[i]].pos;
            const auto& p1 = mesh_.vertices[b.indices[i + 1]].pos;
            const auto& p2 = mesh_.vertices[b.indices[i + 2]].pos;
            if (!drop({p0[0], p0[1], p0[2]}, {p1[0], p1[1], p1[2]}, {p2[0], p2[1], p2[2]})) {
                kept.insert(kept.end(), {b.indices[i], b.indices[i + 1], b.indices[i + 2]});
                keptBlocks.push_back(i / 3 < b.blocks.size() ? b.blocks[i / 3] : 0);
            }
        }
        b.indices = std::move(kept);
        b.blocks = std::move(keptBlocks);
    }
    rebuildIndices();
}

void CityScene::rebuildIndices() {
    if (!cityMesh_)
        return;
    std::vector<uint32_t> indices;
    batches_.clear();
    std::vector<bool> drawn(psdl_.blocks.size(), false);
    // Blocks far enough off for their roads' low-detail textures.
    std::vector<bool> far(psdl_.blocks.size(), false);
    for (size_t i = 1; i < blockBounds_.size() && i < far.size(); ++i) {
        const BlockBounds& bb = blockBounds_[i];
        far[i] = std::hypot((bb.minX + bb.maxX) / 2 - detailFrom_[0], (bb.minZ + bb.maxZ) / 2 - detailFrom_[2]) >
                 kLowDetail;
    }
    for (const CityMesh::Batch& b : mesh_.batches) {
        Batch out{uint32_t(indices.size()), 0, 0, false};
        if (!b.texture.empty())
            out.texture = textures_->get(b.texture, out.translucent);
        for (size_t t = 0; t * 3 + 2 < b.indices.size(); ++t) {
            const uint16_t block = t < b.blocks.size() ? b.blocks[t] : 0;
            if (viewBlock_ && block != 0 && !pvs_.sees(viewBlock_, block)) // 0: no block (decals), always
                continue;
            const bool isFar = block < far.size() && far[block];
            if ((b.detail == 1 && isFar) || (b.detail == 2 && !isFar))
                continue;
            if (block < drawn.size())
                drawn[block] = true;
            indices.insert(indices.end(), b.indices.begin() + std::ptrdiff_t(t * 3),
                           b.indices.begin() + std::ptrdiff_t(t * 3 + 3));
        }
        out.indexCount = uint32_t(indices.size()) - out.firstIndex;
        batches_.push_back(out);
    }
    blocksDrawn_ = size_t(std::count(drawn.begin(), drawn.end(), true));
    renderer_.setMeshIndices(cityMesh_, indices);
}

void CityScene::drawBeacons(const FlareDrawer& flares, const Vec3& eye) const {
    constexpr float kSize = 3, kDistance = 1500;
    if (!lampsLit())
        return;
    for (const Beacon& b : beacons_) {
        const Vec3 d{b.at[0] - eye[0], b.at[1] - eye[1], b.at[2] - eye[2]};
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < kDistance * kDistance)
            flares.draw(b.at, kSize, b.color, eye);
    }
}

size_t CityScene::blockAt(const Vec3& p) const {
    size_t best = 0;
    float bestFloor = 0;
    for (size_t i = 1; i < blockBounds_.size(); ++i) {
        const BlockBounds& bb = blockBounds_[i];
        if (bb.outline.size() < 3 || p[0] < bb.minX || p[0] > bb.maxX || p[2] < bb.minZ || p[2] > bb.maxZ ||
            p[1] < bb.minY - 1 || p[1] > bb.maxY + 30)
            continue;
        bool inside = false;
        for (size_t a = 0, b = bb.outline.size() - 1; a < bb.outline.size(); b = a++) {
            const auto [ax, az] = bb.outline[a];
            const auto [bx, bz] = bb.outline[b];
            if ((az > p[2]) != (bz > p[2]) && p[0] < (bx - ax) * (p[2] - az) / (bz - az) + ax)
                inside = !inside;
        }
        // Over more than one (a road over a tunnel): the highest under it.
        if (inside && (best == 0 || bb.minY > bestFloor)) {
            best = i;
            bestFloor = bb.minY;
        }
    }
    return best;
}

} // namespace reluva::age
