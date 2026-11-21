// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Drawing the game's models in the data viewer's 3D pages.

#include "engines/legoisland/pages/scene.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::legoisland {

namespace {

gfx::Mat4 translation(const std::array<float, 4> &v) {
    gfx::Mat4 m = gfx::identity();
    m[12] = v[0];
    m[13] = v[1];
    m[14] = v[2];
    return m;
}

gfx::Mat4 scaling(const std::array<float, 4> &v) {
    gfx::Mat4 m = gfx::identity();
    m[0] = v[0];
    m[5] = v[1];
    m[10] = v[2];
    return m;
}

// A quaternion (w, x, y, z) as the matrix that turns a point by it.
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

// The way from quaternion a to b, `f` of it (0-1), the shorter way round.
std::array<float, 4> slerp(const std::array<float, 4> &a, std::array<float, 4> b, float f) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        for (float &v : b)
            v = -v;
        dot = -dot;
    }
    float wa = 1 - f;
    float wb = f;
    if (dot < 0.9995f) {
        const float angle = std::acos(dot);
        const float s = std::sin(angle);
        wa = std::sin((1 - f) * angle) / s;
        wb = std::sin(f * angle) / s;
    }
    std::array<float, 4> out{};
    float len = 0;
    for (size_t i = 0; i < 4; i++) {
        out[i] = a[i] * wa + b[i] * wb;
        len += out[i] * out[i];
    }
    len = std::sqrt(len);
    if (len > 0)
        for (float &v : out)
            v /= len;
    return out;
}

// A list of keys' value at `t`: the first's before it, the last's after,
// and between two the way from one to the other.
std::array<float, 4> valueAt(const std::vector<WdbKey> &keys, uint32_t t, bool turn) {
    if (t <= keys.front().time)
        return keys.front().value;
    for (size_t i = 1; i < keys.size(); i++) {
        if (t >= keys[i].time)
            continue;
        const WdbKey &a = keys[i - 1];
        const WdbKey &b = keys[i];
        const float f = float(t - a.time) / float(b.time - a.time);
        if (turn)
            return slerp(a.value, b.value, f);
        std::array<float, 4> out{};
        for (size_t k = 0; k < 4; k++)
            out[k] = a.value[k] + (b.value[k] - a.value[k]) * f;
        return out;
    }
    return keys.back().value;
}

void place(const WdbAnimNode &node, const gfx::Mat4 &parent, uint32_t t, NodePlaces &out) {
    gfx::Mat4 m = parent;
    if (!node.translation.empty())
        m = gfx::multiply(m, translation(valueAt(node.translation, t, false)));
    if (!node.rotation.empty())
        m = gfx::multiply(m, rotation(valueAt(node.rotation, t, true)));
    if (!node.scale.empty())
        m = gfx::multiply(m, scaling(valueAt(node.scale, t, false)));
    const std::string key = fs::toLower(node.name);
    out.places[key] = m;
    // Shown or hidden by its last showing key so far, shown before any.
    for (const WdbKey &k : node.morph)
        if (k.time <= t)
            out.shown[key] = k.value[0] != 0;
    for (const WdbAnimNode &child : node.children)
        place(child, m, t, out);
}

} // namespace

std::array<float, 4> keyValue(const std::vector<WdbKey> &keys, uint32_t timeMs, bool turn) {
    return keys.empty() ? std::array<float, 4>{} : valueAt(keys, timeMs, turn);
}

const WdbAnimNode *findNode(const WdbAnimNode &root, const std::string &name) {
    if (fs::toLower(root.name) == fs::toLower(name))
        return &root;
    for (const WdbAnimNode &child : root.children)
        if (const WdbAnimNode *found = findNode(child, name))
            return found;
    return nullptr;
}

const gfx::Mat4 *NodePlaces::find(const std::string &name) const {
    const auto it = places.find(fs::toLower(name));
    return it != places.end() ? &it->second : nullptr;
}

bool NodePlaces::isShown(const std::string &name) const {
    const auto it = shown.find(fs::toLower(name));
    return it == shown.end() || it->second;
}

void placeNodes(const WdbAnimNode &root, uint32_t timeMs, NodePlaces &out) {
    out.places.clear();
    out.shown.clear();
    place(root, gfx::identity(), timeMs, out);
}

gfx::Mat4 mirrorZ() {
    gfx::Mat4 m = gfx::identity();
    m[10] = -1;
    return m;
}

