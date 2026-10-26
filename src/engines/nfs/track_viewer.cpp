// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::nfs {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr const char* kLaneSheet = "GameData/Render/pc/sfx.fsh";
constexpr const char* kDetailNames[] = {"low", "medium", "high"};

std::string lower(const std::string& s) { return fs::toLower(s); }

} // namespace

TrackPage::TrackPage(const GameFiles& files, const GameFolders& folders) : files_(files), folders_(folders) {
    tracks_ = trackFolders(folders.install);
}

TrackPage::~TrackPage() { release(); }

void TrackPage::activate(System& sys) {
    if (Error err = sys.init3D(kFrameW, kFrameH, renderer_)) {
        renderer_ = nullptr;
        unavailable_ = err.message;
        return;
    }
    overlay_.create(kFrameW, kFrameH, PixelFormat::RGBA32);
    load(index_);
}

void TrackPage::deactivate() {
    release();
    renderer_ = nullptr;
    held_.clear();
}

void TrackPage::release() {
    if (renderer_) {
        if (mesh_)
            renderer_->destroyMesh(mesh_);
        for (gfx::TextureId t : textures_)
            renderer_->destroyTexture(t);
    }
    mesh_ = 0;
    textures_.clear();
    pictures_.clear();
    banks_.clear();
    for (auto& b : batches_)
        b.clear();
}

// The GPU texture for a picture of a sheet (by its number, the shape
// named "0042" in track sheets) or of the lane sheet ("lin3"), made once.
gfx::TextureId TrackPage::picture(const std::string& sheet, int number, bool lane, bool* additive) {
    const std::string key = sheet + "/" + std::to_string(number);
    if (auto it = pictures_.find(key); it != pictures_.end()) {
        *additive = it->second.second;
        return it->second.first;
    }
    pictures_[key] = {0, false};
    *additive = false;
    auto bank = banks_.find(sheet);
    if (bank == banks_.end()) {
        ShapeBank loaded;
        for (const FileRef& f : files_.files())
            if (lower(f.name) == lower(sheet)) {
                std::vector<uint8_t> data;
                if (!files_.read(f, data))
                    parseShapeBank(std::move(data), loaded);
                break;
            }
        bank = banks_.emplace(sheet, std::move(loaded)).first;
    }
    char name[16];
    std::snprintf(name, sizeof name, lane ? "lin%d" : "%04d", number);
    const Shape* shape = nullptr;
    for (const Shape& s : bank->second.shapes)
        if (s.name == name)
            shape = &s;
    if (!shape && !lane && number >= 0 && number < static_cast<int>(bank->second.shapes.size()))
        shape = &bank->second.shapes[number];
    image::Image img;
    gfx::TextureId id = 0;
    if (shape && !decodeShape(*shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id)) {
        textures_.push_back(id);
        for (const std::string& t : shape->texts)
            if (lower(t).find("additive") != std::string::npos)
                *additive = true;
    }
    pictures_[key] = {id, *additive};
    return id;
}

