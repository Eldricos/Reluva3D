// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/soft3d.h"
#include "engines/newworlds/viewers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>

namespace reluva::newworlds {

namespace {

constexpr int kFilterCount = 3;
constexpr const char *kFilterNames[kFilterCount] = {"all", "multi-part", "failed"};
// How a texture's alpha layer (format 6) is used. By default: in _g
// textures (k_bop_g) it marks the lights, so it's a glow map, keeping texels
// lit; in the others (pad's hazard stripes, the door frames) it's
// transparency. The other two settings apply one way to all.
constexpr const char *kAlphaNames[3] = {"alpha as glow", "alpha ignored", "alpha as transparency"};
constexpr const char *kOutlineNames[3] = {"solid", "solid + outlines", "outlines"};
// Front faces are clockwise on screen (models store them clockwise), so by
// default anticlockwise faces are the back faces and are hidden.
constexpr const char *kFaceNames[3] = {"", "showing back faces", "showing back faces only"};

constexpr float kPi = 3.14159265f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
constexpr float kKeyTurn = 10.0f * kPi / 180.0f;
constexpr float kSpinPerMs = 0.0008f;

// Part colours (R, G, B), cycled when a model has more parts.
constexpr uint8_t kPartColours[8][3] = {
    {170, 180, 195}, {225, 140, 70}, {90, 170, 230},  {120, 200, 110},
    {215, 95, 120},  {200, 190, 90}, {160, 120, 220}, {90, 200, 190},
};

} // namespace

ModelViewer::ModelViewer(System &sys, Surface &screen, const Archives &archives)
    : _sys(sys), _screen(screen), _archives(archives), _metamodels(archives) {
    for (size_t a = 0; a < _archives.size(); ++a) {
        const auto &entries = _archives[a]->entries();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].tag == cattag::kGeometry)
                _models.push_back({a, i});
    }
    _textureFiles = listTextureFiles(_archives);
}

void ModelViewer::activate() {
    if (!_classified) {
        classify();
        _classified = true;
        resetView();
        applyFilter(0);
        return;
    }
    _dirty = true;
    updateTitle();
}

// Decode every model once so the filters know which is which.
void ModelViewer::classify() {
    std::vector<uint8_t> data;
    GeoModel m;
    _partCounts.assign(_models.size(), 0);
    for (size_t i = 0; i < _models.size(); ++i) {
        CatArchive &cat = *_archives[_models[i].archive];
        if (!cat.read(cat.entries()[_models[i].entry], data) && !parseGeo(data, m))
            _partCounts[i] = uint32_t(m.parts.size());
    }
}

void ModelViewer::applyFilter(int filter) {
    _filter = filter;
    _visible.clear();
    for (size_t i = 0; i < _models.size(); ++i) {
        const uint32_t parts = _partCounts[i];
        if (filter == 0 || (filter == 1 && parts > 1) || (filter == 2 && parts == 0))
            _visible.push_back(i);
    }
    if (_visible.empty()) {
        _model = GeoModel{};
        _status = "none (press F for the next filter)";
        _dirty = true;
        updateTitle();
        return;
    }
    show(0);
}

