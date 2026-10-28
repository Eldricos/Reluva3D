// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace reluva::nfs {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr const char* kLaneSheet = "GameData/Render/pc/sfx.fsh";
constexpr const char* kDetailNames[] = {"low", "medium", "high"};
// The horizon files and sky pictures of each variant: day, night,
// weather, night with weather.
constexpr const char* kVariantNames[] = {"day", "night", "weather", "night, weather"};
constexpr const char* kHorizonSuffix[] = {"", "n", "w", "nw"};
constexpr const char* kHorizonPicture[] = {"CHRD", "CHRN", "CHRW", "CHNW"};
// The sun (or moon) of each; night with weather has its own on some
// tracks (SNNW), the weather's on the rest.
constexpr const char* kSunPicture[] = {"SUND", "SUNN", "SUNW", "SNNW"};
constexpr const char* kCloudPicture[] = {"CLDD", "CLDN", "CLWD", "CLWN"};
constexpr int kTicksPerSecond = 60; // assumed for the keys' delays (frd.md)

std::string lower(const std::string& s) { return fs::toLower(s); }

void setColour(gfx::Vertex& v, const Horizon::Rgb& c) {
    for (int i = 0; i < 3; ++i)
        v.color[i] = static_cast<uint8_t>(std::clamp(c[i], 0, 255));
    v.color[3] = 255;
}

Horizon::Rgb mix(const Horizon::Rgb& a, const Horizon::Rgb& b, float t) {
    Horizon::Rgb out;
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<int>(a[i] + (b[i] - a[i]) * t);
    return out;
}

// The matrix placing a moving object at a key: the key's rotation (a
// quaternion x, y, z, w in 2.14 fixed point) and position (16.16) in the
// track's left-handed space, then z mirrored into the world's.
gfx::Mat4 keyMatrix(const Track::ExtraObject::Key& a, const Track::ExtraObject::Key& b, float t) {
    float q[4], p[3];
    for (int i = 0; i < 4; ++i)
        q[i] = (a.rotation[i] + (b.rotation[i] - a.rotation[i]) * t) / 16384.0f;
    for (int i = 0; i < 3; ++i)
        p[i] = (a.position[i] + (b.position[i] - a.position[i]) * t) / 65536.0f;
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 0)
        for (float& c : q)
            c /= len;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    gfx::Mat4 m = gfx::identity(); // column-major
    m[0] = 1 - 2 * (y * y + z * z);
    m[1] = 2 * (x * y + z * w);
    m[2] = -(2 * (x * z - y * w));
    m[4] = 2 * (x * y - z * w);
    m[5] = 1 - 2 * (x * x + z * z);
    m[6] = -(2 * (y * z + x * w));
    m[8] = 2 * (x * z + y * w);
    m[9] = 2 * (y * z - x * w);
    m[10] = -(1 - 2 * (x * x + y * y));
    m[12] = p[0];
    m[13] = p[1];
    m[14] = -p[2];
    return m;
}

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

void TrackPage::releaseSky() {
    if (renderer_ && skyMesh_)
        renderer_->destroyMesh(skyMesh_);
    skyMesh_ = 0;
    horizonPicture_ = sunPicture_ = cloudPicture_ = 0; // owned by textures_
    domeIndices_ = bandIndices_ = sunFirst_ = sunIndices_ = cloudFirst_ = cloudIndices_ = 0;
}

void TrackPage::release() {
    releaseSky();
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
    moving_.clear();
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

// A picture of the track's sky.fsh by name, and its text (the sun's).
gfx::TextureId TrackPage::skyPicture(const std::string& name, std::string* text, const Horizon::Rgb* tint,
                                     float tintAmount) {
    const std::string sheet = "GameData/Tracks/" + tracks_[index_] + "/sky.fsh";
    bool additive = false;
    picture(sheet, -1, false, &additive); // loads the sheet
    for (const Shape& s : banks_[sheet].shapes)
        if (s.name == name) {
            if (text && !s.texts.empty())
                *text = s.texts.front();
            image::Image img;
            gfx::TextureId id = 0;
            if (decodeShape(s, img))
                return 0;
            if (tint && tintAmount > 0)
                for (size_t i = 0; i < img.rgba.size(); i += 4)
                    for (int c = 0; c < 3; ++c)
                        img.rgba[i + size_t(c)] = static_cast<uint8_t>(
                            img.rgba[i + size_t(c)] + ((*tint)[size_t(c)] - img.rgba[i + size_t(c)]) * tintAmount);
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id))
                textures_.push_back(id);
            return id;
        }
    return 0;
}

