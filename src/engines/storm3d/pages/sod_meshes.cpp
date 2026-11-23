// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/sod_meshes.h"

#include "common/text.h"
#include "engines/storm3d/pages/model_page.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::storm3d {

void SodMeshes::clear() {
    if (_renderer) {
        for (const gfx::MeshId m : _meshes)
            _renderer->destroyMesh(m);
        for (const auto &[name, id] : _textures)
            if (id)
                _renderer->destroyTexture(id);
    }
    _meshes.clear();
    _textures.clear();
    _models.clear();
    _quads.clear();
}

gfx::TextureId SodMeshes::texture(const std::string &name) {
    const std::string key = text::toLower(name);
    if (const auto it = _textures.find(key); it != _textures.end())
        return it->second;
    gfx::TextureId id = 0;
    // The full-colour texture first, as the Images page shows them.
    for (const char *folder : {"Textures/RGB/", "Textures/Index8/"}) {
        std::vector<uint8_t> data;
        image::Image img;
        if (_files.read(folder + name + ".tga", data) || image::decode(data, img))
            continue;
        if (_renderer->createTexture(img.w, img.h, img.rgba.data(), gfx::TextureParams{}, id))
            id = 0;
        break;
    }
    _textures[key] = id;
    return id;
}

SodMeshes::Built SodMeshes::build(const SodModel &model, int level) {
    Built built;
    if (!_renderer)
        return built;
    const std::vector<gfx::Mat4> world = ModelPage::worldMatrices(model);
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const SodModel::Node &node = model.nodes[i];
        if (node.mesh < 0 || (level >= 0 && !ModelPage::shownAtLevel(model, i, level)))
            continue;
        const SodModel::Mesh &mesh = model.meshes[size_t(node.mesh)];
        // Normals by vertex, the faces' around it added up, for smooth
        // shading. The faces wind so that (c - a) x (b - a) points out: the
        // other way, 4first's moon is dark where the game's draws light it.
        std::vector<gfx::Vec3> normals(mesh.vertices.size(), gfx::Vec3{0, 0, 0});
        for (const SodModel::Group &g : mesh.groups)
            for (const SodModel::Face &f : g.faces) {
                const auto &a = mesh.vertices[f.vertex[0]];
                const auto &b = mesh.vertices[f.vertex[1]];
                const auto &c = mesh.vertices[f.vertex[2]];
                const gfx::Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const gfx::Vec3 v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const gfx::Vec3 n{v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0]};
                for (const uint16_t k : f.vertex)
                    for (int d = 0; d < 3; ++d)
                        normals[k][size_t(d)] += n[size_t(d)];
            }
        std::vector<gfx::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Part> parts;
        for (const SodModel::Group &g : mesh.groups) {
            Part part;
            part.firstIndex = uint32_t(indices.size());
            for (const SodModel::Face &f : g.faces)
                for (int k = 0; k < 3; ++k) {
                    gfx::Vertex v{};
                    const auto &p = mesh.vertices[f.vertex[size_t(k)]];
                    const auto &uv = mesh.uvs[f.uv[size_t(k)]];
                    gfx::Vec3 n = normals[f.vertex[size_t(k)]];
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    for (int d = 0; d < 3; ++d) {
                        v.pos[d] = p[size_t(d)];
                        v.normal[d] = len > 0 ? n[size_t(d)] / len : 0;
                    }
                    v.uv[0] = uv[0];
                    v.uv[1] = uv[1];
                    std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
                    indices.push_back(uint32_t(vertices.size()));
                    vertices.push_back(v);
                    const gfx::Vec3 at = gfx::transformPoint(world[i], {p[0], p[1], p[2]});
                    for (int d = 0; d < 3; ++d) {
                        built.lo[size_t(d)] = std::min(built.lo[size_t(d)], at[size_t(d)]);
                        built.hi[size_t(d)] = std::max(built.hi[size_t(d)], at[size_t(d)]);
                    }
                }
            part.indexCount = uint32_t(indices.size()) - part.firstIndex;
            for (const SodModel::LightingMaterial &m : model.materials)
                if (m.name == g.material)
                    part.colour = {std::min(1.0f, m.diffuse[0]), std::min(1.0f, m.diffuse[1]),
                                   std::min(1.0f, m.diffuse[2]), 1};
            parts.push_back(part);
        }
        if (indices.empty())
            continue;
        gfx::MeshId id = 0;
        if (_renderer->createMesh(vertices, indices, id))
            continue;
        _meshes.push_back(id);
        const gfx::TextureId tex = texture(mesh.texture);
        for (Part &part : parts) {
            part.mesh = id;
            part.texture = tex;
            part.world = world[i];
            part.drawMode = mesh.drawMode;
            built.parts.push_back(part);
        }
    }
    addSprites(model, world, built);
    addEmitters(model, world, built);
    return built;
}