WdbLibrary::WdbLibrary(const WdbFile &wdb) : _wdb(wdb) {
    for (const WdbPartRef &ref : wdb.allParts()) {
        WdbPart part;
        if (!wdb.readPart(ref, part))
            _parts[fs::toLower(ref.name)] = std::move(part);
    }
    for (const WdbModelRef &ref : wdb.allModels())
        _models.emplace(fs::toLower(ref.name), ref);
}

const WdbPart *WdbLibrary::part(const std::string &name) const {
    const auto it = _parts.find(fs::toLower(name));
    return it != _parts.end() ? &it->second : nullptr;
}

const WdbRoi *WdbLibrary::sharedRoi(const std::string &name) const {
    const std::string key = fs::toLower(name);
    for (const WdbRoi &roi : _wdb.sharedPart().rois)
        if (fs::toLower(roi.name) == key)
            return &roi;
    return nullptr;
}

bool WdbLibrary::model(const std::string &name, WdbModel &out) const {
    const auto it = _models.find(fs::toLower(name));
    return it != _models.end() && !_wdb.readModel(it->second, out);
}

const std::vector<WdbLod> *WdbLibrary::lodsOf(const WdbRoi &roi, TextureLists &textures) const {
    if (!roi.usesPart)
        return &roi.lods;
    const WdbPart *p = part(roi.partName());
    if (!p || p->rois.empty())
        return nullptr;
    textures.push_back(&p->textures);
    return &p->rois[0].lods;
}

void addModelRois(RoiScene &scene, const WdbLibrary &lib, const WdbModel &model, int lessDetail,
                  const TextureLists &extraTextures, std::vector<std::pair<size_t, const WdbRoi *>> &out) {
    struct Walk {
        RoiScene &scene;
        const WdbLibrary &lib;
        const WdbModel &model;
        int lessDetail;
        const TextureLists &extra;
        std::vector<std::pair<size_t, const WdbRoi *>> &out;
        void roi(const WdbRoi &r) {
            TextureLists lists = {&model.textures};
            const std::vector<WdbLod> *lods = lib.lodsOf(r, lists);
            lists.insert(lists.end(), extra.begin(), extra.end());
            lists.push_back(&lib.file().sharedPart().textures);
            lists.push_back(&lib.file().sharedTextures());
            if (!lods) {
                scene.addMissing("part " + r.partName());
            } else if (const size_t piece = scene.add(*lods, lessDetail, lists); piece != RoiScene::kNone) {
                out.emplace_back(piece, &r);
            }
            for (const WdbRoi &c : r.children)
                roi(c);
        }
    } walk{scene, lib, model, lessDetail, extraTextures, out};
    walk.roi(model.roi);
}

void Orbit::frame(const gfx::Vec3 &lo, const gfx::Vec3 &hi, bool any) {
    if (any) {
        center = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
        const float dx = hi[0] - lo[0];
        const float dy = hi[1] - lo[1];
        const float dz = hi[2] - lo[2];
        radius = std::max(0.05f, std::sqrt(dx * dx + dy * dy + dz * dz) / 2);
    } else {
        center = {0, 0, 0};
        radius = 1;
    }
    yaw = 0.6f;
    pitch = 0.35f;
    distance = radius * 2.6f;
}

bool Orbit::handle(const Event &ev) {
    if (ev.type == EventType::kKeyDown && (ev.key == '+' || ev.key == '=')) {
        distance = std::max(radius * 0.1f, distance * 0.9f);
    } else if (ev.type == EventType::kKeyDown && ev.key == '-') {
        distance /= 0.9f;
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        dragging = true;
        lastX = ev.mouseX;
        lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        dragging = false;
    } else if (ev.type == EventType::kMouseMove && dragging) {
        yaw -= float(ev.mouseX - lastX) * 0.01f;
        pitch = std::clamp(pitch + float(ev.mouseY - lastY) * 0.01f, -1.5f, 1.5f);
        lastX = ev.mouseX;
        lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        distance = std::max(radius * 0.1f, distance * std::pow(0.9f, float(ev.wheelY)));
    } else {
        return false;
    }
    return true;
}