void ModelViewer::show(int index) {
    _current = index;
    _soloPart = -1;
    _status.clear();
    const EntryRef &ref = _models[_visible[size_t(index)]];
    CatArchive &cat = *_archives[ref.archive];
    std::vector<uint8_t> data;
    if (Error err = cat.read(cat.entries()[ref.entry], data))
        _status = err.message;
    else if (Error err2 = parseGeo(data, _model))
        _status = err2.message;
    if (!_status.empty())
        _model = GeoModel{};
    loadTextures(cat.entries()[ref.entry].name);
    loadMetamodel(cat.entries()[ref.entry].name);
    _selectedPoint = -1;

    // Frame the model: centre of its bounding box, distance to the farthest vertex.
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto &part : _model.parts)
        for (const auto &v : part.vertices) {
            const float c[3] = {v.x, v.y, v.z};
            for (int i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], c[i]);
                hi[i] = std::max(hi[i], c[i]);
            }
        }
    _size = 1;
    if (lo[0] <= hi[0]) {
        for (int i = 0; i < 3; ++i)
            _centre[i] = (lo[i] + hi[i]) / 2;
        float far2 = 0;
        for (const auto &part : _model.parts)
            for (const auto &v : part.vertices) {
                const Vec d = Vec{v.x, v.y, v.z} - Vec{_centre[0], _centre[1], _centre[2]};
                far2 = std::max(far2, dot(d, d));
            }
        _size = std::max(std::sqrt(far2), 1e-3f);
    }
    _dirty = true;
    updateTitle();
}

void ModelViewer::loadMetamodel(const std::string &modelName) {
    _hasMdl = _metamodels.find(modelName, _mdl);
    _animations = _hasMdl ? _metamodels.animations(modelName, _mdl) : std::vector<Animation>{};
    _turret = _hasMdl && hasTurret(_mdl);
    _animation = -1;
    _animFrame = 0;
}

void ModelViewer::loadTextures(const std::string &modelName) {
    _modelTextures.clear();
    _modelTextureNames.clear();
    size_t needed = 0; // the highest texture number + 1
    for (const auto &face : _model.faces)
        needed = std::max<size_t>(needed, face.corners.back().extra[4] + 1);
    std::vector<uint8_t> data;
    std::vector<const TextureFile *> slots = findModelTextures(modelName, _textureFiles, needed);
    addDoorTextures(_model, modelName, _textureFiles, slots);
    addPadTextures(_model, modelName, _textureFiles, slots);
    for (const TextureFile *t : slots) {
        // Unknown or unreadable textures stay empty, keeping the numbering;
        // their faces are drawn untextured.
        Texture tex;
        if (t) {
            CatArchive &cat = *_archives[t->ref.archive];
            if (cat.read(cat.entries()[t->ref.entry], data) || decodeTexture(data, tex))
                tex = Texture{};
        }
        _modelTextures.push_back(std::move(tex));
        _modelTextureNames.push_back(t ? t->stem : "?");
    }
}

void ModelViewer::resetView() {
    _yaw = 35.0f * kPi / 180.0f;
    _pitch = 25.0f * kPi / 180.0f;
    _zoom = 1;
    _dirty = true;
}

