// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: a model drawn with the GPU renderer, its
// parts placed by their .mtx files, lit by a fixed light.

#include "engines/age/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;

// Finest first, as the meshes' names end (BODY_H, BODY_M...).
const char* const kLevels[] = {"H", "M", "L", "VL"};

int levelRank(const std::string& level) {
    for (int i = 0; i < 4; ++i)
        if (level == kLevels[i])
            return i;
    return -1;
}

// "geometry/vpbug.pkg" -> "geometry/vpbug"
std::string stem(const std::string& name) { return name.substr(0, name.rfind('.')); }

} // namespace

ModelViewer::ModelViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("geometry/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".pkg") == 0)
            names_.push_back(n);
    }
}

ModelViewer::~ModelViewer() { deactivate(); }

void ModelViewer::freeGpu() {
    if (!renderer_)
        return;
    if (mesh_)
        renderer_->destroyMesh(mesh_);
    mesh_ = 0;
    draws_.clear();
    for (auto& [name, t] : textures_)
        if (t.id)
            renderer_->destroyTexture(t.id);
    textures_.clear();
}

void ModelViewer::deactivate() {
    freeGpu();
    renderer_ = nullptr;
}

void ModelViewer::activate() {
    if (Error err = ctx_.sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    show(index_);
}

void ModelViewer::show(int index) {
    index_ = index;
    const std::string& name = names_[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");
    freeGpu();
    error_.clear();
    levels_.clear();
    level_ = 0;
    paintJob_ = 0;
    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(name, data))
        error_ = err.message;
    else if (Error e = readPkg(data, pkg_))
        error_ = e.message;
    if (!error_.empty()) {
        pkg_ = Pkg{};
        return;
    }
    for (const char* l : kLevels)
        for (const PkgMesh& m : pkg_.meshes)
            if (m.level() == l) {
                levels_.push_back(l);
                break;
            }
    build();

    // Frame the finest level's bounds.
    Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 hi{-lo[0], -lo[1], -lo[2]};
    bool any = false;
    for (const PkgMesh& m : pkg_.meshes) {
        if (!levels_.empty() && levelRank(m.level()) >= 0 && m.level() != levels_.front())
            continue;
        Vec3 at{};
        Mtx mtx;
        std::vector<uint8_t> placement;
        if (!m.part().empty() && !ctx_.files.read(stem(name) + "_" + m.part() + ".mtx", placement) &&
            !readMtx(placement, mtx))
            at = mtx.origin;
        for (const PkgGroup& g : m.groups)
            for (const PkgVertex& v : g.vertices)
                for (size_t k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], v.pos[k] + at[k]);
                    hi[k] = std::max(hi[k], v.pos[k] + at[k]);
                    any = true;
                }
    }
    radius_ = 1;
    center_ = {0, 0, 0};
    if (any) {
        for (size_t k = 0; k < 3; ++k)
            center_[k] = (lo[k] + hi[k]) / 2;
        radius_ = std::max(0.1f, std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                           (hi[2] - lo[2]) * (hi[2] - lo[2])) /
                                     2);
    }
    resetView();
}

gfx::TextureId ModelViewer::texture(const std::string& name, bool& translucent) {
    const std::string key = fs::toLower(name);
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        LoadedTexture t;
        Tex tex;
        if (Error err = loadTexture(ctx_.files, name, tex)) {
            missing_.push_back(name);
        } else {
            for (size_t i = 3; i < tex.image.rgba.size(); i += 4)
                if (tex.image.rgba[i] != 255) {
                    t.translucent = true;
                    break;
                }
            gfx::TextureParams params;
            params.anisotropic = true;
            if (renderer_->createTexture(tex.image.w, tex.image.h, tex.image.rgba.data(), params, t.id))
                t.id = 0;
        }
        it = textures_.emplace(key, t).first;
    }
    translucent = it->second.translucent;
    return it->second.id;
}