void TrackPage::load(int index) {
    release();
    index_ = index;
    track_ = {};
    error_.clear();
    const std::string folder = "GameData/Tracks/" + tracks_[index] + "/";
    const std::string number = tracks_[index].substr(4); // "trk003" -> "03"
    std::vector<uint8_t> data;
    bool found = false;
    for (const FileRef& f : files_.files())
        if (lower(f.name) == lower(folder + "tr" + number + ".frd")) {
            found = true;
            if (Error err = files_.read(f, data))
                error_ = err.message;
            break;
        }
    if (!found)
        error_ = "No " + folder + "tr" + number + ".frd";
    if (error_.empty())
        if (Error err = parseTrack(data, track_))
            error_ = err.message;
    if (!error_.empty())
        return;

    // One vertex per polygon corner (each has its own texture
    // coordinates); z negated into the right-handed world, as models are.
    const std::string sheet = folder + "tr" + number + std::to_string(textureSet_) + ".qfs";
    std::vector<gfx::Vertex> vertices;
    struct Group {
        std::vector<uint32_t> indices;
        bool additive = false;
    };
    std::array<std::map<gfx::TextureId, Group>, kKinds> groups;
    auto add = [&](Kind kind, const std::vector<Track::Vec3>& verts, const std::vector<uint32_t>& colours,
                   const Track::Vec3& offset, const std::vector<Track::Polygon>& polygons) {
        for (const Track::Polygon& p : polygons) {
            const Track::Texture& tex = track_.textures[p.texture];
            bool additive = false;
            const gfx::TextureId id = tex.lane ? picture(kLaneSheet, tex.picture, true, &additive)
                                               : picture(sheet, tex.picture, false, &additive);
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            for (int k = 0; k < 4; ++k) {
                const Track::Vec3& v = verts[p.vertices[k]];
                const uint32_t c = colours[p.vertices[k]];
                gfx::Vertex out{};
                out.pos[0] = v[0] + offset[0];
                out.pos[1] = v[1] + offset[1];
                out.pos[2] = -(v[2] + offset[2]);
                out.uv[0] = tex.corners[2 * k];
                out.uv[1] = tex.corners[2 * k + 1];
                out.normal[1] = 1;
                out.color[0] = static_cast<uint8_t>(c >> 16);
                out.color[1] = static_cast<uint8_t>(c >> 8);
                out.color[2] = static_cast<uint8_t>(c);
                out.color[3] = 255;
                vertices.push_back(out);
            }
            Group& g = groups[kind][id];
            g.additive = additive;
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                g.indices.push_back(first + i);
        }
    };
    for (const Track::Block& b : track_.blocks) {
        for (int level = 0; level < 3; ++level) {
            add(static_cast<Kind>(kRoadLow + level), b.vertices, b.colours, {}, b.polygons[2 * level]);
            add(static_cast<Kind>(kRoadLow + level), b.vertices, b.colours, {}, b.polygons[2 * level + 1]);
        }
        add(kLanes, b.vertices, b.colours, {}, b.polygons[6]);
        for (const auto& layer : b.objectLayers)
            for (const Track::BlockObject& o : layer)
                add(kObjects, b.vertices, b.colours, {}, o.polygons);
    }
    // Still objects at their reference point; moving ones aren't placed yet
    // (their keys' units are still to be worked out, frd.md).
    for (const auto& group : track_.extraObjects)
        for (const Track::ExtraObject& o : group)
            if (o.type == 4)
                add(kObjects, o.vertices, o.colours, o.reference, o.polygons);

    std::vector<uint32_t> indices;
    for (int kind = 0; kind < kKinds; ++kind)
        for (const auto& [texture, g] : groups[kind]) {
            batches_[kind].push_back({texture, static_cast<uint32_t>(indices.size()),
                                      static_cast<uint32_t>(g.indices.size()), g.additive});
            indices.insert(indices.end(), g.indices.begin(), g.indices.end());
        }
    if (!vertices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;
    toBlock(0);
}

// Puts the camera above a block's centre, looking along the road.
void TrackPage::toBlock(int block) {
    if (track_.blocks.empty())
        return;
    const int n = static_cast<int>(track_.blocks.size());
    block_ = (block % n + n) % n;
    const Track::Vec3& c = track_.blocks[block_].centre;
    const Track::Vec3& next = track_.blocks[(block_ + 1) % n].centre;
    eye_ = {c[0], c[1] + 3.0f, -c[2]};
    yaw_ = std::atan2(next[0] - c[0], next[2] - c[2]);
    pitch_ = -0.08f;
}

void TrackPage::handleEvent(const Event& ev) {
    const int n = static_cast<int>(tracks_.size());
    if (ev.type == EventType::KeyDown) {
        held_.insert(ev.key);
        switch (ev.key) {
        case key::PageDown: index_ = (index_ + 1) % n; if (renderer_) load(index_); break;
        case key::PageUp: index_ = (index_ + n - 1) % n; if (renderer_) load(index_); break;
        case 'n': toBlock(block_ + 1); break;
        case 'b': toBlock(block_ - 1); break;
        case 'r': toBlock(0); break;
        case '1': detail_ = 0; break;
        case '2': detail_ = 1; break;
        case '3': detail_ = 2; break;
        case 't': textured_ = !textured_; break;
        case 'v': vertexLight_ = !vertexLight_; break;
        case 'l': lanes_ = !lanes_; break;
        case 'o': objects_ = !objects_; break;
        case 'f': cullBack_ = !cullBack_; break;
        case 'x':
            textureSet_ = 1 - textureSet_;
            if (renderer_) {
                const auto eye = eye_;
                const float yaw = yaw_, pitch = pitch_;
                load(index_);
                eye_ = eye;
                yaw_ = yaw;
                pitch_ = pitch;
            }
            break;
        default: break;
        }
    } else if (ev.type == EventType::KeyUp) {
        held_.erase(ev.key);
    } else if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        dragging_ = true;
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    } else if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        dragging_ = false;
    } else if (ev.type == EventType::MouseMove && dragging_) {
        yaw_ += static_cast<float>(ev.mouseX - lastX_) * 0.005f;
        pitch_ = std::clamp(pitch_ - static_cast<float>(ev.mouseY - lastY_) * 0.005f, -1.5f, 1.5f);
        lastX_ = ev.mouseX;
        lastY_ = ev.mouseY;
    }
}

