// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Models page: each .FIN model drawn with the GPU renderer, as the
// scene graph places its shapes, with their textures, lightmaps, vertex
// colours, materials and alpha; animated by the game's keys (nodes, skins,
// morphs, materials, flip-book textures), with its glows.

#include "engines/netimmerse/pages/viewers.h"
#include "engines/netimmerse/formats/data.h"

#include "graphics/font.h"
#include "common/fs.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace reluva::netimmerse {

namespace {

// The properties in effect for a shape: its own, or the nearest
// ancestor's, by class.
struct Inherited {
    uint32_t texture = 0, multiTexture = 0, material = 0, alpha = 0, vertexColor = 0, zBuffer = 0;
};

// An object's placement from its parts: translation, then rotation (row by
// row, applied to column vectors), then scale.
gfx::Mat4 placement(const std::array<float, 3> &t, const std::array<float, 9> &r, float scale) {
    gfx::Mat4 m = gfx::identity();
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            m[size_t(col * 4 + row)] = r[size_t(row * 3 + col)] * scale;
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}

// A quaternion (w, x, y, z) as a rotation, row by row.
std::array<float, 9> rotationOf(const std::array<float, 4> &q) {
    const auto [w, x, y, z] = q;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
            2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
            2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
}

// Angles about X, Y and Z as one rotation, row by row: Z × Y × X, as the
// game turns the Hand Cart Worker's head (checked in its memory).
std::array<float, 9> rotationOf(float ax, float ay, float az) {
    const float cx = std::cos(ax);
    const float sx = std::sin(ax);
    const float cy = std::cos(ay);
    const float sy = std::sin(ay);
    const float cz = std::cos(az);
    const float sz = std::sin(az);
    return {cz * cy,
            cz * sy * sx - sz * cx,
            cz * sy * cx + sz * sx,
            sz * cy,
            sz * sy * sx + cz * cx,
            sz * sy * cx - cz * sx,
            -sy,
            cy * sx,
            cy * cx};
}

// Spherical interpolation from a to b; by the shorter way round unless
// `shortest` is false.
std::array<float, 4> slerp(const std::array<float, 4> &a, std::array<float, 4> b, float f, bool shortest = true) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (shortest && dot < 0) {
        dot = -dot;
        for (float &c : b)
            c = -c;
    }
    dot = std::max(dot, -0.9995f);
    float wa = 1 - f;
    float wb = f;
    if (dot < 0.9995f) {
        const float angle = std::acos(dot);
        const float sinAngle = std::sin(angle);
        wa = std::sin((1 - f) * angle) / sinAngle;
        wb = std::sin(f * angle) / sinAngle;
    }
    std::array<float, 4> q{};
    float len = 0;
    for (size_t i = 0; i < 4; ++i) {
        q[i] = a[i] * wa + b[i] * wb;
        len += q[i] * q[i];
    }
    len = std::sqrt(len);
    for (float &c : q)
        c /= len > 0 ? len : 1;
    return q;
}

// The value keyed at `t`: `mix` of the keys either side and how far from
// one to the other (0..1); before the first key or after the last, theirs.
template<typename Key, typename Mix>
auto sample(const std::vector<Key> &keys, float t, Mix mix) {
    if (t <= keys.front().time)
        return keys.front().value;
    if (t >= keys.back().time)
        return keys.back().value;
    auto b = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key &k) { return v < k.time; });
    auto a = b - 1;
    const float u = b->time > a->time ? (t - a->time) / (b->time - a->time) : 0;
    return mix(*a, *b, u);
}

const auto kMixFloat = [](float a, float b, float f) { return a + (b - a) * f; };
const auto kMix3 = [](const std::array<float, 3> &a, const std::array<float, 3> &b, float f) {
    return std::array<float, 3>{a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
};
// Keys whose value is all there is, blended straight.
const auto kStraight3 = [](const auto &a, const auto &b, float u) { return kMix3(a.value, b.value, u); };

// The curves between keys (docs/engines/netimmerse/games/legoalphateam/formats/fin.md): a position
// by its key's cubic, a rotation by squad through the control quaternions,
// a number by Hermite's with the tangents out of one key and into the
// next. Straight keys blend straight.
std::array<float, 3> positionCurve(const FinActor::PositionKey &a, const FinActor::PositionKey &b, float u) {
    if (!a.curve)
        return kMix3(a.value, b.value, u);
    std::array<float, 3> p{};
    for (size_t k = 0; k < 3; ++k)
        p[k] = a.value[k] + u * (a.out[k] + u * (a.a[k] + u * a.b[k]));
    return p;
}

std::array<float, 4> rotationCurve(const FinActor::RotationKey &a, const FinActor::RotationKey &b, float u) {
    if (!a.curve || !b.curve)
        return slerp(a.value, b.value, u);
    // As the game does it (checked against its bones in memory): none of
    // squad's slerps takes the shorter way round.
    return slerp(slerp(a.value, b.value, u, false), slerp(a.out, b.in, u, false), 2 * u * (1 - u), false);
}

float numberCurve(const FinActor::FloatKey &a, const FinActor::FloatKey &b, float u) {
    if (!a.curve || !b.curve)
        return kMixFloat(a.value, b.value, u);
    const float u2 = u * u;
    const float u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * a.value + (u3 - 2 * u2 + u) * a.out + (-2 * u3 + 3 * u2) * b.value +
           (u3 - u2) * b.in;
}

// A node's placement at `t` with its track's keys in place of what was
// saved; and whether its visibility keys show it then.
gfx::Mat4 animated(const FinNode &n, const FinActor::Track &track, float t, bool &visible) {
    std::array<float, 3> translation = n.translation;
    std::array<float, 9> rotation = n.rotation;
    float scale = n.scale;
    if (!track.positions.empty())
        translation = sample(track.positions, t, positionCurve);
    if (!track.rotations.empty()) {
        rotation = rotationOf(sample(track.rotations, t, rotationCurve));
    } else if (!track.euler[0].empty() || !track.euler[1].empty() || !track.euler[2].empty()) {
        float angles[3] = {};
        for (size_t i = 0; i < 3; ++i)
            if (!track.euler[i].empty())
                angles[i] = sample(track.euler[i], t, numberCurve);
        rotation = rotationOf(angles[0], angles[1], angles[2]);
    }
    if (!track.scales.empty())
        scale = sample(track.scales, t, numberCurve);
    if (!track.visible.empty())
        visible = sample(track.visible, t, [](const auto &a, const auto &, float) { return a.value; });
    return placement(translation, rotation, scale);
}

// A shape's vertices for the renderer, at `pos` with `nrm` (either may
// be the shape's own).
std::vector<gfx::Vertex> vertexData(const FinShape &s, const std::vector<float> &pos, const std::vector<float> &nrm) {
    const size_t nv = s.vertexCount;
    const size_t lightSet = s.uvSets ? s.uvSets - 1u : 0;
    std::vector<gfx::Vertex> verts(nv);
    for (size_t v = 0; v < nv; ++v) {
        gfx::Vertex &out = verts[v];
        for (size_t k = 0; k < 3; ++k)
            out.pos[k] = v * 3 + k < pos.size() ? pos[v * 3 + k] : 0;
        if (nrm.size() >= nv * 3)
            for (size_t k = 0; k < 3; ++k)
                out.normal[k] = nrm[v * 3 + k];
        if (s.uvSets && s.uvw.size() >= (lightSet + 1) * nv * 3) {
            out.uv[0] = s.uvw[v * 3];
            out.uv[1] = s.uvw[v * 3 + 1];
            out.uv2[0] = s.uvw[(lightSet * nv + v) * 3];
            out.uv2[1] = s.uvw[(lightSet * nv + v) * 3 + 1];
        }
        for (size_t k = 0; k < 4; ++k)
            out.color[k] =
                s.colors.size() < nv * 4 ? 255 : uint8_t(std::clamp(s.colors[v * 4 + k], 0.0f, 1.0f) * 255 + 0.5f);
    }
    return verts;
}

// Normals worked out from the triangles, for vertices that have moved.
std::vector<float> normalsOf(const std::vector<float> &pos, const std::vector<uint16_t> &triangles) {
    std::vector<float> nrm(pos.size(), 0.0f);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const float *a = &pos[size_t(triangles[t]) * 3];
        const float *b = &pos[size_t(triangles[t + 1]) * 3];
        const float *c = &pos[size_t(triangles[t + 2]) * 3];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                            e1[0] * e2[1] - e1[1] * e2[0]};
        for (size_t j = 0; j < 3; ++j)
            for (size_t k = 0; k < 3; ++k)
                nrm[size_t(triangles[t + j]) * 3 + k] += n[k];
    }
    for (size_t v = 0; v + 2 < nrm.size(); v += 3) {
        float *n = &nrm[v];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0)
            n[0] /= len, n[1] /= len, n[2] /= len;
    }
    return nrm;
}

