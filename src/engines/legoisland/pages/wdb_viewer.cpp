// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's WORLD.WDB pages: its textures, and its models and
// parts in 3D (docs/engines/legoisland/formats/wdb.md).

#include "common/fs.h"
#include "engines/legoisland/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr int kListW = 300;
constexpr int kRowH = font::kLineHeight + 1;
constexpr Color kBackground = {40, 40, 48};
constexpr Color kText = {230, 230, 230};
constexpr Color kDim = {150, 150, 160};
constexpr Color kSelected = {70, 90, 140};
constexpr Color kError = {255, 120, 120};

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

size_t step(size_t current, size_t count, uint32_t key) {
    if (count == 0)
        return 0;
    switch (key) {
    case key::kUp:
        return current > 0 ? current - 1 : current;
    case key::kDown:
        return std::min(current + 1, count - 1);
    case key::kPageUp:
        return current > 20 ? current - 20 : 0;
    case key::kPageDown:
        return std::min(current + 20, count - 1);
    case key::kHome:
        return 0;
    case key::kEnd:
        return count - 1;
    default:
        return current;
    }
}

// The game's points are Direct3D's, left-handed: mirrored in z they come
// out right-handed, as the renderer's are, and not as their mirror image.
gfx::Mat4 mirrorZ() {
    gfx::Mat4 m = gfx::identity();
    m[10] = -1;
    return m;
}

gfx::Mat4 translation(const std::array<float, 4> &v) {
    gfx::Mat4 m = gfx::identity();
    m[12] = v[0];
    m[13] = v[1];
    m[14] = v[2];
    return m;
}

// A key's rotation, a quaternion (w, x, y, z), as the matrix that turns a
// point by it.
gfx::Mat4 rotation(const std::array<float, 4> &q) {
    const float w = q[0];
    const float x = q[1];
    const float y = q[2];
    const float z = q[3];
    gfx::Mat4 m = gfx::identity();
    m[0] = 1 - 2 * (y * y + z * z);
    m[1] = 2 * (x * y + w * z);
    m[2] = 2 * (x * z - w * y);
    m[4] = 2 * (x * y - w * z);
    m[5] = 1 - 2 * (x * x + z * z);
    m[6] = 2 * (y * z + w * x);
    m[8] = 2 * (x * z + w * y);
    m[9] = 2 * (y * z - w * x);
    m[10] = 1 - 2 * (x * x + y * y);
    return m;
}

// Where each node of an animation is at its start, by its name in lower
// case: its first move and turn, after its parent's.
void place(const WdbAnimNode &node, const gfx::Mat4 &parent, std::map<std::string, gfx::Mat4> &out) {
    gfx::Mat4 m = parent;
    if (!node.translation.empty())
        m = gfx::multiply(m, translation(node.translation[0].value));
    if (!node.rotation.empty())
        m = gfx::multiply(m, rotation(node.rotation[0].value));
    out[fs::toLower(node.name)] = m;
    for (const WdbAnimNode &child : node.children)
        place(child, m, out);
}

} // namespace

WdbTexturePage::WdbTexturePage(Surface &screen, const WdbFile &wdb) : _screen(screen) {
    add("shared", wdb.sharedTextures());
    add("shared part", wdb.sharedPart().textures);
    // Read once: the rows point into them.
    const std::vector<WdbPartRef> parts = wdb.allParts();
    const std::vector<WdbModelRef> models = wdb.allModels();
    _parts.resize(parts.size());
    _models.resize(models.size());
    for (size_t i = 0; i < parts.size(); i++)
        if (Error e = wdb.readPart(parts[i], _parts[i]))
            _errors.push_back(e.message);
    for (size_t i = 0; i < models.size(); i++)
        if (Error e = wdb.readModel(models[i], _models[i]))
            _errors.push_back(e.message);
    for (size_t i = 0; i < parts.size(); i++)
        add(parts[i].name, _parts[i].textures);
    for (size_t i = 0; i < models.size(); i++)
        add(models[i].name, _models[i].textures);
}

void WdbTexturePage::add(const std::string &owner, const std::vector<WdbTexture> &textures) {
    for (const WdbTexture &t : textures)
        for (size_t i = 0; i < t.images.size(); i++)
            _rows.push_back({owner, &t, i});
}

void WdbTexturePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    const size_t next = step(_current, _rows.size(), ev.key);
    if (next != _current) {
        _current = next;
        redraw();
    }
}

void WdbTexturePage::redraw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    font::drawText(_screen, 4, 4, "WORLD.WDB: textures (Tab: next page)", kDim);
    // The list, the current row kept in sight.
    const int top = 4 + 2 * kRowH;
    const int rows = (_screen.h - top - 4) / kRowH;
    const size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    for (int i = 0; i < rows && first + size_t(i) < _rows.size(); i++) {
        const Row &r = _rows[first + size_t(i)];
        const int y = top + i * kRowH;
        if (first + size_t(i) == _current)
            fillRect(_screen, 0, y - 1, kListW, kRowH, kSelected);
        std::string label = r.owner + ": " + r.texture->name;
        if (r.image > 0)
            label += " (2)";
        font::drawText(_screen, 4, y, label, kText);
    }
    if (_rows.empty())
        return;

    const Row &r = _rows[_current];
    const WdbImage &img = r.texture->images[r.image];
    const int x0 = kListW + 12;
    int y = top;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s, in %s: %ux%u, %zu colours%s", r.texture->name.c_str(), r.owner.c_str(),
                  img.width, img.height, img.palette.size() / 3,
                  r.texture->images.size() > 1 ? (r.image ? ", its second picture" : ", one of two pictures") : "");
    font::drawText(_screen, x0, y, buf, kText);
    y += 2 * kRowH;
    for (const std::string &e : _errors) {
        font::drawText(_screen, x0, y, e, kError);
        y += kRowH;
    }

    // The palette, a swatch a colour.
    const int swatch = 16;
    for (size_t c = 0; c < img.palette.size() / 3; c++) {
        const int sx = x0 + int(c % 32) * (swatch + 2);
        const int sy = y + int(c / 32) * (swatch + 2);
        fillRect(_screen, sx, sy, swatch, swatch, {img.palette[c * 3], img.palette[c * 3 + 1], img.palette[c * 3 + 2]});
    }
    y += int((img.palette.size() / 3 + 31) / 32) * (swatch + 2) + kRowH;

    // The picture, as many times its size as fits.
    if (img.width == 0 || img.height == 0)
        return;
    const int fitW = _screen.w - x0 - 8;
    const int fitH = _screen.h - y - 8;
    const int zoom = std::max(1, std::min(fitW / int(img.width), fitH / int(img.height)));
    std::vector<uint8_t> rgba;
    img.toRGBA(rgba);
    for (int py = 0; py < int(img.height) * zoom && y + py < _screen.h; py++) {
        uint8_t *dst = _screen.row(y + py) + size_t(x0) * 4;
        const uint8_t *src = &rgba[size_t(py / zoom) * img.width * 4];
        for (int px = 0; px < int(img.width) * zoom && x0 + px < _screen.w; px++)
            std::copy_n(src + size_t(px / zoom) * 4, 4, dst + size_t(px) * 4);
    }
}

WdbModelPage::WdbModelPage(System &sys, Surface &screen, const WdbFile &wdb) : _sys(sys), _screen(screen), _wdb(wdb) {
    _modelRefs = wdb.allModels();
    _partRefs = wdb.allParts();
    for (const WdbPartRef &ref : _partRefs) {
        WdbPart part;
        if (!wdb.readPart(ref, part))
            _parts[fs::toLower(ref.name)] = std::move(part);
    }
    for (size_t i = 0; i < _modelRefs.size(); i++)
        _items.push_back({_modelRefs[i].name, int(i), -1, -1});
    for (size_t i = 0; i < _partRefs.size(); i++)
        _items.push_back({"part " + _partRefs[i].name, -1, int(i), -1});
    for (size_t i = 0; i < wdb.sharedPart().rois.size(); i++)
        _items.push_back({"shared part " + wdb.sharedPart().rois[i].name, -1, -1, int(i)});
}

WdbModelPage::~WdbModelPage() {
    deactivate();
}

