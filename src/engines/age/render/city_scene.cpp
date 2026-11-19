// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/render/city_scene.h"

#include "common/fs.h"

#include "engines/age/formats/pathset.h"
#include "engines/age/formats/tune.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr float kModelDistance = 900; // placed models further than this aren't drawn
constexpr float kLowDetail = 150;     // roads in blocks further than this drawn in their low-detail texture
constexpr float kDetailStep = 60;     // metres the eye moves before that's worked out again

const char *const kTimes[] = {"morning", "noon", "evening", "night"};
const char *const kWeathers[] = {"clear", "cloudy", "foggy", "rainy"};

std::string textOf(const std::vector<uint8_t> &data) {
    return std::string(data.begin(), data.end());
}

// "Key=Value" lines (.cinfo).
std::string iniValue(const std::string &text, const std::string &key) {
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

gfx::Mat4 instanceMatrix(const std::array<float, 12> &m) {
    return {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0, m[9], m[10], m[11], 1};
}

std::vector<CityInfo> findCities(const SearchSet &files) {
    std::vector<CityInfo> cities;
    for (const std::string &n : files.list()) {
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

CityScene::CityScene(gfx::Renderer3D &renderer, const SearchSet &files) : _renderer(renderer), _files(files) {
    _textures = std::make_unique<TextureCache>(_renderer, _files);
    _models = std::make_unique<ModelCache>(_renderer, *_textures, _files);
}

CityScene::~CityScene() {
    unload();
    _models.reset();
    _textures.reset();
}

void CityScene::unload() {
    if (_cityMesh)
        _renderer.destroyMesh(_cityMesh);
    destroyModelMesh(_renderer, _skyMesh);
    _cityMesh = 0;
    _batches.clear();
    _placed.clear();
    _beacons.clear();
    _pvs = Pvs{};
    _blockBounds.clear();
    _viewBlock = _blocksDrawn = 0;
    _instances.clear();
    _notes.clear();
    _psdl = Psdl{};
    _mesh = CityMesh{};
    _models->clear();
    _textures->clear();
}

Error CityScene::load(const std::string &map) {
    unload();
    _map = map;
    _loadedAt = std::chrono::steady_clock::now();
    std::vector<uint8_t> data;
    if (Error err = _files.read("city/" + map + ".psdl", data))
        return err;
    if (Error err = readPsdl(data, _psdl))
        return Error::fail("city/" + map + ".psdl: " + err.message);

    // Start on the road nearest the city's middle, facing along it.
    start = {(_psdl.min[0] + _psdl.max[0]) / 2, _psdl.max[1] + 40, (_psdl.min[2] + _psdl.max[2]) / 2};
    startYaw = 0;
    float nearest = -1;
    const size_t last = _psdl.vertices.empty() ? 0 : _psdl.vertices.size() - 1;
    for (const PsdlBlock &block : _psdl.blocks)
        for (const PsdlAttribute &a : block.attributes) {
            if (a.type != psdlattr::kRoadStrip || a.words.size() < 8 || _psdl.vertices.empty())
                continue;
            auto middle = [&](size_t section) {
                const Vec3 l = _psdl.vertices[std::min<size_t>(a.words[section * 4 + 1], last)];
                const Vec3 r = _psdl.vertices[std::min<size_t>(a.words[section * 4 + 2], last)];
                return Vec3{(l[0] + r[0]) / 2, (l[1] + r[1]) / 2, (l[2] + r[2]) / 2};
            };
            const Vec3 m = middle(0), next = middle(1);
            const float dx = m[0] - _psdl.center[0], dz = m[2] - _psdl.center[2];
            if (nearest < 0 || dx * dx + dz * dz < nearest) {
                nearest = dx * dx + dz * dz;
                start = m;
                startYaw = std::atan2(-(next[0] - m[0]), -(next[2] - m[2]));
            }
        }

    buildCityMesh(_psdl, _mesh);
    // The water under the bridges (city/<map>.water), in the first animated
    // water texture the city names (San Francisco's s_ocean-0007, London's
    // s_thames-0009).
    if (!_files.read("city/" + map + ".water", data)) {
        CityWater water;
        if (Error err = parseCityWater(textOf(data), water)) {
            _notes.push_back("city/" + map + ".water: " + err.message);
        } else {
            for (size_t t = 0; t < _psdl.textures.size(); ++t) {
                std::string base = _psdl.textures[t];
                if (const size_t dash = base.rfind('-'); dash != std::string::npos)
                    base.resize(dash);
                if (_files.has("tune/" + base + ".movie")) {
                    addCityWater(_psdl, water, uint32_t(t + 1), _mesh);
                    break;
                }
            }
        }
    }
    // The road markings (city/<map>/decals.pathset).
    if (!_files.read("city/" + map + "/decals.pathset", data)) {
        std::vector<PathsetPath> decals;
        readPathset(data, decals);
        addCityDecals(decals, _mesh);
    }
    std::vector<uint32_t> indices;
    for (const CityMesh::Batch &b : _mesh.batches) {
        Batch out{uint32_t(indices.size()), uint32_t(b.indices.size()), 0, false};
        if (!b.texture.empty())
            out.texture = _textures->get(b.texture, out.translucent);
        indices.insert(indices.end(), b.indices.begin(), b.indices.end());
        _batches.push_back(out);
    }
    if (!_mesh.vertices.empty() && _renderer.createMesh(_mesh.vertices, indices, _cityMesh))
        _cityMesh = 0;
    char note[160];
    std::snprintf(note, sizeof note, "%zu blocks, %zu triangles in %zu textures", _psdl.blocks.size() - 1,
                  indices.size() / 3, _batches.size());
    _notes.push_back(note);

    // Each block's outline, for finding the eye's; and what can be seen
    // from each.
    _blockBounds.resize(_psdl.blocks.size());
    for (size_t i = 1; i < _psdl.blocks.size(); ++i) {
        BlockBounds &bb = _blockBounds[i];
        bool any = false;
        for (const auto &[v, beyond] : _psdl.blocks[i].perimeter) {
            if (v >= _psdl.vertices.size())
                continue;
            const Vec3 &p = _psdl.vertices[v];
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
    if (!_files.read("city/" + map + ".cpvs", data)) {
        if (Error err = readCpvs(data, _pvs))
            _notes.push_back("city/" + map + ".cpvs: " + err.message);
        else if (_pvs.blocks != _psdl.blocks.size())
            _pvs = Pvs{}, _notes.push_back("city/" + map + ".cpvs is for another city's blocks");
        else
            _notes.push_back("visibility from " + std::to_string(_pvs.rows.size()) + " blocks");
    }
    _blocksDrawn = _psdl.blocks.size() - 1;

    // The models placed in it.
    if (!_files.read("city/" + map + ".inst", data))
        if (Error err = readInstances(data, _instances))
            _notes.push_back("city/" + map + ".inst: " + err.message);
    size_t missing = 0;
    for (const Instance &inst : _instances) {
        const ModelCache::Model &m = _models->get(inst.model);
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
        _placed.push_back(p);
        // The models placed inside it (xrefs: a landmark's warning lights,
        // awnings, doors), in its own coordinates.
        for (const PkgXref &x : m.xrefs) {
            const gfx::Mat4 xMatrix = gfx::multiply(matrix, instanceMatrix(x.matrix));
            const std::string lower = fs::toLower(x.name);
            if (lower == "sp_light_red_f" || lower == "sp_light_white_f") {
                _beacons.push_back({gfx::transformPoint(xMatrix, {0, 0, 0}), lower == "sp_light_red_f"
                                                                                 ? std::array<float, 3>{1, 0.15f, 0.1f}
                                                                                 : std::array<float, 3>{1, 1, 0.9f}});
                continue;
            }
            const ModelCache::Model &xm = _models->get(x.name);
            if (!xm.ok)
                continue;
            _placed.push_back({&xm, xMatrix, gfx::transformPoint(xMatrix, xm.center), xm.radius * scale, inst.block});
        }
    }
    std::snprintf(note, sizeof note, "%zu placed models (%zu not loaded)", _placed.size(), missing);
    _notes.push_back(note);

    // The sky: city/<map>.sky names its dome model.
    _sky.clear();
    if (!_files.read("city/" + map + ".sky", data)) {
        const std::string text = textOf(data);
        _sky = text.substr(0, text.find_first_of(" \t\r\n"));
    }
    // Fog for each lighting: colour (0-255), start, end, a description.
    _fogs.clear();
    if (!_files.read("city/" + map + "_fog.csv", data)) {
        const std::string text = textOf(data);
        size_t at = text.find('\n'); // the header
        while (at != std::string::npos && at + 1 < text.size()) {
            const size_t eol = text.find('\n', at + 1);
            const std::string line = text.substr(at + 1, eol == std::string::npos ? std::string::npos : eol - at - 1);
            at = eol;
            float v[5];
            if (std::sscanf(line.c_str(), "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5)
                _fogs.push_back({{v[0] / 255, v[1] / 255, v[2] / 255}, v[3], v[4]});
        }
    }
    setLighting(_lighting);
    return Error::success();
}

void CityScene::relightCity() {
    if (!_cityMesh)
        return;
    // Each vertex: the ambient colour and each of the three lights by how
    // squarely its face is turned to it, at most 1 (as Direct3D lights
    // vertices).
    for (gfx::Vertex &v : _mesh.vertices) {
        for (size_t c = 0; c < 3; ++c) {
            float sum = _cityLights.ambient[c];
            for (size_t k = 0; k < 3; ++k) {
                const gfx::Vec3 &d = _cityLights.direction[k];
                const float facing = v.normal[0] * d[0] + v.normal[1] * d[1] + v.normal[2] * d[2];
                sum += _cityLights.color[k][c] * std::max(0.0f, facing);
            }
            v.color[c] = uint8_t(std::lround(std::clamp(sum, 0.0f, 1.0f) * 255));
        }
        v.color[3] = 255;
    }
    _renderer.destroyMesh(_cityMesh);
    _cityMesh = 0;
    std::vector<uint32_t> indices;
    for (const CityMesh::Batch &b : _mesh.batches)
        indices.insert(indices.end(), b.indices.begin(), b.indices.end());
    if (_renderer.createMesh(_mesh.vertices, indices, _cityMesh))
        _cityMesh = 0;
    rebuildIndices();
}

std::string CityScene::lightingName() const {
    return std::string(kWeathers[_lighting % 4]) + " " + kTimes[_lighting / 4];
}

void CityScene::setLighting(int index) {
    _lighting = ((index % 16) + 16) % 16;
    char name[64];
    std::snprintf(name, sizeof name, "city/%s.lt%02d", _map.c_str(), _lighting);
    _light = SceneLight{};
    _cityLights = CityLights{};
    std::vector<uint8_t> data;
    TuneNode root;
    if (!_files.read(name, data) && !parseTune(textOf(data), root) && !root.children.empty()) {
        const TuneNode &l = root.children.front();
        // Models, cars and people are lit as the city's surfaces are
        // (relightCity): the ambient colour and the three lights, each by
        // how squarely a surface faces it, as Direct3D lights vertices.
        _light.direction = towards(l.number("KeyHeading"), l.number("KeyPitch"));
        _light.fillDirection = {towards(l.number("Fill1Heading"), l.number("Fill1Pitch")),
                                towards(l.number("Fill2Heading"), l.number("Fill2Pitch"))};
        const uint32_t ambient = uint32_t(int32_t(l.number("Ambient")));
        for (size_t c = 0; c < 3; ++c) {
            _light.directed[c] = l.number("KeyColor", c);
            _light.fill[0][c] = l.number("Fill1Color", c);
            _light.fill[1][c] = l.number("Fill2Color", c);
            _light.ambient[c] = float(ambient >> (16 - 8 * c) & 0xff) / 255;
        }
        // The city's own surfaces get the three lights as they are (see
        // relightCity).
        const char *const names[] = {"Key", "Fill1", "Fill2"};
        for (size_t k = 0; k < 3; ++k) {
            const std::string n = names[k];
            _cityLights.direction[k] = towards(l.number(n + "Heading"), l.number(n + "Pitch"));
            for (size_t c = 0; c < 3; ++c)
                _cityLights.color[k][c] = l.number(n + "Color", c);
        }
        for (size_t c = 0; c < 3; ++c)
            _cityLights.ambient[c] = float(ambient >> (16 - 8 * c) & 0xff) / 255;
    }
    // The fog (city/<map>_fog.csv), a line for each time and weather.
    if (size_t(_lighting) < _fogs.size()) {
        _light.fogColor = _fogs[size_t(_lighting)].color;
        _light.fogStart = _fogs[size_t(_lighting)].start;
        _light.fogEnd = _fogs[size_t(_lighting)].end;
    }
    relightCity();
    // The sky dome: one paint job for each time and weather, in the same order.
    destroyModelMesh(_renderer, _skyMesh);
    std::vector<uint8_t> pkgData;
    Pkg pkg;
    const std::string file = "geometry/" + _sky + ".pkg";
    if (!_sky.empty() && !_files.read(file, pkgData) && !readPkg(pkgData, pkg)) {
        const auto levels = modelLevels(pkg);
        buildModelMesh(_renderer, *_textures, _files, file, pkg, levels.empty() ? "" : levels.front(),
                       pkg.paintJobs ? uint32_t(_lighting) % pkg.paintJobs : 0, false, _skyMesh);
    }
}

void CityScene::drawCloudShadows() {
    // texture/shadmap_day.tga: white, its alpha a tile of cloud shadow
    // (182-255), laid over the city from above and multiplied in. At
    // night shadmap_nite.tga, the same but a 4 x 4 grid of soft pools of
    // light (alpha 79-255), laid so its pools are the street lamps' 29 m
    // apart, still. The day tile's size and drift are Reluva's choice.
    constexpr float kDayTile = 240;   // metres the tile covers
    constexpr float kNightTile = 116; // 4 pools of 29 m
    constexpr float kDrift = 2.5f;    // m/s, with the wind (High, by day)
    if (!_cityMesh || _cloudShadows <= 0)
        return;
    const bool night = lampsLit();
    const float kTile = night ? kNightTile : kDayTile;
    bool translucent = false;
    const gfx::TextureId clouds = _textures->get(night ? "shadmap_nite" : "shadmap_day", translucent, true);
    if (!clouds)
        return;
    const float drift =
        _cloudShadows >= 2 && !night
            ? kDrift / kTile * std::chrono::duration<float>(std::chrono::steady_clock::now() - _loadedAt).count()
            : 0;
    for (const Batch &b : _batches) {
        if (b.translucent)
            continue;
        gfx::DrawCall call;
        call.mesh = _cityMesh;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.texture = clouds;
        call.texGen = gfx::TexGen::kVector;
        call.texVector0 = {1 / kTile, 0, 0};
        call.texVector1 = {0, 0, 1 / kTile};
        call.preRow0 = {1, 0, std::fmod(drift, 1.0f)};
        call.preRow1 = {0, 1, std::fmod(drift * 0.6f, 1.0f)};
        call.state.src = gfx::Blend::kZero; // what's there times the shadow's alpha
        call.state.dst = gfx::Blend::kSrcAlpha;
        call.state.depthEqual = true;
        call.state.depthWrite = false;
        call.state.depthBias = false;
        call.state.cull = gfx::Cull::kNone;
        _renderer.draw(call);
    }
}

size_t CityScene::draw(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, bool models, bool again) {
    _textures->setTime(std::chrono::duration<float>(std::chrono::steady_clock::now() - _loadedAt).count());
    // What can be seen from the eye's block, again when it's in another.
    const size_t from = visibility() ? blockAt({eye[0], eye[1], eye[2]}) : 0;
    const float moved = std::hypot(eye[0] - _detailFrom[0], eye[1] - _detailFrom[1], eye[2] - _detailFrom[2]);
    if (!again && (from != _viewBlock || moved > kDetailStep)) {
        _viewBlock = from;
        _detailFrom = {eye[0], eye[1], eye[2]};
        rebuildIndices();
    }

    // The sky round the eye, behind everything.
    if (_skyMesh.mesh && _skyShown) {
        gfx::Mat4 at = gfx::identity();
        at[12] = eye[0];
        at[13] = eye[1];
        at[14] = eye[2];
        for (const ModelDraw &d : _skyMesh.draws) {
            gfx::DrawCall call;
            call.mesh = _skyMesh.mesh;
            call.firstIndex = d.firstIndex;
            call.indexCount = d.indexCount;
            call.model = at;
            call.texture = d.texture;
            call.state.depthTest = call.state.depthWrite = false;
            call.state.cull = gfx::Cull::kNone;
            _renderer.draw(call);
        }
    }

    // Models outside the view are left out.
    const ViewFrustum frustum(viewProj);
    auto visible = [&](const gfx::Vec3 &c, float radius) { return frustum.visible(c, radius); };

    // The city, opaque then translucent, then the models.
    for (int pass = 0; pass < 2 && _cityMesh; ++pass)
        for (const Batch &b : _batches) {
            if (b.translucent != (pass == 1))
                continue;
            gfx::DrawCall call;
            call.mesh = _cityMesh;
            call.firstIndex = b.firstIndex;
            call.indexCount = b.indexCount;
            call.texture = _textures->frame(b.texture);
            call.rgbSource = gfx::ColorSource::kVertex; // lit by relightCity
            call.state.cull = gfx::Cull::kNone;
            call.state.depthBias = pass == 1;
            if (pass == 1) {
                call.state.src = gfx::Blend::kSrcAlpha;
                call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
                call.state.depthWrite = false;
                call.state.alphaTest = gfx::AlphaTest::kGt0;
            }
            applyFog(call, _light);
            _renderer.draw(call);
        }
    drawCloudShadows();
    if (!models)
        return 0;
    std::vector<std::pair<const Placed *, const ModelMesh *>> shown;
    for (const Placed &p : _placed) {
        if (_viewBlock && !_pvs.sees(_viewBlock, p.block))
            continue;
        const float dx = p.center[0] - eye[0], dy = p.center[1] - eye[1], dz = p.center[2] - eye[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance - p.radius > std::min(kModelDistance, _modelDistance) || !visible(p.center, p.radius))
            continue;
        // Finer levels nearer, by how large the model looks.
        const float size = distance / std::max(1.0f, p.radius) / std::max(0.1f, _detail);
        const size_t levels = p.model->levels.size();
        const size_t level = std::min<size_t>(levels - 1, size < 10 ? 0 : size < 25 ? 1 : size < 60 ? 2 : 3);
        shown.push_back({&p, &p.model->levels[level]});
    }
    for (const auto &[p, mesh] : shown)
        drawModelMesh(_renderer, *mesh, p->matrix, _light, false, _textures.get());
    for (const auto &[p, mesh] : shown)
        drawModelMesh(_renderer, *mesh, p->matrix, _light, true, _textures.get());
    return shown.size();
}

void CityScene::drawDynamicLights(std::vector<gfx::DynamicLight> lights, const gfx::Vec3 &eye) {
    if (lights.empty() || !_cityMesh)
        return;
    auto distance = [&](const gfx::DynamicLight &l) {
        const float dx = l.origin[0] - eye[0], dy = l.origin[1] - eye[1], dz = l.origin[2] - eye[2];
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(lights.begin(), lights.end(),
              [&](const gfx::DynamicLight &a, const gfx::DynamicLight &b) { return distance(a) < distance(b); });
    if (lights.size() > size_t(gfx::kMaxDynamicLights))
        lights.resize(size_t(gfx::kMaxDynamicLights));
    _renderer.setDynamicLights(lights);
    for (const Batch &b : _batches) {
        if (b.translucent)
            continue;
        gfx::DrawCall call;
        call.mesh = _cityMesh;
        call.firstIndex = b.firstIndex;
        call.indexCount = b.indexCount;
        call.dynamicLights = true;
        // Over what's drawn: what's there times (1 + the light).
        call.state.src = gfx::Blend::kDstColor;
        call.state.dst = gfx::Blend::kOne;
        call.state.depthEqual = true;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::kNone;
        _renderer.draw(call);
    }
}

void CityScene::hide(const std::function<bool(const Vec3 &a, const Vec3 &b, const Vec3 &c)> &drop) {
    if (!_cityMesh)
        return;
    for (CityMesh::Batch &b : _mesh.batches) {
        std::vector<uint32_t> kept;
        std::vector<uint16_t> keptBlocks;
        for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
            const auto &p0 = _mesh.vertices[b.indices[i]].pos;
            const auto &p1 = _mesh.vertices[b.indices[i + 1]].pos;
            const auto &p2 = _mesh.vertices[b.indices[i + 2]].pos;
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
    if (!_cityMesh)
        return;
    std::vector<uint32_t> indices;
    _batches.clear();
    std::vector<bool> drawn(_psdl.blocks.size(), false);
    // Blocks far enough off for their roads' low-detail textures.
    std::vector<bool> far(_psdl.blocks.size(), false);
    for (size_t i = 1; i < _blockBounds.size() && i < far.size(); ++i) {
        const BlockBounds &bb = _blockBounds[i];
        far[i] =
            std::hypot((bb.minX + bb.maxX) / 2 - _detailFrom[0], (bb.minZ + bb.maxZ) / 2 - _detailFrom[2]) > kLowDetail;
    }
    for (const CityMesh::Batch &b : _mesh.batches) {
        Batch out{uint32_t(indices.size()), 0, 0, false};
        if (!b.texture.empty())
            out.texture = _textures->get(b.texture, out.translucent);
        for (size_t t = 0; t * 3 + 2 < b.indices.size(); ++t) {
            const uint16_t block = t < b.blocks.size() ? b.blocks[t] : 0;
            if (_viewBlock && block != 0 && !_pvs.sees(_viewBlock, block)) // 0: no block (decals), always
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
        _batches.push_back(out);
    }
    _blocksDrawn = size_t(std::count(drawn.begin(), drawn.end(), true));
    _renderer.setMeshIndices(_cityMesh, indices);
}

void CityScene::drawBeacons(const FlareDrawer &flares, const Vec3 &eye) const {
    constexpr float kSize = 3, kDistance = 1500;
    if (!lampsLit())
        return;
    for (const Beacon &b : _beacons) {
        const Vec3 d{b.at[0] - eye[0], b.at[1] - eye[1], b.at[2] - eye[2]};
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < kDistance * kDistance)
            flares.draw(b.at, kSize, b.color, eye);
    }
}

size_t CityScene::blockAt(const Vec3 &p) const {
    size_t best = 0;
    float bestFloor = 0;
    for (size_t i = 1; i < _blockBounds.size(); ++i) {
        const BlockBounds &bb = _blockBounds[i];
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