// NetImmerse's blend factors (NiAlphaProperty), as the renderer's.
gfx::Blend blendOf(uint32_t f) {
    switch (f) {
    case 0:
        return gfx::Blend::kOne;
    case 1:
        return gfx::Blend::kZero;
    case 2:
        return gfx::Blend::kSrcColor;
    case 3:
        return gfx::Blend::kOneMinusSrcColor;
    case 4:
        return gfx::Blend::kDstColor;
    case 5:
        return gfx::Blend::kOneMinusDstColor;
    case 6:
        return gfx::Blend::kSrcAlpha;
    case 7:
        return gfx::Blend::kOneMinusSrcAlpha;
    case 8:
        return gfx::Blend::kDstAlpha;
    case 9:
        return gfx::Blend::kOneMinusDstAlpha;
    default:
        return gfx::Blend::kOne;
    }
}

// An object's placement: translation, then rotation (stored row by row,
// applied to column vectors), then scale.
gfx::Mat4 localMatrix(const FinAvObject &o) {
    return placement(o.translation, o.rotation, o.scale);
}

constexpr int kFrameW = 640, kFrameH = 480;

// A billboard node's placement turned so its +Z axis points at the camera,
// with +Y as near the world's up (+Z) as that allows; its position and
// scale kept.
gfx::Mat4 facingCamera(const gfx::Mat4 &at, const gfx::Vec3 &eye) {
    const gfx::Vec3 origin{at[12], at[13], at[14]};
    const float scale = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    gfx::Vec3 z{eye[0] - origin[0], eye[1] - origin[1], eye[2] - origin[2]};
    float len = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (len < 1e-6f)
        return at;
    for (float &c : z)
        c /= len;
    // x = up x z, unless z is (nearly) straight up or down.
    gfx::Vec3 x{-z[1], z[0], 0};
    len = std::sqrt(x[0] * x[0] + x[1] * x[1]);
    if (len < 1e-4f)
        x = {1, 0, 0};
    else
        x = {x[0] / len, x[1] / len, 0};
    const gfx::Vec3 y{z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    gfx::Mat4 m = gfx::identity();
    for (size_t r = 0; r < 3; ++r) {
        m[0 + r] = x[r] * scale;
        m[4 + r] = y[r] * scale;
        m[8 + r] = z[r] * scale;
    }
    m[12] = origin[0];
    m[13] = origin[1];
    m[14] = origin[2];
    return m;
}

} // namespace

struct ModelPage::Item {
    enum class Kind : uint8_t { kShape, kSkin, kMorph, kGlow, kLight } kind = Kind::kShape;
    uint32_t id = 0; // the shape's (or light's)
    gfx::MeshId mesh = 0;
    uint32_t indexCount = 0;
    bool visible = true; // reached by the last walk
    gfx::Mat4 world = gfx::identity();
    std::vector<std::string> frames; // the texture's images: one, or a flip-book's
    const FinFlip *flip = nullptr;
    mutable size_t image = 0; // the flip-book's, kept while a clip doesn't move it
    std::string lightmap;
    uint32_t material = 0;
    std::array<float, 4> baseColor{1, 1, 1, 1}, color{1, 1, 1, 1};
    bool vertexColors = false, lit = false;
    bool blend = false, additive = false;
    bool backdrop = false; // under a level's "Exterior" node: its sky
    // Under an NiBillboardNode (or a glow, itself one): drawn at billboardAt
    // turned to face the camera, then `rel` (the placement below it).
    bool billboard = false;
    gfx::Mat4 billboardAt = gfx::identity(), rel = gfx::identity();
    gfx::Blend src = gfx::Blend::kOne, dst = gfx::Blend::kZero;
    // Depth test and write: on, unless an NiZBufferProperty turns them off.
    bool depthTest = true, depthWrite = true;
    gfx::Vec3 localCentre{}, centre{}; // of its vertices; in the world, for sorting what blends
};

ModelPage::ModelPage(ViewerContext &ctx) : Page(ctx) {
    auto fin = ctx.archives.find("Fin.pac");
    if (fin == ctx.archives.end())
        return;
    for (const PacArchive::Entry &e : fin->second->entries())
        if (e.size)
            _names.push_back(e.name);
}

ModelPage::~ModelPage() {
    deactivate();
}

void ModelPage::clearItems() {
    if (_renderer) {
        for (const Item &item : _items)
            if (item.mesh)
                _renderer->destroyMesh(item.mesh);
        if (_footprint)
            _renderer->destroyMesh(_footprint);
    }
    _items.clear();
    _itemOf.clear();
    _footprint = 0;
    _footprintCount = 0;
    stopClipSound();
}

void ModelPage::stopClipSound() {
    if (_ctx.mixer && _clipSound != audio::kNoSound)
        _ctx.mixer->stop(_clipSound);
    _clipSound = audio::kNoSound;
    _soundClip = -1;
    _soundPass = -1;
}

void ModelPage::clipSound() {
    if (!_ctx.mixer || _clips.empty())
        return;
    const FinActor::Clip &c = _clips[size_t(_clip)];
    const float length = c.end - c.start;
    const long pass = length > 0 ? long(_clipTime / length) : 0;
    if (_clip == _soundClip && (pass == _soundPass || c.loop))
        return;
    stopClipSound();
    _soundClip = _clip;
    _soundPass = pass;
    if (c.sound.empty() || !_playing)
        return;
    auto it = _sounds.find(c.sound);
    if (it == _sounds.end()) {
        std::shared_ptr<audio::Sound> sound = std::make_shared<audio::Sound>();
        std::vector<uint8_t> data;
        if (_ctx.files.read(c.sound + ".DDS", data) || decodeDds(data, *sound))
            sound.reset();
        it = _sounds.emplace(c.sound, sound).first;
    }
    if (it->second)
        _clipSound = _ctx.mixer->play(it->second, audio::SoundType::kEffects,
                                      int(std::clamp(c.volume, 0.0f, 1.0f) * audio::kMaxVolume), 0, c.loop);
}