void WdbModelPage::activate() {
    if (Error e = _sys.init3D(kViewerW, kViewerH, _renderer)) {
        _renderer = nullptr;
        fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
        font::drawText(_screen, 4, 4, "WORLD.WDB's models need 3D rendering, which isn't available:", kError);
        font::drawText(_screen, 4, 4 + kRowH, e.message, kText);
        return;
    }
    _overlay.create(kViewerW, kViewerH, PixelFormat::kRGBA32);
    show(_current);
}

void WdbModelPage::deactivate() {
    clearScene();
    _renderer = nullptr;
}

void WdbModelPage::clearScene() {
    if (_renderer) {
        for (const auto &list : {&_opaque, &_translucent})
            for (const Draw &d : *list)
                _renderer->destroyMesh(d.mesh);
        for (const auto &[name, id] : _textures)
            if (id)
                _renderer->destroyTexture(id);
    }
    _opaque.clear();
    _translucent.clear();
    _textures.clear();
    _missing.clear();
    _triangles = 0;
}

void WdbModelPage::show(size_t index) {
    _current = index;
    _lod = 0;
    build();
    resetView();
}

void WdbModelPage::build() {
    clearScene();
    _error.clear();
    _lods = 1;
    if (!_renderer || _items.empty())
        return;
    const Item &item = _items[_current];
    const std::vector<const std::vector<WdbTexture> *> shared = {&_wdb.sharedPart().textures, &_wdb.sharedTextures()};
    if (item.model >= 0) {
        if (Error e = _wdb.readModel(_modelRefs[size_t(item.model)], _model)) {
            _error = e.message;
            return;
        }
        std::map<std::string, gfx::Mat4> placed;
        place(_model.animation, gfx::identity(), placed);
        addRoi(_model.roi, placed, &_model);
    } else if (item.part >= 0) {
        const auto it = _parts.find(fs::toLower(_partRefs[size_t(item.part)].name));
        if (it == _parts.end()) {
            _error = "The part can't be read";
            return;
        }
        std::vector<const std::vector<WdbTexture> *> lists = {&it->second.textures};
        lists.insert(lists.end(), shared.begin(), shared.end());
        for (const WdbRoi &roi : it->second.rois)
            addLods(roi.lods, gfx::identity(), lists);
    } else {
        addLods(_wdb.sharedPart().rois[size_t(item.sharedRoi)].lods, gfx::identity(), shared);
    }
}

void WdbModelPage::addRoi(const WdbRoi &roi, const std::map<std::string, gfx::Mat4> &placed, const WdbModel *model) {
    const auto at = placed.find(fs::toLower(roi.name));
    const gfx::Mat4 transform = at != placed.end() ? at->second : gfx::identity();
    std::vector<const std::vector<WdbTexture> *> lists = {&model->textures};
    const std::vector<WdbLod> *lods = &roi.lods;
    if (roi.usesPart) {
        const auto part = _parts.find(fs::toLower(roi.partName()));
        if (part == _parts.end() || part->second.rois.empty()) {
            _missing.push_back("part " + roi.partName());
            lods = nullptr;
        } else {
            lods = &part->second.rois[0].lods;
            lists.push_back(&part->second.textures);
        }
    }
    lists.push_back(&_wdb.sharedPart().textures);
    lists.push_back(&_wdb.sharedTextures());
    if (lods)
        addLods(*lods, transform, lists);
    for (const WdbRoi &child : roi.children)
        addRoi(child, placed, model);
}