void ModelViewer::handleEvent(const Event &ev) {
    switch (ev.type) {
    case EventType::MouseDown:
        if (ev.button == MouseButton::Left) {
            _dragging = true;
            _dragX = ev.mouseX;
            _dragY = ev.mouseY;
        }
        return;
    case EventType::MouseUp:
        if (ev.button == MouseButton::Left)
            _dragging = false;
        return;
    case EventType::MouseMove:
        if (_dragging) {
            _yaw += float(ev.mouseX - _dragX) * 0.01f;
            _pitch = std::clamp(_pitch + float(ev.mouseY - _dragY) * 0.01f, -kMaxPitch, kMaxPitch);
            _dragX = ev.mouseX;
            _dragY = ev.mouseY;
            _dirty = true;
        }
        return;
    case EventType::MouseWheel:
        _zoom = std::clamp(_zoom * std::pow(0.9f, float(ev.wheelY)), 0.05f, 20.0f);
        _dirty = true;
        return;
    case EventType::KeyDown:
        break;
    default:
        return;
    }

    const int n = int(_visible.size());
    switch (ev.key) {
    case 'f':
        applyFilter((_filter + 1) % kFilterCount);
        return;
    case 'a':
        _yaw -= kKeyTurn;
        break;
    case 'd':
        _yaw += kKeyTurn;
        break;
    case 'w':
        _pitch = std::min(_pitch + kKeyTurn, kMaxPitch);
        break;
    case 's':
        _pitch = std::max(_pitch - kKeyTurn, -kMaxPitch);
        break;
    case '=':
    case '+':
        _zoom = std::max(_zoom * 0.8f, 0.05f);
        break;
    case '-':
        _zoom = std::min(_zoom * 1.25f, 20.0f);
        break;
    case 'r':
        resetView();
        break;
    case key::Space:
        _spin = !_spin;
        break;
    case 'u':
        _uvCheck = !_uvCheck;
        break;
    case 'c':
        _colourByTail = !_colourByTail;
        break;
    case 't':
        _textured = !_textured;
        break;
    case 'g':
        _alphaMode = (_alphaMode + 1) % 3;
        break;
    case 'l':
        _outlines = (_outlines + 1) % 3;
        break;
    case 'b':
        _faces = (_faces + 1) % 3;
        break;
    case 'm':
        _markers = !_markers;
        break;
    case 'k':
        // The animations in turn, then the turret sweep for a model with
        // a turret, then still again.
        _animation = _animation + 1 >= int(_animations.size()) + (_turret ? 1 : 0) ? -1 : _animation + 1;
        _animFrame = 0;
        break;
    case 'n':
        _selectedPoint = _selectedPoint + 1 >= int(_model.attachments.size()) ? -1 : _selectedPoint + 1;
        break;
    case 'h':
        _smooth = !_smooth;
        break;
    case 'p':
        if (!_model.parts.empty())
            _soloPart = _soloPart + 1 >= int(_model.parts.size()) ? -1 : _soloPart + 1;
        break;
    default:
        if (n == 0)
            return;
        switch (ev.key) {
        case key::Right:
            show((_current + 1) % n);
            return;
        case key::Left:
            show((_current + n - 1) % n);
            return;
        case key::PageDown:
            show(std::min(_current + 10, n - 1));
            return;
        case key::PageUp:
            show(std::max(_current - 10, 0));
            return;
        case key::Home:
            show(0);
            return;
        case key::End:
            show(n - 1);
            return;
        default:
            return;
        }
    }
    _dirty = true;
    updateTitle();
}

void ModelViewer::update(uint64_t nowMs) {
    if (_spin && _lastMs) {
        _yaw += float(nowMs - _lastMs) * kSpinPerMs;
        _dirty = true;
    }
    if (_animation >= 0 && nowMs >= _animMs + kFrameMs) {
        _animMs = nowMs;
        ++_animFrame;
        _dirty = true;
        updateTitle();
    }
    _lastMs = nowMs;
    if (_dirty) {
        render();
        _dirty = false;
    }
}