void ModelPage::deactivate() {
    // The renderer's textures and meshes go with it when another page
    // takes the screen.
    clearItems();
    if (_renderer) {
        for (const auto &[name, id] : _textures)
            if (id)
                _renderer->destroyTexture(id);
        if (_glow)
            _renderer->destroyTexture(_glow);
    }
    _textures.clear();
    _glow = 0;
    _renderer = nullptr;
}

void ModelPage::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        use2D();
        clear(16, 18, 24);
        text(8, 8, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(8, 8 + font::kLineHeight, err.message);
        _ctx.sys.updateScreen(_ctx.screen);
        setTitle("Models: 3D rendering not available");
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    show(_index);
}

gfx::TextureId ModelPage::texture(const std::string &name) {
    const std::string key = fs::toLower(name);
    if (auto it = _textures.find(key); it != _textures.end())
        return it->second;
    gfx::TextureId id = 0;
    std::vector<uint8_t> data;
    image::Image img;
    if (!_ctx.files.read(name, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        if (_renderer->createTexture(img.w, img.h, img.rgba.data(), params, id))
            id = 0;
    }
    _textures[key] = id;
    return id;
}

// A soft white spot, for the glows without an image of their own and the
// lights' markers.
gfx::TextureId ModelPage::glowTexture() {
    if (_glow)
        return _glow;
    constexpr int kSize = 32;
    std::vector<uint8_t> rgba(kSize * kSize * 4, 255);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float dx = (float(x) + 0.5f) / kSize * 2 - 1;
            const float dy = (float(y) + 0.5f) / kSize * 2 - 1;
            const float a = std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            rgba[size_t(y * kSize + x) * 4 + 3] = uint8_t(a * a * 255);
        }
    if (_renderer->createTexture(kSize, kSize, rgba.data(), gfx::TextureParams{}, _glow))
        _glow = 0;
    return _glow;
}

bool ModelPage::clipMoves(const std::vector<uint16_t> FinActor::Clip::*list, size_t number) const {
    if (_clip == 0 || _clips.empty())
        return true;
    const std::vector<uint16_t> &l = _clips[size_t(_clip)].*list;
    return std::find(l.begin(), l.end(), number) != l.end();
}

float ModelPage::clipTicks() const {
    if (_clips.empty())
        return 0;
    const FinActor::Clip &c = _clips[size_t(_clip)];
    const float length = c.end - c.start;
    return (c.start + (length > 0 ? std::fmod(_clipTime, length) : 0)) * kTicksPerSecond;
}

void ModelPage::show(int index) {
    const int n = int(_names.size());
    _index = ((index % n) + n) % n;
    clearItems();
    _info.clear();
    _error.clear();
    _actor = nullptr;
    _trackOf.clear();
    _animatorsOf.clear();
    _flipOf.clear();
    _flipNumber.clear();
    _morphNumber.clear();
    _heldPose.clear();
    _heldWeights.clear();
    _clips.clear();
    _clip = 0;
    _clipTime = 0;
    const std::string &name = _names[size_t(_index)];
    setTitle("Models: " + name + " (" + std::to_string(_index + 1) + "/" + std::to_string(n) + ")");

    std::vector<uint8_t> data;
    Error err = _ctx.archives.at("Fin.pac")->read(name, data);
    if (!err)
        err = _model.parse(data);
    if (err) {
        _error = name + ": " + err.message;
        _model = FinFile();
        return;
    }

    // Animation: the actor's tracks go to the 3ds nodes in depth-first
    // order; the materials' animators and the textures' flip-books by what
    // they target. The whole timeline is the first clip, then the actor's.
    float lastKey = 0;
    if (!_model.actors.empty()) {
        _actor = &_model.actors.begin()->second;
        const std::vector<uint32_t> nodes = _model.animatedNodes();
        if (nodes.size() == _actor->tracks.size())
            for (size_t i = 0; i < nodes.size(); ++i) {
                _trackOf[nodes[i]] = i;
                const FinActor::Track &t = _actor->tracks[i];
                for (float k :
                     {t.positions.empty() ? 0 : t.positions.back().time,
                      t.rotations.empty() ? 0 : t.rotations.back().time, t.scales.empty() ? 0 : t.scales.back().time,
                      t.visible.empty() ? 0 : t.visible.back().time})
                    lastKey = std::max(lastKey, k);
                for (const auto &axis : t.euler)
                    if (!axis.empty())
                        lastKey = std::max(lastKey, axis.back().time);
            }
    }
    for (const auto &[id, a] : _model.materialAnimators) {
        _animatorsOf[a.target].push_back(&a);
        if (!a.keys.empty())
            lastKey = std::max(lastKey, a.keys.back().time);
    }
    for (const auto &[id, f] : _model.flips)
        _flipOf[f.target] = &f;
    for (const FinObjectInfo &o : _model.objects())
        if (auto f = _model.flips.find(o.id); o.type == "NiFlipTextures" && f != _model.flips.end())
            _flipNumber[&f->second] = uint16_t(_flipNumber.size());
    for (const FinObjectInfo &o : _model.objects())
        if (o.type == "Ni3dsMorphShape" && _model.morphs.count(o.id))
            _morphNumber[o.id] = uint16_t(_morphNumber.size());
    for (const auto &[id, m] : _model.morphs)
        if (!m.keys.empty())
            lastKey = std::max(lastKey, m.keys.back().time);
    // An actor's clips end before a few stray keys far out (Evil Ogel's go
    // to 381 s, his clips to 74 s): its whole timeline is theirs.
    if (_actor && !_actor->clips.empty()) {
        lastKey = 0;
        for (const FinActor::Clip &c : _actor->clips)
            lastKey = std::max(lastKey, c.end * kTicksPerSecond);
    }
    FinActor::Clip whole;
    whole.name = "whole timeline";
    whole.end = lastKey / kTicksPerSecond;
    _clips.push_back(whole);
    if (_actor)
        for (const FinActor::Clip &c : _actor->clips)
            _clips.push_back(c);

    walk(clipTicks(), true);
    _posedTicks = clipTicks();
    resetView();
    // The footprint: its polygon as a fan of triangles, a little above the
    // floor it's on.
    if (_actor && _actor->footprint.size() >= 3) {
        std::vector<gfx::Vertex> verts(_actor->footprint.size());
        std::vector<uint32_t> fan;
        for (size_t i = 0; i < verts.size(); ++i) {
            for (size_t k = 0; k < 3; ++k)
                verts[i].pos[k] = _actor->footprint[i][k];
            verts[i].normal[2] = 1;
            for (size_t k = 0; k < 4; ++k)
                verts[i].color[k] = 255;
            if (i >= 2)
                fan.insert(fan.end(), {0u, uint32_t(i - 1), uint32_t(i)});
        }
        if (!_renderer->createMesh(verts, fan, _footprint))
            _footprintCount = uint32_t(fan.size());
        else
            _footprint = 0;
    }

    unsigned shapes = 0;
    unsigned triangles = 0;
    unsigned glows = 0;
    unsigned missing = 0;
    std::vector<std::string> missingNames;
    for (const Item &item : _items) {
        if (item.kind == Item::Kind::kGlow)
            ++glows;
        else if (item.kind != Item::Kind::kLight) {
            ++shapes;
            triangles += item.indexCount / 3;
        }
        std::vector<const std::string *> images{&item.lightmap};
        for (const std::string &f : item.frames)
            images.push_back(&f);
        for (const std::string *img : images)
            if (!img->empty() && !texture(*img)) {
                ++missing;
                if (missingNames.size() < 3)
                    missingNames.push_back(*img);
            }
    }
    unsigned unread = 0;
    for (const auto &o : _model.objects())
        unread += !o.read;
    _info.push_back(name + ": " + std::to_string(shapes) + " shapes, " + std::to_string(triangles) + " triangles, " +
                    std::to_string(glows) + " glows, " + std::to_string(_model.lights.size()) + " lights, " +
                    std::to_string(_model.objects().size()) + " objects" +
                    (unread ? " (" + std::to_string(unread) + " not read)" : ""));
    _info.push_back("Animated: " + std::to_string(_trackOf.size()) + " nodes, " + std::to_string(_model.morphs.size()) +
                    " morphs, " + std::to_string(_model.materialAnimators.size()) + " material animators, " +
                    std::to_string(_model.flips.size()) + " flip-books");
    if (missing) {
        std::string m = std::to_string(missing) + " images not found:";
        for (const std::string &name : missingNames)
            m += " " + name;
        _info.push_back(m);
    }
}

