// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr const char* kLevelNames[] = {"high", "medium", "small", "tiny", "all"};

std::string lower(const std::string& s) { return fs::toLower(s); }

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
std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
std::string stem(const std::string& path) {
    const std::string base = baseName(path);
    return base.substr(0, base.rfind('.'));
}

} // namespace

CarPage::CarPage(const GameFiles& files) : files_(files) { models_ = files.withExtensions({".fce"}); }

CarPage::~CarPage() { release(); }

void CarPage::activate(System& sys) {
    if (Error err = sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        unavailable_ = err.message;
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    load(index_);
}

void CarPage::deactivate() {
    release();
    renderer_ = nullptr;
}

void CarPage::release() {
    if (renderer_) {
        if (mesh_)
            renderer_->destroyMesh(mesh_);
        if (skin_)
            renderer_->destroyTexture(skin_);
        for (gfx::TextureId page : pages_)
            if (page)
                renderer_->destroyTexture(page);
    }
    mesh_ = 0;
    skin_ = 0;
    pages_.clear();
    pageRuns_.clear();
}

void CarPage::resetView() {
    yaw_ = 0.6f;
    pitch_ = 0.25f;
    distance_ = radius_ * 2.6f;
}

void CarPage::load(int index) {
    release();
    index_ = index;
    model_ = {};
    error_.clear();
    skinName_.clear();
    partLevels_.clear();
    const FileRef& ref = models_[index];

    std::vector<uint8_t> data;
    if (Error err = files_.read(ref, data)) {
        error_ = err.message;
        return;
    }
    if (Error err = parseModel(data, model_)) {
        error_ = err.message;
        return;
    }
    radius_ = std::max(0.5f, std::hypot(model_.halfSize[0], model_.halfSize[1], model_.halfSize[2]));
    resetView();

    // A Render/pc model's pages: its .art, a picture for each page
    // (fce.md), used in place of a skin.
    const std::string folder = ref.name.substr(0, ref.name.size() - baseName(ref.name).size());
    for (const FileRef& f : files_.files()) {
        if (lower(f.name) != lower(folder + stem(ref.name) + ".art"))
            continue;
        std::vector<uint8_t> art;
        std::vector<image::Image> images;
        if (files_.read(f, art) || parseArt(art, images))
            break;
        for (const image::Image& img : images) {
            gfx::TextureId id = 0;
            renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id);
            pages_.push_back(id);
        }
        skinName_ = baseName(f.name) + " (" + std::to_string(images.size()) + " pages)";
        break;
    }
    for (const Model::Part& part : model_.parts) {
        std::vector<std::array<uint32_t, 3>> runs;
        for (uint32_t t = part.firstTriangle; t < part.firstTriangle + part.triangleCount; ++t) {
            const uint32_t page = model_.triangles[t].texturePage;
            if (!runs.empty() && runs.back()[2] == page && runs.back()[0] + runs.back()[1] == t)
                ++runs.back()[1];
            else
                runs.push_back({t, 1, page});
        }
        pageRuns_.push_back(std::move(runs));
    }

    // The skin: car00.tga beside a car.fce, goNN00.tga beside goNN.fce,
    // cone.tga beside cone.fce.
    const std::string wanted[] = {folder + "car00.tga", folder + stem(ref.name) + "00.tga",
                                  folder + stem(ref.name) + ".tga"};
    for (const std::string& name : pages_.empty() ? std::vector<std::string>(std::begin(wanted), std::end(wanted))
                                                  : std::vector<std::string>{}) {
        for (const FileRef& f : files_.files()) {
            if (lower(f.name) != lower(name))
                continue;
            std::vector<uint8_t> tga;
            image::Image img;
            if (!files_.read(f, tga) && !image::decode(tga, img) &&
                !renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, skin_)) {
                skinName_ = baseName(f.name);
            }
            break;
        }
        if (skin_)
            break;
    }

    // One vertex per triangle corner, as each corner has its own texture
    // coordinates. z is negated: the model is left-handed, the world
    // right-handed (fce.md), which also makes front faces counter-clockwise.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const Model::Part& part : model_.parts) {
        partLevels_.push_back(partLevel(partLevels_.size(), model_.parts.size()));
        for (uint32_t t = part.firstTriangle; t < part.firstTriangle + part.triangleCount; ++t) {
            const Model::Triangle& tri = model_.triangles[t];
            for (int k = 0; k < 3; ++k) {
                const Model::Vec3& p = model_.vertices[tri.vertices[k]];
                const Model::Vec3& n = model_.normals[tri.vertices[k]];
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
            }
        }
    }
    if (!vertices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;
}