const SpriteTable *SodMeshes::sprites() {
    if (!_spriteTable) {
        _spriteTable.emplace();
        const SpriteTable::Loader loader = [this](std::string_view name, std::string &text) {
            std::vector<uint8_t> data;
            if (Error err = _files.read("Sprites/" + std::string(name), data))
                return err;
            text.assign(data.begin(), data.end());
            return Error::success();
        };
        // A table that fails leaves what was read before it: those sprites
        // are drawn, the rest aren't.
        _spriteTable->load("sprites.spr", loader);
    }
    return &*_spriteTable;
}

// Each sprite node's sprite: a unit square of its rectangle of the texture
// (made once a sprite), placed at the node.
void SodMeshes::addSprites(const SodModel &model, const std::vector<gfx::Mat4> &world, Built &built) {
    for (size_t i = 0; i < model.nodes.size(); ++i)
        if (model.nodes[i].type == SodModel::kSprite)
            addSprite(sprites()->spriteNodeFor(model.nodes[i].name), world[i], built);
}

// A sprite node's sprite: a unit square of its rectangle of the texture
// (made once a sprite), placed at `world`.
void SodMeshes::addSprite(const SpriteTable::SpriteNode *node, const gfx::Mat4 &world, Built &built) {
    const SpriteTable *table = sprites();
    const SpriteTable::Sprite *sprite = node ? table->sprite(node->sprite) : nullptr;
    if (!sprite || !_renderer)
        return;
    SpritePart part;
    part.texture = texture(sprite->texture);
    if (!part.texture)
        return;
    auto quad = _quads.find(sprite->name);
    if (quad == _quads.end()) {
        const float rw = float(sprite->referenceWidth > 0 ? sprite->referenceWidth : 1);
        const float rh = float(sprite->referenceHeight > 0 ? sprite->referenceHeight : 1);
        const float u0 = float(sprite->u) / rw;
        const float v0 = float(sprite->v) / rh;
        const float u1 = float(sprite->u + sprite->w) / rw;
        const float v1 = float(sprite->v + sprite->h) / rh;
        std::vector<gfx::Vertex> vertices(4);
        const float corners[4][4] = {
            {-0.5f, 0.5f, u0, v0}, {0.5f, 0.5f, u1, v0}, {0.5f, -0.5f, u1, v1}, {-0.5f, -0.5f, u0, v1}};
        for (size_t k = 0; k < 4; ++k) {
            vertices[k].pos[0] = corners[k][0];
            vertices[k].pos[1] = corners[k][1];
            vertices[k].normal[2] = 1;
            vertices[k].uv[0] = corners[k][2];
            vertices[k].uv[1] = corners[k][3];
            std::fill(std::begin(vertices[k].color), std::end(vertices[k].color), uint8_t(255));
        }
        gfx::MeshId id = 0;
        if (_renderer->createMesh(vertices, {0, 1, 2, 0, 2, 3}, id))
            return;
        _meshes.push_back(id);
        quad = _quads.emplace(sprite->name, id).first;
    }
    part.quad = quad->second;
    part.world = world;
    // A sprite node's size is half its width and height: the planets'
    // glows, 495 for planets of 450, show as rims just outside them only so.
    part.width = float(node->width) * 2;
    part.height = float(node->height) * 2;
    for (size_t k = 0; k < 3; ++k)
        part.colour[k] = float(node->colour[k]);
    const SpriteTable::Animation *a = table->animation(node->animation);
    if (a && a->kind == "colour" && !a->keyframes.empty())
        part.colourAnimation = a;
    part.billboard = node->billboard;
    part.additive = sprite->material != "alphathreshold";
    built.sprites.push_back(part);
    // The sprite counts towards the model's size.
    const gfx::Vec3 at = gfx::transformPoint(world, {0, 0, 0});
    const float r = std::max(part.width, part.height) / 2;
    for (size_t d = 0; d < 3; ++d) {
        built.lo[d] = std::min(built.lo[d], at[d] - r);
        built.hi[d] = std::max(built.hi[d], at[d] + r);
    }
}

