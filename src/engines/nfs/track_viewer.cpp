// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <tuple>

namespace reluva::nfs {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr float kPi = 3.14159265358979f;
constexpr const char* kLaneSheet = "GameData/Render/pc/sfx.fsh";
constexpr const char* kDetailNames[] = {"low", "medium", "high"};
// The horizon files and sky pictures of each variant: day, night,
// weather, night with weather. (sky.fsh's CHRD, CHRN... are the cars'
// reflections; the horizon picture is in the track's texture sheet.)
constexpr const char* kVariantNames[] = {"day", "night", "weather", "night, weather"};
// The tracks by number (text strings 102 on, docs/formats/nfs/tracks.md).
constexpr const char* kTrackNames[] = {"Hometown",      "Redrock Ridge", "Atlantica", "Rocky Pass", "Country Woods",
                                       "Lost Canyons", "Aquatica",      "Summit",    "Empire City"};

// The track's name from its folder ("trk003"), whose number is in hex.
std::string trackName(const std::string& folder)
{
    const unsigned long number = std::strtoul(folder.c_str() + 3, nullptr, 16);
    return number < std::size(kTrackNames) ? kTrackNames[number] : std::string();
}
constexpr const char* kHorizonSuffix[] = {"", "n", "w", "nw"};
// The sun (or moon) of each; night with weather has its own on some
// tracks (SNNW), the weather's on the rest.
constexpr const char* kSunPicture[] = {"SUND", "SUNN", "SUNW", "SNNW"};
constexpr const char* kCloudPicture[] = {"CLDD", "CLDN", "CLWD", "CLWN"};
// The game's ticks (0x7d3684 counts them): 641 in 10 seconds of a race.
// Moving objects' key delays and the clouds' drift are in them.
constexpr int kGameTicksPerSecond = 64;

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

TrackPage::TrackPage(const GameFiles& files, const GameFolders& folders, audio::Mixer* mixer)
    : files_(files), folders_(folders), mixer_(mixer) {
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
    if (renderer_ && boltMesh_)
        renderer_->destroyMesh(boltMesh_);
    skyMesh_ = boltMesh_ = 0;
    boltIndices_ = 0;
    skyVertices_.clear();
    bolts_.clear();
    sunRotate_ = skyChanged_ = false;
    horizonPicture_ = sunPicture_ = cloudPicture_ = 0; // owned by textures_
    domeIndices_ = earthIndices_ = bandIndices_ = sunFirst_ = sunIndices_ = cloudFirst_ = cloudIndices_ = 0;
}

void TrackPage::release() {
    releaseSky();
    stopSounds();
    soundData_.clear();
    soundBank_ = {};
    soundSlots_.clear();
    marks_.clear();
    glowPicture_ = 0; // owned by textures_
    if (renderer_ && markMesh_)
        renderer_->destroyMesh(markMesh_);
    markMesh_ = 0;
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
    for (auto& a : animations_)
        a.clear();
}

// The track's texture sheet: sets 0 and 1, then the same in 8 bits (for
// the software renderer).
std::string TrackPage::textureSheet() const {
    const std::string number = tracks_[index_].substr(4); // "trk003" -> "03"
    return "GameData/Tracks/" + tracks_[index_] + "/tr" + number + std::to_string(textureSet_ % 2) +
           (textureSet_ >= 2 ? "_8" : "") + ".qfs";
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
        // As the game's picture loader (0x4d40c3), which looks for exactly
        // "<additive>": tr000's and tr001's bare "additive" don't count.
        for (const std::string& t : shape->texts)
            if (t.find("<additive>") != std::string::npos)
                *additive = true;
    }
    pictures_[key] = {id, *additive};
    return id;
}

// A picture of the track's sky.fsh by name, and its text (the sun's).
gfx::TextureId TrackPage::skyPicture(const std::string& name, std::string* text) {
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
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id))
                textures_.push_back(id);
            return id;
        }
    return 0;
}

