// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Models page: each .FIN model drawn with the GPU renderer, as the
// scene graph places its shapes, with their textures, lightmaps, vertex
// colours, materials and alpha; animated by the game's keys (nodes, skins,
// morphs, materials, flip-book textures), with its glows.

#include "engines/netimmerse/viewers.h"

#include "common/font.h"
#include "common/fs.h"
#include "common/image.h"

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
gfx::Mat4 placement(const std::array<float, 3>& t, const std::array<float, 9>& r, float scale) {
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
std::array<float, 9> rotationOf(const std::array<float, 4>& q) {
    const auto [w, x, y, z] = q;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
            2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
            2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
}

// Angles about X, Y and Z as one rotation, row by row: Z × Y × X (*guess*:
// the 246 Euler keys are all on a few small parts, where this looks right).
std::array<float, 9> rotationOf(float ax, float ay, float az) {
    const float cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az),
                sz = std::sin(az);
    return {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
            sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
            -sy,     cy * sx,                cy * cx};
}

std::array<float, 4> slerp(const std::array<float, 4>& a, std::array<float, 4> b, float f) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        dot = -dot;
        for (float& c : b)
            c = -c;
    }
    float wa = 1 - f, wb = f;
    if (dot < 0.9995f) {
        const float angle = std::acos(dot), sinAngle = std::sin(angle);
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
    for (float& c : q)
        c /= len > 0 ? len : 1;
    return q;
}

// The value keyed at `t`: between the keys either side, blended by `mix`;
// before the first key or after the last, theirs. Tangents are ignored.
template <typename Key, typename Mix>
auto sample(const std::vector<Key>& keys, float t, Mix mix) {
    if (t <= keys.front().time)
        return keys.front().value;
    if (t >= keys.back().time)
        return keys.back().value;
    auto b = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key& k) { return v < k.time; });
    auto a = b - 1;
    const float f = b->time > a->time ? (t - a->time) / (b->time - a->time) : 0;
    return mix(a->value, b->value, f);
}

const auto mixFloat = [](float a, float b, float f) { return a + (b - a) * f; };
const auto mix3 = [](const std::array<float, 3>& a, const std::array<float, 3>& b, float f) {
    return std::array<float, 3>{a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
};

// A node's placement at `t` with its track's keys in place of what was
// saved; and whether its visibility keys show it then.
gfx::Mat4 animated(const FinNode& n, const FinActor::Track& track, float t, bool& visible) {
    std::array<float, 3> translation = n.translation;
    std::array<float, 9> rotation = n.rotation;
    float scale = n.scale;
    if (!track.positions.empty())
        translation = sample(track.positions, t, mix3);
    if (!track.rotations.empty()) {
        rotation = rotationOf(sample(track.rotations, t, slerp));
    } else if (!track.euler[0].empty() || !track.euler[1].empty() || !track.euler[2].empty()) {
        float angles[3] = {};
        for (size_t i = 0; i < 3; ++i)
            if (!track.euler[i].empty())
                angles[i] = sample(track.euler[i], t, mixFloat);
        rotation = rotationOf(angles[0], angles[1], angles[2]);
    }
    if (!track.scales.empty())
        scale = sample(track.scales, t, mixFloat);
    if (!track.visible.empty())
        visible = sample(track.visible, t, [](bool a, bool, float) { return a; });
    return placement(translation, rotation, scale);
}

// A shape's vertices for the renderer, at `pos` with `nrm` (either may
// be the shape's own).
std::vector<gfx::Vertex> vertexData(const FinShape& s, const std::vector<float>& pos, const std::vector<float>& nrm) {
    const size_t nv = s.vertexCount;
    const size_t lightSet = s.uvSets ? s.uvSets - 1u : 0;
    std::vector<gfx::Vertex> verts(nv);
    for (size_t v = 0; v < nv; ++v) {
        gfx::Vertex& out = verts[v];
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
            out.color[k] = s.colors.size() < nv * 4
                               ? 255
                               : uint8_t(std::clamp(s.colors[v * 4 + k], 0.0f, 1.0f) * 255 + 0.5f);
    }
    return verts;
}

// Normals worked out from the triangles, for vertices that have moved.
std::vector<float> normalsOf(const std::vector<float>& pos, const std::vector<uint16_t>& triangles) {
    std::vector<float> nrm(pos.size(), 0.0f);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const float* a = &pos[size_t(triangles[t]) * 3];
        const float* b = &pos[size_t(triangles[t + 1]) * 3];
        const float* c = &pos[size_t(triangles[t + 2]) * 3];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (size_t j = 0; j < 3; ++j)
            for (size_t k = 0; k < 3; ++k)
                nrm[size_t(triangles[t + j]) * 3 + k] += n[k];
    }
    for (size_t v = 0; v + 2 < nrm.size(); v += 3) {
        float* n = &nrm[v];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0)
            n[0] /= len, n[1] /= len, n[2] /= len;
    }
    return nrm;
}