void ModelViewer::updateTitle() {
    std::string title = std::string("New Worlds models [") + kFilterNames[_filter] + "] ";
    if (_visible.empty()) {
        _sys.setWindowTitle(title + _status + " (Tab: textures)");
        return;
    }
    const EntryRef &ref = _models[_visible[size_t(_current)]];
    title += std::to_string(_current + 1) + "/" + std::to_string(_visible.size()) + ": " +
             _archives[ref.archive]->entries()[ref.entry].name;
    if (!_status.empty()) {
        _sys.setWindowTitle(title + " (" + _status + ")");
        return;
    }
    title += " (" + std::to_string(_model.faces.size()) + " faces, " + std::to_string(_model.parts.size()) +
             (_model.parts.size() == 1 ? " part" : " parts");
    if (_soloPart >= 0)
        title += "; showing part " + std::to_string(_soloPart);
    if (std::all_of(_modelTextureNames.begin(), _modelTextureNames.end(),
                    [](const std::string &n) { return n == "?"; })) {
        title += "; no textures found";
    } else {
        title += "; textures";
        for (size_t i = 0; i < _modelTextureNames.size(); ++i)
            title += (i ? ", " : " ") + _modelTextureNames[i];
        if (!_textured)
            title += " (off)";
    }
    title += ")";
    if (_hasMdl) {
        title += " [" + _mdl.title;
        for (size_t i = 0; i < _mdl.states.size(); ++i)
            title += (i ? "/" : ": ") + (_mdl.states[i].empty() ? std::string("(unnamed)") : _mdl.states[i]);
        title += "; " + std::to_string(_model.attachments.size()) + " points";
        if (_turret && _animation == int(_animations.size())) {
            title += "; turret sweep";
        } else if (_animation >= 0 && size_t(_animation) < _animations.size()) {
            const auto &a = _animations[size_t(_animation)];
            title += "; animation " + a.name + (a.state.empty() ? "" : " (" + a.state + ")") + " frame " +
                     std::to_string(_animFrame % int(a.frames.size()) + 1) + "/" + std::to_string(a.frames.size());
        } else if (!_animations.empty() || _turret) {
            title +=
                "; " + std::to_string(_animations.size()) + " animations" + (_turret ? " and a turret" : "") + " (K)";
        }
        if (_selectedPoint >= 0 && size_t(_selectedPoint) < _model.attachments.size()) {
            std::string name = "#" + std::to_string(_selectedPoint);
            for (const auto &n : _mdl.points)
                if (n.value == _selectedPoint)
                    name = n.name;
            const auto &a = _model.attachments[size_t(_selectedPoint)].position;
            char where[64];
            std::snprintf(where, sizeof where, " (%.0f, %.0f, %.0f)", a.x, a.y, a.z);
            title += ", " + name + where;
        }
        title += "]";
    } else if (!_model.attachments.empty()) {
        title += " [" + std::to_string(_model.attachments.size()) + " points]";
    }
    if (_outlines)
        title += std::string(" [") + kOutlineNames[_outlines] + "]";
    if (_faces)
        title += std::string(" [") + kFaceNames[_faces] + "]";
    if (_colourByTail)
        title += " [colour: texture number]";
    if (!_smooth)
        title += " [flat shading]";
    if (_alphaMode) // glow is the default
        title += std::string(" [") + kAlphaNames[_alphaMode] + "]";
    if (_uvCheck)
        title += " [UV check]";
    _sys.setWindowTitle(title);
}