void ModelViewer::build() {
    if (mesh_)
        renderer_->destroyMesh(mesh_);
    mesh_ = 0;
    draws_.clear();
    missing_.clear();
    triangles_ = vertices_ = 0;

    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    const std::string level = levels_.empty() ? std::string() : levels_[size_t(level_)];
    const std::string base = stem(names_[size_t(index_)]);
    for (const PkgMesh& m : pkg_.meshes) {
        // Meshes of the chosen level, and any not named by level; the lit
        // parts only when asked for, at whatever level they're made.
        if (isLightPart(m.part()) ? !showLights_ : levelRank(m.level()) >= 0 && m.level() != level)
            continue;
        // A part with a placement (wheels, lights, breakable pieces) is
        // modelled about its own centre and moved to its origin.
        Vec3 at{};
        Mtx mtx;
        std::vector<uint8_t> placement;
        if (!m.part().empty() && !ctx_.files.read(base + "_" + m.part() + ".mtx", placement) &&
            !readMtx(placement, mtx))
            at = mtx.origin;
        const bool lit = m.fvf & 0x10; // has normals
        for (const PkgGroup& g : m.groups) {
            Draw d;
            d.firstIndex = uint32_t(indices.size());
            d.indexCount = uint32_t(g.indices.size());
            if (const PkgShader* s = pkg_.shader(paintJob_, g.shader)) {
                d.color = s->diffuse;
                if (!s->texture.empty())
                    d.texture = texture(s->texture, d.translucent);
                // A glow: no diffuse colour, only emissive, added.
                if (s->diffuse[3] == 0 && (s->emissive[0] > 0 || s->emissive[1] > 0 || s->emissive[2] > 0)) {
                    d.glow = true;
                    d.color = {s->emissive[0], s->emissive[1], s->emissive[2], 1};
                }
            }
            d.translucent = d.glow || d.translucent || d.color[3] < 1;
            d.lit = lit && !d.glow;
            const uint32_t first = uint32_t(vertices.size());
            for (const PkgVertex& v : g.vertices) {
                gfx::Vertex out{};
                for (size_t k = 0; k < 3; ++k) {
                    out.pos[k] = v.pos[k] + at[k];
                    out.normal[k] = v.normal[k];
                }
                out.uv[0] = v.uv[0];
                out.uv[1] = v.uv[1];
                out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
                vertices.push_back(out);
            }
            for (uint16_t i : g.indices)
                indices.push_back(first + i);
            draws_.push_back(d);
            vertices_ += g.vertices.size();
            triangles_ += g.indices.size() / 3;
        }
    }
    if (!vertices.empty() && renderer_->createMesh(vertices, indices, mesh_))
        mesh_ = 0;
}

void ModelViewer::resetView() {
    // From the front left, a little above: the cars face -z.
    yaw_ = -2.4f;
    pitch_ = 0.3f;
    distance_ = radius_ * 2.4f;
}