void TrackPage::draw(Surface& s) {
    std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
    font::drawText(s, 16, 16, "The Tracks page needs 3D rendering, which isn't available:", {255, 120, 100});
    font::drawText(s, 16, 16 + font::kLineHeight, unavailable_, {230, 230, 230});
}

void TrackPage::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    const float dt = lastMs_ ? std::min<float>(static_cast<float>(nowMs - lastMs_), 100) / 1000.0f : 0;
    lastMs_ = nowMs;

    // The camera: yaw 0 looks along the track's +z (the world's -z).
    auto held = [&](uint32_t k) { return held_.count(k) != 0; };
    if (held(key::Left))
        yaw_ -= 1.5f * dt;
    if (held(key::Right))
        yaw_ += 1.5f * dt;
    if (held(key::Up))
        pitch_ = std::min(pitch_ + 1.0f * dt, 1.5f);
    if (held(key::Down))
        pitch_ = std::max(pitch_ - 1.0f * dt, -1.5f);
    const gfx::Vec3 forward{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_)};
    const gfx::Vec3 right{std::cos(yaw_), 0, std::sin(yaw_)};
    const float speed = (held(key::LShift) || held(key::RShift) ? 120.0f : 25.0f) * dt;
    auto move = [&](const gfx::Vec3& dir, float amount) {
        for (int i = 0; i < 3; ++i)
            eye_[i] += dir[i] * amount;
    };
    if (held('w'))
        move(forward, speed);
    if (held('s'))
        move(forward, -speed);
    if (held('d'))
        move(right, speed);
    if (held('a'))
        move(right, -speed);
    if (held(' '))
        eye_[1] += speed;
    if (held('c'))
        eye_[1] -= speed;

    const gfx::Vec3 eye{eye_[0], eye_[1], eye_[2]};
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(1.0f, float(kFrameW) / float(kFrameH), 0.5f, 6000.0f),
                      gfx::lookAlong(eye, forward, {0, 1, 0}));
    renderer_->beginFrame(viewProj, eye, {110, 150, 200});

    size_t drawn = 0;
    if (mesh_) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.rgbSource = vertexLight_ ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
        call.state.cull = cullBack_ ? gfx::Cull::Back : gfx::Cull::None;
        call.state.alphaTest = gfx::AlphaTest::Ge128;
        std::vector<const std::vector<Batch>*> solid = {&batches_[kRoadLow + detail_]};
        if (objects_)
            solid.push_back(&batches_[kObjects]);
        std::vector<const std::vector<Batch>*> lanes;
        if (lanes_)
            lanes.push_back(&batches_[kLanes]);
        std::vector<const std::vector<Batch>*> all = solid;
        all.insert(all.end(), lanes.begin(), lanes.end());
        auto pass = [&](const std::vector<const std::vector<Batch>*>& lists, bool additive) {
            for (const auto* list : lists)
                for (const Batch& b : *list) {
                    if (b.additive != additive)
                        continue;
                    call.texture = textured_ ? b.texture : 0;
                    call.firstIndex = b.firstIndex;
                    call.indexCount = b.indexCount;
                    renderer_->draw(call);
                    drawn += b.indexCount / 6;
                }
        };
        // Solid first, then the lane markings pulled onto the road, then
        // additive pictures (lights, glows) over everything.
        pass(solid, false);
        call.state.depthBias = true;
        pass(lanes, false);
        call.state.depthBias = false;
        call.state.src = call.state.dst = gfx::Blend::One;
        call.state.depthWrite = false;
        call.state.alphaTest = gfx::AlphaTest::None;
        pass(all, true);
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(index_ + 1) + "/" + std::to_string(tracks_.size()) +
                                      "  " + tracks_[index_]};
    if (!error_.empty()) {
        lines.push_back(error_);
    } else {
        char where[160];
        std::snprintf(where, sizeof where, "block %d/%zu  at %.0f %.0f %.0f  %s detail  set %d  %zu quads", block_,
                      track_.blocks.size(), eye_[0], eye_[1], -eye_[2], kDetailNames[detail_], textureSet_, drawn);
        lines.push_back(where);
    }
    lines.push_back("PgUp/PgDn: track  WASD/arrows/drag: move, look  N/B: block  1-3: detail  T V L O X F: toggles");
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
