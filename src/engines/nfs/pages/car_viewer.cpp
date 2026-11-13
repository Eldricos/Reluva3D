// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/pages/viewers.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

namespace reluva::nfs {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr const char *kLevelNames[] = {"high", "medium", "small", "tiny", "all"};

std::string lower(const std::string &s) {
    return fs::toLower(s);
}

// A part's level of detail, by its place in the model: the parts' names
// can't be relied on (fce.md). With 12 or more parts: 0-4 high, 5-9
// medium, 10 small, 11 tiny, the rest (headlights) high. With 6: the body
// and wheels, then a tiny body. Anything else is all one level.
int partLevel(size_t part, size_t parts) {
    if (parts >= 12)
        return part < 5 ? 0 : part < 10 ? 1 : part == 10 ? 2 : part == 11 ? 3 : 0;
    if (parts == 6)
        return part == 5 ? 3 : 0;
    return 0;
}

// The part of a path after its last '/', and that without its extension.
std::string baseName(const std::string &path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
std::string stem(const std::string &path) {
    const std::string base = baseName(path);
    return base.substr(0, base.rfind('.'));
}

} // namespace

CarPage::CarPage(const GameFiles &files) : _files(files) {
    _models = files.withExtensions({".fce"});
}

CarPage::~CarPage() {
    release();
}

void CarPage::activate(System &sys) {
    if (Error err = sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _unavailable = err.message;
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    loadSkies();
    // RELUVA_NFS_CARS_VIEW="model yaw pitch distance": the page opened on
    // that model (its number on the page less 1), seen from there
    // (radians; the distance in the model's radii, 2.6 by default), for
    // screenshots that can be repeated.
    const char *view = std::getenv("RELUVA_NFS_CARS_VIEW");
    int wanted = _index;
    float yaw = 0;
    float pitch = 0;
    float distance = 0;
    const int got = view ? std::sscanf(view, "%d %f %f %f", &wanted, &yaw, &pitch, &distance) : 0;
    if (got >= 1 && !_models.empty())
        _index = std::clamp(wanted, 0, static_cast<int>(_models.size()) - 1);
    load(_index);
    if (got >= 3) {
        _yaw = yaw;
        _pitch = pitch;
    }
    if (got >= 4 && distance > 0)
        _distance = _radius * distance;
}

void CarPage::deactivate() {
    release();
    if (_renderer)
        for (const auto &[name, id] : _skies)
            _renderer->destroyTexture(id);
    _skies.clear();
    _renderer = nullptr;
}

void CarPage::release() {
    if (_renderer) {
        if (_mesh)
            _renderer->destroyMesh(_mesh);
        if (_envMesh)
            _renderer->destroyMesh(_envMesh);
        if (_skin)
            _renderer->destroyTexture(_skin);
        for (gfx::TextureId page : _pages)
            if (page)
                _renderer->destroyTexture(page);
    }
    _mesh = _envMesh = 0;
    _skin = 0;
    _pages.clear();
    _runs.clear();
    _envRuns.clear();
    _envVertices.clear();
    _envNormals.clear();
}

void CarPage::resetView() {
    _yaw = 0.6f;
    _pitch = 0.25f;
    _distance = _radius * 2.6f;
}

void CarPage::load(int index) {
    release();
    _index = index;
    _model = {};
    _error.clear();
    _skinName.clear();
    _partLevels.clear();
    const FileRef &ref = _models[index];

    std::vector<uint8_t> data;
    if (Error err = _files.read(ref, data)) {
        _error = err.message;
        return;
    }
    if (Error err = parseModel(data, _model)) {
        _error = err.message;
        return;
    }
    _radius = std::max(0.5f, std::hypot(_model.halfSize[0], _model.halfSize[1], _model.halfSize[2]));
    resetView();

    // A Render/pc model's pages: its .art, a picture for each page
    // (fce.md), used in place of a skin.
    const std::string folder = ref.name.substr(0, ref.name.size() - baseName(ref.name).size());
    for (const FileRef &f : _files.files()) {
        if (lower(f.name) != lower(folder + stem(ref.name) + ".art"))
            continue;
        std::vector<uint8_t> art;
        std::vector<image::Image> images;
        if (_files.read(f, art) || parseArt(art, images))
            break;
        for (const image::Image &img : images) {
            gfx::TextureId id = 0;
            _renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id);
            _pages.push_back(id);
        }
        _skinName = baseName(f.name) + " (" + std::to_string(images.size()) + " pages)";
        break;
    }

    // The skin: car00.tga beside a car.fce, goNN00.tga beside goNN.fce,
    // cone.tga beside cone.fce.
    const std::string wanted[] = {folder + "car00.tga", folder + stem(ref.name) + "00.tga",
                                  folder + stem(ref.name) + ".tga"};
    for (const std::string &name :
         _pages.empty() ? std::vector<std::string>(std::begin(wanted), std::end(wanted)) : std::vector<std::string>{}) {
        for (const FileRef &f : _files.files()) {
            if (lower(f.name) != lower(name))
                continue;
            std::vector<uint8_t> tga;
            image::Image img;
            if (!_files.read(f, tga) && !image::decode(tga, img) &&
                (skinRowsAsStored(tga, img),
                 !_renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), _skin))) {
                _skinName = baseName(f.name);
            }
            break;
        }
        if (_skin)
            break;
    }

    // One vertex per triangle corner, as each corner has its own texture
    // coordinates. z is negated: the model is left-handed, the world
    // right-handed (fce.md), which also makes front faces counter-clockwise.
    // Each part's triangles are put in order of how they're drawn (page,
    // two-sided, unlit), so that each run is one range of indices.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> envIndices;
    for (const Model::Part &part : _model.parts) {
        _partLevels.push_back(partLevel(_partLevels.size(), _model.parts.size()));
        std::vector<uint32_t> order(part.triangleCount);
        for (uint32_t i = 0; i < part.triangleCount; ++i)
            order[i] = part.firstTriangle + i;
        auto drawnAs = [&](uint32_t t) {
            const Model::Triangle &tri = _model.triangles[t];
            return std::make_tuple(_pages.empty() ? 0u : tri.texturePage, (tri.flags & Model::Triangle::kTwoSided) != 0,
                                   (tri.flags & Model::Triangle::kUnlit) != 0);
        };
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return drawnAs(a) < drawnAs(b); });
        std::vector<Run> runs;
        std::vector<Run> envRuns;
        for (uint32_t t : order) {
            const Model::Triangle &tri = _model.triangles[t];
            const auto [page, twoSided, unlit] = drawnAs(t);
            if (runs.empty() || runs.back().page != page || runs.back().twoSided != twoSided ||
                runs.back().unlit != unlit)
                runs.push_back({static_cast<uint32_t>(indices.size()), 0, page, twoSided, unlit, false});
            runs.back().indexCount += 3;
            const bool reflects = !(tri.flags & Model::Triangle::kNoReflection);
            const bool strong = (tri.flags & Model::Triangle::kStrongReflection) != 0;
            if (reflects && (envRuns.empty() || envRuns.back().strong != strong || envRuns.back().twoSided != twoSided))
                envRuns.push_back({static_cast<uint32_t>(envIndices.size()), 0, 0, twoSided, false, strong});
            if (reflects)
                envRuns.back().indexCount += 3;
            for (int k = 0; k < 3; ++k) {
                const Model::Vec3 &p = _model.vertices[tri.vertices[k]];
                const Model::Vec3 &n = _model.normals[tri.vertices[k]];
                gfx::Vertex v{};
                v.pos[0] = p[0] + part.position[0];
                v.pos[1] = p[1] + part.position[1];
                v.pos[2] = -(p[2] + part.position[2]);
                v.normal[0] = n[0];
                v.normal[1] = n[1];
                v.normal[2] = -n[2];
                v.uv[0] = tri.u[k];
                v.uv[1] = tri.v[k];
                std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.push_back(v);
                if (reflects) {
                    // Half grey at half alpha, or white for a strong one
                    // (0x55e6dc, 0x55e6e0).
                    gfx::Vertex e = v;
                    std::fill(std::begin(e.color), std::end(e.color), uint8_t(strong ? 255 : 127));
                    envIndices.push_back(static_cast<uint32_t>(_envVertices.size()));
                    _envVertices.push_back(e);
                    _envNormals.push_back({v.normal[0], v.normal[1], v.normal[2]});
                }
            }
        }
        _runs.push_back(std::move(runs));
        _envRuns.push_back(std::move(envRuns));
    }
    if (!vertices.empty())
        if (Error err = _renderer->createMesh(vertices, indices, _mesh))
            _error = err.message;
    if (!_envVertices.empty())
        if (Error err = _renderer->createMesh(_envVertices, envIndices, _envMesh))
            _envMesh = 0;
}