const SodMeshes::Built *SodMeshes::spriteNode(const std::string &name) {
    const std::string key = "@" + text::toLower(name);
    if (const auto it = _models.find(key); it != _models.end())
        return it->second ? &*it->second : nullptr;
    std::optional<Built> &built = _models[key];
    // By its own name only: a sprite node is an object's when its
    // definition is named for it (the planets' "Nimbus").
    const SpriteTable *table = sprites();
    for (const SpriteTable::SpriteNode &n : table->spriteNodes())
        if (text::equalsIgnoreCase(n.name, name)) {
            Built b;
            addSprite(&n, gfx::identity(), b);
            if (!b.sprites.empty())
                built = std::move(b);
            break;
        }
    return built ? &*built : nullptr;
}

// Each emitter node's emitter, by name: its settings, and its particles'
// sprite as a sprite part placed at the node.
void SodMeshes::addEmitters(const SodModel &model, const std::vector<gfx::Mat4> &world, Built &built) {
    const SpriteTable *table = sprites();
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].type != SodModel::kEmitter)
            continue;
        const SpriteTable::Emitter *emitter = nullptr;
        for (const SpriteTable::Emitter &e : table->emitters())
            if (text::equalsIgnoreCase(e.name, model.nodes[i].emitter))
                emitter = &e;
        if (!emitter)
            continue;
        auto setting = [emitter](const char *key, float fallback) {
            const auto it = emitter->settings.find(key);
            return it == emitter->settings.end() ? fallback : float(std::atof(it->second.c_str()));
        };
        const auto kind = emitter->settings.find("type");
        const auto sprite = emitter->settings.find("sprite");
        if (kind == emitter->settings.end() || sprite == emitter->settings.end() ||
            (kind->second != "spray" && kind->second != "cloud"))
            continue; // a beam, or one with no sprite: not drawn
        Built one;
        addSprite(table->spriteNodeFor(sprite->second), world[i], one);
        if (one.sprites.empty())
            continue;
        EmitterPart part;
        part.kind = kind->second == "cloud" ? EmitterPart::Kind::kCloud : EmitterPart::Kind::kSpray;
        part.particle = one.sprites[0];
        part.period = std::max(0.001f, setting("firing_period", 0.1f));
        part.lifespan = std::max(0.01f, setting("lifespan", 1));
        part.velocity = setting("velocity", 0);
        part.spread = setting("spread_angle", 0);
        part.expansion = setting("expansion_rate", 0);
        part.count = int(setting("particle_count", 0));
        part.radius = setting("cloud_radius", 0);
        part.seed = uint32_t(i * 2654435761u) ^ uint32_t(built.emitters.size() + 1);
        // emitter.spr's "Damage emitters" (plasma and steam venting) hang
        // under the ship's systems: they run when the system is damaged.
        static const char *const kSystems[] = {"damage", "crew", "engines", "life", "sensors", "shield", "weapons"};
        for (const char *system : kSystems)
            if (text::equalsIgnoreCase(model.nodes[i].parent, system))
                part.damage = true;
        built.emitters.push_back(part);
    }
}