void ModelViewer::render() {
    const int w = _screen.w;
    const int h = _screen.h;
    for (int y = 0; y < h; ++y) { // dark blue-grey gradient
        uint8_t *row = _screen.row(y);
        const uint8_t v = uint8_t(20 + 30 * y / h);
        for (int x = 0; x < w; ++x, row += 4) {
            row[0] = v;
            row[1] = uint8_t(v + 4);
            row[2] = uint8_t(v + 12);
            row[3] = 255;
        }
    }
    if (!_status.empty() && !_visible.empty()) { // didn't decode: dark red
        for (int y = 0; y < h; ++y) {
            uint8_t *row = _screen.row(y);
            for (int x = 0; x < w; ++x, row += 4)
                row[0] = 80, row[1] = 0, row[2] = 0;
        }
        return;
    }

    // Orbit camera around the model's centre, z up.
    const OrbitCamera cam(Vec{_centre[0], _centre[1], _centre[2]}, _yaw, _pitch, _size * 2.6f * _zoom, w, h);
    const Vec light = normalize(Vec{-0.4f, 0.5f, -0.75f}); // towards the light, in view space

    // The animation's current frame, or the turret sweep: the turret round
    // once in 12 s, the gun rising and falling by 25 degrees.
    Pose poses(_model.parts.size());
    if (_animation >= 0 && size_t(_animation) < _animations.size()) {
        const auto &frames = _animations[size_t(_animation)].frames;
        poses = framePose(_model, _mdl, frames[size_t(_animFrame) % frames.size()]);
    } else if (_turret && _animation == int(_animations.size())) {
        const float t = float(_animFrame) * float(kFrameMs) / 1000;
        poses = turretPose(_model, _mdl, t * 2 * kPi / 12, 0.436f * (0.5f - 0.5f * std::cos(t * 2 * kPi / 6)));
    }
    auto toVec = [](const GeoModel::Vec3 &v) { return Vec{v.x, v.y, v.z}; };
    auto posed = [&](uint32_t part, const GeoModel::Vec3 &v) { return toVec(poses[part].apply(v)); };

    _raster.begin(_screen);
    std::vector<std::vector<ScreenVertex>> outlines;
    std::vector<ViewVertex> poly;
    std::vector<ScreenVertex> projected;
    // With alpha as transparency, faces whose texture has an alpha layer are
    // blended over the rest, so they're drawn in a second pass.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto &face : _model.faces) {
            if (_soloPart >= 0 && int(face.part) != _soloPart)
                continue;
            // The last value of the face's last corner picks its texture.
            const uint32_t textureIndex = face.corners.back().extra[4];
            const Texture *tex = nullptr;
            if (_textured && !_colourByTail && textureIndex < _modelTextures.size() &&
                !_modelTextures[textureIndex].rgba.empty())
                tex = &_modelTextures[textureIndex];
            const bool hasAlpha = tex && !tex->alpha.empty();
            const std::string &texName =
                textureIndex < _modelTextureNames.size() ? _modelTextureNames[textureIndex] : "";
            const bool glowMap = texName.size() > 2 && texName.compare(texName.size() - 2, 2, "_g") == 0;
            const bool blended = hasAlpha && (_alphaMode == 2 || (_alphaMode == 0 && !glowMap));
            if (blended != (pass == 1))
                continue;
            const auto &verts = _model.parts[face.part].vertices;

            // Face normal (Newell's method), in view space.
            Vec normal{0, 0, 0};
            poly.clear();
            for (size_t i = 0; i < face.corners.size(); ++i) {
                const Vec a = posed(face.part, verts[face.corners[i].vertex]);
                const Vec b = posed(face.part, verts[face.corners[(i + 1) % face.corners.size()].vertex]);
                normal.x += (a.y - b.y) * (a.z + b.z);
                normal.y += (a.z - b.z) * (a.x + b.x);
                normal.z += (a.x - b.x) * (a.y + b.y);
                poly.push_back(cam.toView(a, face.corners[i].u, face.corners[i].v));
            }
            normal = normalize(normal);
            // Light levels per corner: from the corner's own normal (its first
            // extra value numbers the part's second normal list) when shading
            // smoothly, else from the face's.
            const auto &normalLists = _model.parts[face.part].normalLists;
            for (size_t i = 0; i < face.corners.size(); ++i) {
                const uint32_t k = face.corners[i].extra[0];
                Vec n = normal;
                if (_smooth && normalLists.size() > 1 && k < normalLists[1].size()) {
                    n = Vec{normalLists[1][k].x, normalLists[1][k].y, normalLists[1][k].z};
                    if (poses[face.part].moved)
                        n = toVec(poses[face.part].turn({n.x, n.y, n.z}));
                }
                const float d = std::fabs(dot(cam.directionToView(n), light));
                poly[i].s = tex ? 0.45f + 0.55f * d : 0.3f + 0.7f * d;
            }

            poly = clipNear(poly, cam.nearZ);
            if (poly.size() < 3)
                continue;
            projected.clear();
            for (const auto &p : poly)
                projected.push_back(cam.project(p));

            // Twice the projected area; positive when clockwise on the screen (y down).
            float area = 0;
            for (size_t i = 0; i < projected.size(); ++i) {
                const auto &a = projected[i];
                const auto &b = projected[(i + 1) % projected.size()];
                area += a.x * b.y - b.x * a.y;
            }
            const bool front = area > 0;
            if ((_faces == 0 && !front) || (_faces == 2 && front))
                continue;

            if (_outlines != 2) {
                // Untextured: colour by part, or by the texture number.
                const uint8_t *base = kPartColours[(_colourByTail ? textureIndex : face.part) % 8];
                const bool check = _uvCheck;
                auto shade = [&](float u, float v, float intensity, uint8_t *out) {
                    float k = intensity;
                    if (check && ((int(std::floor(u * 8)) + int(std::floor(v * 8))) & 1))
                        k *= 0.55f;
                    const uint8_t *c = base;
                    float a = 1;
                    if (tex) { // nearest texel, wrapping
                        int tx = int(std::floor(u * float(tex->width))) % tex->width;
                        int ty = int(std::floor(v * float(tex->height))) % tex->height;
                        tx += tx < 0 ? tex->width : 0;
                        ty += ty < 0 ? tex->height : 0;
                        const size_t texel = size_t(ty) * size_t(tex->width) + size_t(tx);
                        c = &tex->rgba[texel * 4];
                        if (c[3] == 0)
                            return false; // colour key: nothing drawn
                        const float layer = hasAlpha ? float(tex->alpha[texel]) / 255.0f : 1.0f;
                        a = blended ? layer : 1.0f;
                        if (hasAlpha && _alphaMode == 0 && glowMap) // glow: alpha keeps the texel lit
                            k = std::max(k, layer);
                        if (a <= 0)
                            return false;
                    }
                    for (int i = 0; i < 3; ++i)
                        out[i] = uint8_t(std::min(255.0f, c[i] * k * a + float(out[i]) * (1 - a)));
                    out[3] = 255;
                    return a > 0.98f; // only (nearly) opaque pixels hide what's behind
                };
                for (size_t i = 1; i + 1 < projected.size(); ++i)
                    _raster.drawTriangle(projected[0], projected[i], projected[i + 1], shade);
            }
            if (_outlines != 0)
                outlines.push_back(projected);
        }
    for (const auto &o : outlines)
        for (size_t i = 0; i < o.size(); ++i)
            drawLine(o[i].x, o[i].y, o[(i + 1) % o.size()].x, o[(i + 1) % o.size()].y);

    // The model's points, as crosses over everything; the selected one larger.
    if (_markers)
        for (size_t i = 0; i < _model.attachments.size(); ++i) {
            const auto &a = _model.attachments[i].position;
            const ViewVertex v = cam.toView(Vec{a.x, a.y, a.z});
            if (v.z < cam.nearZ)
                continue;
            const ScreenVertex sv = cam.project(v);
            const bool selected = int(i) == _selectedPoint;
            static const uint8_t kPoint[3] = {255, 210, 40};
            static const uint8_t kSelected[3] = {255, 60, 60};
            const float r = selected ? 9.0f : 4.0f;
            for (float d = -1; d <= 1; d += 1) { // three pixels thick
                drawLine(sv.x - r, sv.y + d, sv.x + r, sv.y + d, selected ? kSelected : kPoint);
                drawLine(sv.x + d, sv.y - r, sv.x + d, sv.y + r, selected ? kSelected : kPoint);
            }
        }
}

void ModelViewer::drawLine(float x0, float y0, float x1, float y1, const uint8_t *colour) {
    static const uint8_t kOutline[3] = {240, 240, 200};
    if (!colour)
        colour = kOutline;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const int steps = int(std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
    if (steps > 4 * (_screen.w + _screen.h))
        return; // nearly behind the camera; not worth drawing
    for (int i = 0; i <= steps; ++i) {
        const float t = steps ? float(i) / float(steps) : 0;
        const int x = int(x0 + dx * t);
        const int y = int(y0 + dy * t);
        if (x < 0 || y < 0 || x >= _screen.w || y >= _screen.h)
            continue;
        uint8_t *p = _screen.row(y) + x * 4;
        p[0] = colour[0];
        p[1] = colour[1];
        p[2] = colour[2];
    }
}

} // namespace reluva::newworlds