// The horizon picture: the track sheet's pictures 0000-0007 side by side,
// the eight tiles nfs3.exe draws round the horizon (thrash.md's log of its
// drawing; hrz.md).
gfx::TextureId TrackPage::horizonStrip() {
    const std::string sheet = textureSheet();
    bool additive = false;
    picture(sheet, 0, false, &additive); // loads the sheet
    auto bank = banks_.find(sheet);
    if (bank == banks_.end())
        return 0;
    constexpr int kTiles = 8;
    std::array<image::Image, kTiles> tiles;
    for (int i = 0; i < kTiles; ++i) {
        char name[8];
        std::snprintf(name, sizeof name, "%04d", i);
        const Shape* shape = nullptr;
        for (const Shape& s : bank->second.shapes)
            if (s.name == name)
                shape = &s;
        if (!shape || decodeShape(*shape, tiles[size_t(i)]) || tiles[size_t(i)].w != tiles[0].w ||
            tiles[size_t(i)].h != tiles[0].h)
            return 0;
    }
    image::Image strip;
    strip.w = tiles[0].w * kTiles;
    strip.h = tiles[0].h;
    strip.rgba.resize(size_t(strip.w) * size_t(strip.h) * 4);
    const size_t row = size_t(tiles[0].w) * 4;
    for (int i = 0; i < kTiles; ++i)
        for (int y = 0; y < strip.h; ++y)
            std::copy_n(tiles[size_t(i)].rgba.data() + size_t(y) * row, row,
                        strip.rgba.data() + size_t(y) * row * kTiles + size_t(i) * row);
    gfx::TextureId id = 0;
    if (renderer_->createTexture(strip.w, strip.h, strip.rgba.data(), {}, id))
        return 0;
    textures_.push_back(id);
    return id;
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

    // The sun from its picture's text ("A200 B45 R25 ROTATE"; fsh.md).
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
    const float sunSize = sunValue('R', 25);
    sunAdditive_ = sunText.find("NOADD") == std::string::npos;
    sunInFront_ = sunText.find("INFRONT") != std::string::npos;
    sunRotate_ = sunText.find("ROTATE") != std::string::npos;

    const Horizon& h = horizon_;
    const float radius = static_cast<float>(std::max(h.radius, 100));
    const float base = static_cast<float>(h.shadedBase);
    const float mid = base + static_cast<float>(h.shadedMidpoint);
    const float top = base + static_cast<float>(h.shadedHeight);
    constexpr int kSegments = 32;
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    // Rings: the earth's base, the horizon (earth's top and the sky's
    // base), the sky's top, and a cap above. Vertex i is at the game's
    // slot i / 2 (16 slots of 22.5 degrees round, from the rotation:
    // horizonTurn()), with the horizon picture twice round, a tile a slot
    // (0x478bc0), and the sky's base colours by slot edge.
    const std::array<Horizon::Rgb, 16> skyBase = skyBaseColours(h);
    const float turn = horizonTurn(h);
    auto skyBaseAt = [&](int i) {
        const Horizon::Rgb& a = skyBase[size_t((i / 2) % 16)];
        if (i % 2 == 0)
            return a;
        return mix(a, skyBase[size_t((i / 2 + 1) % 16)], 0.5f);
    };
    auto ring = [&](float y, float r, const Horizon::Rgb* colour, float v) {
        for (int i = 0; i <= kSegments; ++i) {
            // The heading in turns, as atan2(x, z) / 2 pi in the track's
            // coordinates; z is negated in the page's world.
            const float a = 2 * kPi * (static_cast<float>(i) / kSegments - 0.75f - turn);
            gfx::Vertex out{};
            out.pos[0] = r * std::sin(a);
            out.pos[1] = y;
            out.pos[2] = -r * std::cos(a);
            setColour(out, colour ? *colour : skyBaseAt(i));
            out.uv[0] = static_cast<float>(i) / kSegments * 2;
            out.uv[1] = v;
            vertices.push_back(out);
        }
    };
    auto strip = [&](uint32_t first) {
        for (int i = 0; i < kSegments; ++i)
            for (uint32_t k : {0u, 1u, uint32_t(kSegments + 2), 0u, uint32_t(kSegments + 2), uint32_t(kSegments + 1)})
                indices.push_back(first + i + k);
    };
    ring(base, radius, &h.earthBase, 0);
    ring(mid, radius, &h.earthTop, 0);
    ring(mid, radius, nullptr, 0);
    ring(top, radius, &h.skyTop, 0);
    ring(top + radius * 0.5f, 0, &h.skyTop, 0);
    strip(0);
    earthIndices_ = static_cast<uint32_t>(indices.size()); // the earth's, fogged as the sky
    strip(2 * (kSegments + 1));
    strip(3 * (kSegments + 1));
    domeIndices_ = static_cast<uint32_t>(indices.size());

    // The horizon picture between its bottom and top, slightly inside
    // the dome: the track sheet's first eight pictures (0000-0007) side by
    // side. With the mirror flag the second copy is the first flipped, as
    // the game draws slots 8-15 (tiles 7 to 0, each flipped).
    if (h.hasPixmap)
        horizonPicture_ = horizonStrip();
    if (horizonPicture_) {
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        // White, day or night (0x478310).
        const Horizon::Rgb white{255, 255, 255};
        ring(base + static_cast<float>(h.pixmapBottom), radius * 0.98f, &white, 1);
        ring(base + static_cast<float>(h.pixmapTop), radius * 0.98f, &white, 0);
        if (h.mirror)
            for (uint32_t i = first; i < vertices.size(); ++i) {
                const float u = vertices[i].uv[0];
                const float whole = std::floor(u);
                vertices[i].uv[0] = static_cast<int>(whole) % 2 ? whole + 1 - (u - whole) : u;
            }
        strip(first);
        bandIndices_ = static_cast<uint32_t>(indices.size()) - domeIndices_;
    }

    // The sun (or moon): its picture facing the camera at (cos A, cos B,
    // sin A) x 3000 in the track's coordinates, a square whose corners
    // are R x 10 from its centre (0x47ab40, 0x479ed0). So B = 45 puts it
    // 35 degrees up. (With ROTATE the game also turns it as it crosses
    // the screen; it's drawn upright here.)
    if (sunPicture_) {
        sunFirst_ = static_cast<uint32_t>(indices.size());
        const gfx::Vec3 centre{3000 * std::cos(sunAngle), 3000 * std::cos(sunHeight), -3000 * std::sin(sunAngle)};
        const float length = std::sqrt(centre[0] * centre[0] + centre[1] * centre[1] + centre[2] * centre[2]);
        const gfx::Vec3 dir{centre[0] / length, centre[1] / length, centre[2] / length};
        // Right and up on the picture: across the direction, and up.
        const float across = std::hypot(dir[0], dir[2]);
        const gfx::Vec3 right{-dir[2] / across, 0, dir[0] / across};
        const gfx::Vec3 up{right[1] * dir[2] - right[2] * dir[1], right[2] * dir[0] - right[0] * dir[2],
                           right[0] * dir[1] - right[1] * dir[0]};
        const float half = sunSize * 10 / std::sqrt(2.0f);
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        sunVertex_ = first;
        sunCentre_ = centre;
        for (int k = 0; k < 3; ++k) {
            sunRight_[size_t(k)] = right[size_t(k)] * half;
            sunUp_[size_t(k)] = up[size_t(k)] * half;
        }
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner == 1 || corner == 2 ? 1.0f : -1.0f, y = corner >= 2 ? 1.0f : -1.0f;
            gfx::Vertex v{};
            for (int k = 0; k < 3; ++k)
                v.pos[k] = centre[k] + (right[k] * x + up[k] * y) * half;
            v.uv[0] = (x + 1) / 2;
            v.uv[1] = (1 - y) / 2;
            setColour(v, {255, 255, 255});
            vertices.push_back(v);
        }
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(first + k);
        sunIndices_ = 6;
    }

    // The clouds: the variant's tile on a dome around the camera
    // (cloudHeight(); 0x47ae10): 13 x 13 points 450 apart, each its grid
    // position as texture coordinates, u along x and v along the track's
    // z, and a brightness from cloudBrightness() as colour and alpha.
    // Cloud type 1 is added, 2 blended.
    if (h.cloudType != 0)
        cloudPicture_ = skyPicture(kCloudPicture[variant_], nullptr);
    if (cloudPicture_) {
        cloudFirst_ = static_cast<uint32_t>(indices.size());
        const auto brightness = cloudBrightness(h, 12345);
        const uint32_t first = static_cast<uint32_t>(vertices.size());
        cloudVertex_ = first;
        cloudBase_ = brightness;
        for (int row = 0; row < kCloudPoints; ++row)
            for (int col = 0; col < kCloudPoints; ++col) {
                gfx::Vertex v{};
                const float x = kCloudFirst + kCloudStep * static_cast<float>(col);
                const float z = kCloudFirst + kCloudStep * static_cast<float>(row);
                const uint8_t b = brightness[size_t(row * kCloudPoints + col)];
                v.pos[0] = x;
                v.pos[1] = cloudHeight(h, std::hypot(x, z), b);
                v.pos[2] = -z;
                v.uv[0] = static_cast<float>(col);
                v.uv[1] = static_cast<float>(row);
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = b;
                vertices.push_back(v);
            }
        for (int row = 0; row + 1 < kCloudPoints; ++row)
            for (int col = 0; col + 1 < kCloudPoints; ++col) {
                const uint32_t p = first + uint32_t(row * kCloudPoints + col), q = p + kCloudPoints;
                for (uint32_t k : {p, q, q + 1, p, q + 1, p + 1})
                    indices.push_back(k);
            }
        cloudIndices_ = static_cast<uint32_t>(indices.size()) - cloudFirst_;
    }
    skyVertices_ = vertices;
    if (Error err = renderer_->createMesh(vertices, indices, skyMesh_))
        skyError_ = err.message;

    // Lightning's bolts: a quad for each point but the first, up to the
    // game's 240 points.
    if (h.lightning > 0 && h.lightningChance > 0) {
        constexpr uint32_t kQuads = 240;
        std::vector<gfx::Vertex> quads(kQuads * 4);
        std::vector<uint32_t> quadIndices;
        for (uint32_t q = 0; q < kQuads; ++q)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                quadIndices.push_back(q * 4 + k);
        if (!renderer_->createMesh(quads, quadIndices, boltMesh_))
            boltIndices_ = 0;
    }
}