std::vector<SodMeshes::Particle> SodMeshes::particlesAt(const EmitterPart &e, double seconds) {
    // A number from 0 to 1 for each particle and draw, the same each time.
    auto unit = [](uint32_t a, uint32_t b, uint32_t c) {
        uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ c * 0xc2b2ae35u;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return float(h & 0xffffff) / float(0x1000000);
    };
    const gfx::Mat4 &w = e.particle.world;
    const gfx::Vec3 origin{w[12], w[13], w[14]};
    const gfx::Vec3 axis{w[8], w[9], w[10]}; // its front: the third row of its transform
    auto particle = [&](float age, const gfx::Vec3 &from, const gfx::Vec3 &dir) {
        Particle p;
        for (size_t d = 0; d < 3; ++d)
            p.at[d] = from[d] + dir[d] * e.velocity * age;
        p.size = std::max(0.0f, 1 + e.expansion * age);
        p.colour = e.particle.colourAnimation ? colourAt(*e.particle.colourAnimation, age) : e.particle.colour;
        return p;
    };
    // A direction within `degrees` of `axis`, or anywhere if it's 180.
    auto within = [&](float degrees, uint32_t k, uint32_t c) {
        const float cone = degrees * 3.14159265f / 180 * unit(e.seed, k, c);
        const float around = 6.2831853f * unit(e.seed, k, c + 1);
        // Two axes square to the emitter's.
        gfx::Vec3 a{axis[1], -axis[0], 0};
        if (std::fabs(axis[2]) > 0.9f)
            a = {0, axis[2], -axis[1]};
        const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        for (float &v : a)
            v /= la;
        const gfx::Vec3 b{axis[1] * a[2] - axis[2] * a[1], axis[2] * a[0] - axis[0] * a[2],
                          axis[0] * a[1] - axis[1] * a[0]};
        gfx::Vec3 d{};
        for (size_t i = 0; i < 3; ++i)
            d[i] = axis[i] * std::cos(cone) + (a[i] * std::cos(around) + b[i] * std::sin(around)) * std::sin(cone);
        return d;
    };
    std::vector<Particle> out;
    if (e.kind == EmitterPart::Kind::kSpray) {
        // Fired at each multiple of the period: those of the last lifespan.
        const double last = std::floor(seconds / e.period);
        for (int k = 0; k < 400; ++k) {
            const double n = last - k;
            const float age = float(seconds - n * e.period);
            if (age >= e.lifespan)
                break;
            const uint32_t id = uint32_t(int64_t(n));
            out.push_back(particle(age, origin, within(e.spread, id, 0)));
        }
    } else {
        // Each renewed in its own time, somewhere new within the radius.
        for (int i = 0; i < e.count && i < 400; ++i) {
            const double t = seconds + double(unit(e.seed, uint32_t(i), 7)) * e.lifespan;
            const uint32_t round = uint32_t(int64_t(std::floor(t / e.lifespan)));
            const float age = float(t - std::floor(t / e.lifespan) * e.lifespan);
            const gfx::Vec3 dir = within(180, uint32_t(i), round * 4 + 11);
            const float r = e.radius * std::cbrt(unit(e.seed, uint32_t(i), round * 4 + 13));
            const gfx::Vec3 from{origin[0] + dir[0] * r, origin[1] + dir[1] * r, origin[2] + dir[2] * r};
            out.push_back(particle(age, from, within(180, uint32_t(i), round * 4 + 17)));
        }
    }
    return out;
}

std::array<float, 3> SodMeshes::colourAt(const SpriteTable::Animation &a, double seconds) {
    const std::vector<std::vector<double>> &keys = a.keyframes;
    auto colour = [](const std::vector<double> &row) {
        std::array<float, 3> c{1, 1, 1};
        for (size_t k = 0; k < 3 && k + 1 < row.size(); ++k)
            c[k] = float(row[k + 1]);
        return c;
    };
    if (keys.empty())
        return {1, 1, 1};
    const double t = a.duration > 0 ? std::fmod(seconds, a.duration) : 0;
    if (keys[0].empty() || t <= keys[0][0])
        return colour(keys[0]);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (keys[i].empty() || t > keys[i][0])
            continue;
        const std::array<float, 3> from = colour(keys[i - 1]);
        if (a.interpolation == "step")
            return from;
        const std::array<float, 3> to = colour(keys[i]);
        const double span = keys[i][0] - keys[i - 1][0];
        const float f = span > 0 ? float((t - keys[i - 1][0]) / span) : 1;
        return {from[0] + (to[0] - from[0]) * f, from[1] + (to[1] - from[1]) * f, from[2] + (to[2] - from[2]) * f};
    }
    return colour(keys.back());
}

const SodMeshes::Built *SodMeshes::model(const std::string &name) {
    const std::string key = text::toLower(name);
    if (const auto it = _models.find(key); it != _models.end())
        return it->second ? &*it->second : nullptr;
    std::optional<Built> &built = _models[key];
    std::vector<uint8_t> data;
    SodModel model;
    if (_files.read("SOD/" + name + ".sod", data) || model.parse(data))
        return nullptr;
    const std::vector<int> levels = ModelPage::detailLevels(model);
    // The higher a level's number, the more detailed it is.
    built = build(model, levels.empty() ? -1 : levels.back());
    return &*built;
}