// NetImmerse's blend factors (NiAlphaProperty), as the renderer's.
gfx::Blend blendOf(uint32_t f) {
    switch (f) {
    case 0: return gfx::Blend::One;
    case 1: return gfx::Blend::Zero;
    case 2: return gfx::Blend::SrcColor;
    case 3: return gfx::Blend::OneMinusSrcColor;
    case 4: return gfx::Blend::DstColor;
    case 5: return gfx::Blend::OneMinusDstColor;
    case 6: return gfx::Blend::SrcAlpha;
    case 7: return gfx::Blend::OneMinusSrcAlpha;
    case 8: return gfx::Blend::DstAlpha;
    case 9: return gfx::Blend::OneMinusDstAlpha;
    default: return gfx::Blend::One;
    }
}

// An object's placement: translation, then rotation (stored row by row,
// applied to column vectors), then scale.
gfx::Mat4 localMatrix(const FinAvObject& o) { return placement(o.translation, o.rotation, o.scale); }

constexpr int kFrameW = 640, kFrameH = 480;

// A billboard node's placement turned so its +Z axis points at the camera,
// with +Y as near the world's up (+Z) as that allows; its position and
// scale kept.
gfx::Mat4 facingCamera(const gfx::Mat4& at, const gfx::Vec3& eye) {
    const gfx::Vec3 origin{at[12], at[13], at[14]};
    const float scale = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    gfx::Vec3 z{eye[0] - origin[0], eye[1] - origin[1], eye[2] - origin[2]};
    float len = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (len < 1e-6f)
        return at;
    for (float& c : z)
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
    enum class Kind : uint8_t { Shape, Skin, Morph, Glow, Light } kind = Kind::Shape;
    uint32_t id = 0; // the shape's (or light's)
    gfx::MeshId mesh = 0;
    uint32_t indexCount = 0;
    bool visible = true; // reached by the last walk
    gfx::Mat4 world = gfx::identity();
    std::vector<std::string> frames; // the texture's images: one, or a flip-book's
    const FinFlip* flip = nullptr;
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
    gfx::Blend src = gfx::Blend::One, dst = gfx::Blend::Zero;
    // Depth test and write: on, unless an NiZBufferProperty turns them off.
    bool depthTest = true, depthWrite = true;
    gfx::Vec3 localCentre{}, centre{}; // of its vertices; in the world, for sorting what blends
};

ModelPage::ModelPage(ViewerContext& ctx) : Page(ctx) {
    auto fin = ctx.archives.find("Fin.pac");
    if (fin == ctx.archives.end())
        return;
    for (const PacArchive::Entry& e : fin->second->entries())
        if (e.size)
            names_.push_back(e.name);
}

ModelPage::~ModelPage() { deactivate(); }

void ModelPage::clearItems() {
    if (renderer_)
        for (const Item& item : items_)
            if (item.mesh)
                renderer_->destroyMesh(item.mesh);
    items_.clear();
    itemOf_.clear();
}

void ModelPage::deactivate() {
    // The renderer's textures and meshes go with it when another page
    // takes the screen.
    clearItems();
    if (renderer_) {
        for (const auto& [name, id] : textures_)
            if (id)
                renderer_->destroyTexture(id);
        if (glow_)
            renderer_->destroyTexture(glow_);
    }
    textures_.clear();
    glow_ = 0;
    renderer_ = nullptr;
}