// The sky's game ticks since the last frame (64 a second, at most a
// second's worth at once): lightning starting, and its bolts jittering.
void TrackPage::tickSky(uint64_t nowMs) {
    const uint64_t target = nowMs * kGameTicksPerSecond / 1000;
    if (skyTick_ == 0 || target < skyTick_ || target - skyTick_ > uint64_t(kGameTicksPerSecond))
        skyTick_ = target;
    while (skyTick_ < target && skyError_.empty()) {
        ++skyTick_;
        if (boltMesh_ && lightningStarts(horizon_, skyTick_, skyRandom_)) {
            size_t points = 0;
            for (const LightningBolt& b : bolts_)
                points += b.points.size();
            LightningBolt bolt = makeBolt(horizon_, skyRandom_);
            if (points + bolt.points.size() <= 240)
                bolts_.push_back(std::move(bolt));
        }
        if (!bolts_.empty())
            skyChanged_ = true;
        for (LightningBolt& b : bolts_)
            tickBolt(b, skyRandom_);
        bolts_.erase(std::remove_if(bolts_.begin(), bolts_.end(), [](const LightningBolt& b) { return b.life <= 0; }),
                     bolts_.end());
    }
}

// The sky's vertices that change, before the frame: the sun turned by
// where it is on the screen (ROTATE: by its x / z in the view, in turns;
// 0x479fd9), the clouds lit around the latest bolt (0x47b3a0: the grid
// points next to where it starts, the rows either side at half), and the
// bolts' quads.
void TrackPage::updateSky(const gfx::Vec3& forward, const gfx::Vec3& right) {
    if (!skyMesh_ || skyVertices_.empty() || !(sunRotate_ || skyChanged_))
        return;
    constexpr float kTau = 6.28318530718f;
    if (sunRotate_ && sunIndices_) {
        const float x = sunCentre_[0] * right[0] + sunCentre_[1] * right[1] + sunCentre_[2] * right[2];
        const float z = sunCentre_[0] * forward[0] + sunCentre_[1] * forward[1] + sunCentre_[2] * forward[2];
        const float turn = z > 0 ? x / z * kTau : 0;
        const float c = std::cos(turn), sn = std::sin(turn);
        for (int corner = 0; corner < 4; ++corner) {
            const float cx = corner == 1 || corner == 2 ? 1.0f : -1.0f, cy = corner >= 2 ? 1.0f : -1.0f;
            const float rx = cx * c - cy * sn, ry = cx * sn + cy * c;
            gfx::Vertex& v = skyVertices_[sunVertex_ + uint32_t(corner)];
            for (int k = 0; k < 3; ++k)
                v.pos[k] = sunCentre_[size_t(k)] + sunRight_[size_t(k)] * rx + sunUp_[size_t(k)] * ry;
        }
    }
    if (cloudIndices_) {
        for (size_t i = 0; i < cloudBase_.size(); ++i) {
            uint8_t* c = skyVertices_[cloudVertex_ + i].color;
            c[0] = c[1] = c[2] = c[3] = cloudBase_[i];
        }
        if (!bolts_.empty()) {
            const LightningBolt& bolt = bolts_.back();
            const int flash = boltFlash(bolt);
            auto cell = [](float at) {
                return std::clamp(static_cast<int>(std::floor((at - kCloudFirst) / kCloudStep + 0.5f)), 0,
                                  kCloudPoints - 1);
            };
            const int cx = cell(bolt.points[0].at[0]), cz = cell(bolt.points[0].at[2]);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || x >= kCloudPoints || z < 0 || z >= kCloudPoints)
                        continue;
                    const int value = static_cast<int>(static_cast<float>(flash) * (1 - 0.5f * std::abs(dx)));
                    uint8_t* c = skyVertices_[cloudVertex_ + uint32_t(z * kCloudPoints + x)].color;
                    if (value <= c[0])
                        continue;
                    c[0] = c[1] = c[2] = static_cast<uint8_t>(value);
                    if (horizon_.cloudType != 2)
                        c[3] = static_cast<uint8_t>(value);
                }
        }
    }
    renderer_->setMeshVertices(skyMesh_, skyVertices_);

    if (boltMesh_) {
        std::vector<gfx::Vertex> quads(240 * 4);
        uint32_t q = 0;
        for (const LightningBolt& bolt : bolts_)
            for (const LightningBolt::Point& p : bolt.points) {
                if (p.parent < 0 || q >= 240)
                    continue;
                // Track coordinates to the page's (z negated); about a pixel
                // and a half wide, across the line as seen.
                const auto& a = bolt.points[size_t(p.parent)].at;
                const gfx::Vec3 from{a[0], a[1], -a[2]}, to{p.at[0], p.at[1], -p.at[2]};
                const gfx::Vec3 along{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                const gfx::Vec3 mid{(to[0] + from[0]) / 2, (to[1] + from[1]) / 2, (to[2] + from[2]) / 2};
                gfx::Vec3 side{along[1] * mid[2] - along[2] * mid[1], along[2] * mid[0] - along[0] * mid[2],
                               along[0] * mid[1] - along[1] * mid[0]};
                const float length = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
                const float distance = std::sqrt(mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2]);
                if (length <= 0)
                    continue;
                const float width = distance * 0.0035f / length;
                const gfx::Vec3* ends[4] = {&from, &to, &to, &from};
                for (int k = 0; k < 4; ++k) {
                    gfx::Vertex& v = quads[q * 4 + uint32_t(k)];
                    const float s = k == 0 || k == 1 ? -width : width;
                    for (int i = 0; i < 3; ++i)
                        v.pos[i] = (*ends[k])[size_t(i)] + side[size_t(i)] * s;
                    // 0xffe2e2ff, pale blue-white (0x4946fd).
                    v.color[0] = v.color[1] = 0xe2;
                    v.color[2] = v.color[3] = 0xff;
                }
                ++q;
            }
        boltIndices_ = q * 6;
        if (q)
            renderer_->setMeshVertices(boltMesh_, quads);
    }
    skyChanged_ = !bolts_.empty();
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
    // Sets 0 and 1, then the same in 8 bits (for the software renderer).
    const std::string sheet = textureSheet();
    std::vector<gfx::Vertex> vertices;
    struct Group {
        std::vector<uint32_t> indices;
        bool additive = false;
    };
    // By picture, then two-sided, then see-through.
    using Groups = std::map<std::tuple<gfx::TextureId, bool, bool>, Group>;
    std::array<Groups, kKinds> groups;
    std::vector<Groups> movingGroups;
    // Animated polygons by kind and animation byte, a Groups per frame.
    std::map<std::pair<int, uint8_t>, std::vector<Groups>> animatedGroups;
    // One polygon with the texture entry given.
    auto addPolygon = [&](Groups& into, const std::vector<Track::Vec3>& verts, const std::vector<uint32_t>& colours,
                          const Track::Vec3& offset, float mirror, const Track::Polygon& p, uint16_t entry) {
        {
            const Track::Texture& tex = track_.textures[entry < track_.textures.size() ? entry : p.texture];
            // The game draws these in a constant half grey, half clear
            // (0x41b596), sorted by depth.
            const bool seeThrough = (tex.flags & Track::Texture::kSeeThrough) != 0;
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
                out.color[0] = seeThrough ? 127 : static_cast<uint8_t>(c >> 16);
                out.color[1] = seeThrough ? 127 : static_cast<uint8_t>(c >> 8);
                out.color[2] = seeThrough ? 127 : static_cast<uint8_t>(c);
                out.color[3] = seeThrough ? 127 : 255;
                vertices.push_back(out);
            }
            Group& g = into[{id, (p.flags & Track::Polygon::kTwoSided) != 0, seeThrough}];
            g.additive = additive;
            for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                g.indices.push_back(first + i);
        }
    };
    // Polygons of a kind (-1: a moving object's, not animated); an
    // animated one goes in once for each frame.
    auto add = [&](int kind, Groups& into, const std::vector<Track::Vec3>& verts,
                   const std::vector<uint32_t>& colours, const Track::Vec3& offset, float mirror,
                   const std::vector<Track::Polygon>& polygons) {
        for (const Track::Polygon& p : polygons) {
            if (kind < 0 || !(p.flags & Track::Polygon::kAnimated) || p.frameDelay() == 0 || p.frameCount() < 2) {
                addPolygon(into, verts, colours, offset, mirror, p, p.textureAt(0));
                continue;
            }
            auto& frames = animatedGroups[{kind, p.animation}];
            frames.resize(static_cast<size_t>(p.frameCount()));
            for (int f = 0; f < p.frameCount(); ++f)
                addPolygon(frames[size_t(f)], verts, colours, offset, mirror, p,
                           p.textureAt(static_cast<uint64_t>(f * p.frameDelay())));
        }
    };
    for (const Track::Block& b : track_.blocks) {
        for (int level = 0; level < 3; ++level) {
            add(kRoadLow + level, groups[kRoadLow + level], b.vertices, b.colours, {}, -1, b.polygons[2 * level]);
            add(kRoadLow + level, groups[kRoadLow + level], b.vertices, b.colours, {}, -1,
                b.polygons[2 * level + 1]);
        }
        add(kLanes, groups[kLanes], b.vertices, b.colours, {}, -1, b.polygons[6]);
        for (const auto& layer : b.objectLayers)
            for (const Track::BlockObject& o : layer)
                add(kObjects, groups[kObjects], b.vertices, b.colours, {}, -1, o.polygons);
    }
    for (const auto& group : track_.extraObjects)
        for (const Track::ExtraObject& o : group) {
            if (o.type == 4) {
                add(kObjects, groups[kObjects], o.vertices, o.colours, o.reference, -1, o.polygons);
            } else if (!o.keys.empty()) {
                moving_.push_back({o.keys, o.keyDelay, {}});
                movingGroups.emplace_back();
                add(-1, movingGroups.back(), o.vertices, o.colours, {}, 1, o.polygons);
            }
        }

    // The virtual road's walls: a barrier along each side, red on the left
    // and green on the right, from each road point to the next (a guide
    // for checking the .col data against the scenery). Lower and yellow,
    // the road's edges from its lane counts and widths.
    const auto& road = collision_.road;
    for (size_t i = 0; i + 1 < road.size(); ++i) {
        const auto& a = road[i];
        const auto& b = road[i + 1];
        for (int side = 0; side < 4; ++side) {
            const bool right = side % 2 != 0, edge = side >= 2;
            const float sa = edge ? (right ? a.rightEdge() : -a.leftEdge()) : (right ? a.rightWall : -a.leftWall);
            const float sb = edge ? (right ? b.rightEdge() : -b.leftEdge()) : (right ? b.rightWall : -b.leftWall);
            const float height = edge ? 0.4f : 1.5f;
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            for (int k = 0; k < 4; ++k) {
                const auto& p = k < 2 ? a : b;
                const float s = k < 2 ? sa : sb;
                gfx::Vertex out{};
                out.pos[0] = p.position[0] + p.right[0] * s;
                out.pos[1] = p.position[1] + p.right[1] * s + (k == 1 || k == 2 ? height : 0);
                out.pos[2] = -(p.position[2] + p.right[2] * s);
                out.normal[1] = 1;
                out.color[0] = edge || !right ? 255 : 40;
                out.color[1] = edge || right ? 255 : 40;
                out.color[2] = 40;
                out.color[3] = 255;
                vertices.push_back(out);
            }
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                groups[kWalls][{0, false, false}].indices.push_back(first + k);
        }
    }

    std::vector<uint32_t> indices;
    auto flush = [&](const Groups& from, std::vector<Batch>& to) {
        for (const auto& [key, g] : from) {
            const auto& [texture, twoSided, seeThrough] = key;
            to.push_back({texture, static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(g.indices.size()),
                          g.additive, twoSided, seeThrough});
            indices.insert(indices.end(), g.indices.begin(), g.indices.end());
        }
    };
    for (int kind = 0; kind < kKinds; ++kind)
        flush(groups[kind], batches_[kind]);
    for (size_t i = 0; i < moving_.size(); ++i)
        flush(movingGroups[i], moving_[i].batches);
    for (const auto& [key, frames] : animatedGroups) {
        Animation a;
        a.delay = key.second >> 3;
        a.frames.resize(frames.size());
        for (size_t f = 0; f < frames.size(); ++f)
            flush(frames[f], a.frames[f]);
        animations_[size_t(key.first)].push_back(std::move(a));
    }
    if (!vertices.empty())
        if (Error err = renderer_->createMesh(vertices, indices, mesh_))
            error_ = err.message;
    loadSky();
    loadMarks();
    toBlock(0);
}