void CarPage::handleEvent(const Event& ev) {
    const int n = static_cast<int>(models_.size());
    if (ev.type == EventType::KeyDown) {
        switch (ev.key) {
        case key::Right:
            if (renderer_)
                load((index_ + 1) % n);
            else
                index_ = (index_ + 1) % n;
            break;
        case key::Left:
            if (renderer_)
                load((index_ + n - 1) % n);
            else
                index_ = (index_ + n - 1) % n;
            break;
        case 'a': yaw_ -= 0.15f; break;
        case 'd': yaw_ += 0.15f; break;
        case 'w': pitch_ = std::min(pitch_ + 0.1f, 1.5f); break;
        case 's': pitch_ = std::max(pitch_ - 0.1f, -1.5f); break;
        case '+':
        case '=': distance_ = std::max(radius_ * 0.6f, distance_ * 0.9f); break;
        case '-': distance_ /= 0.9f; break;
        case 'l': level_ = (level_ + 1) % 5; break;
        case 't': textures_ = !textures_; break;
        case 'c': cullBack_ = !cullBack_; break;
        case ' ': spin_ = !spin_; break;
        case 'r': resetView(); break;
        default: break;
        }
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ += static_cast<float>(ev.mouseX - lastX_) * 0.01f;
        pitch_ = std::clamp(pitch_ + static_cast<float>(ev.mouseY - lastY_) * 0.01f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseWheel) {
        distance_ = std::max(radius_ * 0.6f, distance_ * std::pow(0.9f, static_cast<float>(ev.wheelY)));
    }
}

void CarPage::draw(Surface& s) {
    // Only shown when the renderer couldn't be had.
    std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
    font::drawText(s, 16, 16, "The Cars page needs 3D rendering, which isn't available:", {255, 120, 100});
    font::drawText(s, 16, 16 + font::kLineHeight, unavailable_, {230, 230, 230});
}

void CarPage::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    if (spin_ && lastMs_)
        yaw_ += static_cast<float>(std::min<uint64_t>(nowMs - lastMs_, 100)) * 0.0006f;
    lastMs_ = nowMs;

    const gfx::Vec3 offset{std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
    const gfx::Vec3 eye{offset[0] * distance_, offset[1] * distance_, offset[2] * distance_};
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(0.8f, float(kFrameW) / float(kFrameH), 0.05f, 500.0f),
                      gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye, {70, 74, 82});

    size_t triangles = 0;
    if (mesh_) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.texture = textures_ ? skin_ : 0;
        call.rgbSource = gfx::ColorSource::Lighting;
        call.ambientLight = {0.45f, 0.45f, 0.45f};
        call.directedLight = {0.7f, 0.7f, 0.7f};
        call.lightDirection = {0.36f, 0.8f, 0.48f};
        call.state.cull = cullBack_ ? gfx::Cull::Back : gfx::Cull::None;
        for (size_t p = 0; p < model_.parts.size(); ++p) {
            const Model::Part& part = model_.parts[p];
            if (part.triangleCount == 0 || (level_ < 4 && partLevels_[p] != level_))
                continue;
            if (pages_.empty()) {
                call.firstIndex = part.firstTriangle * 3;
                call.indexCount = part.triangleCount * 3;
                renderer_->draw(call);
            } else {
                for (const auto& [first, count, page] : pageRuns_[p]) {
                    call.texture = textures_ && page < pages_.size() ? pages_[page] : 0;
                    call.firstIndex = first * 3;
                    call.indexCount = count * 3;
                    renderer_->draw(call);
                }
            }
            triangles += part.triangleCount;
        }
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Cars  " + std::to_string(index_ + 1) + "/" + std::to_string(models_.size()) +
                                      "  " + models_[index_].name};
    if (!error_.empty()) {
        lines.push_back(error_);
    } else {
        lines.push_back(std::to_string(model_.parts.size()) + " parts, " + std::to_string(model_.triangles.size()) +
                        " triangles; showing " + kLevelNames[level_] + " detail (" + std::to_string(triangles) +
                        ")  skin: " + (skinName_.empty() ? "none" : skinName_) + (cullBack_ ? "" : "  back faces"));
    }
    lines.push_back("Left/Right: model  drag/WASD: turn  wheel: zoom  L: detail  T: skin  C: faces  Space: spin");
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = i + 1 == lines.size() ? kFrameH - font::kLineHeight - 4 : 4 + static_cast<int>(i) * font::kLineHeight;
        for (int yy = y - 2; yy < y + font::kLineHeight - 1; ++yy)
            for (int x = 0; x < kFrameW; ++x)
                overlay_.row(yy)[x * 4 + 3] = 140;
        font::drawText(overlay_, 6, y, lines[i], i == 1 && !error_.empty() ? Color{255, 120, 100} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::nfs