void WdbModelPage::addLods(const std::vector<WdbLod> &lods, const gfx::Mat4 &transform,
                           const std::vector<const std::vector<WdbTexture> *> &textures) {
    if (lods.empty())
        return;
    _lods = std::max(_lods, int(lods.size()));
    const WdbLod &lod = lods[lods.size() - 1 - size_t(std::min(_lod, int(lods.size()) - 1))];
    for (const WdbMesh &mesh : lod.meshes) {
        std::vector<gfx::Vertex> vertices;
        vertices.reserve(mesh.corners.size());
        for (const WdbCorner &c : mesh.corners) {
            gfx::Vertex v{};
            std::copy_n(lod.vertices[c.vertex].data(), 3, v.pos);
            std::copy_n(lod.normals[c.normal].data(), 3, v.normal);
            if (c.texCoord != WdbCorner::kNone)
                std::copy_n(lod.texCoords[c.texCoord].data(), 2, v.uv);
            std::fill_n(v.color, 4, uint8_t(255));
            vertices.push_back(v);
        }
        std::vector<uint32_t> indices(mesh.triangles.begin(), mesh.triangles.end());
        Draw d;
        if (_renderer->createMesh(vertices, indices, d.mesh))
            continue;
        d.indices = uint32_t(indices.size());
        d.transform = gfx::multiply(mirrorZ(), transform);
        d.texture = mesh.texture.empty() ? 0 : texture(mesh.texture, textures);
        d.color = {mesh.color[0] / 255.0f, mesh.color[1] / 255.0f, mesh.color[2] / 255.0f,
                   1 - std::clamp(mesh.transparency, 0.0f, 1.0f)};
        _triangles += indices.size() / 3;
        (mesh.transparency > 0 ? _translucent : _opaque).push_back(d);
    }
}

gfx::TextureId WdbModelPage::texture(const std::string &name,
                                     const std::vector<const std::vector<WdbTexture> *> &lists) {
    const std::string key = fs::toLower(name);
    if (const auto it = _textures.find(key); it != _textures.end())
        return it->second;
    gfx::TextureId id = 0;
    for (const std::vector<WdbTexture> *list : lists) {
        for (const WdbTexture &t : *list) {
            // A `^` texture is named by meshes without its `^`.
            const std::string tn = fs::toLower(t.name.starts_with('^') ? t.name.substr(1) : t.name);
            if (tn != key || t.images.empty())
                continue;
            std::vector<uint8_t> rgba;
            t.images[0].toRGBA(rgba);
            _renderer->createTexture(int(t.images[0].width), int(t.images[0].height), rgba.data(), {}, id);
            break;
        }
        if (id)
            break;
    }
    if (!id)
        _missing.push_back("texture " + name);
    _textures[key] = id;
    return id;
}

void WdbModelPage::resetView() {
    // Frame what's drawn, by its meshes' points as placed.
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    auto grow = [&](const std::vector<WdbLod> &lods, const gfx::Mat4 &t) {
        if (lods.empty())
            return;
        for (const auto &v : lods.back().vertices) {
            const gfx::Vec3 p = gfx::transformPoint(gfx::multiply(mirrorZ(), t), {v[0], v[1], v[2]});
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], p[size_t(k)]);
                hi[k] = std::max(hi[k], p[size_t(k)]);
            }
            any = true;
        }
    };
    if (!_items.empty() && _items[_current].model >= 0 && _error.empty()) {
        std::map<std::string, gfx::Mat4> placed;
        place(_model.animation, gfx::identity(), placed);
        struct Walk {
            WdbModelPage &page;
            const std::map<std::string, gfx::Mat4> &placed;
            decltype(grow) &g;
            void roi(const WdbRoi &r) {
                const auto at = placed.find(fs::toLower(r.name));
                const gfx::Mat4 t = at != placed.end() ? at->second : gfx::identity();
                if (r.usesPart) {
                    const auto part = page._parts.find(fs::toLower(r.partName()));
                    if (part != page._parts.end() && !part->second.rois.empty())
                        g(part->second.rois[0].lods, t);
                } else {
                    g(r.lods, t);
                }
                for (const WdbRoi &c : r.children)
                    roi(c);
            }
        } walk{*this, placed, grow};
        walk.roi(_model.roi);
    } else if (!_items.empty() && _items[_current].part >= 0) {
        const auto it = _parts.find(fs::toLower(_partRefs[size_t(_items[_current].part)].name));
        if (it != _parts.end())
            for (const WdbRoi &r : it->second.rois)
                grow(r.lods, gfx::identity());
    } else if (!_items.empty() && _items[_current].sharedRoi >= 0) {
        grow(_wdb.sharedPart().rois[size_t(_items[_current].sharedRoi)].lods, gfx::identity());
    }
    if (any) {
        _center = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
        _radius = std::max(0.05f, std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                            (hi[2] - lo[2]) * (hi[2] - lo[2])) /
                                      2);
    } else {
        _center = {0, 0, 0};
        _radius = 1;
    }
    _yaw = 0.6f;
    _pitch = 0.35f;
    _distance = _radius * 2.6f;
}