void ModelViewer::handleEvent(const Event& ev) {
    if (!renderer_ || names_.empty())
        return;
    if (ev.type == EventType::KeyDown) {
        const int n = static_cast<int>(names_.size());
        int i = index_;
        if (ev.key == key::Left)
            i = (i + n - 1) % n;
        else if (ev.key == key::Right)
            i = (i + 1) % n;
        else if (ev.key == key::PageUp)
            i = std::max(0, i - 10);
        else if (ev.key == key::PageDown)
            i = std::min(n - 1, i + 10);
        else if (ev.key == key::Home)
            i = 0;
        else if (ev.key == key::End)
            i = n - 1;
        else if (ev.key == key::Up || ev.key == key::Down) {
            // The first model of the next (or previous) kind.
            const int step = ev.key == key::Down ? 1 : n - 1;
            const std::string kind = kindOf(names_[size_t(i)]);
            do
                i = (i + step) % n;
            while (i != index_ && kindOf(names_[size_t(i)]) == kind);
            if (ev.key == key::Up)
                while (i > 0 && kindOf(names_[size_t(i - 1)]) == kindOf(names_[size_t(i)]))
                    --i;
        } else if (ev.key == 'l' && levels_.size() > 1) {
            level_ = (level_ + 1) % int(levels_.size());
            build();
        } else if (ev.key == 'p' && pkg_.paintJobs > 1) {
            paintJob_ = (paintJob_ + 1) % pkg_.paintJobs;
            build();
        } else if (ev.key == 'g') {
            showLights_ = !showLights_;
            build();
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            showInfo_ = !showInfo_;
        } else if (ev.key == '+' || ev.key == '=') {
            distance_ = std::max(radius_ * 0.2f, distance_ * 0.9f);
        } else if (ev.key == '-') {
            distance_ /= 0.9f;
        }
        if (i != index_)
            show(i);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ -= float(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + float(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::max(radius_ * 0.2f, distance_ * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::drawInfo() {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    const int lh = font::kLineHeight;
    std::vector<std::pair<std::string, Color>> lines;
    const Color white{230, 230, 230}, blue{150, 200, 255}, red{255, 120, 120};
    lines.push_back({names_[size_t(index_)] + "  (" + std::to_string(index_ + 1) + "/" +
                         std::to_string(names_.size()) + ")",
                     blue});
    if (!error_.empty()) {
        lines.push_back({error_, red});
    } else if (showInfo_) {
        std::string lv;
        for (size_t i = 0; i < levels_.size(); ++i)
            lv += (i ? " " : "") + (int(i) == level_ ? "[" + levels_[i] + "]" : levels_[i]);
        char buf[200];
        std::snprintf(buf, sizeof buf, "level %s (L), paint job %u of %u (P), lit parts %s (G); %zu vertices, %zu triangles",
                      lv.empty() ? "-" : lv.c_str(), paintJob_ + 1, std::max(pkg_.paintJobs, 1u),
                      showLights_ ? "shown" : "hidden", vertices_, triangles_);
        lines.push_back({buf, white});
        std::string parts;
        for (const PkgMesh& m : pkg_.meshes)
            if (isLightPart(m.part()) ? showLights_
                                      : levels_.empty() || m.level() == levels_[size_t(level_)] ||
                                            levelRank(m.level()) < 0)
                parts += (parts.empty() ? "" : " ") + m.name;
        lines.push_back({"meshes: " + parts, white});
        std::snprintf(buf, sizeof buf, "%u shaders a paint job; %zu placed models", pkg_.shadersPerJob,
                      pkg_.xrefs.size());
        lines.push_back({buf, white});
        for (const PkgXref& x : pkg_.xrefs)
            if (lines.size() < 12)
                lines.push_back({"  " + x.name + " at " + std::to_string(x.matrix[9]) + " " +
                                     std::to_string(x.matrix[10]) + " " + std::to_string(x.matrix[11]),
                                 white});
        for (const std::string& m : missing_)
            lines.push_back({"missing texture: " + m, red});
    }
    int y = 4;
    for (const auto& [line, color] : lines) {
        if (y > kFrameH - lh)
            break;
        font::drawText(overlay_, 4, y, line, color);
        y += lh;
    }
}

void ModelViewer::update(uint64_t) {
    if (!renderer_)
        return;
    const gfx::Vec3 offset{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
    const gfx::Vec3 eye{center_[0] + offset[0] * distance_, center_[1] + offset[1] * distance_,
                        center_[2] + offset[2] * distance_};
    const gfx::Mat4 viewProj = gfx::multiply(
        gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), std::max(0.01f, radius_ * 0.01f), radius_ * 50),
        gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye, {70, 74, 82});
    // Opaque first, then the translucent over them.
    for (int pass = 0; pass < 2 && mesh_; ++pass)
        for (const Draw& d : draws_) {
            if (d.translucent != (pass == 1))
                continue;
            gfx::DrawCall call;
            call.mesh = mesh_;
            call.firstIndex = d.firstIndex;
            call.indexCount = d.indexCount;
            call.texture = d.texture;
            call.color = d.color;
            call.rgbSource = d.lit ? gfx::ColorSource::Lighting : gfx::ColorSource::Constant;
            call.ambientLight = {0.55f, 0.55f, 0.55f};
            call.directedLight = {0.6f, 0.6f, 0.6f};
            call.lightDirection = {-0.4f, 0.8f, -0.45f}; // from above, front left
            if (d.glow) {
                call.state.src = call.state.dst = gfx::Blend::One;
                call.state.depthWrite = false;
                call.state.cull = gfx::Cull::None;
            } else if (pass == 1) {
                call.state.src = gfx::Blend::SrcAlpha;
                call.state.dst = gfx::Blend::OneMinusSrcAlpha;
                call.state.depthWrite = false;
                call.state.alphaTest = gfx::AlphaTest::Gt0;
            }
            renderer_->draw(call);
        }
    drawInfo();
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::age