// The sphere maps of Hometown's sky.fsh (CHRD day, CHRN night, CHRW and
// CHNW weather), which the game gives the cars as their reflection.
void CarPage::loadSkies() {
    if (!_skies.empty())
        return;
    for (const FileRef &f : _files.files()) {
        if (lower(f.name) != "gamedata/tracks/trk000/sky.fsh")
            continue;
        std::vector<uint8_t> data;
        ShapeBank bank;
        if (_files.read(f, data) || parseShapeBank(std::move(data), bank))
            break;
        for (const char *name : {"CHRD", "CHRN", "CHRW", "CHNW"})
            for (const Shape &shape : bank.shapes) {
                image::Image img;
                gfx::TextureId id = 0;
                if (shape.name == name && !decodeShape(shape, img) &&
                    !_renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id))
                    _skies.push_back({name, id});
            }
        break;
    }
}

void CarPage::handleEvent(const Event &ev) {
    const int n = static_cast<int>(_models.size());
    if (ev.type == EventType::kKeyDown) {
        switch (ev.key) {
        case key::kRight:
            if (_renderer)
                load((_index + 1) % n);
            else
                _index = (_index + 1) % n;
            break;
        case key::kLeft:
            if (_renderer)
                load((_index + n - 1) % n);
            else
                _index = (_index + n - 1) % n;
            break;
        case 'a':
            _yaw -= 0.15f;
            break;
        case 'd':
            _yaw += 0.15f;
            break;
        case 'w':
            _pitch = std::min(_pitch + 0.1f, 1.5f);
            break;
        case 's':
            _pitch = std::max(_pitch - 0.1f, -1.5f);
            break;
        case '+':
        case '=':
            _distance = std::max(_radius * 0.6f, _distance * 0.9f);
            break;
        case '-':
            _distance /= 0.9f;
            break;
        case 'l':
            _level = (_level + 1) % 5;
            break;
        case 't':
            _textures = !_textures;
            break;
        case 'c':
            _cullBack = !_cullBack;
            break;
        case 'e':
            _sky = (_sky + 1) % static_cast<int>(_skies.size() + 1);
            break;
        case ' ':
            _spin = !_spin;
            break;
        case 'r':
            resetView();
            break;
        default:
            break;
        }
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw += static_cast<float>(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + static_cast<float>(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::max(_radius * 0.6f, _distance * std::pow(0.9f, static_cast<float>(ev.wheelY)));
    }
}

void CarPage::draw(Surface &s) {
    // Only shown when the renderer couldn't be had.
    std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
    font::drawText(s, 16, 16, "The Cars page needs 3D rendering, which isn't available:", {255, 120, 100});
    font::drawText(s, 16, 16 + font::kLineHeight, _unavailable, {230, 230, 230});
}

void CarPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    if (_spin && _lastMs)
        _yaw += static_cast<float>(std::min<uint64_t>(nowMs - _lastMs, 100)) * 0.0006f;
    _lastMs = nowMs;

    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{offset[0] * _distance, offset[1] * _distance, offset[2] * _distance};
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), 0.05f, 500.0f),
                                             gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});

    size_t triangles = 0;
    if (_mesh) {
        gfx::DrawCall call;
        call.mesh = _mesh;
        call.texture = _textures ? _skin : 0;
        call.rgbSource = gfx::ColorSource::kLighting;
        call.ambientLight = {0.45f, 0.45f, 0.45f};
        call.directedLight = {0.7f, 0.7f, 0.7f};
        call.lightDirection = {0.36f, 0.8f, 0.48f};
        auto shown = [&](size_t p) {
            return _model.parts[p].triangleCount && (_level == 4 || _partLevels[p] == _level);
        };
        for (size_t p = 0; p < _model.parts.size(); ++p) {
            if (!shown(p))
                continue;
            for (const Run &r : _runs[p]) {
                if (!_pages.empty())
                    call.texture = _textures && r.page < _pages.size() ? _pages[r.page] : 0;
                call.state.cull = _cullBack && !r.twoSided ? gfx::Cull::kBack : gfx::Cull::kNone;
                // Unlit: one constant white (0x55e6e8) for the lighting.
                call.rgbSource = r.unlit ? gfx::ColorSource::kConstant : gfx::ColorSource::kLighting;
                call.firstIndex = r.firstIndex;
                call.indexCount = r.indexCount;
                _renderer->draw(call);
            }
            triangles += _model.parts[p].triangleCount;
        }

        // The reflection: each corner's normal as the camera sees it
        // (right, down, ahead) to the sphere map, added with source alpha
        // (THRASH blend 1) in the corner's colour.
        if (_envMesh && _sky < static_cast<int>(_skies.size())) {
            const gfx::Vec3 ahead{-offset[0], -offset[1], -offset[2]};
            gfx::Vec3 right{-ahead[2], 0, ahead[0]}; // ahead x up
            const float rl = std::hypot(right[0], right[2]);
            right = {right[0] / rl, 0, right[2] / rl};
            // down = ahead x right
            const gfx::Vec3 dn{ahead[1] * right[2] - ahead[2] * right[1], ahead[2] * right[0] - ahead[0] * right[2],
                               ahead[0] * right[1] - ahead[1] * right[0]};
            for (size_t i = 0; i < _envVertices.size(); ++i) {
                const auto &n = _envNormals[i];
                const auto uv = reflectionUv(n[0] * right[0] + n[1] * right[1] + n[2] * right[2],
                                             n[0] * dn[0] + n[1] * dn[1] + n[2] * dn[2],
                                             n[0] * ahead[0] + n[1] * ahead[1] + n[2] * ahead[2]);
                _envVertices[i].uv[0] = uv[0];
                _envVertices[i].uv[1] = uv[1];
            }
            _renderer->setMeshVertices(_envMesh, _envVertices);
            gfx::DrawCall env;
            env.mesh = _envMesh;
            env.texture = _skies[size_t(_sky)].second;
            env.rgbSource = env.alphaSource = gfx::ColorSource::kVertex;
            env.state.src = gfx::Blend::kSrcAlpha;
            env.state.dst = gfx::Blend::kOne;
            env.state.depthWrite = false;
            for (size_t p = 0; p < _model.parts.size(); ++p) {
                if (!shown(p))
                    continue;
                for (const Run &r : _envRuns[p]) {
                    env.state.cull = _cullBack && !r.twoSided ? gfx::Cull::kBack : gfx::Cull::kNone;
                    env.firstIndex = r.firstIndex;
                    env.indexCount = r.indexCount;
                    _renderer->draw(env);
                }
            }
        }
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Cars  " + std::to_string(_index + 1) + "/" + std::to_string(_models.size()) +
                                      "  " + _models[_index].name};
    if (!_error.empty()) {
        lines.push_back(_error);
    } else {
        lines.push_back(std::to_string(_model.parts.size()) + " parts, " + std::to_string(_model.triangles.size()) +
                        " triangles; showing " + kLevelNames[_level] + " detail (" + std::to_string(triangles) +
                        ")  skin: " + (_skinName.empty() ? "none" : _skinName) + "  reflection: " +
                        (_sky < static_cast<int>(_skies.size()) ? _skies[size_t(_sky)].first : "off") +
                        (_cullBack ? "" : "  back faces"));
    }
    lines.push_back(
        "Left/Right: model  drag/WASD: turn  wheel: zoom  L: detail  T: skin  C: faces  E: sky  Space: spin");
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y =
            i + 1 == lines.size() ? kFrameH - font::kLineHeight - 4 : 4 + static_cast<int>(i) * font::kLineHeight;
        for (int yy = y - 2; yy < y + font::kLineHeight - 1; ++yy)
            for (int x = 0; x < kFrameW; ++x)
                _overlay.row(yy)[x * 4 + 3] = 140;
        font::drawText(_overlay, 6, y, lines[i],
                       i == 1 && !_error.empty() ? Color{255, 120, 100} : Color{230, 230, 230});
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::nfs