gfx::Mat4 Orbit::viewProj(int width, int height, gfx::Vec3 &eye) const {
    const gfx::Vec3 offset{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    eye = {center[0] + offset[0] * distance, center[1] + offset[1] * distance, center[2] + offset[2] * distance};
    return gfx::multiply(
        gfx::perspective(0.8f, float(width) / float(height), std::max(0.01f, radius * 0.01f), radius * 60 + distance),
        gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
}

bool RoiScene::grow(size_t piece, const gfx::Mat4 &transform, gfx::Vec3 &lo, gfx::Vec3 &hi) const {
    if (piece >= _bounds.size() || _bounds[piece][0][0] > _bounds[piece][1][0])
        return false;
    const gfx::Mat4 m = gfx::multiply(mirrorZ(), transform);
    const auto &[a, b] = _bounds[piece];
    for (int corner = 0; corner < 8; corner++) {
        const gfx::Vec3 p =
            gfx::transformPoint(m, {corner & 1 ? b[0] : a[0], corner & 2 ? b[1] : a[1], corner & 4 ? b[2] : a[2]});
        for (size_t k = 0; k < 3; k++) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }
    return true;
}

void RoiScene::clear() {
    for (const std::vector<Mesh> &piece : _pieces)
        for (const Mesh &m : piece)
            _renderer.destroyMesh(m.mesh);
    for (const auto &[name, id] : _textures)
        if (id)
            _renderer.destroyTexture(id);
    _pieces.clear();
    _bounds.clear();
    _textures.clear();
    _queued.clear();
    _missing.clear();
    _lodLevels = 1;
    _triangles = 0;
}

void RoiScene::addMissing(const std::string &what) {
    if (std::find(_missing.begin(), _missing.end(), what) == _missing.end())
        _missing.push_back(what);
}

size_t RoiScene::add(const std::vector<WdbLod> &lods, int lessDetail, const TextureLists &textures) {
    if (lods.empty())
        return kNone;
    _lodLevels = std::max(_lodLevels, int(lods.size()));
    const WdbLod &lod = lods[lods.size() - 1 - size_t(std::min(lessDetail, int(lods.size()) - 1))];
    std::vector<Mesh> piece;
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
        const std::vector<uint32_t> indices(mesh.triangles.begin(), mesh.triangles.end());
        Mesh m;
        if (_renderer.createMesh(vertices, indices, m.mesh))
            continue;
        m.indices = uint32_t(indices.size());
        m.texture = mesh.texture.empty() ? 0 : texture(mesh.texture, textures);
        // The transparency is one minus an alpha (formats/wdb.md).
        m.color = {mesh.color[0] / 255.0f, mesh.color[1] / 255.0f, mesh.color[2] / 255.0f,
                   1 - std::clamp(mesh.transparency, 0.0f, 1.0f)};
        m.translucent = mesh.transparency > 0;
        _triangles += indices.size() / 3;
        piece.push_back(m);
    }
    _pieces.push_back(std::move(piece));
    std::array<gfx::Vec3, 2> box = {gfx::Vec3{1e30f, 1e30f, 1e30f}, gfx::Vec3{-1e30f, -1e30f, -1e30f}};
    for (const auto &v : lod.vertices)
        for (size_t k = 0; k < 3; k++) {
            box[0][k] = std::min(box[0][k], v[k]);
            box[1][k] = std::max(box[1][k], v[k]);
        }
    _bounds.push_back(box);
    return _pieces.size() - 1;
}

gfx::TextureId RoiScene::texture(const std::string &name, const TextureLists &lists) {
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
            _renderer.createTexture(int(t.images[0].width), int(t.images[0].height), rgba.data(), {}, id);
            break;
        }
        if (id)
            break;
    }
    if (!id)
        addMissing("texture " + name);
    _textures[key] = id;
    return id;
}

void RoiScene::put(size_t piece, const gfx::Mat4 &transform) {
    if (piece < _pieces.size())
        _queued.emplace_back(piece, gfx::multiply(mirrorZ(), transform));
}

void RoiScene::flush() {
    for (const bool translucent : {false, true})
        for (const auto &[piece, transform] : _queued)
            for (const Mesh &m : _pieces[piece]) {
                if (m.translucent != translucent)
                    continue;
                gfx::DrawCall call;
                call.mesh = m.mesh;
                call.indexCount = m.indices;
                call.model = transform;
                call.texture = m.texture;
                // A texture is drawn as it is, lit; a colour too.
                call.color = m.texture ? std::array<float, 4>{1, 1, 1, m.color[3]} : m.color;
                call.rgbSource = gfx::ColorSource::kLighting;
                call.ambientLight = {0.45f, 0.45f, 0.45f};
                call.directedLight = {0.6f, 0.6f, 0.6f};
                call.lightDirection = {0.4f, 0.8f, 0.45f};
                // Mirrored, the faces' winding is the other way round; both
                // sides are drawn rather than rely on it.
                call.state.cull = gfx::Cull::kNone;
                if (translucent) {
                    call.state.src = gfx::Blend::kSrcAlpha;
                    call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
                    call.state.depthWrite = false;
                }
                _renderer.draw(call);
            }
    _queued.clear();
}

} // namespace reluva::legoisland