void ModelPage::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        use2D();
        clear(16, 18, 24);
        text(8, 8, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(8, 8 + font::kLineHeight, err.message);
        ctx_.sys.updateScreen(ctx_.screen);
        setTitle("Models: 3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    show(index_);
}

gfx::TextureId ModelPage::texture(const std::string& name) {
    const std::string key = fs::toLower(name);
    if (auto it = textures_.find(key); it != textures_.end())
        return it->second;
    gfx::TextureId id = 0;
    std::vector<uint8_t> data;
    image::Image img;
    if (!ctx_.files.read(name, data) && !image::decode(data, img)) {
        gfx::TextureParams params;
        if (renderer_->createTexture(img.w, img.h, img.rgba.data(), params, id))
            id = 0;
    }
    textures_[key] = id;
    return id;
}

// A soft white spot, for the glows without an image of their own and the
// lights' markers.
gfx::TextureId ModelPage::glowTexture() {
    if (glow_)
        return glow_;
    constexpr int kSize = 32;
    std::vector<uint8_t> rgba(kSize * kSize * 4, 255);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const float dx = (float(x) + 0.5f) / kSize * 2 - 1, dy = (float(y) + 0.5f) / kSize * 2 - 1;
            const float a = std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            rgba[size_t(y * kSize + x) * 4 + 3] = uint8_t(a * a * 255);
        }
    if (renderer_->createTexture(kSize, kSize, rgba.data(), gfx::TextureParams{}, glow_))
        glow_ = 0;
    return glow_;
}

float ModelPage::clipTicks() const {
    if (clips_.empty())
        return 0;
    const FinActor::Clip& c = clips_[size_t(clip_)];
    const float length = c.end - c.start;
    return (c.start + (length > 0 ? std::fmod(clipTime_, length) : 0)) * kTicksPerSecond;
}

void ModelPage::show(int index) {
    const int n = int(names_.size());
    index_ = ((index % n) + n) % n;
    clearItems();
    info_.clear();
    error_.clear();
    actor_ = nullptr;
    trackOf_.clear();
    animatorsOf_.clear();
    flipOf_.clear();
    clips_.clear();
    clip_ = 0;
    clipTime_ = 0;
    const std::string& name = names_[size_t(index_)];
    setTitle("Models: " + name + " (" + std::to_string(index_ + 1) + "/" + std::to_string(n) + ")");

    std::vector<uint8_t> data;
    Error err = ctx_.archives.at("Fin.pac")->read(name, data);
    if (!err)
        err = model_.parse(data);
    if (err) {
        error_ = name + ": " + err.message;
        model_ = FinFile();
        return;
    }

    // Animation: the actor's tracks go to the 3ds nodes in depth-first
    // order; the materials' animators and the textures' flip-books by what
    // they target. The whole timeline is the first clip, then the actor's.
    float lastKey = 0;
    if (!model_.actors.empty()) {
        actor_ = &model_.actors.begin()->second;
        const std::vector<uint32_t> nodes = model_.animatedNodes();
        if (nodes.size() == actor_->tracks.size())
            for (size_t i = 0; i < nodes.size(); ++i) {
                trackOf_[nodes[i]] = i;
                const FinActor::Track& t = actor_->tracks[i];
                for (float k : {t.positions.empty() ? 0 : t.positions.back().time,
                                t.rotations.empty() ? 0 : t.rotations.back().time,
                                t.scales.empty() ? 0 : t.scales.back().time,
                                t.visible.empty() ? 0 : t.visible.back().time})
                    lastKey = std::max(lastKey, k);
                for (const auto& axis : t.euler)
                    if (!axis.empty())
                        lastKey = std::max(lastKey, axis.back().time);
            }
    }
    for (const auto& [id, a] : model_.materialAnimators) {
        animatorsOf_[a.target].push_back(&a);
        if (!a.keys.empty())
            lastKey = std::max(lastKey, a.keys.back().time);
    }
    for (const auto& [id, f] : model_.flips)
        flipOf_[f.target] = &f;
    for (const auto& [id, m] : model_.morphs)
        if (!m.keys.empty())
            lastKey = std::max(lastKey, m.keys.back().time);
    // An actor's clips end before a few stray keys far out (Evil Ogel's go
    // to 381 s, his clips to 74 s): its whole timeline is theirs.
    if (actor_ && !actor_->clips.empty()) {
        lastKey = 0;
        for (const FinActor::Clip& c : actor_->clips)
            lastKey = std::max(lastKey, c.end * kTicksPerSecond);
    }
    clips_.push_back({"whole timeline", 0, lastKey / kTicksPerSecond, ""});
    if (actor_)
        for (const FinActor::Clip& c : actor_->clips)
            clips_.push_back(c);

    walk(clipTicks(), true);
    posedTicks_ = clipTicks();
    resetView();

    unsigned shapes = 0, triangles = 0, glows = 0, missing = 0;
    std::vector<std::string> missingNames;
    for (const Item& item : items_) {
        if (item.kind == Item::Kind::Glow)
            ++glows;
        else if (item.kind != Item::Kind::Light) {
            ++shapes;
            triangles += item.indexCount / 3;
        }
        std::vector<const std::string*> images{&item.lightmap};
        for (const std::string& f : item.frames)
            images.push_back(&f);
        for (const std::string* img : images)
            if (!img->empty() && !texture(*img)) {
                ++missing;
                if (missingNames.size() < 3)
                    missingNames.push_back(*img);
            }
    }
    unsigned unread = 0;
    for (const auto& o : model_.objects())
        unread += !o.read;
    info_.push_back(name + ": " + std::to_string(shapes) + " shapes, " + std::to_string(triangles) + " triangles, " +
                    std::to_string(glows) + " glows, " + std::to_string(model_.lights.size()) + " lights, " +
                    std::to_string(model_.objects().size()) + " objects" +
                    (unread ? " (" + std::to_string(unread) + " not read)" : ""));
    info_.push_back("Animated: " + std::to_string(trackOf_.size()) + " nodes, " +
                    std::to_string(model_.morphs.size()) + " morphs, " + std::to_string(model_.materialAnimators.size()) +
                    " material animators, " + std::to_string(model_.flips.size()) + " flip-books");
    if (missing) {
        std::string m = std::to_string(missing) + " images not found:";
        for (const std::string& name : missingNames)
            m += " " + name;
        info_.push_back(m);
    }
}