void ModelPage::pose(float ticks) {
    walk(ticks, false);
    _posedTicks = ticks;
}

void ModelPage::walk(float ticks, bool create) {
    for (Item &item : _items)
        item.visible = false;
    std::array<float, 3> lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
    std::array<float, 3> hi{-lo[0], -lo[1], -lo[2]};
    if (create)
        _hiddenCount = 0;

    auto applyProperties = [&](const FinAvObject &o, Inherited in) {
        for (uint32_t p : o.properties) {
            const std::string type = _model.typeOf(p);
            if (type == "NiTextureProperty")
                in.texture = p;
            else if (type == "NiMultiTextureProperty")
                in.multiTexture = p;
            else if (type == "NiMaterialProperty")
                in.material = p;
            else if (type == "NiAlphaProperty")
                in.alpha = p;
            else if (type == "NiVertexColorProperty")
                in.vertexColor = p;
            else if (type == "NiZBufferProperty")
                in.zBuffer = p;
        }
        return in;
    };
    struct Billboard {
        bool on = false;
        gfx::Mat4 at = gfx::identity(), rel = gfx::identity();
    };

    // The item for shape `id`: made from `s` and its properties the first
    // time; found again after.
    auto itemFor = [&](uint32_t id, Item::Kind kind, const FinShape &s, const Inherited &in, bool backdrop) -> Item * {
        if (!create) {
            auto it = _itemOf.find(id);
            return it == _itemOf.end() ? nullptr : &_items[it->second];
        }
        Item item;
        item.kind = kind;
        item.id = id;
        item.backdrop = backdrop;
        // Images: the texture's (its first, or a flip-book's frames), the
        // multi-texture's lightmap. The texture uses the first uv set, the
        // lightmap the last.
        if (auto t = _model.textures.find(in.texture); t != _model.textures.end())
            for (uint32_t frame : t->second)
                if (auto img = _model.images.find(frame); img != _model.images.end())
                    item.frames.push_back(img->second);
        if (auto f = _flipOf.find(in.texture); f != _flipOf.end() && item.frames.size() > 1)
            item.flip = f->second;
        else if (item.frames.size() > 1)
            item.frames.resize(1);
        if (auto mt = _model.multiTextures.find(in.multiTexture); mt != _model.multiTextures.end())
            if (auto img = _model.images.find(mt->second); img != _model.images.end())
                item.lightmap = img->second;
        item.material = in.material;
        if (auto mat = _model.materials.find(in.material); mat != _model.materials.end())
            item.baseColor = {mat->second.diffuse[0], mat->second.diffuse[1], mat->second.diffuse[2],
                              mat->second.alpha};
        if (auto al = _model.alphas.find(in.alpha); al != _model.alphas.end() && al->second.blend) {
            item.blend = true;
            item.src = blendOf(al->second.source);
            item.dst = blendOf(al->second.destination);
        }
        if (auto z = _model.zBuffers.find(in.zBuffer); z != _model.zBuffers.end()) {
            item.depthTest = z->second[0] != 0;
            item.depthWrite = z->second[1] != 0;
        }
        item.vertexColors = !s.colors.empty();
        item.lit = !item.vertexColors && !s.normals.empty();
        if (kind == Item::Kind::kGlow) {
            // Added to what's behind, facing the camera.
            item.blend = item.additive = item.billboard = true;
            item.src = gfx::Blend::kSrcAlpha;
            item.dst = gfx::Blend::kOne;
            item.depthWrite = false;
            item.lit = false;
        }
        _itemOf[id] = _items.size();
        _items.push_back(std::move(item));
        return &_items.back();
    };
    // A fixed mesh made once; a skin's or morph's again each time.
    auto setMesh = [&](Item &item, const std::vector<gfx::Vertex> &verts, const std::vector<uint32_t> &indices) {
        if (item.mesh)
            _renderer->destroyMesh(item.mesh);
        item.mesh = 0;
        if (_renderer->createMesh(verts, indices, item.mesh))
            item.mesh = 0;
        item.indexCount = uint32_t(indices.size());
        item.localCentre = {};
        for (const gfx::Vertex &v : verts)
            for (size_t k = 0; k < 3; ++k)
                item.localCentre[k] += v.pos[k] / float(verts.size());
    };
    // Where an item is now; the framing counts what isn't sky or a glow.
    auto place = [&](Item &item, const gfx::Mat4 &world, const Billboard &billboard, const std::vector<float> &local) {
        item.visible = true;
        item.world = world;
        if (item.kind == Item::Kind::kGlow || item.kind == Item::Kind::kLight) {
            item.billboard = true;
            item.billboardAt = world;
            item.rel = gfx::identity();
        } else {
            item.billboard = billboard.on;
            item.billboardAt = billboard.at;
            item.rel = billboard.rel;
        }
        item.centre = gfx::transformPoint(world, item.localCentre);
        if (create && !item.backdrop && item.kind != Item::Kind::kGlow && item.kind != Item::Kind::kLight)
            for (size_t v = 0; v + 2 < local.size(); v += 3) {
                const gfx::Vec3 w = gfx::transformPoint(world, {local[v], local[v + 1], local[v + 2]});
                for (size_t k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], w[k]);
                    hi[k] = std::max(hi[k], w[k]);
                }
            }
    };
    auto indicesOf = [](const FinShape &s) { return std::vector<uint32_t>(s.triangles.begin(), s.triangles.end()); };

    // Every node's place in the world, for the skins' bones; and the skins,
    // placed once all of them are known (they come before their bones).
    std::map<uint32_t, gfx::Mat4> worlds;
    struct PendingSkin {
        uint32_t id;
        const FinSkin *skin;
        gfx::Mat4 parent;
        Inherited in;
        bool backdrop;
    };
    std::vector<PendingSkin> skins;

    // The levels' sky is under a node named "Exterior": it's left out of
    // the view's framing, so the camera starts on the level itself. Hidden
    // objects (effects the game shows on events) are left out unless H
    // shows them; so are the characters' far-away stand-ins: "LOD_..."
    // nodes over a billboard of a flat limb (textures LodBlack.tga...),
    // which the game must switch to itself, as nothing in the file says.
    auto isStandIn = [&](const FinNode &n) {
        return n.name.rfind("LOD_", 0) == 0 && n.children.size() == 1 &&
               _model.typeOf(n.children.front()) == "NiBillboardNode";
    };
    std::function<void(uint32_t, const gfx::Mat4 &, const Inherited &, int, bool, Billboard)> visit =
        [&](uint32_t id, const gfx::Mat4 &parent, const Inherited &inherited, int depth, bool backdrop,
            Billboard billboard) {
            if (depth > 64)
                return;
            if (auto node = _model.nodes.find(id); node != _model.nodes.end()) {
                const FinNode &n = node->second;
                if (n.hidden || isStandIn(n)) {
                    _hiddenCount += create;
                    if (!_showHidden)
                        return;
                }
                bool shown = true;
                const auto track = _trackOf.find(id);
                gfx::Mat4 local;
                if (track == _trackOf.end()) {
                    local = localMatrix(n);
                } else if (clipMoves(&FinActor::Clip::tracks, track->second)) {
                    local = animated(n, _actor->tracks[track->second], ticks, shown);
                    _heldPose[id] = {local, shown};
                } else if (auto held = _heldPose.find(id); held != _heldPose.end()) {
                    local = held->second.local, shown = held->second.shown;
                } else {
                    local = localMatrix(n); // as saved, which is the first key's pose
                }
                if (!shown)
                    return;
                const gfx::Mat4 world = gfx::multiply(parent, local);
                worlds[id] = world;
                if (billboard.on) {
                    billboard.rel = gfx::multiply(billboard.rel, local);
                } else if (_model.typeOf(id) == "NiBillboardNode") {
                    billboard.on = true;
                    billboard.at = world;
                }
                const Inherited in = applyProperties(n, inherited);
                const bool sky = backdrop || n.name == "Exterior";
                const size_t count = n.levelsOfDetail ? std::min<size_t>(1, n.children.size()) : n.children.size();
                for (size_t i = 0; i < count; ++i)
                    visit(n.children[i], world, in, depth + 1, sky, billboard);
                return;
            }
            if (auto skin = _model.skins.find(id); skin != _model.skins.end()) {
                skins.push_back({id, &skin->second, parent, applyProperties(skin->second, inherited), backdrop});
                return;
            }
            if (auto light = _model.lights.find(id); light != _model.lights.end()) {
                const FinLight &l = light->second;
                if (Item *item = itemFor(id, Item::Kind::kLight, FinShape{}, inherited, backdrop)) {
                    if (create) {
                        // A small spot in the light's colour, sized with the view.
                        static const std::vector<uint32_t> kQuad{0, 1, 2, 0, 2, 3};
                        std::vector<gfx::Vertex> verts(4);
                        const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                        for (size_t v = 0; v < 4; ++v) {
                            verts[v].pos[0] = corner[v][0];
                            verts[v].pos[1] = corner[v][1];
                            verts[v].uv[0] = (corner[v][0] + 1) / 2;
                            verts[v].uv[1] = (corner[v][1] + 1) / 2;
                            for (size_t k = 0; k < 4; ++k)
                                verts[v].color[k] = 255;
                        }
                        setMesh(*item, verts, kQuad);
                        item->blend = item->additive = true;
                        item->src = gfx::Blend::kSrcAlpha;
                        item->dst = gfx::Blend::kOne;
                        item->depthWrite = false;
                        item->baseColor = {std::min(1.0f, l.diffuse[0]), std::min(1.0f, l.diffuse[1]),
                                           std::min(1.0f, l.diffuse[2]), 1};
                    }
                    place(*item, gfx::multiply(parent, localMatrix(l)), billboard, {});
                }
                return;
            }
            const FinShape *s = nullptr;
            Item::Kind kind = Item::Kind::kShape;
            if (auto it = _model.shapes.find(id); it != _model.shapes.end()) {
                s = &it->second;
            } else if (auto m = _model.morphs.find(id); m != _model.morphs.end()) {
                s = &m->second;
                kind = Item::Kind::kMorph;
            } else if (auto g = _model.coronas.find(id); g != _model.coronas.end()) {
                s = &g->second;
                kind = Item::Kind::kGlow;
            }
            if (!s || s->vertices.empty() || (s->hidden && !_showHidden))
                return;
            if (kind != Item::Kind::kGlow && s->triangles.empty())
                return;
            if (billboard.on)
                billboard.rel = gfx::multiply(billboard.rel, localMatrix(*s));
            Item *item = itemFor(id, kind, *s, applyProperties(*s, inherited), backdrop);
            if (!item)
                return;
            const std::vector<float> *local = &s->vertices;
            std::vector<float> blended;
            if (kind == Item::Kind::kMorph) {
                // The targets blended by the weights keyed now.
                const FinMorph &m = _model.morphs.at(id);
                if (!m.keys.empty() && !m.targets.empty() && m.keys.front().weights.size() == m.targets.size()) {
                    // Each weight by Hermite's curve, with the key's tangent
                    // out and the next's in (NiBaryMorphKey::MorphInterpolate).
                    // Those the clip doesn't move stay as they were.
                    const auto number = _morphNumber.find(id);
                    const bool moves =
                        number == _morphNumber.end() || clipMoves(&FinActor::Clip::morphs, number->second);
                    std::vector<float> w;
                    if (auto held = _heldWeights.find(id); !moves && held != _heldWeights.end()) {
                        w = held->second;
                    } else {
                        const float t = moves ? ticks : m.keys.front().time;
                        if (t <= m.keys.front().time || m.keys.size() == 1) {
                            w = m.keys.front().weights;
                        } else if (t >= m.keys.back().time) {
                            w = m.keys.back().weights;
                        } else {
                            size_t k = 1;
                            while (m.keys[k].time <= t)
                                ++k;
                            const FinMorph::Key &p = m.keys[k - 1];
                            const FinMorph::Key &q = m.keys[k];
                            const float u = q.time > p.time ? (t - p.time) / (q.time - p.time) : 0;
                            w.resize(p.weights.size());
                            for (size_t i = 0; i < w.size(); ++i) {
                                const float d = q.weights[i] - p.weights[i];
                                const float ca = 3 * d - 2 * p.out[i] - q.in[i];
                                const float cb = p.out[i] + q.in[i] - 2 * d;
                                w[i] = p.weights[i] + u * (p.out[i] + u * (ca + u * cb));
                            }
                        }
                        _heldWeights[id] = w;
                    }
                    // The first target is the shape; the others are offsets
                    // from it, added by their weights.
                    blended = m.targets.front();
                    for (size_t t = 1; t < m.targets.size(); ++t)
                        if (m.targets[t].size() == blended.size())
                            for (size_t v = 0; v < blended.size(); ++v)
                                blended[v] += m.targets[t][v] * w[t];
                    local = &blended;
                }
                setMesh(*item, vertexData(*s, *local, normalsOf(*local, s->triangles)), indicesOf(*s));
            } else if (create && kind == Item::Kind::kGlow) {
                // A square (in X and Z) and its centre, turned to lie in X
                // and Y so it faces the camera as a billboard does; a fan of
                // four triangles from the centre.
                std::vector<float> flat(s->vertices.size());
                for (size_t v = 0; v + 2 < flat.size(); v += 3) {
                    flat[v] = s->vertices[v];
                    flat[v + 1] = s->vertices[v + 2];
                    flat[v + 2] = -s->vertices[v + 1];
                }
                std::vector<uint32_t> fan;
                if (s->vertexCount == 5)
                    fan = {4, 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0};
                setMesh(*item, vertexData(*s, flat, {}), fan);
            } else if (create) {
                setMesh(*item, vertexData(*s, s->vertices, s->normals), indicesOf(*s));
            }
            place(*item, gfx::multiply(parent, localMatrix(*s)), billboard, *local);
        };
    for (uint32_t root : _model.roots())
        visit(root, gfx::identity(), Inherited{}, 0, false, Billboard{});

    // Skins: each vertex where its bones put it (the weighted sum of each
    // bone's world placement of the vertex's offset), and normals worked out
    // again from the triangles, as the stored ones are for the modelled
    // pose. A vertex whose bone wasn't reached stays as modelled.
    for (const PendingSkin &p : skins) {
        const FinSkin &s = *p.skin;
        if (s.triangles.empty() || s.vertices.empty() || s.firstInfluence.size() != size_t(s.vertexCount) + 1)
            continue;
        Item *item = itemFor(p.id, Item::Kind::kSkin, s, p.in, p.backdrop);
        if (!item)
            continue;
        const gfx::Mat4 own = gfx::multiply(p.parent, localMatrix(s));
        std::vector<float> pos(s.vertices.size());
        for (size_t v = 0; v < s.vertexCount; ++v) {
            gfx::Vec3 sum{};
            float total = 0;
            for (uint32_t i = s.firstInfluence[v]; i < s.firstInfluence[v + 1]; ++i) {
                const FinSkin::Influence &in = s.influences[i];
                auto bone = worlds.find(in.bone);
                if (bone == worlds.end())
                    continue;
                const gfx::Vec3 w = gfx::transformPoint(bone->second, in.offset);
                for (size_t k = 0; k < 3; ++k)
                    sum[k] += w[k] * in.weight;
                total += in.weight;
            }
            if (total <= 0)
                sum = gfx::transformPoint(own, {s.vertices[v * 3], s.vertices[v * 3 + 1], s.vertices[v * 3 + 2]});
            else
                for (float &c : sum)
                    c /= total;
            for (size_t k = 0; k < 3; ++k)
                pos[v * 3 + k] = sum[k];
        }
        setMesh(*item, vertexData(s, pos, normalsOf(pos, s.triangles)), indicesOf(s));
        place(*item, gfx::identity(), Billboard{}, pos);
    }

    if (!create)
        return;
    if (lo[0] > hi[0]) {
        _centre = {0, 0, 0};
        _radius = 10;
    } else {
        _centre = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
        _radius = std::max(1.0f, std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                           (hi[2] - lo[2]) * (hi[2] - lo[2])) /
                                     2);
    }
    _home = _centre;
}