void WdbModelPage::handleEvent(const Event &ev) {
    if (!_renderer || _items.empty())
        return;
    if (ev.type == EventType::kKeyDown) {
        const size_t next = step(_current, _items.size(), ev.key);
        if (next != _current) {
            show(next);
        } else if (ev.key == 'l') {
            _lod = (_lod + 1) % std::max(1, _lods);
            build();
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            _showInfo = !_showInfo;
        } else if (ev.key == '+' || ev.key == '=') {
            _distance = std::max(_radius * 0.1f, _distance * 0.9f);
        } else if (ev.key == '-') {
            _distance /= 0.9f;
        }
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::max(_radius * 0.1f, _distance * std::pow(0.9f, float(ev.wheelY)));
    }
}

void WdbModelPage::drawInfo() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    int y = 4;
    auto line = [&](const std::string &s, Color c) {
        if (y > kViewerH - kRowH)
            return;
        font::drawText(_overlay, 4, y, s, c);
        y += kRowH;
    };
    if (_items.empty()) {
        line("WORLD.WDB has no models", kError);
        return;
    }
    const Item &item = _items[_current];
    line("WORLD.WDB: " + item.name + "  (" + std::to_string(_current + 1) + "/" + std::to_string(_items.size()) +
             ", Tab: next page)",
         {150, 200, 255});
    if (!_error.empty())
        line(_error, kError);
    if (!_showInfo)
        return;
    char buf[200];
    std::snprintf(buf, sizeof buf, "LOD %d of %d from the finest (L), %zu triangles; drag to turn, I hides this",
                  _lod + 1, _lods, _triangles);
    line(buf, kText);
    if (item.model >= 0) {
        const WdbModelRef &ref = _modelRefs[size_t(item.model)];
        line("presenter " + (ref.presenter.empty() ? std::string("-") : ref.presenter) + ", " +
                 std::to_string(_model.textures.size()) + " textures of its own",
             kText);
        std::vector<std::string> worlds;
        for (const WdbWorld &w : _wdb.worlds())
            for (const WdbModelRef &m : w.models)
                if (m.offset == ref.offset && std::find(worlds.begin(), worlds.end(), w.name) == worlds.end())
                    worlds.push_back(w.name);
        std::string in = "in";
        for (const std::string &w : worlds)
            in += " " + w;
        line(in, kText);
    }
    for (const std::string &m : _missing)
        line("not found: " + m, kError);
}

void WdbModelPage::update(uint64_t) {
    if (!_renderer)
        return;
    const gfx::Vec3 offset{std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch), std::cos(_pitch) * std::cos(_yaw)};
    const gfx::Vec3 eye{_center[0] + offset[0] * _distance, _center[1] + offset[1] * _distance,
                        _center[2] + offset[2] * _distance};
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(0.8f, float(kViewerW) / float(kViewerH), std::max(0.01f, _radius * 0.01f), _radius * 60),
        gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});
    for (const auto &list : {&_opaque, &_translucent})
        for (const Draw &d : *list) {
            gfx::DrawCall call;
            call.mesh = d.mesh;
            call.indexCount = d.indices;
            call.model = d.transform;
            call.texture = d.texture;
            // A texture is drawn as it is, lit; a colour too.
            call.color = d.texture ? std::array<float, 4>{1, 1, 1, d.color[3]} : d.color;
            call.rgbSource = gfx::ColorSource::kLighting;
            call.ambientLight = {0.45f, 0.45f, 0.45f};
            call.directedLight = {0.6f, 0.6f, 0.6f};
            call.lightDirection = {0.4f, 0.8f, 0.45f};
            // Mirrored, the faces' winding is the other way round; the
            // viewer draws both sides rather than rely on it.
            call.state.cull = gfx::Cull::kNone;
            if (list == &_translucent) {
                call.state.src = gfx::Blend::kSrcAlpha;
                call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
                call.state.depthWrite = false;
            }
            _renderer->draw(call);
        }
    drawInfo();
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::legoisland