// The sky of the variant shown: a dome shaded from the earth's colours
// below the horizon to the sky's above, and the horizon picture in a band
// around it, as the .hrz file lays them out (hrz.md); drawn around the
// camera.
void TrackPage::loadSky() {
    releaseSky();
    horizon_ = {};
    skyError_.clear();
    const std::string number = tracks_[index_].substr(4);
    const std::string file = "GameData/Tracks/" + tracks_[index_] + "/3tr" + number + kHorizonSuffix[variant_] + ".hrz";
    bool found = false;
    for (const FileRef& f : files_.files())
        if (lower(f.name) == lower(file)) {
            found = true;
            std::vector<uint8_t> data;
            if (Error err = files_.read(f, data))
                skyError_ = err.message;
            else if (Error err2 = parseHorizon(std::string(data.begin(), data.end()), horizon_))
                skyError_ = err2.message;
            break;
        }
    if (!found)
        skyError_ = "No " + file;
    if (!skyError_.empty())
        return;

    // The sun's direction from its picture's text ("A200 B45 R25": A the
    // angle round, in degrees; fsh.md), for which side of the sky gets
    // the sun's colour.
    std::string sunText;
    sunPicture_ = skyPicture(kSunPicture[variant_], &sunText);
    if (!sunPicture_ && variant_ == 3)
        sunPicture_ = skyPicture("SUNW", &sunText);
    auto sunValue = [&](char letter, float otherwise) {
        for (size_t i = 0; i < sunText.size(); ++i)
            if (sunText[i] == letter && (i == 0 || sunText[i - 1] == ' ') && i + 1 < sunText.size() &&
                (std::isdigit(static_cast<unsigned char>(sunText[i + 1])) || sunText[i + 1] == '-'))
                return std::strtof(sunText.c_str() + i + 1, nullptr);
        return otherwise;
    };
    const float sunAngle = sunValue('A', 0) * kPi / 180.0f;
    const float sunHeight = sunValue('B', 45) * kPi / 180.0f;
    const float sunSize = sunValue('R', 25) * kPi / 180.0f;
    sunAdditive_ = sunText.find("NOADD") == std::string::npos;
    sunInFront_ = sunText.find("INFRONT") != std::string::npos;

    const Horizon& h = horizon_;
    const float radius = static_cast<float>(std::max(h.radius, 100));
    const float base = static_cast<float>(h.shadedBase);
    const float mid = base + static_cast<float>(h.shadedMidpoint);
    const float top = base + static_cast<float>(h.shadedHeight);
    constexpr int kSegments = 32;
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    // Rings: the earth's base, the horizon (earth's top and the sky's
    // base), the sky's top, and a cap above.
    auto ring = [&](float y, float r, const Horizon::Rgb& sunSide, const Horizon::Rgb& opposite, float v) {
        for (int i = 0; i <= kSegments; ++i) {
            const float a = 2 * kPi * i / kSegments;
            gfx::Vertex out{};
            out.pos[0] = r * std::sin(a);
            out.pos[1] = y;
            out.pos[2] = -r * std::cos(a);
            const float toSun = 0.5f + 0.5f * std::cos(a - sunAngle);
            setColour(out, mix(opposite, sunSide, toSun));
            out.uv[0] = static_cast<float>(i) / kSegments * 4; // the picture four times round
            out.uv[1] = v;
            vertices.push_back(out);
        }
    };
    auto strip = [&](uint32_t first) {
        for (int i = 0; i < kSegments; ++i)
            for (uint32_t k : {0u, 1u, uint32_t(kSegments + 2), 0u, uint32_t(kSegments + 2), uint32_t(kSegments + 1)})
                indices.push_back(first + i + k);
    };
    ring(base, radius, h.earthBase, h.earthBase, 0);
    ring(mid, radius, h.earthTop, h.earthTop, 0);
    ring(mid, radius, h.skyBaseSun, h.skyBaseOpposite, 0);
    ring(top, radius, h.skyTop, h.skyTop, 0);
    ring(top + radius * 0.5f, 0, h.skyTop, h.skyTop, 0);
    strip(0);
    strip(2 * (kSegments + 1));
    strip(3 * (kSegments + 1));
    domeIndices_ = static_cast<uint32_t>(indices.size());

    // The horizon picture between its bottom and top, slightly inside
    // the dome. With the mirror flag, every other copy is flipped.
    if (h.hasPixmap) {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        ring(base + static_cast<float>(h.pixmapBottom), radius * 0.98f, {255, 255, 255}, {255, 255, 255}, 1);
        ring(base + static_cast<float>(h.pixmapTop), radius * 0.98f, {255, 255, 255}, {255, 255, 255}, 0);
        if (h.mirror)
            for (uint32_t i = first; i < vertices.size(); ++i) {
                const float u = vertices[i].uv[0];
                const float whole = std::floor(u);
                vertices[i].uv[0] = static_cast<int>(whole) % 2 ? whole + 1 - (u - whole) : u;
            }
        strip(first);
        bandIndices_ = static_cast<uint32_t>(indices.size()) - domeIndices_;
        // Fogged by the fog's effect on it (percent) times its density.
        horizonPicture_ = skyPicture(kHorizonPicture[variant_], nullptr, &h.fogColour,
                                     h.fogOnPixmap / 100.0f * h.fogDensity / 100.0f);
    }

    // The sun (or moon): its picture facing the camera, A degrees round
    // and B above the horizon, R degrees across, inside the dome.
    if (sunPicture_) {
        sunFirst_ = static_cast<uint32_t>(indices.size());
        const float distance = radius * 0.9f, half = distance * std::tan(sunSize / 2);
        const gfx::Vec3 centre{distance * std::cos(sunHeight) * std::sin(sunAngle), distance * std::sin(sunHeight),
                               -distance * std::cos(sunHeight) * std::cos(sunAngle)};
        const gfx::Vec3 right{std::cos(sunAngle), 0, std::sin(sunAngle)};
        // Up on the picture: perpendicular to the direction and to right.
        const gfx::Vec3 up{-std::sin(sunHeight) * std::sin(sunAngle), std::cos(sunHeight),
                           std::sin(sunHeight) * std::cos(sunAngle)};
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner == 1 || corner == 2 ? 1.0f : -1.0f, y = corner >= 2 ? 1.0f : -1.0f;
            gfx::Vertex v{};
            for (int i = 0; i < 3; ++i)
                v.pos[i] = centre[i] + (right[i] * x + up[i] * y) * half;
            v.uv[0] = (x + 1) / 2;
            v.uv[1] = (1 - y) / 2;
            setColour(v, {255, 255, 255});
            vertices.push_back(v);
        }
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(first + k);
        sunIndices_ = 6;
    }

    // The clouds: the variant's tile over a layer at the visual height,
    // fading out towards the horizon (cloud type 1 additive, 2 blended;
    // brightness 0-255). They drift with the wind (update()).
    if (h.cloudType != 0)
        cloudPicture_ = skyPicture(kCloudPicture[variant_], nullptr);
    if (cloudPicture_) {
        cloudFirst_ = static_cast<uint32_t>(indices.size());
        constexpr int kRings = 8;
        const float extent = radius * 1.5f, height = static_cast<float>(std::max(h.visualHeight, 100));
        // Lit as the scene is: by the ambient light, but never below a
        // third, so that night clouds still show against the sky.
        const float ambient = std::clamp(std::max({h.ambient[0], h.ambient[1], h.ambient[2]}) / 100.0f, 0.33f, 1.0f);
        const uint8_t bright = static_cast<uint8_t>(std::clamp(h.cloudBright, 0, 255) * ambient);
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        for (int r = 0; r <= kRings; ++r)
            for (int i = 0; i <= kSegments; ++i) {
                const float a = 2 * kPi * i / kSegments, d = extent * r / kRings;
                gfx::Vertex v{};
                v.pos[0] = d * std::sin(a);
                v.pos[1] = height;
                v.pos[2] = -d * std::cos(a);
                v.uv[0] = v.pos[0] / 300.0f; // a tile every 300 units
                v.uv[1] = v.pos[2] / 300.0f;
                v.color[0] = v.color[1] = v.color[2] = bright;
                v.color[3] = static_cast<uint8_t>(255 * (1 - static_cast<float>(r) / kRings));
                vertices.push_back(v);
            }
        for (int r = 0; r < kRings; ++r)
            for (int i = 0; i < kSegments; ++i) {
                const uint32_t p = first + uint32_t(r * (kSegments + 1) + i), q = p + kSegments + 1;
                for (uint32_t k : {p, q, q + 1, p, q + 1, p + 1})
                    indices.push_back(k);
            }
        cloudIndices_ = static_cast<uint32_t>(indices.size()) - cloudFirst_;
    }
    if (Error err = renderer_->createMesh(vertices, indices, skyMesh_))
        skyError_ = err.message;
}

