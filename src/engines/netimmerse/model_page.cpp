// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Models page: each .FIN model drawn with the GPU renderer, as the
// scene graph places its shapes, with their textures, lightmaps, vertex
// colours, materials and alpha.

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
    uint32_t texture = 0, multiTexture = 0, material = 0, alpha = 0, vertexColor = 0;
};

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
gfx::Mat4 localMatrix(const FinAvObject& o) {
    gfx::Mat4 m = gfx::identity();
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            m[size_t(col * 4 + row)] = o.rotation[size_t(row * 3 + col)] * o.scale;
    m[12] = o.translation[0];
    m[13] = o.translation[1];
    m[14] = o.translation[2];
    return m;
}

constexpr int kFrameW = 640, kFrameH = 480;

} // namespace

struct ModelPage::Item {
    gfx::MeshId mesh = 0;
    uint32_t indexCount = 0;
    gfx::Mat4 world = gfx::identity();
    std::string texture, lightmap; // image names
    std::array<float, 4> color{1, 1, 1, 1};
    bool vertexColors = false, lit = false;
    bool blend = false;
    bool backdrop = false; // under a level's "Exterior" node: its sky
    gfx::Blend src = gfx::Blend::One, dst = gfx::Blend::Zero;
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

void ModelPage::deactivate() {
    // The renderer's textures and meshes go with it when another page
    // takes the screen.
    if (renderer_) {
        for (gfx::MeshId m : meshes_)
            renderer_->destroyMesh(m);
        for (const auto& [name, id] : textures_)
            if (id)
                renderer_->destroyTexture(id);
    }
    meshes_.clear();
    textures_.clear();
    items_.clear();
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

void ModelPage::show(int index) {
    const int n = int(names_.size());
    index_ = ((index % n) + n) % n;
    for (gfx::MeshId m : meshes_)
        renderer_->destroyMesh(m);
    meshes_.clear();
    items_.clear();
    info_.clear();
    error_.clear();
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

    // Walk the scene graph from the roots, as the game would draw it: the
    // nearest level of a level-of-detail node only.
    std::array<float, 3> lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
    std::array<float, 3> hi{-lo[0], -lo[1], -lo[2]};
    unsigned shapes = 0, triangles = 0, missing = 0;
    std::vector<std::string> missingNames;
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
        }
        return in;
    };
    // The levels' sky is under a node named "Exterior": it's left out of
    // the view's framing, so the camera starts on the level itself.
    std::function<void(uint32_t, const gfx::Mat4&, const Inherited&, int, bool)> visit =
        [&](uint32_t id, const gfx::Mat4& parent, const Inherited& inherited, int depth, bool backdrop) {
            if (depth > 64)
                return;
            if (auto node = model_.nodes.find(id); node != model_.nodes.end()) {
                const gfx::Mat4 world = gfx::multiply(parent, localMatrix(node->second));
                const Inherited in = applyProperties(node->second, inherited);
                const bool sky = backdrop || node->second.name == "Exterior";
                const auto& children = node->second.children;
                const size_t count = node->second.levelsOfDetail ? std::min<size_t>(1, children.size()) : children.size();
                for (size_t i = 0; i < count; ++i)
                    visit(children[i], world, in, depth + 1, sky);
                return;
            }
            auto shapeIt = model_.shapes.find(id);
            if (shapeIt == model_.shapes.end())
                return;
            const FinShape& s = shapeIt->second;
            if (s.triangles.empty() || s.vertices.empty())
                return;
            const Inherited in = applyProperties(s, inherited);
            Item item;
            item.world = gfx::multiply(parent, localMatrix(s));
            item.backdrop = backdrop;

            // Images: the texture's first frame, the multi-texture's
            // lightmap. The texture uses the first uv set, the lightmap the
            // last.
            if (auto t = model_.textures.find(in.texture); t != model_.textures.end() && !t->second.empty())
                if (auto img = model_.images.find(t->second.front()); img != model_.images.end())
                    item.texture = img->second;
            if (auto mt = model_.multiTextures.find(in.multiTexture); mt != model_.multiTextures.end())
                if (auto img = model_.images.find(mt->second); img != model_.images.end())
                    item.lightmap = img->second;
            if (auto mat = model_.materials.find(in.material); mat != model_.materials.end()) {
                item.color = {mat->second.diffuse[0], mat->second.diffuse[1], mat->second.diffuse[2], mat->second.alpha};
            }
            if (auto a = model_.alphas.find(in.alpha); a != model_.alphas.end() && a->second.blend) {
                item.blend = true;
                item.src = blendOf(a->second.source);
                item.dst = blendOf(a->second.destination);
            }
            item.vertexColors = !s.colors.empty();
            item.lit = !item.vertexColors && !s.normals.empty();
            if (item.vertexColors)
                item.color = {1, 1, 1, item.color[3]};

            const size_t nv = s.vertexCount;
            const size_t lightSet = s.uvSets ? s.uvSets - 1u : 0;
            std::vector<gfx::Vertex> verts(nv);
            for (size_t v = 0; v < nv; ++v) {
                gfx::Vertex& out = verts[v];
                for (int k = 0; k < 3; ++k)
                    out.pos[k] = s.vertices[v * 3 + size_t(k)];
                if (!s.normals.empty())
                    for (int k = 0; k < 3; ++k)
                        out.normal[k] = s.normals[v * 3 + size_t(k)];
                if (s.uvSets) {
                    out.uv[0] = s.uvw[v * 3];
                    out.uv[1] = s.uvw[v * 3 + 1];
                    out.uv2[0] = s.uvw[(lightSet * nv + v) * 3];
                    out.uv2[1] = s.uvw[(lightSet * nv + v) * 3 + 1];
                }
                for (int k = 0; k < 4; ++k)
                    out.color[k] = s.colors.empty() ? 255
                                                    : uint8_t(std::clamp(s.colors[v * 4 + size_t(k)], 0.0f, 1.0f) * 255 + 0.5f);
                const gfx::Vec3 w = gfx::transformPoint(item.world, {out.pos[0], out.pos[1], out.pos[2]});
                for (int k = 0; k < 3 && !backdrop; ++k) {
                    lo[size_t(k)] = std::min(lo[size_t(k)], w[size_t(k)]);
                    hi[size_t(k)] = std::max(hi[size_t(k)], w[size_t(k)]);
                }
            }
            std::vector<uint32_t> indices(s.triangles.begin(), s.triangles.end());
            if (renderer_->createMesh(verts, indices, item.mesh))
                return;
            meshes_.push_back(item.mesh);
            item.indexCount = uint32_t(indices.size());
            for (const std::string* img : {&item.texture, &item.lightmap})
                if (!img->empty() && !texture(*img)) {
                    ++missing;
                    if (missingNames.size() < 3)
                        missingNames.push_back(*img);
                }
            ++shapes;
            triangles += uint32_t(indices.size() / 3);
            items_.push_back(std::move(item));
        };
    for (uint32_t root : model_.roots())
        visit(root, gfx::identity(), Inherited{}, 0, false);

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
    resetView();