void SodMeshes::drawPart(const Part &part, const gfx::Mat4 &place, bool behind) const {
    // A background is added up, onto the frame cleared black: the game draws
    // MbgKlin3's star sphere (one, zero) and its nebula's cube over it (one,
    // one), which comes to the same.
    const bool additive = behind || part.drawMode == "additive" || part.drawMode == "wormhole";
    gfx::DrawCall call;
    call.mesh = part.mesh;
    call.firstIndex = part.firstIndex;
    call.indexCount = part.indexCount;
    call.model = gfx::multiply(place, part.world);
    call.texture = part.texture;
    call.color = part.colour;
    // Both sides drawn: whether the game culls the back ones isn't known.
    call.state.cull = gfx::Cull::kNone;
    if (additive) {
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
        call.state.depthWrite = false;
    } else {
        call.rgbSource = gfx::ColorSource::kLighting;
        call.ambientLight = _lighting.ambient;
        for (size_t i = 0; i < _lighting.lights.size() && i < 3; ++i) {
            const Lighting::Directed &l = _lighting.lights[i];
            if (i == 0)
                call.directedLight = l.colour, call.lightDirection = l.toLight;
            else
                call.fillLight[i - 1] = l.colour, call.fillDirection[i - 1] = l.toLight;
        }
        if (part.drawMode == "alphathreshold")
            call.state.alphaTest = gfx::AlphaTest::kGe128;
    }
    if (behind) {
        call.state.depthTest = false;
        call.state.depthWrite = false;
    }
    _renderer->draw(call);
}

void SodMeshes::drawSprite(const SpritePart &sprite, const gfx::Mat4 &place) const {
    const gfx::Mat4 node = gfx::multiply(place, sprite.world);
    // How much the model is scaled: its node's first axis' length.
    const float scale = std::sqrt(node[0] * node[0] + node[1] * node[1] + node[2] * node[2]);
    gfx::Mat4 m = gfx::identity();
    if (sprite.billboard) {
        // Facing the camera: the screen's axes, at the node.
        for (size_t d = 0; d < 3; ++d) {
            m[d] = _right[d] * sprite.width * scale;
            m[4 + d] = _up[d] * sprite.height * scale;
            m[12 + d] = node[12 + d];
        }
        m[10] = 1;
    } else {
        gfx::Mat4 size = gfx::identity();
        size[0] = sprite.width;
        size[5] = sprite.height;
        m = gfx::multiply(node, size);
    }
    gfx::DrawCall call;
    call.mesh = sprite.quad;
    call.firstIndex = 0;
    call.indexCount = 6;
    call.model = m;
    call.texture = sprite.texture;
    const std::array<float, 3> c = sprite.colourAnimation ? colourAt(*sprite.colourAnimation, _seconds) : sprite.colour;
    call.color = {c[0], c[1], c[2], 1};
    call.state.cull = gfx::Cull::kNone;
    call.state.depthWrite = false;
    if (sprite.additive) {
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
    } else {
        call.state.alphaTest = gfx::AlphaTest::kGe128;
    }
    _renderer->draw(call);
}

void SodMeshes::draw(const Built &built, const gfx::Mat4 &place, bool behind) const {
    draw({{&built, place}}, behind);
}

void SodMeshes::draw(const std::vector<std::pair<const Built *, gfx::Mat4>> &batch, bool behind) const {
    if (!_renderer)
        return;
    // Opaque parts first, then the additive ones and the sprites over them.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto &[built, place] : batch)
            for (const Part &part : built->parts)
                if ((part.drawMode == "additive" || part.drawMode == "wormhole") == (pass == 1))
                    drawPart(part, place, behind);
    if (behind)
        return;
    for (const auto &[built, place] : batch)
        for (const SpritePart &sprite : built->sprites)
            drawSprite(sprite, place);
    for (const auto &[built, place] : batch)
        for (const EmitterPart &emitter : built->emitters)
            for (const Particle &p :
                 emitter.damage && !_showDamage ? std::vector<Particle>{} : particlesAt(emitter, _seconds)) {
                SpritePart sprite = emitter.particle;
                // At the particle, with the emitter node's own axes.
                sprite.world[12] = p.at[0];
                sprite.world[13] = p.at[1];
                sprite.world[14] = p.at[2];
                sprite.width *= p.size;
                sprite.height *= p.size;
                sprite.colourAnimation = nullptr;
                sprite.colour = p.colour;
                drawSprite(sprite, place);
            }
}

} // namespace reluva::storm3d