void TrackPage::fogAt(int block, Horizon::Rgb& colour, float& density) const {
    colour = horizon_.fogColour;
    density = static_cast<float>(horizon_.fogDensity);
    for (const Horizon::FogRegion& r : horizon_.fogRegions) {
        auto lerp = [&](const Horizon::FogPoint& a, const Horizon::FogPoint& b, float t) {
            colour = mix(a.colour, b.colour, t);
            density = a.density + (b.density - a.density) * t;
        };
        if (block >= r.beforeSlice && block < r.centreSlice) {
            lerp(r.start, r.apex, float(block - r.beforeSlice) / float(r.centreSlice - r.beforeSlice));
            return;
        }
        if (block >= r.centreSlice && block <= r.afterSlice) {
            lerp(r.apex, r.end, r.afterSlice > r.centreSlice
                                    ? float(block - r.centreSlice) / float(r.afterSlice - r.centreSlice)
                                    : 0.0f);
            return;
        }
    }
}

void TrackPage::load(int index) {
    release();
    index_ = index;
    track_ = {};
    collision_ = {};
    error_.clear();
    collisionError_.clear();
    const std::string folder = "GameData/Tracks/" + tracks_[index] + "/";
    const std::string number = tracks_[index].substr(4); // "trk003" -> "03"
    auto read = [&](const std::string& name, std::vector<uint8_t>& data) -> Error {
        for (const FileRef& f : files_.files())
            if (lower(f.name) == lower(folder + name))
                return files_.read(f, data);
        return Error::fail("No " + folder + name);
    };
    std::vector<uint8_t> data;
    if (Error err = read("tr" + number + ".frd", data))
        error_ = err.message;
    else if (Error err2 = parseTrack(data, track_))
        error_ = err2.message;
    if (!error_.empty())
        return;
    if (Error err = read("tr" + number + ".col", data))
        collisionError_ = err.message;
    else if (Error err2 = parseCollision(data, collision_))
        collisionError_ = err2.message;

    // One vertex per polygon corner (each has its own texture
    // coordinates); z negated into the right-handed world, as models are,
    // except moving objects', which are placed each frame.
    const std::string sheet = folder + "tr" + number + std::to_string(textureSet_) + ".qfs";
    std::vector<gfx::Vertex> vertices;
    struct Group {
        std::vector<uint32_t> indices;
        bool additive = false;
    };
    using Groups = std::map<gfx::TextureId, Group>;
    std::array<Groups, kKinds> groups;
    std::vector<Groups> movingGroups;
    auto add = [&](Groups& into, const std::vector<Track::Vec3>& verts, const std::vector<uint32_t>& colours,
                   const Track::Vec3& offset, float mirror, const std::vector<Track::Polygon>& polygons) {
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
                out.pos[2] = mirror * (v[2] + offset[2]);
                out.uv[0] = tex.corners[2 * k];
                out.uv[1] = tex.corners[2 * k + 1];
                out.normal[1] = 1;
                out.color[0] = static_cast<uint8_t>(c >> 16);
                out.color[1] = static_cast<uint8_t>(c >> 8);
                out.color[2] = static_cast<uint8_t>(c);
                out.color[3] = 255;
                vertices.push_back(out);
            }
            Group& g = into[id];
            g.additive = additive;
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                g.indices.push_back(first + i);
        }
    };
    for (const Track::Block& b : track_.blocks) {
        for (int level = 0; level < 3; ++level) {
            add(groups[kRoadLow + level], b.vertices, b.colours, {}, -1, b.polygons[2 * level]);
            add(groups[kRoadLow + level], b.vertices, b.colours, {}, -1, b.polygons[2 * level + 1]);
        }
        add(groups[kLanes], b.vertices, b.colours, {}, -1, b.polygons[6]);
        for (const auto& layer : b.objectLayers)
            for (const Track::BlockObject& o : layer)
                add(groups[kObjects], b.vertices, b.colours, {}, -1, o.polygons);
    }
    for (const auto& group : track_.extraObjects)
        for (const Track::ExtraObject& o : group) {
            if (o.type == 4) {
                add(groups[kObjects], o.vertices, o.colours, o.reference, -1, o.polygons);
            } else if (!o.keys.empty()) {
                moving_.push_back({o.keys, o.keyDelay, {}});
                movingGroups.emplace_back();
                add(movingGroups.back(), o.vertices, o.colours, {}, 1, o.polygons);
            }
        }

    // The virtual road's walls: a barrier along each side, red on the left
    // and green on the right, from each road point to the next (a guide
    // for checking the .col data against the scenery).
    const auto& road = collision_.road;
    for (size_t i = 0; i + 1 < road.size(); ++i) {
        const auto& a = road[i];
        const auto& b = road[i + 1];
        for (int side = 0; side < 2; ++side) {
            const float sa = side ? a.rightWall : -a.leftWall, sb = side ? b.rightWall : -b.leftWall;
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            for (int k = 0; k < 4; ++k) {
                const auto& p = k < 2 ? a : b;
                const float s = k < 2 ? sa : sb;
                gfx::Vertex out{};
                out.pos[0] = p.position[0] + p.right[0] * s;
                out.pos[1] = p.position[1] + p.right[1] * s + (k == 1 || k == 2 ? 1.5f : 0);
                out.pos[2] = -(p.position[2] + p.right[2] * s);
                out.normal[1] = 1;
                out.color[0] = side ? 40 : 255;
                out.color[1] = side ? 255 : 40;
                out.color[2] = 40;
                out.color[3] = 255;
                vertices.push_back(out);
            }
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                groups[kWalls][0].indices.push_back(first + k);
        }
    }

    std::vector<uint32_t> indices;
    auto flush = [&](const Groups& from, std::vector<Batch>& to) {
        for (const auto& [texture, g] : from) {
            to.push_back({texture, static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(g.indices.size()),
                          g.additive});
            indices.insert(indices.end(), g.indices.begin(), g.indices.end());
        }
    };
    for (int kind = 0; kind < kKinds; ++kind)
        flush(groups[kind], batches_[kind]);
    for (size_t i = 0; i < moving_.size(); ++i)
        flush(movingGroups[i], moving_[i].batches);
    if (!vertices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;
    loadSky();
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
        case key::PageDown:
            index_ = (index_ + 1) % n;
            if (renderer_)
                load(index_);
            break;
        case key::PageUp:
            index_ = (index_ + n - 1) % n;
            if (renderer_)
                load(index_);
            break;
        case 'n': toBlock(block_ + 1); break;
        case 'b': toBlock(block_ - 1); break;
        case 'r': toBlock(0); break;
        case '1': detail_ = 0; break;
        case '2': detail_ = 1; break;
        case '3': detail_ = 2; break;
        case 'h':
            variant_ = (variant_ + 1) % 4;
            if (renderer_ && error_.empty())
                loadSky();
            break;
        case 't': textured_ = !textured_; break;
        case 'v': vertexLight_ = !vertexLight_; break;
        case 'l': lanes_ = !lanes_; break;
        case 'o': objects_ = !objects_; break;
        case 'g': walls_ = !walls_; break;
        case 'f': cullBack_ = !cullBack_; break;
        case 'y': fog_ = !fog_; break;
        case 'x':
            textureSet_ = 1 - textureSet_;
            if (renderer_) {
                const auto eye = eye_;
                const float yaw = yaw_, pitch = pitch_;
                const int block = block_;
                load(index_);
                eye_ = eye;
                yaw_ = yaw;
                pitch_ = pitch;
                block_ = block;
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
    Horizon::Rgb fog;
    float fogDensity = 0;
    fogAt(block_, fog, fogDensity);
    if (!fog_)
        fogDensity = 0;
    // Lightning: a chance each second (out of 256) of a flash, a fifth of
    // a second long, that lights everything up.
    float flash = 0;
    if (horizon_.lightning && horizon_.lightningChance > 0 && skyError_.empty()) {
        if (nowMs - flashCheckedAt_ >= 1000) {
            flashCheckedAt_ = nowMs;
            flashSeed_ = flashSeed_ * 1103515245u + 12345u;
            if (int((flashSeed_ >> 16) & 255) < horizon_.lightningChance)
                flashAt_ = nowMs;
        }
        if (flashAt_ && nowMs - flashAt_ < 200)
            flash = 1 - static_cast<float>(nowMs - flashAt_) / 200.0f;
    }
    renderer_->beginFrame(viewProj, eye,
                          {static_cast<uint8_t>(fog[0]), static_cast<uint8_t>(fog[1]), static_cast<uint8_t>(fog[2])});

    // The sky first, around the camera, behind everything.
    if (skyMesh_) {
        gfx::DrawCall sky;
        sky.mesh = skyMesh_;
        sky.followCamera = true;
        sky.rgbSource = gfx::ColorSource::Vertex;
        sky.state.cull = gfx::Cull::None;
        sky.state.depthTest = sky.state.depthWrite = false;
        for (int i = 0; i < 3; ++i)
            sky.color[size_t(i)] = 1 + flash;
        sky.indexCount = domeIndices_;
        renderer_->draw(sky);
        auto drawSun = [&] {
            if (!sunIndices_)
                return;
            gfx::DrawCall sun = sky;
            sun.firstIndex = sunFirst_;
            sun.indexCount = sunIndices_;
            sun.texture = sunPicture_;
            sun.state.src = gfx::Blend::SrcAlpha;
            sun.state.dst = sunAdditive_ ? gfx::Blend::One : gfx::Blend::OneMinusSrcAlpha;
            renderer_->draw(sun);
        };
        if (!sunInFront_)
            drawSun();
        if (cloudIndices_) {
            gfx::DrawCall clouds = sky;
            clouds.firstIndex = cloudFirst_;
            clouds.indexCount = cloudIndices_;
            clouds.texture = cloudPicture_;
            clouds.alphaSource = gfx::ColorSource::Vertex;
            clouds.state.src = gfx::Blend::SrcAlpha;
            clouds.state.dst = horizon_.cloudType == 1 ? gfx::Blend::One : gfx::Blend::OneMinusSrcAlpha;
            // Drifting with the wind: its speed (a hundredth of a tile a
            // second for each unit) towards its direction (degrees).
            const float drift = static_cast<float>(nowMs % 10000000) / 1000.0f * horizon_.windSpeed / 100.0f;
            const float towards = static_cast<float>(horizon_.windDirection) * kPi / 180.0f;
            clouds.preRow0[2] = -drift * std::sin(towards);
            clouds.preRow1[2] = drift * std::cos(towards);
            renderer_->draw(clouds);
        }
        if (bandIndices_ && horizonPicture_) {
            sky.firstIndex = domeIndices_;
            sky.indexCount = bandIndices_;
            sky.texture = horizonPicture_;
            sky.state.src = gfx::Blend::SrcAlpha;
            sky.state.dst = gfx::Blend::OneMinusSrcAlpha;
            renderer_->draw(sky);
        }
        if (sunInFront_)
            drawSun();
    }

    size_t drawn = 0;
    if (mesh_) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.rgbSource = vertexLight_ ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
        // The horizon's ambient light (percent) over the baked lighting.
        if (error_.empty() && skyError_.empty())
            for (int i = 0; i < 3; ++i)
                call.color[i] = static_cast<float>(horizon_.ambient[i]) / 100.0f + flash;
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
        auto pass = [&](const std::vector<Batch>& list, bool additive) {
            for (const Batch& b : list) {
                if (b.additive != additive)
                    continue;
                call.texture = textured_ ? b.texture : 0;
                call.firstIndex = b.firstIndex;
                call.indexCount = b.indexCount;
                renderer_->draw(call);
                drawn += b.indexCount / 6;
            }
        };
        // Moving objects at the key for now, between it and the next.
        auto movingPass = [&](bool additive) {
            if (!objects_)
                return;
            for (const Moving& m : moving_) {
                const float perKey = std::max(1, m.keyDelay) / static_cast<float>(kTicksPerSecond);
                const float t = static_cast<float>(nowMs) / 1000.0f / perKey;
                const size_t k = static_cast<size_t>(t) % m.keys.size();
                call.model = keyMatrix(m.keys[k], m.keys[(k + 1) % m.keys.size()], t - std::floor(t));
                pass(m.batches, additive);
            }
            call.model = gfx::identity();
        };
        // Solid first, then the lane markings pulled onto the road, then
        // additive pictures (lights, glows) over everything.
        for (const auto* list : solid)
            pass(*list, false);
        movingPass(false);
        // Fog: the solid geometry again in the fog's colour, over what was
        // drawn, more of it the further away. The density (percent) is
        // taken as how much there is at the horizon's distance.
        if (fogDensity > 0 && skyError_.empty()) {
            const gfx::DrawCall plain = call;
            call.texture = 0;
            call.rgbSource = gfx::ColorSource::Constant;
            call.color = {fog[0] / 255.0f, fog[1] / 255.0f, fog[2] / 255.0f, 1};
            call.fogDistance = static_cast<float>(std::max(horizon_.radius, 100)) / std::min(fogDensity / 100.0f, 1.0f);
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::OneMinusSrcAlpha;
            call.state.depthEqual = true;
            call.state.depthWrite = false;
            call.state.alphaTest = gfx::AlphaTest::None;
            const bool textured = textured_;
            textured_ = false;
            for (const auto* list : solid)
                pass(*list, false);
            movingPass(false);
            textured_ = textured;
            call = plain;
        }
        call.state.depthBias = true;
        for (const auto* list : lanes)
            pass(*list, false);
        call.state.depthBias = false;
        if (walls_) {
            // A guide drawn over everything, so the walls show through the
            // ground and fences they follow.
            gfx::DrawCall wall = call;
            wall.rgbSource = gfx::ColorSource::Vertex;
            wall.color = {1, 1, 1, 1};
            wall.texture = 0;
            wall.state.depthTest = wall.state.depthWrite = false;
            for (const Batch& b : batches_[kWalls]) {
                wall.firstIndex = b.firstIndex;
                wall.indexCount = b.indexCount;
                renderer_->draw(wall);
            }
        }
        call.state.src = call.state.dst = gfx::Blend::One;
        call.state.depthWrite = false;
        call.state.alphaTest = gfx::AlphaTest::None;
        for (const auto* list : all)
            pass(*list, true);
        movingPass(true);
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(index_ + 1) + "/" + std::to_string(tracks_.size()) +
                                      "  " + tracks_[index_] + "  " + kVariantNames[variant_]};
    if (!error_.empty()) {
        lines.push_back(error_);
    } else {
        char where[200];
        std::snprintf(where, sizeof where, "block %d/%zu  at %.0f %.0f %.0f  %s detail  set %d  %zu quads  %zu moving",
                      block_, track_.blocks.size(), eye_[0], eye_[1], -eye_[2], kDetailNames[detail_], textureSet_, drawn,
                      moving_.size());
        lines.push_back(where);
        if (!skyError_.empty())
            lines.push_back(skyError_);
        if (!collisionError_.empty())
            lines.push_back(collisionError_);
    }
    lines.push_back("PgUp/PgDn: track  WASD/arrows/drag: move  N/B: block  1-3: detail  H: sky  T V L O G X F Y: toggles");
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = i + 1 == lines.size() ? kFrameH - font::kLineHeight - 4 : 4 + static_cast<int>(i) * font::kLineHeight;
        for (int yy = y - 2; yy < y + font::kLineHeight - 1; ++yy)
            for (int x = 0; x < kFrameW; ++x)
                overlay_.row(yy)[x * 4 + 3] = 140;
        font::drawText(overlay_, 6, y, lines[i], i >= 2 && i + 1 < lines.size() ? Color{255, 120, 100} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::nfs