int TrackPage::nearestBlock() const {
    int best = block_;
    float bestDistance = 1e30f;
    for (size_t i = 0; i < track_.blocks.size(); ++i) {
        const Track::Vec3& c = track_.blocks[i].centre;
        const float dx = c[0] - eye_[0], dy = c[1] - eye_[1], dz = -c[2] - eye_[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// The blocks' lights and sounds (frd.md), and the glow picture.
void TrackPage::loadMarks() {
    marks_.clear();
    for (const Track::Block& b : track_.blocks) {
        for (const Track::Block::Light& l : b.lightList)
            marks_.push_back({{l.position[0], l.position[1], -l.position[2]}, true, static_cast<int>(l.type), 0});
        for (const Track::Block::PlacedSound& snd : b.soundList)
            marks_.push_back({{snd.position[0], snd.position[1], -snd.position[2]}, false, snd.slot, snd.kind,
                              &snd == &b.soundList.front()});
    }
    bool additive = false;
    picture(kLaneSheet, -1, false, &additive); // loads the sheet
    for (const Shape& shape : banks_[kLaneSheet].shapes)
        if (shape.name == "glw0") {
            image::Image img;
            if (!decodeShape(shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, glowPicture_))
                textures_.push_back(glowPicture_);
        }
    if (marks_.empty())
        return;
    std::vector<gfx::Vertex> vertices(marks_.size() * 4);
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < marks_.size(); ++i)
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(4 * i + k);
    if (Error err = renderer_->createMesh(vertices, indices, markMesh_))
        markMesh_ = 0;
    loadSounds();
}

// Each mark as a quad facing the camera: a light's glow as its type has
// it (lightLook: colour, size, blinking at the sky's ticks), a small cyan
// diamond for a sound.
void TrackPage::drawMarks(const gfx::Vec3& eye, const gfx::Vec3& right, const gfx::Vec3& up) {
    if (!markMesh_ || !marksShown_)
        return;
    (void)eye;
    std::vector<gfx::Vertex> vertices;
    vertices.reserve(marks_.size() * 4);
    for (const Mark& m : marks_) {
        const LightLook look = m.light ? lightLook(static_cast<uint32_t>(m.value), skyTick_) : LightLook{};
        const float half = !m.light ? 0.6f : look.shown ? look.size : 0.0f;
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner == 1 || corner == 2 ? 1.0f : -1.0f, y = corner >= 2 ? 1.0f : -1.0f;
            gfx::Vertex v{};
            for (int i = 0; i < 3; ++i)
                v.pos[i] = m.at[size_t(i)] + (right[size_t(i)] * x + up[size_t(i)] * y) * half;
            v.uv[0] = (x + 1) / 2;
            v.uv[1] = (1 - y) / 2;
            if (m.light) {
                v.color[0] = static_cast<uint8_t>(look.color >> 16);
                v.color[1] = static_cast<uint8_t>(look.color >> 8);
                v.color[2] = static_cast<uint8_t>(look.color);
                v.color[3] = static_cast<uint8_t>(look.color >> 24);
            } else {
                // The diamond: the quad's corners pulled in to its edges' middles.
                for (int i = 0; i < 3; ++i)
                    v.pos[i] = m.at[size_t(i)] + (corner % 2 ? right[size_t(i)] * x : up[size_t(i)] * y) * half;
                v.color[0] = 60;
                v.color[1] = v.color[2] = v.color[3] = 255;
            }
            vertices.push_back(v);
        }
    }
    renderer_->setMeshVertices(markMesh_, vertices);
    gfx::DrawCall call;
    call.mesh = markMesh_;
    call.rgbSource = call.alphaSource = gfx::ColorSource::Vertex;
    call.state.cull = gfx::Cull::None;
    call.state.depthWrite = false;
    // The glows added, over everything drawn but not through it.
    std::vector<std::pair<uint32_t, uint32_t>> lights, sounds; // ranges of marks
    for (size_t i = 0; i < marks_.size(); ++i) {
        auto& list = marks_[i].light ? lights : sounds;
        if (!list.empty() && list.back().second == i)
            ++list.back().second;
        else
            list.push_back({uint32_t(i), uint32_t(i + 1)});
    }
    call.texture = glowPicture_;
    call.state.src = gfx::Blend::SrcAlpha;
    call.state.dst = gfx::Blend::One;
    for (const auto& [from, to] : lights) {
        call.firstIndex = 6 * from;
        call.indexCount = 6 * (to - from);
        renderer_->draw(call);
    }
    call.texture = 0;
    call.state.dst = gfx::Blend::Zero;
    call.state.src = gfx::Blend::One;
    for (const auto& [from, to] : sounds) {
        call.firstIndex = 6 * from;
        call.indexCount = 6 * (to - from);
        renderer_->draw(call);
    }
}

// The variant's bank of placed sounds: tr3dNN.bnk, ...n, ...w, ...nw.
void TrackPage::loadSounds() {
    stopSounds();
    soundData_.clear();
    soundBank_ = {};
    soundSlots_.clear();
    if (!mixer_)
        return;
    const std::string file =
        "GameData/Audio/SFX/tr3d" + tracks_[index_].substr(4) + kHorizonSuffix[variant_] + ".bnk";
    for (const FileRef& f : files_.files())
        if (lower(f.name) == lower(file)) {
            if (files_.read(f, soundData_) || parseSoundBank(soundData_, soundBank_)) {
                soundData_.clear();
                soundBank_ = {};
            }
            return;
        }
}

void TrackPage::stopSounds() {
    if (mixer_)
        for (const auto& [mark, handle] : soundsPlaying_)
            mixer_->stop(handle);
    soundsPlaying_.clear();
}

// The placed sounds within reach, as the game plays them (0x41d210):
// only each block's first; the nearest loop on one channel, stopped when
// none is in reach; one-shots on frames the timer picks. The volume falls
// with the square of the distance; the pan is by side. The game takes
// the sounds of the blocks seen from the camera's (and the first loop
// among them), where this takes those within reach (and the nearest).
void TrackPage::updateSounds(const gfx::Vec3& eye, const gfx::Vec3& right) {
    if (!mixer_ || soundData_.empty())
        return;
    constexpr float kReach = 300; // volume 2 of 127 there
    ++soundFrame_;
    auto decoded = [&](int slot) -> std::shared_ptr<audio::Sound> {
        if (slot < 0 || slot >= static_cast<int>(soundBank_.slots.size()) || !soundBank_.slots[size_t(slot)])
            return nullptr;
        auto& sound = soundSlots_[slot];
        if (!sound) {
            sound = std::make_shared<audio::Sound>();
            if (decodeBankSound(soundData_, *soundBank_.slots[size_t(slot)], *sound))
                sound = std::make_shared<audio::Sound>(); // nothing to play
        }
        return sound->samples.empty() ? nullptr : sound;
    };
    size_t loop = marks_.size();
    float loopDistance = kReach;
    int loopVolume = 0, loopPan = 0;
    for (size_t i = 0; i < marks_.size(); ++i) {
        const Mark& m = marks_[i];
        if (m.light || !m.heard)
            continue;
        const float dx = m.at[0] - eye[0], dy = m.at[1] - eye[1], dz = m.at[2] - eye[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > kReach)
            continue;
        const int volume = placedSoundVolume(d) * audio::kMaxVolume / 127;
        const int pan = d > 0 ? static_cast<int>(audio::kMaxPan * (dx * right[0] + dz * right[2]) / d) : 0;
        if (m.kind == 1) {
            if (oneShots_.fires(soundFrame_, soundRandom_))
                if (auto sound = decoded(m.value))
                    mixer_->play(sound, audio::SoundType::Effects, volume, pan, false);
        } else if (d < loopDistance) {
            loop = i;
            loopDistance = d;
            loopVolume = volume;
            loopPan = pan;
        }
    }
    // The loop: kept going while it's still the one, else changed.
    for (auto it = soundsPlaying_.begin(); it != soundsPlaying_.end();) {
        if (it->first == loop) {
            mixer_->setVolume(it->second, loopVolume);
            mixer_->setPan(it->second, loopPan);
            ++it;
        } else {
            mixer_->stop(it->second);
            it = soundsPlaying_.erase(it);
        }
    }
    if (loop < marks_.size() && !soundsPlaying_.count(loop))
        if (auto sound = decoded(marks_[loop].value))
            soundsPlaying_[loop] = mixer_->play(sound, audio::SoundType::Effects, loopVolume, loopPan, true);
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
            if (renderer_ && error_.empty()) {
                loadSky();
                loadSounds();
            }
            break;
        case 't': textured_ = !textured_; break;
        case 'v': vertexLight_ = !vertexLight_; break;
        case 'l': lanes_ = !lanes_; break;
        case 'o': objects_ = !objects_; break;
        case 'g': walls_ = !walls_; break;
        case 'f': cullBack_ = !cullBack_; break;
        case 'y': fog_ = !fog_; break;
        case 'm':
            marksShown_ = !marksShown_;
            if (!marksShown_)
                stopSounds();
            break;
        case 'x':
            textureSet_ = (textureSet_ + 1) % 4;
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
    const int here = nearestBlock();
    fogAt(here, fog, fogDensity);
    if (!fog_)
        fogDensity = 0;
    tickSky(nowMs);
    updateSky(forward, right);
    renderer_->beginFrame(viewProj, eye,
                          {static_cast<uint8_t>(fog[0]), static_cast<uint8_t>(fog[1]), static_cast<uint8_t>(fog[2])});
    // Fog as the game has the card draw it (thrash.md, Fog): its curve over
    // the depth, with the values it sends for the scenery and for the sky.
    const bool fogged = fog_ && skyError_.empty();
    const float sceneryFog = fogged ? static_cast<float>(sceneryFogValue(static_cast<int>(fogDensity + 0.5f))) : 0;
    const float skyFog = fogged ? static_cast<float>(skyFogValue(horizon_, static_cast<int>(fogDensity + 0.5f))) : 0;
    auto fogDraw = [&](gfx::DrawCall& call, float value) {
        call.tableFog = value;
        call.tableFogFromDepth = true;
        call.tableFogColor = {fog[0] / 255.0f, fog[1] / 255.0f, fog[2] / 255.0f};
    };

    // The sky first, around the camera, behind everything.
    if (skyMesh_) {
        gfx::DrawCall sky;
        sky.mesh = skyMesh_;
        sky.followCamera = true;
        sky.rgbSource = gfx::ColorSource::Vertex;
        sky.state.cull = gfx::Cull::None;
        sky.state.depthTest = sky.state.depthWrite = false;
        // As the game draws it: the sky's shading, the sun and clouds, then
        // the earth's shading and the horizon picture, fogged as the sky.
        sky.firstIndex = earthIndices_;
        sky.indexCount = domeIndices_ - earthIndices_;
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
            // Drifting with the wind: cloudDrift() a game tick, added to
            // the texture coordinates and kept within a tile.
            const std::array<float, 2> drift = cloudDrift(horizon_);
            const float ticks = static_cast<float>(nowMs % 100000000) * kGameTicksPerSecond / 1000.0f;
            clouds.preRow0[2] = std::fmod(drift[0] * ticks, 1.0f);
            clouds.preRow1[2] = std::fmod(drift[1] * ticks, 1.0f);
            if (horizon_.fogClouds)
                fogDraw(clouds, skyFog);
            renderer_->draw(clouds);
        }
        // Lightning's bolts, over the clouds (0x47a1c4).
        if (boltMesh_ && boltIndices_) {
            gfx::DrawCall bolts = sky;
            bolts.mesh = boltMesh_;
            bolts.firstIndex = 0;
            bolts.indexCount = boltIndices_;
            fogDraw(bolts, skyFog);
            renderer_->draw(bolts);
        }
        // INFRONT: the sun over the clouds, still behind the horizon.
        if (sunInFront_)
            drawSun();
        fogDraw(sky, skyFog);
        sky.firstIndex = 0;
        sky.indexCount = earthIndices_;
        renderer_->draw(sky);
        if (bandIndices_ && horizonPicture_) {
            sky.firstIndex = domeIndices_;
            sky.indexCount = bandIndices_;
            sky.texture = horizonPicture_;
            sky.state.src = gfx::Blend::SrcAlpha;
            sky.state.dst = gfx::Blend::OneMinusSrcAlpha;
            renderer_->draw(sky);
        }
    }

    size_t drawn = 0;
    if (mesh_) {
        gfx::DrawCall call;
        call.mesh = mesh_;
        call.rgbSource = vertexLight_ ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
        // The horizon's ambient light (percent) over the baked lighting.
        if (error_.empty() && skyError_.empty())
            for (int i = 0; i < 3; ++i)
                call.color[i] = static_cast<float>(horizon_.ambient[i]) / 100.0f;
        call.state.alphaTest = gfx::AlphaTest::Ge128;
        if (sceneryFog > 0)
            fogDraw(call, sceneryFog);
        // Each kind's batches, and its animated polygons at this tick's
        // frame.
        auto kind = [&](int k, std::vector<const std::vector<Batch>*>& into) {
            into.push_back(&batches_[k]);
            for (const Animation& a : animations_[k])
                into.push_back(&a.at(skyTick_));
        };
        std::vector<const std::vector<Batch>*> solid;
        kind(kRoadLow + detail_, solid);
        if (objects_)
            kind(kObjects, solid);
        std::vector<const std::vector<Batch>*> lanes;
        if (lanes_)
            kind(kLanes, lanes);
        std::vector<const std::vector<Batch>*> all = solid;
        all.insert(all.end(), lanes.begin(), lanes.end());
        // Additive pictures, see-through ones, or the rest.
        enum Pass { kSolid, kSeeThrough, kAdditive };
        auto pass = [&](const std::vector<Batch>& list, Pass which) {
            for (const Batch& b : list) {
                if ((b.additive ? kAdditive : b.seeThrough ? kSeeThrough : kSolid) != which)
                    continue;
                call.state.cull = cullBack_ && !b.twoSided ? gfx::Cull::Back : gfx::Cull::None;
                call.texture = textured_ ? b.texture : 0;
                call.firstIndex = b.firstIndex;
                call.indexCount = b.indexCount;
                renderer_->draw(call);
                drawn += b.indexCount / 6;
            }
        };
        // Moving objects at the key for now, between it and the next.
        auto movingPass = [&](Pass which) {
            if (!objects_)
                return;
            for (const Moving& m : moving_) {
                // As nfs3.exe (0x40f4b0): a key every `delay` ticks (1-400,
                // else 6), round keys - 1 of them (the last is the first
                // again), blended towards the next by the ticks since.
                if (m.keys.size() < 2)
                    continue;
                const int delay = m.keyDelay >= 1 && m.keyDelay <= 400 ? m.keyDelay : 6;
                const float ticks = static_cast<float>(nowMs % 100000000) * kGameTicksPerSecond / 1000.0f;
                const float t = ticks / static_cast<float>(delay);
                const size_t k = static_cast<size_t>(t) % (m.keys.size() - 1);
                call.model = keyMatrix(m.keys[k], m.keys[k + 1], t - std::floor(t));
                pass(m.batches, which);
            }
            call.model = gfx::identity();
        };
        // Solid first, then the lane markings pulled onto the road, then
        // additive pictures (lights, glows) over everything.
        for (const auto* list : solid)
            pass(*list, kSolid);
        movingPass(kSolid);
        call.state.depthBias = true;
        for (const auto* list : lanes)
            pass(*list, kSolid);
        call.state.depthBias = false;
        if (walls_) {
            // A guide drawn over everything, so the walls show through the
            // ground and fences they follow.
            gfx::DrawCall wall = call;
            wall.tableFog = 0;
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
        // See-through surfaces (glass, water) blended over what's behind,
        // in the half grey and half alpha of their vertices. The game sorts
        // them by depth; here they go in the mesh's order.
        {
            const gfx::DrawCall before = call;
            call.rgbSource = call.alphaSource = gfx::ColorSource::Vertex;
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::OneMinusSrcAlpha;
            call.state.depthWrite = false;
            call.state.alphaTest = gfx::AlphaTest::None;
            for (const auto* list : all)
                pass(*list, kSeeThrough);
            movingPass(kSeeThrough);
            call = before;
        }
        call.state.src = call.state.dst = gfx::Blend::One;
        call.state.depthWrite = false;
        call.state.alphaTest = gfx::AlphaTest::None;
        for (const auto* list : all)
            pass(*list, kAdditive);
        movingPass(kAdditive);
    }

    {
        const gfx::Vec3 up{-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_), std::sin(pitch_) * std::cos(yaw_)};
        drawMarks(eye, right, up);
        if (marksShown_)
            updateSounds(eye, right);
    }

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(index_ + 1) + "/" + std::to_string(tracks_.size()) +
                                      "  " + tracks_[index_] + "  " + trackName(tracks_[index_]) + "  " +
                                      kVariantNames[variant_]};
    if (!error_.empty()) {
        lines.push_back(error_);
    } else {
        char where[200];
        std::snprintf(where, sizeof where, "block %d/%zu  at %.0f %.0f %.0f  %s detail  set %d%s  %zu quads  %zu moving",
                      here, track_.blocks.size(), eye_[0], eye_[1], -eye_[2], kDetailNames[detail_], textureSet_ % 2, textureSet_ >= 2 ? " (8-bit)" : "", drawn,
                      moving_.size());
        std::string line = where;
        // The nearest light or sound within 40 units.
        const Mark* nearest = nullptr;
        float nearestDistance = 40 * 40;
        for (const Mark& m : marks_) {
            const float dx = m.at[0] - eye_[0], dy = m.at[1] - eye_[1], dz = m.at[2] - eye_[2];
            if (dx * dx + dy * dy + dz * dz < nearestDistance) {
                nearestDistance = dx * dx + dy * dy + dz * dz;
                nearest = &m;
            }
        }
        if (nearest && marksShown_) {
            char what[120];
            if (nearest->light)
                std::snprintf(what, sizeof what, "light, type %d", nearest->value);
            else
                std::snprintf(what, sizeof what, "sound, slot %d of tr3d%s%s.bnk, %s%s", nearest->value,
                              tracks_[index_].substr(4).c_str(), kHorizonSuffix[variant_],
                              nearest->kind ? "one-shot" : "loop", nearest->heard ? "" : " (not played)");
            line += std::string("  near: ") + what;
        }
        lines.push_back(line);
        if (!skyError_.empty())
            lines.push_back(skyError_);
        if (!collisionError_.empty())
            lines.push_back(collisionError_);
    }
    lines.push_back("PgUp/PgDn: track  WASD/arrows/drag: move  N/B: block  1-3: detail  H: sky  T V L O G X F Y M: toggles");
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