void ModelPage::pose(float ticks) {
    walk(ticks, false);
    posedTicks_ = ticks;
}

void ModelPage::walk(float ticks, bool create) {
    for (Item& item : items_)
        item.visible = false;
    std::array<float, 3> lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
    std::array<float, 3> hi{-lo[0], -lo[1], -lo[2]};
    if (create)
        hiddenCount_ = 0;

    auto applyProperties = [&](const FinAvObject& o, Inherited in) {
        for (uint32_t p : o.properties) {
            const std::string type = model_.typeOf(p);
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
    auto itemFor = [&](uint32_t id, Item::Kind kind, const FinShape& s, const Inherited& in, bool backdrop) -> Item* {
        if (!create) {
            auto it = itemOf_.find(id);
            return it == itemOf_.end() ? nullptr : &items_[it->second];
        }
        Item item;
        item.kind = kind;
        item.id = id;
        item.backdrop = backdrop;
        // Images: the texture's (its first, or a flip-book's frames), the
        // multi-texture's lightmap. The texture uses the first uv set, the
        // lightmap the last.
        if (auto t = model_.textures.find(in.texture); t != model_.textures.end())
            for (uint32_t frame : t->second)
                if (auto img = model_.images.find(frame); img != model_.images.end())
                    item.frames.push_back(img->second);
        if (auto f = flipOf_.find(in.texture); f != flipOf_.end() && item.frames.size() > 1)
            item.flip = f->second;
        else if (item.frames.size() > 1)
            item.frames.resize(1);
        if (auto mt = model_.multiTextures.find(in.multiTexture); mt != model_.multiTextures.end())
            if (auto img = model_.images.find(mt->second); img != model_.images.end())
                item.lightmap = img->second;
        item.material = in.material;
        if (auto mat = model_.materials.find(in.material); mat != model_.materials.end())
            item.baseColor = {mat->second.diffuse[0], mat->second.diffuse[1], mat->second.diffuse[2], mat->second.alpha};
        if (auto al = model_.alphas.find(in.alpha); al != model_.alphas.end() && al->second.blend) {
            item.blend = true;
            item.src = blendOf(al->second.source);
            item.dst = blendOf(al->second.destination);
        }
        if (auto z = model_.zBuffers.find(in.zBuffer); z != model_.zBuffers.end()) {
            item.depthTest = z->second[0] != 0;
            item.depthWrite = z->second[1] != 0;
        }
        item.vertexColors = !s.colors.empty();
        item.lit = !item.vertexColors && !s.normals.empty();
        if (kind == Item::Kind::Glow) {
            // Added to what's behind, facing the camera.
            item.blend = item.additive = item.billboard = true;
            item.src = gfx::Blend::SrcAlpha;
            item.dst = gfx::Blend::One;
            item.depthWrite = false;
            item.lit = false;
        }
        itemOf_[id] = items_.size();
        items_.push_back(std::move(item));
        return &items_.back();
    };
    // A fixed mesh made once; a skin's or morph's again each time.
    auto setMesh = [&](Item& item, const std::vector<gfx::Vertex>& verts, const std::vector<uint32_t>& indices) {
        if (item.mesh)
            renderer_->destroyMesh(item.mesh);
        item.mesh = 0;
        if (renderer_->createMesh(verts, indices, item.mesh))
            item.mesh = 0;
        item.indexCount = uint32_t(indices.size());
        item.localCentre = {};
        for (const gfx::Vertex& v : verts)
            for (size_t k = 0; k < 3; ++k)
                item.localCentre[k] += v.pos[k] / float(verts.size());
    };
    // Where an item is now; the framing counts what isn't sky or a glow.
    auto place = [&](Item& item, const gfx::Mat4& world, const Billboard& billboard, const std::vector<float>& local) {
        item.visible = true;
        item.world = world;
        if (item.kind == Item::Kind::Glow || item.kind == Item::Kind::Light) {
            item.billboard = true;
            item.billboardAt = world;
            item.rel = gfx::identity();
        } else {
            item.billboard = billboard.on;
            item.billboardAt = billboard.at;
            item.rel = billboard.rel;
        }
        item.centre = gfx::transformPoint(world, item.localCentre);
        if (create && !item.backdrop && item.kind != Item::Kind::Glow && item.kind != Item::Kind::Light)
            for (size_t v = 0; v + 2 < local.size(); v += 3) {
                const gfx::Vec3 w = gfx::transformPoint(world, {local[v], local[v + 1], local[v + 2]});
                for (size_t k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], w[k]);
                    hi[k] = std::max(hi[k], w[k]);
                }
            }
    };
    auto indicesOf = [](const FinShape& s) { return std::vector<uint32_t>(s.triangles.begin(), s.triangles.end()); };

    // Every node's place in the world, for the skins' bones; and the skins,
    // placed once all of them are known (they come before their bones).
    std::map<uint32_t, gfx::Mat4> worlds;
    struct PendingSkin {
        uint32_t id;
        const FinSkin* skin;
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
    auto isStandIn = [&](const FinNode& n) {
        return n.name.rfind("LOD_", 0) == 0 && n.children.size() == 1 &&
               model_.typeOf(n.children.front()) == "NiBillboardNode";
    };
    std::function<void(uint32_t, const gfx::Mat4&, const Inherited&, int, bool, Billboard)> visit =
        [&](uint32_t id, const gfx::Mat4& parent, const Inherited& inherited, int depth, bool backdrop,
            Billboard billboard) {
            if (depth > 64)
                return;
            if (auto node = model_.nodes.find(id); node != model_.nodes.end()) {
                const FinNode& n = node->second;
                if (n.hidden || isStandIn(n)) {
                    hiddenCount_ += create;
                    if (!showHidden_)
                        return;
                }
                bool shown = true;
                const auto track = trackOf_.find(id);
                const gfx::Mat4 local =
                    track != trackOf_.end() ? animated(n, actor_->tracks[track->second], ticks, shown) : localMatrix(n);
                if (!shown)
                    return;
                const gfx::Mat4 world = gfx::multiply(parent, local);
                worlds[id] = world;
                if (billboard.on) {
                    billboard.rel = gfx::multiply(billboard.rel, local);
                } else if (model_.typeOf(id) == "NiBillboardNode") {
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
            if (auto skin = model_.skins.find(id); skin != model_.skins.end()) {
                skins.push_back({id, &skin->second, parent, applyProperties(skin->second, inherited), backdrop});
                return;
            }
            if (auto light = model_.lights.find(id); light != model_.lights.end()) {
                const FinLight& l = light->second;
                if (Item* item = itemFor(id, Item::Kind::Light, FinShape{}, inherited, backdrop)) {
                    if (create) {
                        // A small spot in the light's colour, sized with the view.
                        static const std::vector<uint32_t> quad{0, 1, 2, 0, 2, 3};
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
                        setMesh(*item, verts, quad);
                        item->blend = item->additive = true;
                        item->src = gfx::Blend::SrcAlpha;
                        item->dst = gfx::Blend::One;
                        item->depthWrite = false;
                        item->baseColor = {std::min(1.0f, l.diffuse[0]), std::min(1.0f, l.diffuse[1]),
                                           std::min(1.0f, l.diffuse[2]), 1};
                    }
                    place(*item, gfx::multiply(parent, localMatrix(l)), billboard, {});
                }
                return;
            }
            const FinShape* s = nullptr;
            Item::Kind kind = Item::Kind::Shape;
            if (auto it = model_.shapes.find(id); it != model_.shapes.end()) {
                s = &it->second;
            } else if (auto m = model_.morphs.find(id); m != model_.morphs.end()) {
                s = &m->second;
                kind = Item::Kind::Morph;
            } else if (auto g = model_.coronas.find(id); g != model_.coronas.end()) {
                s = &g->second;
                kind = Item::Kind::Glow;
            }
            if (!s || s->vertices.empty() || (s->hidden && !showHidden_))
                return;
            if (kind != Item::Kind::Glow && s->triangles.empty())
                return;
            if (billboard.on)
                billboard.rel = gfx::multiply(billboard.rel, localMatrix(*s));
            Item* item = itemFor(id, kind, *s, applyProperties(*s, inherited), backdrop);
            if (!item)
                return;
            const std::vector<float>* local = &s->vertices;
            std::vector<float> blended;
            if (kind == Item::Kind::Morph) {
                // The targets blended by the weights keyed now.
                const FinMorph& m = model_.morphs.at(id);
                if (!m.keys.empty() && !m.targets.empty() && m.keys.front().weights.size() == m.targets.size()) {
                    std::vector<FinActor::Key<std::vector<float>>> keys;
                    for (const FinMorph::Key& k : m.keys)
                        keys.push_back({k.time, k.weights});
                    const std::vector<float> w = sample(keys, ticks, [](const auto& a, const auto& b, float f) {
                        std::vector<float> out(a.size());
                        for (size_t i = 0; i < a.size(); ++i)
                            out[i] = a[i] + (b[i] - a[i]) * f;
                        return out;
                    });
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
            } else if (create && kind == Item::Kind::Glow) {
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
    for (uint32_t root : model_.roots())
        visit(root, gfx::identity(), Inherited{}, 0, false, Billboard{});

    // Skins: each vertex where its bones put it (the weighted sum of each
    // bone's world placement of the vertex's offset), and normals worked out
    // again from the triangles, as the stored ones are for the modelled
    // pose. A vertex whose bone wasn't reached stays as modelled.
    for (const PendingSkin& p : skins) {
        const FinSkin& s = *p.skin;
        if (s.triangles.empty() || s.vertices.empty() || s.firstInfluence.size() != size_t(s.vertexCount) + 1)
            continue;
        Item* item = itemFor(p.id, Item::Kind::Skin, s, p.in, p.backdrop);
        if (!item)
            continue;
        const gfx::Mat4 own = gfx::multiply(p.parent, localMatrix(s));
        std::vector<float> pos(s.vertices.size());
        for (size_t v = 0; v < s.vertexCount; ++v) {
            gfx::Vec3 sum{};
            float total = 0;
            for (uint32_t i = s.firstInfluence[v]; i < s.firstInfluence[v + 1]; ++i) {
                const FinSkin::Influence& in = s.influences[i];
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
                for (float& c : sum)
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
        centre_ = {0, 0, 0};
        radius_ = 10;
    } else {
        centre_ = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
        radius_ = std::max(1.0f, std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                           (hi[2] - lo[2]) * (hi[2] - lo[2])) /
                                     2);
    }
    home_ = centre_;
}

void ModelPage::resetView() {
    centre_ = home_;
    yaw_ = 0.8f;
    pitch_ = 0.5f;
    distance_ = radius_ * 2.2f;
}

void ModelPage::handleEvent(const Event& ev) {
    if (!renderer_)
        return;
    const int n = int(names_.size());
    switch (ev.type) {
    case EventType::KeyDown:
        switch (ev.key) {
        case key::Left: show(index_ - 1); break;
        case key::Right: show(index_ + 1); break;
        case key::PageUp: show(index_ - 10); break;
        case key::PageDown: show(index_ + 10); break;
        case key::Home: show(0); break;
        case key::End: show(n - 1); break;
        case '+':
        case '=': distance_ *= 0.85f; break;
        case '-': distance_ /= 0.85f; break;
        case 'r': resetView(); break;
        case 'i': showInfo_ = !showInfo_; break;
        case 't': useTextures_ = !useTextures_; break;
        case 'l': useLightmaps_ = !useLightmaps_; break;
        case 'b': showBackdrop_ = !showBackdrop_; break;
        case 'g': showLights_ = !showLights_; break;
        case 'c':
        case 'v':
            if (!clips_.empty()) {
                const int count = int(clips_.size());
                clip_ = (clip_ + (ev.key == 'c' ? 1 : count - 1)) % count;
                clipTime_ = 0;
            }
            break;
        case ' ': playing_ = !playing_; break;
        case 'h': {
            showHidden_ = !showHidden_;
            // Keep the view and the clip.
            const gfx::Vec3 centre = centre_;
            const float yaw = yaw_, pitch = pitch_, distance = distance_, time = clipTime_;
            const int clip = clip_;
            show(index_);
            centre_ = centre;
            yaw_ = yaw;
            pitch_ = pitch;
            distance_ = distance;
            clip_ = clip;
            clipTime_ = time;
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
            const float step = distance_ * 0.1f;
            const gfx::Vec3 fwd{-std::cos(yaw_), -std::sin(yaw_), 0}, right{std::sin(yaw_), -std::cos(yaw_), 0};
            const float f = ev.key == 'w' ? step : ev.key == 's' ? -step : 0;
            const float r = ev.key == 'd' ? step : ev.key == 'a' ? -step : 0;
            const float u = ev.key == 'e' ? step : ev.key == 'q' ? -step : 0;
            for (size_t k = 0; k < 3; ++k)
                centre_[k] += fwd[k] * f + right[k] * r;
            centre_[2] += u;
            break;
        }
        default: break;
        }
        break;
    case EventType::MouseDown:
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
        break;
    case EventType::MouseUp: dragging_ = false; break;
    case EventType::MouseMove:
        if (dragging_) {
            yaw_ -= float(ev.mouseX - lastX_) * 0.01f;
            pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
            lastX_ = ev.mouseX;
            lastY_ = ev.mouseY;
        }
        break;
    case EventType::MouseWheel: distance_ *= ev.wheelY > 0 ? 0.85f : 1 / 0.85f; break;
    default: break;
    }
}

void ModelPage::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    // The clock: the clip moves on while playing; flip-books always.
    const uint64_t step = lastMs_ ? std::min<uint64_t>(nowMs - std::min(nowMs, lastMs_), 100) : 0;
    lastMs_ = nowMs;
    clockMs_ += step;
    if (playing_)
        clipTime_ += float(step) / 1000;
    if (const float ticks = clipTicks(); ticks != posedTicks_)
        pose(ticks);

    const gfx::Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 eye{centre_[0] + offset[0] * distance_, centre_[1] + offset[1] * distance_,
                        centre_[2] + offset[2] * distance_};
    const float zNear = std::max(0.5f, distance_ / 1000), zFar = distance_ + radius_ * 4;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.9f, float(kFrameW) / float(kFrameH), zNear, zFar),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    renderer_->beginFrame(viewProj, eye, {60, 64, 74});

    // Material animators: the colour and alpha keyed now. Colour 1 is
    // taken as the diffuse colour, 3 as emissive (added); 0 is left out.
    const float ticks = posedTicks_;
    for (Item& item : items_) {
        item.color = item.baseColor;
        if (auto a = animatorsOf_.find(item.material); a != animatorsOf_.end())
            for (const FinMaterialAnimator* anim : a->second) {
                if (anim->keys.empty())
                    continue;
                const std::array<float, 3> v = sample(anim->keys, ticks, mix3);
                if (anim->alpha) {
                    item.color[3] = v[0];
                } else if (anim->channel == 1) {
                    item.color = {v[0], v[1], v[2], item.color[3]};
                } else if (anim->channel == 3) {
                    for (size_t k = 0; k < 3; ++k)
                        item.color[k] += v[k];
                }
            }
        if (item.vertexColors && item.kind != Item::Kind::Light)
            item.color = {1, 1, 1, item.color[3]};
    }

    // Opaque shapes first, each with its lightmap multiplied over it; then
    // those that blend, the farthest first.
    std::vector<const Item*> order;
    auto drawn = [&](const Item& item) {
        return item.visible && item.mesh && (showBackdrop_ || !item.backdrop) &&
               (item.kind != Item::Kind::Light || showLights_);
    };
    for (const Item& item : items_)
        if (!item.blend && drawn(item))
            order.push_back(&item);
    const size_t opaque = order.size();
    for (const Item& item : items_)
        if (item.blend && drawn(item))
            order.push_back(&item);
    auto distance2 = [&](const Item* i) {
        const gfx::Vec3 c = i->billboard ? gfx::Vec3{i->billboardAt[12], i->billboardAt[13], i->billboardAt[14]} : i->centre;
        return (c[0] - eye[0]) * (c[0] - eye[0]) + (c[1] - eye[1]) * (c[1] - eye[1]) + (c[2] - eye[2]) * (c[2] - eye[2]);
    };
    std::stable_sort(order.begin() + std::ptrdiff_t(opaque), order.end(),
                     [&](const Item* a, const Item* b) { return distance2(a) > distance2(b); });
    const float markerSize = radius_ * 0.03f;
    for (const Item* drawnItem : order) {
        const Item& item = *drawnItem;
        gfx::DrawCall call;
        call.mesh = item.mesh;
        call.indexCount = item.indexCount;
        call.model = item.billboard ? gfx::multiply(facingCamera(item.billboardAt, eye), item.rel) : item.world;
        if (item.kind == Item::Kind::Light) {
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
        // A flip-book's frame: each lasts 160 ticks of its own time (the
        // clock times its frequency, plus its phase).
        size_t frame = 0;
        if (item.flip && item.frames.size() > 1) {
            const double own = (double(clockMs_) / 1000 * kTicksPerSecond + item.flip->phase) * item.flip->frequency;
            frame = size_t(std::max(0.0, own / 160)) % item.frames.size();
        }
        call.texture = useTextures_ && !item.frames.empty() ? texture(item.frames[frame]) : 0;
        if ((item.kind == Item::Kind::Glow && !call.texture) || item.kind == Item::Kind::Light)
            call.texture = glowTexture();
        call.color = item.color;
        call.rgbSource = item.vertexColors && item.kind != Item::Kind::Light ? gfx::ColorSource::Vertex
                         : item.lit                                          ? gfx::ColorSource::Lighting
                                                                             : gfx::ColorSource::Constant;
        call.alphaSource = item.vertexColors ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
        // A light over the viewer's shoulder.
        call.ambientLight = {0.45f, 0.45f, 0.45f};
        call.directedLight = {0.6f, 0.6f, 0.6f};
        call.lightDirection = {offset[0] * 0.6f - offset[1] * 0.5f, offset[1] * 0.6f + offset[0] * 0.5f, 0.7f};
        // Back faces culled, as Direct3D does by default and nothing in
        // the files asks otherwise (closed shapes that blend, such as the
        // skeletons' heads, would otherwise show their insides);
        // billboards are seen from the front anyway.
        call.state.cull = item.billboard ? gfx::Cull::None : gfx::Cull::Back;
        call.state.depthTest = item.depthTest;
        call.state.depthWrite = item.depthWrite;
        if (item.blend) {
            call.state.src = item.src;
            call.state.dst = item.dst;
            call.state.alphaTest = gfx::AlphaTest::Gt0;
        }
        renderer_->draw(call);
        if (useLightmaps_ && !item.lightmap.empty() && !item.blend) {
            if (const gfx::TextureId lm = texture(item.lightmap)) {
                gfx::DrawCall light = call;
                light.texture = lm;
                light.texGen = gfx::TexGen::Uv2;
                light.color = {1, 1, 1, 1};
                light.rgbSource = light.alphaSource = gfx::ColorSource::Constant;
                light.state.src = gfx::Blend::DstColor;
                light.state.dst = gfx::Blend::Zero;
                light.state.depthEqual = true;
                light.state.depthWrite = false;
                renderer_->draw(light);
            }
        }
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (showInfo_) {
        lines = info_;
        if (clips_.size() > 1 || (!clips_.empty() && clips_.front().end > 0)) {
            const FinActor::Clip& c = clips_[size_t(clip_)];
            char at[64];
            std::snprintf(at, sizeof at, "%.2f s of %.2f", c.end > c.start ? std::fmod(clipTime_, c.end - c.start) : 0.0f,
                          c.end - c.start);
            lines.push_back("Clip " + std::to_string(clip_ + 1) + "/" + std::to_string(clips_.size()) + ": " + c.name +
                            "  " + at + (playing_ ? "" : " (paused)") + "   C/V: clip   Space: pause");
        }
        lines.push_back("Left/Right: model   drag: turn   wheel: zoom   W/A/S/D/Q/E: move   R: reset   I: info");
        lines.push_back(std::string("T: textures (") + (useTextures_ ? "on" : "off") + ")   L: lightmaps (" +
                        (useLightmaps_ ? "on" : "off") + ")   B: sky (" + (showBackdrop_ ? "on" : "off") +
                        ")   G: lights (" + (showLights_ ? "on" : "off") + ")   H: hidden (" +
                        std::to_string(hiddenCount_) + ", " + (showHidden_ ? "shown" : "not shown") + ")");
    }
    if (!error_.empty())
        lines.push_back(error_);
    if (!lines.empty()) {
        const int lh = font::kLineHeight;
        size_t widest = 0;
        for (const std::string& l : lines)
            widest = std::max(widest, font::length(l));
        const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8), boxH = int(lines.size()) * lh + 6;
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                overlay_.row(y)[x * 4 + 3] = 150;
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(overlay_, 4, 3 + int(i) * lh, lines[i],
                           lines[i] == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::netimmerse