    unsigned unread = 0;
    for (const auto& o : model_.objects())
        unread += !o.read;
    info_.push_back(name + ": " + std::to_string(shapes) + " shapes, " + std::to_string(triangles) + " triangles, " +
                    std::to_string(model_.objects().size()) + " objects (" + std::to_string(unread) +
                    " not read: animation, skins, lights)");
    if (missing) {
        std::string m = std::to_string(missing) + " images not found:";
        for (const std::string& n : missingNames)
            m += " " + n;
        info_.push_back(m);
    }
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

void ModelPage::update(uint64_t) {
    if (!renderer_)
        return;
    const gfx::Vec3 offset{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
    const gfx::Vec3 eye{centre_[0] + offset[0] * distance_, centre_[1] + offset[1] * distance_,
                        centre_[2] + offset[2] * distance_};
    const float zNear = std::max(0.5f, distance_ / 1000), zFar = distance_ + radius_ * 4;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.9f, float(kFrameW) / float(kFrameH), zNear, zFar),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    renderer_->beginFrame(viewProj, eye, {60, 64, 74});

    // Opaque shapes first, then those that blend, each with its lightmap
    // multiplied over it.
    for (int pass = 0; pass < 2; ++pass)
        for (const Item& item : items_) {
            if (item.blend != (pass == 1) || (item.backdrop && !showBackdrop_))
                continue;
            gfx::DrawCall call;
            call.mesh = item.mesh;
            call.indexCount = item.indexCount;
            call.model = item.world;
            call.texture = useTextures_ && !item.texture.empty() ? texture(item.texture) : 0;
            call.color = item.color;
            call.rgbSource = item.vertexColors ? gfx::ColorSource::Vertex
                             : item.lit        ? gfx::ColorSource::Lighting
                                               : gfx::ColorSource::Constant;
            call.alphaSource = item.vertexColors ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
            // A light over the viewer's shoulder.
            call.ambientLight = {0.45f, 0.45f, 0.45f};
            call.directedLight = {0.6f, 0.6f, 0.6f};
            call.lightDirection = {offset[0] * 0.6f - offset[1] * 0.5f, offset[1] * 0.6f + offset[0] * 0.5f, 0.7f};
            call.state.cull = gfx::Cull::None; // two-sided, as the exporter's quads often are
            if (item.blend) {
                call.state.src = item.src;
                call.state.dst = item.dst;
                call.state.depthWrite = false;
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
        lines.push_back("Left/Right: model   drag: turn   wheel: zoom   W/A/S/D/Q/E: move   R: reset   I: info");
        lines.push_back(std::string("T: textures (") + (useTextures_ ? "on" : "off") + ")   L: lightmaps (" +
                        (useLightmaps_ ? "on" : "off") + ")   B: sky (" + (showBackdrop_ ? "on" : "off") + ")");
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