void ModelPage::resetView() {
    _centre = _home;
    _yaw = 0.8f;
    _pitch = 0.5f;
    _distance = _radius * 2.2f;
}

void ModelPage::handleEvent(const Event &ev) {
    if (!_renderer)
        return;
    const int n = int(_names.size());
    switch (ev.type) {
    case EventType::kKeyDown:
        switch (ev.key) {
        case key::kLeft:
            show(_index - 1);
            break;
        case key::kRight:
            show(_index + 1);
            break;
        case key::kPageUp:
            show(_index - 10);
            break;
        case key::kPageDown:
            show(_index + 10);
            break;
        case key::kHome:
            show(0);
            break;
        case key::kEnd:
            show(n - 1);
            break;
        case '+':
        case '=':
            _distance *= 0.85f;
            break;
        case '-':
            _distance /= 0.85f;
            break;
        case 'r':
            resetView();
            break;
        case 'i':
            _showInfo = !_showInfo;
            break;
        case 't':
            _useTextures = !_useTextures;
            break;
        case 'l':
            _useLightmaps = !_useLightmaps;
            break;
        case 'b':
            _showBackdrop = !_showBackdrop;
            break;
        case 'g':
            _lightMode = (_lightMode + 1) % 3;
            break;
        case 'f':
            _showFootprint = !_showFootprint;
            break;
        case 'c':
        case 'v':
            if (!_clips.empty()) {
                const int count = int(_clips.size());
                _clip = (_clip + (ev.key == 'c' ? 1 : count - 1)) % count;
                _clipTime = 0;
            }
            break;
        case ' ':
            _playing = !_playing;
            if (!_playing)
                stopClipSound();
            break;
        case 'h': {
            _showHidden = !_showHidden;
            // Keep the view and the clip.
            const gfx::Vec3 centre = _centre;
            const float yaw = _yaw;
            const float pitch = _pitch;
            const float distance = _distance;
            const float time = _clipTime;
            const int clip = _clip;
            show(_index);
            _centre = centre;
            _yaw = yaw;
            _pitch = pitch;
            _distance = distance;
            _clip = clip;
            _clipTime = time;
            break;
        }
        case 'w':
        case 's':
        case 'a':
        case 'd':
        case 'q':
        case 'e': {
            // Move the centre along the view's directions, a tenth of the
            // distance a press.
            const float step = _distance * 0.1f;
            const gfx::Vec3 fwd{-std::cos(_yaw), -std::sin(_yaw), 0};
            const gfx::Vec3 right{std::sin(_yaw), -std::cos(_yaw), 0};
            const float f = ev.key == 'w' ? step : ev.key == 's' ? -step : 0;
            const float r = ev.key == 'd' ? step : ev.key == 'a' ? -step : 0;
            const float u = ev.key == 'e' ? step : ev.key == 'q' ? -step : 0;
            for (size_t k = 0; k < 3; ++k)
                _centre[k] += fwd[k] * f + right[k] * r;
            _centre[2] += u;
            break;
        }
        default:
            break;
        }
        break;
    case EventType::kMouseDown:
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
        break;
    case EventType::kMouseUp:
        _dragging = false;
        break;
    case EventType::kMouseMove:
        if (_dragging) {
            _yaw -= float(ev.mouseX - _lastX) * 0.01f;
            _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
            _lastX = ev.mouseX;
            _lastY = ev.mouseY;
        }
        break;
    case EventType::kMouseWheel:
        _distance *= ev.wheelY > 0 ? 0.85f : 1 / 0.85f;
        break;
    default:
        break;
    }
}

void ModelPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    // The clock: the clip moves on while playing.
    const uint64_t step = _lastMs ? std::min<uint64_t>(nowMs - std::min(nowMs, _lastMs), 100) : 0;
    _lastMs = nowMs;
    if (_playing)
        _clipTime += float(step) / 1000;
    if (const float ticks = clipTicks(); ticks != _posedTicks)
        pose(ticks);
    clipSound();

    const gfx::Vec3 offset{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 eye{_centre[0] + offset[0] * _distance, _centre[1] + offset[1] * _distance,
                        _centre[2] + offset[2] * _distance};
    const float zNear = std::max(0.5f, _distance / 1000);
    const float zFar = _distance + _radius * 4;
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(0.9f, float(kFrameW) / float(kFrameH), zNear, zFar),
                                             gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    _renderer->beginFrame(viewProj, eye, {60, 64, 74});

    // Material animators: the colour and alpha keyed now. Colour 1 is the
    // diffuse colour, 3 emissive (added); 0, ambient, is left out.
    const float ticks = _posedTicks;
    for (Item &item : _items) {
        item.color = item.baseColor;
        if (auto a = _animatorsOf.find(item.material); a != _animatorsOf.end())
            for (const FinMaterialAnimator *anim : a->second) {
                if (anim->keys.empty())
                    continue;
                const std::array<float, 3> v = sample(anim->keys, ticks, kStraight3);
                if (anim->alpha) {
                    item.color[3] = v[0];
                } else if (anim->channel == 1) {
                    item.color = {v[0], v[1], v[2], item.color[3]};
                } else if (anim->channel == 3) {
                    for (size_t k = 0; k < 3; ++k)
                        item.color[k] += v[k];
                }
            }
        if (item.vertexColors && item.kind != Item::Kind::kLight)
            item.color = {1, 1, 1, item.color[3]};
    }

    // Opaque shapes first, each with its lightmap multiplied over it; then
    // those that blend, the farthest first.
    std::vector<const Item *> order;
    auto drawn = [&](const Item &item) {
        return item.visible && item.mesh && (_showBackdrop || !item.backdrop) &&
               (item.kind != Item::Kind::kLight || _lightMode > 0);
    };
    for (const Item &item : _items)
        if (!item.blend && drawn(item))
            order.push_back(&item);
    const size_t opaque = order.size();
    for (const Item &item : _items)
        if (item.blend && drawn(item))
            order.push_back(&item);
    auto distance2 = [&](const Item *i) {
        const gfx::Vec3 c =
            i->billboard ? gfx::Vec3{i->billboardAt[12], i->billboardAt[13], i->billboardAt[14]} : i->centre;
        return (c[0] - eye[0]) * (c[0] - eye[0]) + (c[1] - eye[1]) * (c[1] - eye[1]) +
               (c[2] - eye[2]) * (c[2] - eye[2]);
    };
    std::stable_sort(order.begin() + std::ptrdiff_t(opaque), order.end(),
                     [&](const Item *a, const Item *b) { return distance2(a) > distance2(b); });
    const float markerSize = _radius * 0.03f;
    // The model's own lights (G twice), for the shapes lit by normals, as
    // NiDirect3d lights vertices (fin.md): every light's ambient, and the
    // one light reaching the shape most strongly, from its direction. Each
    // colour is times the dimmer. Point lights and spots fade with distance
    // L when attenuated, as 1 / (a L + b L^3); a spot reaches only within
    // its cone, fading to its edge.
    struct SceneLight {
        gfx::Vec3 position{}, direction{}, color{};
        bool directional = false, attenuated = false, spot = false;
        float linear = 0, cubic = 0; // a and b
        float cos2 = 0, sin2 = 1;    // a spot's angle's cosine and sine, squared
    };
    std::vector<SceneLight> sceneLights;
    gfx::Vec3 sceneAmbient{};
    if (_lightMode == 2)
        for (const Item &item : _items) {
            if (item.kind != Item::Kind::kLight || !item.visible)
                continue;
            const FinLight &l = _model.lights.at(item.id);
            if (!l.on)
                continue;
            SceneLight sl;
            sl.position = {item.world[12], item.world[13], item.world[14]};
            sl.directional = l.type == FinLight::kDirectional;
            if (l.type == FinLight::kSpot) {
                const float angle = l.spotAngle * 3.14159265f / 180;
                sl.spot = std::cos(angle) > 0; // wider than a half-space: as a point light
                sl.cos2 = std::cos(angle) * std::cos(angle);
                sl.sin2 = std::sin(angle) * std::sin(angle);
            }
            gfx::Vec3 d{};
            for (size_t r = 0; r < 3; ++r)
                d[r] = item.world[r] * l.direction[0] + item.world[4 + r] * l.direction[1] +
                       item.world[8 + r] * l.direction[2];
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (size_t k = 0; k < 3; ++k) {
                sl.direction[k] = len > 0 ? d[k] / len : 0;
                sl.color[k] = std::clamp(l.diffuse[k] * l.dimmer, 0.0f, 2.0f);
                sceneAmbient[k] += l.ambient[k] * l.dimmer;
            }
            if (l.type == FinLight::kAmbient)
                continue;
            // At the distance D, 1/64; the curve c sets the cubic part,
            // k = c^4. No distance: nothing reaches.
            sl.attenuated = l.attenuated;
            const float reach = l.attenuationDistance;
            if (reach > 0) {
                const float k = l.attenuationCurve * l.attenuationCurve * l.attenuationCurve * l.attenuationCurve;
                const float scale = 64 / (reach * (1 - k) + k * reach * reach * reach);
                sl.linear = (1 - k) * scale;
                sl.cubic = k * scale;
            } else {
                sl.linear = sl.cubic = std::numeric_limits<float>::max();
            }
            sceneLights.push_back(sl);
        }
    for (const Item *drawnItem : order) {
        const Item &item = *drawnItem;
        gfx::DrawCall call;
        call.mesh = item.mesh;
        call.indexCount = item.indexCount;
        call.model = item.billboard ? gfx::multiply(facingCamera(item.billboardAt, eye), item.rel) : item.world;
        if (item.kind == Item::Kind::kLight) {
            gfx::Mat4 scale = gfx::identity();
            scale[0] = scale[5] = scale[10] = markerSize;
            gfx::Mat4 at = facingCamera(item.billboardAt, eye);
            // The light's own scale is left out: a marker is the same size
            // everywhere.
            const float s = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
            for (size_t k = 0; k < 12; ++k)
                at[k] /= s > 0 ? s : 1;
            call.model = gfx::multiply(at, scale);
        }
        // A flip-book's image, as NiFlipTextures::Act gives it at the
        // clip's time in whole ticks: those the clip lists (all of them
        // on the whole timeline); the others keep theirs.
        if (item.flip && item.frames.size() > 1) {
            const FinActor::Clip &c = _clips[size_t(_clip)];
            const auto number = _flipNumber.find(item.flip);
            if (_clip == 0 || (number != _flipNumber.end() &&
                               std::find(c.flips.begin(), c.flips.end(), number->second) != c.flips.end()))
                item.image = item.flip->image(std::floor(double(clipTicks()) + 0.5), item.frames.size());
        }
        const size_t frame = item.image;
        call.texture = _useTextures && !item.frames.empty() ? texture(item.frames[frame]) : 0;
        if ((item.kind == Item::Kind::kGlow && !call.texture) || item.kind == Item::Kind::kLight)
            call.texture = glowTexture();
        call.color = item.color;
        call.rgbSource = item.vertexColors && item.kind != Item::Kind::kLight ? gfx::ColorSource::kVertex
                         : item.lit                                           ? gfx::ColorSource::kLighting
                                                                              : gfx::ColorSource::kConstant;
        call.alphaSource = item.vertexColors ? gfx::ColorSource::kVertex : gfx::ColorSource::kConstant;
        // A light over the viewer's shoulder; or the model's own.
        call.ambientLight = {0.45f, 0.45f, 0.45f};
        call.directedLight = {0.6f, 0.6f, 0.6f};
        call.lightDirection = {offset[0] * 0.6f - offset[1] * 0.5f, offset[1] * 0.6f + offset[0] * 0.5f, 0.7f};
        if (!sceneLights.empty() && item.lit) {
            const gfx::Vec3 at = item.billboard
                                     ? gfx::Vec3{item.billboardAt[12], item.billboardAt[13], item.billboardAt[14]}
                                     : item.centre;
            float best = -1;
            call.ambientLight = {0.15f + sceneAmbient[0], 0.15f + sceneAmbient[1], 0.15f + sceneAmbient[2]};
            call.directedLight = {};
            for (const SceneLight &l : sceneLights) {
                gfx::Vec3 to{}; // towards the light
                float strength = 1;
                if (l.directional) {
                    to = {-l.direction[0], -l.direction[1], -l.direction[2]};
                } else {
                    const gfx::Vec3 d{l.position[0] - at[0], l.position[1] - at[1], l.position[2] - at[2]};
                    const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    to = dist > 0 ? gfx::Vec3{d[0] / dist, d[1] / dist, d[2] / dist} : gfx::Vec3{0, 0, 1};
                    if (l.attenuated) {
                        const double fall = double(l.linear) * dist + double(l.cubic) * dist * dist * dist;
                        strength = fall > 0 ? float(std::min(1.0, 1 / fall)) : 1;
                    }
                    // A spot's cone, as NiDirect3d works it out: with s the
                    // distance along its direction, (s^2 - L^2 cos^2) /
                    // (s^2 sin^2) of the angle, 1 on its axis and 0 at its
                    // edge; nothing outside it or behind.
                    if (l.spot) {
                        const float s = -(d[0] * l.direction[0] + d[1] * l.direction[1] + d[2] * l.direction[2]);
                        const float cone = s > 0 && l.sin2 > 0 ? (s * s - dist * dist * l.cos2) / (s * s * l.sin2) : 0;
                        strength *= std::clamp(cone, 0.0f, 1.0f);
                    }
                }
                const float bright = strength * (l.color[0] + l.color[1] + l.color[2]);
                if (bright > best) {
                    best = bright;
                    call.lightDirection = to;
                    call.directedLight = {l.color[0] * strength, l.color[1] * strength, l.color[2] * strength};
                }
            }
        }
        // Back faces culled, as Direct3D does by default and nothing in
        // the files asks otherwise (closed shapes that blend, such as the
        // skeletons' heads, would otherwise show their insides);
        // billboards are seen from the front anyway.
        call.state.cull = item.billboard ? gfx::Cull::kNone : gfx::Cull::kBack;
        call.state.depthTest = item.depthTest;
        call.state.depthWrite = item.depthWrite;
        if (item.blend) {
            call.state.src = item.src;
            call.state.dst = item.dst;
            call.state.alphaTest = gfx::AlphaTest::kGt0;
        }
        _renderer->draw(call);
        if (_useLightmaps && !item.lightmap.empty() && !item.blend) {
            if (const gfx::TextureId lm = texture(item.lightmap)) {
                gfx::DrawCall light = call;
                light.texture = lm;
                light.texGen = gfx::TexGen::kUv2;
                light.color = {1, 1, 1, 1};
                light.rgbSource = light.alphaSource = gfx::ColorSource::kConstant;
                light.state.src = gfx::Blend::kDstColor;
                light.state.dst = gfx::Blend::kZero;
                light.state.depthEqual = true;
                light.state.depthWrite = false;
                _renderer->draw(light);
            }
        }
    }

    if (_showFootprint && _footprint) {
        gfx::DrawCall call;
        call.mesh = _footprint;
        call.indexCount = _footprintCount;
        call.color = {0.3f, 1, 0.4f, 0.4f};
        call.state.cull = gfx::Cull::kNone;
        call.state.depthTest = false; // over the model, as it's under it
        call.state.depthWrite = false;
        call.state.src = gfx::Blend::kSrcAlpha;
        call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
        _renderer->draw(call);
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    std::vector<std::string> lines;
    if (_showInfo) {
        lines = _info;
        if (_clips.size() > 1 || (!_clips.empty() && _clips.front().end > 0)) {
            const FinActor::Clip &c = _clips[size_t(_clip)];
            char at[64];
            std::snprintf(at, sizeof at, "%.2f s of %.2f",
                          c.end > c.start ? std::fmod(_clipTime, c.end - c.start) : 0.0f, c.end - c.start);
            std::string sound;
            if (!c.sound.empty()) {
                auto it = _sounds.find(c.sound);
                sound = "  sound " + c.sound + (it != _sounds.end() && !it->second ? " (not found)" : "") +
                        (c.loop ? ", looping" : "");
            }
            lines.push_back("Clip " + std::to_string(_clip + 1) + "/" + std::to_string(_clips.size()) + ": " + c.name +
                            "  " + at + (_playing ? "" : " (paused)") + sound + "   C/V: clip   Space: pause");
        }
        lines.push_back("Left/Right: model   drag: turn   wheel: zoom   W/A/S/D/Q/E: move   R: reset   I: info");
        lines.push_back(
            std::string("T: textures (") + (_useTextures ? "on" : "off") + ")   L: lightmaps (" +
            (_useLightmaps ? "on" : "off") + ")   B: sky (" + (_showBackdrop ? "on" : "off") + ")   G: lights (" +
            (_lightMode == 0   ? "off"
             : _lightMode == 1 ? "marked"
                               : "lighting") +
            ")   F: footprint (" +
            (!_footprint      ? "none"
             : _showFootprint ? "on"
                              : "off") +
            ")   H: hidden (" + std::to_string(_hiddenCount) + ", " + (_showHidden ? "shown" : "not shown") + ")");
    }
    if (!_error.empty())
        lines.push_back(_error);
    if (!lines.empty()) {
        const int lh = font::kLineHeight;
        size_t widest = 0;
        for (const std::string &l : lines)
            widest = std::max(widest, font::length(l));
        const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8);
        const int boxH = int(lines.size()) * lh + 6;
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                _overlay.row(y)[x * 4 + 3] = 150;
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(_overlay, 4, 3 + int(i) * lh, lines[i],
                           lines[i] == _error ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::netimmerse
