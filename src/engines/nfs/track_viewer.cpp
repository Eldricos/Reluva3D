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
constexpr const char* kDetailNames[] = {"low", "medium", "high", "game"};
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
// The camera's vertical field of view (radians), as gfx::perspective has it.
constexpr float kFieldOfView = 1.0f;

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
    skyHeights_.clear();
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
    particles_.clear();
    pendingHits_.clear();
    hitText_.clear();
    particlePictures_.clear(); // owned by textures_
    particleRuns_.clear();
    if (renderer_ && particleMesh_)
        renderer_->destroyMesh(particleMesh_);
    particleMesh_ = 0;
    if (renderer_ && lightMesh_)
        renderer_->destroyMesh(lightMesh_);
    lightMesh_ = 0;
    lightCapacity_ = 0;
    lightRuns_.clear();
    headlightPicture_ = 0; // owned by textures_
    if (renderer_) {
        if (cutMesh_)
            renderer_->destroyMesh(cutMesh_); // before the mesh whose vertices it shares
        if (mesh_)
            renderer_->destroyMesh(mesh_);
        for (gfx::TextureId t : textures_)
            renderer_->destroyTexture(t);
    }
    mesh_ = cutMesh_ = 0;
    meshVertices_.clear();
    meshIndices_.clear();
    cutIndices_.clear();
    textures_.clear();
    pictures_.clear();
    banks_.clear();
    moving_.clear();
    batches_.clear();
    animations_.clear();
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
    // A track sheet's picture is the shape at that place in its directory,
    // whatever its name: the game loads the shapes in order into its slots
    // (0x41db10) and the texture's number indexes them (0x419ab0). The names
    // don't always follow the order (tr010 lacks 0095, tr070 has 0001-0003
    // twice, tr040's are HOR1, ...). Lane pictures go by name (lin3).
    const Shape* shape = nullptr;
    if (lane) {
        char name[16];
        std::snprintf(name, sizeof name, "lin%d", number);
        for (const Shape& s : bank->second.shapes)
            if (s.name == name)
                shape = &s;
    } else if (number >= 0 && number < static_cast<int>(bank->second.shapes.size())) {
        shape = &bank->second.shapes[size_t(number)];
    }
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
            // Without mipmaps, as THRASH's textures have none: averaged
            // down, the night's one-texel stars (CLDN) all but vanish.
            gfx::TextureParams params;
            params.mipmaps = false;
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), params, id))
                textures_.push_back(id);
            return id;
        }
    return 0;
}

// The horizon picture: the track sheet's first eight pictures side by side,
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
        // By place, as picture() (0000-0007 on most sheets, HOR1-HOR8 on tr040 and tr060).
        const Shape* shape = i < static_cast<int>(bank->second.shapes.size()) ? &bank->second.shapes[size_t(i)] : nullptr;
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
        // A texel in from the top and bottom (the tiles are 64 high): the
        // texture repeats, so at v = 0 the filter would mix in the bottom
        // row, a dashed line along the top of the hills.
        constexpr float kInset = 1.0f / 64;
        ring(base + static_cast<float>(h.pixmapBottom), radius * 0.98f, &white, 1 - kInset);
        ring(base + static_cast<float>(h.pixmapTop), radius * 0.98f, &white, kInset);
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
    skyHeights_.clear();
    for (const gfx::Vertex& v : vertices)
        skyHeights_.push_back(v.pos[1]);
    skyEyeY_ = 0; // the heights as made: updateSky moves them by the camera's
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
    // The sky's heights are the world's, less the camera's (0x479500 takes
    // the camera's y from the shaded area's; the clouds' 0x47b4f0 too): all
    // but the sun's, which is placed round the camera.
    const bool raised = std::fabs(eye_[1] - skyEyeY_) > 0.01f;
    if (!skyMesh_ || skyVertices_.empty() || !(sunRotate_ || skyChanged_ || raised))
        return;
    if (raised) {
        skyEyeY_ = eye_[1];
        for (size_t i = 0; i < skyVertices_.size() && i < skyHeights_.size(); ++i)
            if (!sunIndices_ || i < sunVertex_ || i >= sunVertex_ + 4)
                skyVertices_[i].pos[1] = skyHeights_[i] - eye_[1];
    }
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
    const size_t slots = (track_.blocks.size() + 1) * kKinds; // by block and kind
    const int everywhere = static_cast<int>(track_.blocks.size()) * kKinds;
    std::vector<Groups> groups(slots);
    std::vector<Groups> movingGroups;
    // Animated polygons by kind and animation byte, a Groups per frame.
    std::map<std::pair<int, uint8_t>, std::vector<Groups>> animatedGroups;
    // One polygon with the texture entry given.
    auto addPolygon = [&](Groups& into, const std::vector<Track::Vec3>& verts, const std::vector<uint32_t>& colours,
                          const Track::Vec3& offset, float mirror, bool placed, const Track::Polygon& p,
                          uint16_t entry) {
        {
            const Track::Texture& tex = track_.textures[entry < track_.textures.size() ? entry : p.texture];
            // The game draws these in a constant half grey at half alpha
            // (0x41b596), sorted by depth, added: the queued polygon's flag
            // 1 sets THRASH blend 1, source alpha and one (0x4332b0).
            const bool seeThrough = (tex.flags & Track::Texture::kSeeThrough) != 0;
            bool additive = false;
            const gfx::TextureId id = tex.lane ? picture(kLaneSheet, tex.picture, true, &additive)
                                               : picture(sheet, tex.picture, false, &additive);
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            for (int k = 0; k < 4; ++k) {
                const uint16_t vertex = p.vertices[size_t(polygonSlot(k, placed))];
                const Track::Vec3& v = verts[vertex];
                const uint32_t c = colours[vertex];
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
            // The game's front faces turn the other way from the renderer's
            // once z is mirrored: wound backwards, the game's culling
            // (0x41b300: not drawn if back-facing, unless flag 0x10) is the
            // renderer's back-face culling.
            for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u})
                g.indices.push_back(first + i);
        }
    };
    // Polygons of a slot, block × kKinds + kind (-1: a moving object's,
    // not animated); an animated one goes in once for each frame.
    auto add = [&](int kind, Groups& into, const std::vector<Track::Vec3>& verts,
                   const std::vector<uint32_t>& colours, const Track::Vec3& offset, float mirror, bool placed,
                   const std::vector<Track::Polygon>& polygons) {
        for (const Track::Polygon& p : polygons) {
            if (kind < 0 || !(p.flags & Track::Polygon::kAnimated) || p.frameDelay() == 0 || p.frameCount() < 2) {
                addPolygon(into, verts, colours, offset, mirror, placed, p, p.textureAt(0));
                continue;
            }
            auto& frames = animatedGroups[{kind, p.animation}];
            frames.resize(static_cast<size_t>(p.frameCount()));
            for (int f = 0; f < p.frameCount(); ++f)
                addPolygon(frames[size_t(f)], verts, colours, offset, mirror, placed, p,
                           p.textureAt(static_cast<uint64_t>(f * p.frameDelay())));
        }
    };
    for (size_t i = 0; i < track_.blocks.size(); ++i) {
        const Track::Block& b = track_.blocks[i];
        const int base = static_cast<int>(i) * kKinds;
        for (int level = 0; level < 3; ++level) {
            const int slot = base + kRoadLow + level;
            add(slot, groups[size_t(slot)], b.vertices, b.colours, {}, -1, false, b.polygons[2 * level]);
            add(slot, groups[size_t(slot)], b.vertices, b.colours, {}, -1, false, b.polygons[2 * level + 1]);
        }
        add(base + kLanes, groups[size_t(base + kLanes)], b.vertices, b.colours, {}, -1, false, b.polygons[6]);
        for (size_t layer = 0; layer < b.objectLayers.size(); ++layer) {
            const int slot = base + (layer == 3 ? kObjectsDay : kObjects);
            for (const Track::BlockObject& o : b.objectLayers[layer])
                add(slot, groups[size_t(slot)], b.vertices, b.colours, {}, -1, false, o.polygons);
        }
    }
    // Four groups of placed objects a block (0x5718d4 + block × 0x5c0 +
    // 8 × group), and a last one.
    for (size_t gi = 0; gi < track_.extraObjects.size(); ++gi) {
        const size_t block = gi / 4;
        const bool own = block < track_.blocks.size();
        const int slot = own ? static_cast<int>(block) * kKinds + (gi % 4 == 3 ? kObjectsDay : kObjects)
                             : everywhere + kObjects;
        for (const Track::ExtraObject& o : track_.extraObjects[gi]) {
            if (o.type == 4) {
                add(slot, groups[size_t(slot)], o.vertices, o.colours, o.reference, -1, true, o.polygons);
            } else if (!o.keys.empty()) {
                moving_.push_back({own ? static_cast<int>(block) : -1, o.keys, o.keyDelay, {}});
                movingGroups.emplace_back();
                add(-1, movingGroups.back(), o.vertices, o.colours, {}, 1, true, o.polygons);
            }
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
                groups[size_t(everywhere + kWalls)][{0, false, false}].indices.push_back(first + k);
        }
    }

    std::vector<uint32_t> indices;
    auto flush = [&](const Groups& from, std::vector<Batch>& to) {
        for (const auto& [key, g] : from) {
            const auto& [texture, twoSided, seeThrough] = key;
            Batch b{texture, static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(g.indices.size()),
                    g.additive, twoSided, seeThrough};
            // The box round the polygons' first corners (each polygon's
            // six indices start with its corner 0), for the cut-off.
            std::array<float, 3> low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
            for (size_t i = 0; i < g.indices.size(); i += 6)
                for (int k = 0; k < 3; ++k) {
                    low[size_t(k)] = std::min(low[size_t(k)], vertices[g.indices[i]].pos[k]);
                    high[size_t(k)] = std::max(high[size_t(k)], vertices[g.indices[i]].pos[k]);
                }
            float r2 = 0;
            for (int k = 0; k < 3; ++k) {
                b.centre[size_t(k)] = (low[size_t(k)] + high[size_t(k)]) / 2;
                r2 += (high[size_t(k)] - low[size_t(k)]) * (high[size_t(k)] - low[size_t(k)]) / 4;
            }
            b.radius = std::sqrt(r2);
            to.push_back(b);
            indices.insert(indices.end(), g.indices.begin(), g.indices.end());
        }
    };
    batches_.resize(slots);
    animations_.resize(slots);
    for (size_t slot = 0; slot < slots; ++slot)
        flush(groups[slot], batches_[slot]);
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
    meshVertices_ = std::move(vertices);
    meshIndices_ = std::move(indices);
    loadSky();
    loadMarks();
    toBlock(0);
}

std::vector<std::pair<int, int>> TrackPage::blocksInView(int here) const {
    std::vector<std::pair<int, int>> out;
    if (here < 0 || here >= static_cast<int>(track_.blocks.size()))
        return out;
    const bool night = variant_ == 1 || variant_ == 3;
    auto add = [&](int block) {
        const Track::Vec3& c = track_.blocks[size_t(block)].centre;
        const float dx = c[0] - eye_[0], dz = -c[2] - eye_[2];
        const int detail = blockDetailAt(std::sqrt(dx * dx + dz * dz), night);
        if (allBlocks_ || detail >= 0)
            out.push_back({block, std::max(detail, 0)});
    };
    if (allBlocks_) {
        for (size_t i = 0; i < track_.blocks.size(); ++i)
            add(static_cast<int>(i));
        return out;
    }
    std::vector<bool> seen(track_.blocks.size(), false);
    for (int16_t n : track_.blocks[size_t(here)].neighbours)
        if (n >= 0 && n < static_cast<int>(track_.blocks.size()) && !seen[size_t(n)]) {
            seen[size_t(n)] = true;
            add(n);
        }
    return out;
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

// The game's cut-off (kPolygonReach, 0x41b3bf): each batch whose
// polygons' first corners are all within the reach ahead is drawn whole,
// one all beyond it not at all, and one between from cutIndices_, which
// keeps only its polygons whose first corner is within it.
void TrackPage::cutFar(std::vector<Batch>& list, const gfx::Vec3& eye, const gfx::Vec3& forward) {
    for (Batch& b : list) {
        auto ahead = [&](const float* at) {
            return (at[0] - eye[0]) * forward[0] + (at[1] - eye[1]) * forward[1] + (at[2] - eye[2]) * forward[2];
        };
        const float centre = ahead(b.centre.data());
        if (polygonInReach(centre + b.radius))
            continue;
        const uint32_t first = static_cast<uint32_t>(cutIndices_.size());
        if (polygonInReach(centre - b.radius))
            for (uint32_t i = b.firstIndex; i + 6 <= b.firstIndex + b.indexCount; i += 6)
                if (polygonInReach(ahead(meshVertices_[meshIndices_[i]].pos)))
                    cutIndices_.insert(cutIndices_.end(), meshIndices_.begin() + i, meshIndices_.begin() + i + 6);
        b.firstIndex = first;
        b.indexCount = static_cast<uint32_t>(cutIndices_.size()) - first;
        b.cut = true;
    }
}

constexpr float kVirtualCarHalfLength = 2.40644f;

// The headlights of a car at the camera, heading where it looks, as the
// game's pass for each car does at night (0x41c670, 0x41ae90): for each
// block drawn within 150 of the car on the ground, its road and roadside
// at the block's detail, object layers 0-2 and, at high detail, the lane
// markings (placed objects aren't lit). A polygon within the reach whose
// texture lacks flag 0x04 gets glw2 projected on it, as two triangles
// (corners 0, 1, 2 and 0, 2, 3) added in the light's colour; the others
// raise the colours of their corners inside the light to its colour, which
// every polygon of those lists sharing such a corner shows (drawn here as
// the difference added over the polygon's own picture).
void TrackPage::prepareHeadlights(int here, const gfx::Vec3& eye, const gfx::Vec3& forward) {
    lightRuns_.clear();
    const bool night = variant_ == 1 || variant_ == 3;
    if (!night || beams_ == 0 || track_.blocks.empty() || !headlightPicture_)
        return;
    // The car at the camera, upright, as long as the Corvette (half
    // length 2.40644, car.fce); its light where the game puts it.
    Headlight light = carHeadlight(beams_, projected_, {eye_[0], eye_[1], -eye_[2]}, yaw_);
    light.at = headlightPlace(light, {eye_[0], eye_[1], -eye_[2]}, kVirtualCarHalfLength);
    const std::string sheet = textureSheet();
    float ambient[3] = {1, 1, 1};
    if (skyError_.empty())
        for (int i = 0; i < 3; ++i)
            ambient[i] = static_cast<float>(horizon_.ambient[size_t(i)]) / 100.0f;
    std::vector<gfx::Vertex> projectedTris; // the projected picture's triangles
    std::map<gfx::TextureId, std::vector<gfx::Vertex>> floors; // the colour raised, by texture
    auto vertexAt = [](const Track::Vec3& p) {
        gfx::Vertex v{};
        v.pos[0] = p[0];
        v.pos[1] = p[1];
        v.pos[2] = -p[2];
        v.normal[1] = 1;
        return v;
    };
    for (const auto& [block, blockDetail] : blocksInView(here)) {
        const Track::Block& b = track_.blocks[size_t(block)];
        const float dx = b.centre[0] - eye_[0], dz = -b.centre[2] - eye_[2];
        if (dx * dx + dz * dz > 150.0f * 150.0f) // 0x41c6fe
            continue;
        const int level = detail_ < 3 ? detail_ : blockDetail;
        std::vector<const std::vector<Track::Polygon>*> lists = {&b.polygons[size_t(2 * level)],
                                                               &b.polygons[size_t(2 * level + 1)]};
        for (size_t layer = 0; layer < 3 && layer < b.objectLayers.size(); ++layer)
            for (const Track::BlockObject& o : b.objectLayers[layer])
                lists.push_back(&o.polygons);
        if (level == 2 && lanes_)
            lists.push_back(&b.polygons[6]);
        std::vector<HeadlightPoint> points(b.vertices.size());
        for (size_t i = 0; i < b.vertices.size(); ++i)
            points[i] = headlightAt(light, b.vertices[i]);
        std::vector<bool> raised(b.vertices.size(), false);
        auto entryOf = [&](const Track::Polygon& p) -> const Track::Texture& {
            const uint16_t e = p.textureAt(skyTick_);
            return track_.textures[e < track_.textures.size() ? e : p.texture];
        };
        for (const auto* list : lists)
            for (const Track::Polygon& p : *list) {
                if (p.vertices[0] >= b.vertices.size() || p.vertices[1] >= b.vertices.size() ||
                    p.vertices[2] >= b.vertices.size() || p.vertices[3] >= b.vertices.size())
                    continue;
                const Track::Vec3& c0 = b.vertices[p.vertices[0]];
                if (!polygonInReach((c0[0] - eye[0]) * forward[0] + (c0[1] - eye[1]) * forward[1] +
                                    (-c0[2] - eye[2]) * forward[2]))
                    continue;
                if (projected_ && !(entryOf(p).flags & 0x04)) {
                    // Wound backwards, as the track's polygons are here.
                    for (const auto& tri : {std::array<int, 3>{0, 2, 1}, std::array<int, 3>{0, 3, 2}}) {
                        const HeadlightPoint* corner[3];
                        for (int k = 0; k < 3; ++k)
                            corner[k] = &points[p.vertices[size_t(tri[size_t(k)])]];
                        if (!headlightTriangle(*corner[0], *corner[1], *corner[2]))
                            continue;
                        for (int k = 0; k < 3; ++k) {
                            gfx::Vertex v = vertexAt(b.vertices[p.vertices[size_t(tri[size_t(k)])]]);
                            v.uv[0] = corner[k]->u;
                            v.uv[1] = corner[k]->v;
                            v.color[0] = static_cast<uint8_t>(corner[k]->colour >> 16);
                            v.color[1] = static_cast<uint8_t>(corner[k]->colour >> 8);
                            v.color[2] = static_cast<uint8_t>(corner[k]->colour);
                            v.color[3] = static_cast<uint8_t>(corner[k]->colour >> 24);
                            projectedTris.push_back(v);
                        }
                    }
                } else {
                    for (uint16_t vertex : p.vertices)
                        if (points[vertex].outside == 0)
                            raised[vertex] = true;
                }
            }
        for (const auto* list : lists)
            for (const Track::Polygon& p : *list) {
                bool any = false;
                for (uint16_t vertex : p.vertices)
                    any = any || (vertex < raised.size() && raised[vertex]);
                if (!any)
                    continue;
                const Track::Texture& tex = entryOf(p);
                bool additive = false;
                const gfx::TextureId id = tex.lane ? picture(kLaneSheet, tex.picture, true, &additive)
                                                   : picture(sheet, tex.picture, false, &additive);
                if (additive || (tex.flags & Track::Texture::kSeeThrough))
                    continue;
                auto& into = floors[id];
                for (int k : {0, 2, 1, 0, 3, 2}) {
                    const uint16_t vertex = p.vertices[size_t(k)];
                    gfx::Vertex v = vertexAt(b.vertices[vertex]);
                    v.uv[0] = tex.corners[size_t(2 * k)];
                    v.uv[1] = tex.corners[size_t(2 * k + 1)];
                    if (raised[vertex]) {
                        const uint32_t baked = b.colours[vertex], lit = points[vertex].colour;
                        for (int i = 0; i < 3; ++i) {
                            const int shift = 16 - 8 * i;
                            const float own = static_cast<float>((baked >> shift) & 0xff) * (vertexLight_ ? ambient[i] : 1);
                            const float up = static_cast<float>((lit >> shift) & 0xff) - own;
                            v.color[i] = static_cast<uint8_t>(std::clamp(up, 0.0f, 255.0f));
                        }
                    }
                    v.color[3] = 255;
                    into.push_back(v);
                }
            }
    }
    std::vector<gfx::Vertex> vertices = std::move(projectedTris);
    if (!vertices.empty())
        lightRuns_.push_back({headlightPicture_, true, 0, static_cast<uint32_t>(vertices.size())});
    for (auto& [id, list] : floors) {
        lightRuns_.push_back({id, false, static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(list.size())});
        vertices.insert(vertices.end(), list.begin(), list.end());
    }
    if (vertices.empty())
        return;
    // One mesh, its indices in order, grown as needed; unused vertices
    // left as degenerate triangles.
    if (vertices.size() > lightCapacity_) {
        if (lightMesh_)
            renderer_->destroyMesh(lightMesh_);
        lightMesh_ = 0;
        lightCapacity_ = std::max<size_t>(vertices.size() * 3 / 2, 3000);
        std::vector<uint32_t> indices(lightCapacity_);
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = static_cast<uint32_t>(i);
        vertices.resize(lightCapacity_);
        if (renderer_->createMesh(vertices, indices, lightMesh_)) {
            lightMesh_ = 0;
            lightCapacity_ = 0;
            lightRuns_.clear();
        }
        return;
    }
    vertices.resize(lightCapacity_);
    renderer_->setMeshVertices(lightMesh_, vertices);
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
        } else if (shape.name == "glw2") {
            // The headlights' picture (0x4d5538 loads it into 0x8b4814,
            // which 0x41a120 draws with); clamped, its edges black.
            image::Image img;
            gfx::TextureParams params;
            params.clamp = true;
            if (!decodeShape(shape, img) &&
                !renderer_->createTexture(img.w, img.h, img.rgba.data(), params, headlightPicture_))
                textures_.push_back(headlightPicture_);
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
        LightLook look = m.light ? lightLook(static_cast<uint32_t>(m.value), skyTick_) : LightLook{};
        look.color = dimLight(look.color, lightIntensity_);
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

// A wall hit at the virtual road's wall nearest the line of sight (ahead,
// within 150 units): a car going along the road the way the
// camera looks, at 40 (fast) or 18, its side against the wall, as the
// game's impact code (0x42ac40) would see it. Made at the next tick.
void TrackPage::hitWall(bool fast) {
    const gfx::Vec3 forward{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_)};
    const auto& road = collision_.road;
    int best = -1;
    bool bestRight = false;
    float bestDistance = 1e9f;
    for (size_t i = 0; i < road.size(); ++i)
        for (bool right : {false, true}) {
            const TrackCollision::RoadPoint& p = road[i];
            const int code = (p.unknown >> (right ? 16 : 20)) & 15; // byte 14: low nibble right
            if (code == 0)
                continue;
            const float s = right ? p.rightWall : -p.leftWall;
            const float d[3] = {p.position[0] + p.right[0] * s - eye_[0], p.position[1] + p.right[1] * s - eye_[1],
                                -(p.position[2] + p.right[2] * s) - eye_[2]};
            // How far from the line of sight, of those ahead.
            const float ahead = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
            const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (ahead < 1 || distance > 150)
                continue;
            const float off = std::sqrt(std::max(0.0f, distance * distance - ahead * ahead));
            if (off >= bestDistance)
                continue;
            best = static_cast<int>(i);
            bestRight = right;
            bestDistance = off;
        }
    if (best < 0) {
        hitText_ = road.empty() ? "wall hit: this track has no virtual road" : "wall hit: no wall ahead";
        return;
    }
    const TrackCollision::RoadPoint& p = road[size_t(best)];
    WallHit hit;
    hit.code = (p.unknown >> (bestRight ? 16 : 20)) & 15;
    hit.speed = fast ? 40.0f : 18.0f;
    hit.impact = 10;
    const float s = bestRight ? p.rightWall : -p.leftWall;
    const float out = bestRight ? 1.0f : -1.0f; // from the road to the wall
    // The contact half a unit in from the wall's line and up from the road
    // (the game's is the car's corner, +0x178), the car going the way the
    // camera looks (the track's z is the world's -z), its centre a unit
    // further in.
    const float ahead = p.forward[0] * forward[0] + p.forward[1] * forward[1] - p.forward[2] * forward[2] >= 0 ? 1.0f : -1.0f;
    for (int i = 0; i < 3; ++i) {
        hit.contact[size_t(i)] =
            p.position[size_t(i)] + p.right[size_t(i)] * (s - out * 0.5f) + p.normal[size_t(i)] * 0.5f;
        hit.carPosition[size_t(i)] = hit.contact[size_t(i)] - p.right[size_t(i)] * out;
        hit.carVelocity[size_t(i)] = p.forward[size_t(i)] * ahead * hit.speed;
    }
    pendingHits_.push_back(hit);
    const int kind = wallParticleKind(hit.code);
    const char* what = kind == 3 ? "sparks" : kind == 2 ? "dust" : "none";
    const char* slow = hit.speed > 15 ? "smoke" : "nothing more";
    char text[200];
    const float dx = hit.contact[0] - eye_[0], dy = hit.contact[1] - eye_[1], dz = -hit.contact[2] - eye_[2];
    std::snprintf(text, sizeof text, "wall hit: point %d, %s side, %.0f away, code %d (%s), speed %.0f: %s", best,
                  bestRight ? "right" : "left", std::sqrt(dx * dx + dy * dy + dz * dz), hit.code, what, hit.speed,
                  fast ? (kind == 3 ? "a burst of sparks" : kind == 2 ? "more dust" : "nothing more") : slow);
    hitText_ = text;
}

// The particles at the game's rate: each 64 Hz tick the hits waiting,
// then the particles' step (0x4cb2c0 runs after the cars in the "Sc64"
// list). Not more than a second's worth at once.
void TrackPage::tickParticles(uint64_t nowMs) {
    if (!particleStartMs_ || nowMs < particleStartMs_)
        particleStartMs_ = nowMs;
    const uint64_t target = (nowMs - particleStartMs_) * kGameTicksPerSecond / 1000;
    if (target > particleTick_ + kGameTicksPerSecond)
        particleTick_ = target - kGameTicksPerSecond;
    for (; particleTick_ < target; ++particleTick_) {
        const uint32_t tick = static_cast<uint32_t>(particleTick_);
        for (const WallHit& hit : pendingHits_)
            wallHitParticles(particles_, hit, tick, particleRandom_);
        pendingHits_.clear();
        particles_.step(tick, particleRandom_);
    }
}

// A kind's picture: SNnn of sfx.fsh, SPnn at night (0x4cb590).
gfx::TextureId TrackPage::particlePicture(int kind) {
    char name[8];
    std::snprintf(name, sizeof name, "%s%02d", variant_ % 2 ? "SP" : "SN", kind % 100);
    if (auto it = particlePictures_.find(name); it != particlePictures_.end())
        return it->second;
    gfx::TextureId id = 0;
    bool additive = false;
    picture(kLaneSheet, -1, false, &additive); // loads the sheet
    for (const Shape& shape : banks_[kLaneSheet].shapes)
        if (shape.name == name) {
            image::Image img;
            if (!decodeShape(shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), {}, id))
                textures_.push_back(id);
            else
                id = 0;
        }
    particlePictures_[name] = id;
    return id;
}

// The particles as 0x4cbc90 draws them: each a quad on the screen in its
// colour over its kind's picture, nearer than the template's reach and in
// front of the camera; sparks as streaks from where they were, the rest
// as squares turned by their angle. Farthest first (the game sorts them
// by depth, 0x4c7610).
void TrackPage::prepareParticles(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right,
                                 const gfx::Vec3& up) {
    particleRuns_.clear();
    if (!renderer_ || particles_.live() == 0)
        return;
    constexpr uint32_t kQuads = ParticleSystem::kSlots;
    if (!particleMesh_) {
        std::vector<gfx::Vertex> vertices(kQuads * 4);
        std::vector<uint32_t> indices;
        for (uint32_t i = 0; i < kQuads; ++i)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(4 * i + k);
        if (renderer_->createMesh(vertices, indices, particleMesh_))
            particleMesh_ = 0;
        if (!particleMesh_)
            return;
    }
    // In the track's coordinates (z mirrored).
    const ParticleVec r{right[0], right[1], -right[2]}, u{up[0], up[1], -up[2]};
    const float focal = static_cast<float>(kFrameH) / 2 / std::tan(kFieldOfView / 2); // pixels
    struct Shown {
        Particle* p;
        float depth;
    };
    std::vector<Shown> shown;
    for (Particle& p : particles_.particles()) {
        if (p.life == 0)
            continue;
        const ParticleTemplate* t = particleTemplate(p.kind);
        const float depth = (p.position[0] - eye[0]) * forward[0] + (p.position[1] - eye[1]) * forward[1] +
                            (-p.position[2] - eye[2]) * forward[2];
        if (!t || depth <= 0 || depth > t->reach) {
            p.tailSet = false;
            continue;
        }
        shown.push_back({&p, depth});
    }
    std::sort(shown.begin(), shown.end(), [](const Shown& a, const Shown& b) { return a.depth > b.depth; });
    std::vector<gfx::Vertex> vertices(kQuads * 4);
    static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (size_t q = 0; q < shown.size(); ++q) {
        Particle& p = *shown[q].p;
        const ParticleTemplate& t = *particleTemplate(p.kind);
        const float pixel = shown[q].depth / focal;
        const std::array<ParticleVec, 4> corners =
            t.streak ? streakCorners(p, r, u, pixel, p.size / pixel) : pictureCorners(p, r, u, p.size);
        for (int k = 0; k < 4; ++k) {
            gfx::Vertex& v = vertices[q * 4 + size_t(k)];
            v.pos[0] = corners[size_t(k)][0];
            v.pos[1] = corners[size_t(k)][1];
            v.pos[2] = -corners[size_t(k)][2];
            v.uv[0] = kUv[k][0];
            v.uv[1] = kUv[k][1];
            v.normal[1] = 1;
            v.color[0] = static_cast<uint8_t>(p.colour >> 16);
            v.color[1] = static_cast<uint8_t>(p.colour >> 8);
            v.color[2] = static_cast<uint8_t>(p.colour);
            v.color[3] = static_cast<uint8_t>(p.colour >> 24);
        }
        const gfx::TextureId texture = particlePicture(p.kind);
        const bool added = (t.flags & 1) != 0;
        if (particleRuns_.empty() || particleRuns_.back().texture != texture || particleRuns_.back().added != added)
            particleRuns_.push_back({texture, added, static_cast<uint32_t>(q), 0});
        ++particleRuns_.back().count;
    }
    if (renderer_->setMeshVertices(particleMesh_, vertices))
        particleRuns_.clear();
}

// Over the scene, tested against its depth but not written to it: blended
// by their alpha, or added (polygon flag 1, as the lights' glows; inferred).
void TrackPage::drawParticles() {
    if (particleRuns_.empty())
        return;
    gfx::DrawCall call;
    call.mesh = particleMesh_;
    call.rgbSource = call.alphaSource = gfx::ColorSource::Vertex;
    call.state.cull = gfx::Cull::None;
    call.state.depthWrite = false;
    call.state.src = gfx::Blend::SrcAlpha;
    for (const ParticleRun& run : particleRuns_) {
        call.texture = run.texture;
        call.state.dst = run.added ? gfx::Blend::One : gfx::Blend::OneMinusSrcAlpha;
        call.firstIndex = 6 * run.first;
        call.indexCount = 6 * run.count;
        renderer_->draw(call);
    }
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
        case '4': detail_ = 3; break;
        case 'k': allBlocks_ = !allBlocks_; break;
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
        case 'p': hitWall((ev.mods & keymod::Shift) != 0); break;
        case 'i': lightIntensity_ = lightIntensity_ <= 0.25f ? 1.0f : lightIntensity_ - 0.25f; break;
        case 'm':
            marksShown_ = !marksShown_;
            if (!marksShown_)
                stopSounds();
            break;
        case 'e':
            // Shift+E: the projected picture (the projtex option) on or off;
            // E: off, low and high beams in turn, as the game's key (0x40de50).
            if (ev.mods & keymod::Shift)
                projected_ = !projected_;
            else
                beams_ = (beams_ + 1) % 3;
            break;
        case 'u':
        case 'x':
            if (ev.key == 'u') {
                // The mirrored track draws texture set 1 (0x41dbc0).
                mirrored_ = !mirrored_;
                textureSet_ = (textureSet_ & 2) | (mirrored_ ? 1 : 0);
            } else {
                textureSet_ = (textureSet_ + 1) % 4;
            }
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
        yaw_ += static_cast<float>(ev.mouseX - lastX_) * 0.005f * (mirrored_ ? -1.0f : 1.0f);
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
    // Mirrored, left and right on the screen are the world's other way.
    const float side = mirrored_ ? -1.0f : 1.0f;
    if (held(key::Left))
        yaw_ -= 1.5f * dt * side;
    if (held(key::Right))
        yaw_ += 1.5f * dt * side;
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
        move(right, speed * side);
    if (held('a'))
        move(right, -speed * side);
    if (held(' '))
        eye_[1] += speed;
    if (held('c'))
        eye_[1] -= speed;

    const gfx::Vec3 eye{eye_[0], eye_[1], eye_[2]};
    gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(kFieldOfView, float(kFrameW) / float(kFrameH), 0.5f, 6000.0f),
                      gfx::lookAlong(eye, forward, {0, 1, 0}));
    // The mirrored track: the game negates its projection's x scale
    // (0x4bf260, by 0x6fd4c4), so the picture is flipped left to right and
    // its front faces turn the other way (0x4349a0).
    if (mirrored_) {
        gfx::Mat4 flip = gfx::identity();
        flip[0] = -1;
        viewProj = gfx::multiply(flip, viewProj);
    }
    const gfx::Cull backFaces = mirrored_ ? gfx::Cull::Front : gfx::Cull::Back;
    Horizon::Rgb fog;
    float fogDensity = 0;
    const int here = nearestBlock();
    fogAt(here, fog, fogDensity);
    if (!fog_)
        fogDensity = 0;
    tickSky(nowMs);
    updateSky(forward, right);
    const gfx::Vec3 up{-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_), std::sin(pitch_) * std::cos(yaw_)};
    tickParticles(nowMs);
    prepareParticles(eye, forward, right, up);

    // The batches to draw: the blocks the game draws from here, each at its
    // detail (or the one chosen), lanes only at high detail (0x41cdc0);
    // objects of layer 3 and the fourth groups only by day without
    // weather. Those straddling the game's reach ahead are cut (cutFar).
    std::vector<Batch> solid, lanes;
    std::vector<bool> shown(track_.blocks.size() + 1, false);
    if (mesh_) {
        auto kind = [&](int slot, std::vector<Batch>& into) {
            into.insert(into.end(), batches_[size_t(slot)].begin(), batches_[size_t(slot)].end());
            for (const Animation& a : animations_[size_t(slot)]) {
                const std::vector<Batch>& frame = a.at(skyTick_);
                into.insert(into.end(), frame.begin(), frame.end());
            }
        };
        const bool clearDay = variant_ == 0;
        shown.back() = true;
        for (const auto& [block, blockDetail] : blocksInView(here)) {
            shown[size_t(block)] = true;
            const int base = block * kKinds, level = detail_ < 3 ? detail_ : blockDetail;
            kind(base + kRoadLow + level, solid);
            if (objects_) {
                kind(base + kObjects, solid);
                if (clearDay)
                    kind(base + kObjectsDay, solid);
            }
            if (lanes_ && level == 2)
                kind(base + kLanes, lanes);
        }
        if (objects_)
            kind(static_cast<int>(track_.blocks.size()) * kKinds + kObjects, solid);
        cutIndices_.clear();
        if (!allBlocks_) {
            cutFar(solid, eye, forward);
            cutFar(lanes, eye, forward);
        }
        if (!cutIndices_.empty()) {
            if (cutMesh_ ? renderer_->setMeshIndices(cutMesh_, cutIndices_)
                         : renderer_->createIndexedMesh(mesh_, cutIndices_, cutMesh_)) {
                // Without it, drawn whole.
                for (auto* list : {&solid, &lanes})
                    for (Batch& b : *list)
                        b.cut = false;
            }
        }
        prepareHeadlights(here, eye, forward);
    }
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
            // And by the camera's place over the visual height (0x47b88a):
            // the grid follows the camera, the clouds pass over it.
            const std::array<float, 2> offset = cloudOffset(horizon_, eye_[0], -eye_[2]);
            clouds.preRow0[2] = std::fmod(drift[0] * ticks + offset[0], 1.0f);
            clouds.preRow1[2] = std::fmod(drift[1] * ticks + offset[1], 1.0f);
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
        std::vector<Batch> all = solid;
        all.insert(all.end(), lanes.begin(), lanes.end());
        // See-through textures (flag 0x02, whatever the picture's text),
        // other additive pictures, or the rest.
        enum Pass { kSolid, kSeeThrough, kAdditive };
        auto pass = [&](const std::vector<Batch>& list, Pass which) {
            for (const Batch& b : list) {
                if ((b.seeThrough ? kSeeThrough : b.additive ? kAdditive : kSolid) != which)
                    continue;
                if (b.indexCount == 0)
                    continue;
                call.state.cull = cullBack_ && !b.twoSided ? backFaces : gfx::Cull::None;
                call.texture = textured_ ? b.texture : 0;
                call.mesh = b.cut ? cutMesh_ : mesh_;
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
                if (m.block >= 0 && !shown[size_t(m.block)])
                    continue;
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
            call.mesh = mesh_;
        };
        // Solid first, then the lane markings pulled onto the road, then
        // additive pictures (lights, glows) over everything.
        pass(solid, kSolid);
        movingPass(kSolid);
        call.state.depthBias = true;
        pass(lanes, kSolid);
        call.state.depthBias = false;
        // The headlights (prepareHeadlights), over the scenery: the picture
        // added by the light's alpha (THRASH blend 1, source alpha and one,
        // as the queued polygons' flag 1 sets it), and the raised colours.
        if (lightMesh_ && !lightRuns_.empty()) {
            gfx::DrawCall light;
            light.mesh = lightMesh_;
            light.rgbSource = light.alphaSource = gfx::ColorSource::Vertex;
            light.state.depthWrite = false;
            light.state.depthBias = true;
            for (const LightRun& run : lightRuns_) {
                light.texture = run.picture || textured_ ? run.texture : 0;
                light.alphaSource = run.picture ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
                light.state.src = run.picture ? gfx::Blend::SrcAlpha : gfx::Blend::One;
                light.state.dst = gfx::Blend::One;
                light.state.alphaTest = run.picture ? gfx::AlphaTest::None : gfx::AlphaTest::Ge128;
                light.state.cull = cullBack_ ? backFaces : gfx::Cull::None;
                light.firstIndex = run.first;
                light.indexCount = run.count;
                renderer_->draw(light);
            }
        }
        if (walls_) {
            // A guide drawn over everything, so the walls show through the
            // ground and fences they follow.
            gfx::DrawCall wall = call;
            wall.mesh = mesh_;
            wall.tableFog = 0;
            wall.rgbSource = gfx::ColorSource::Vertex;
            wall.color = {1, 1, 1, 1};
            wall.texture = 0;
            wall.state.depthTest = wall.state.depthWrite = false;
            wall.state.cull = gfx::Cull::None;
            for (const Batch& b : batches_[track_.blocks.size() * kKinds + kWalls]) {
                wall.firstIndex = b.firstIndex;
                wall.indexCount = b.indexCount;
                renderer_->draw(wall);
            }
        }
        // See-through surfaces (glass, water, glints) added over what's
        // behind by their alpha, in the constant half grey and half alpha
        // of their vertices (0x7F7F7F7F, 0x41b9b0; no ambient light): the
        // queued polygon's flag 1 sets THRASH blend 1, source alpha and
        // one (0x4332b0), so a texel adds a quarter of itself. The game
        // sorts them by depth; added, the order doesn't matter.
        {
            const gfx::DrawCall before = call;
            call.rgbSource = call.alphaSource = gfx::ColorSource::Vertex;
            call.color = {1, 1, 1, 1};
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::One;
            call.state.depthWrite = false;
            call.state.alphaTest = gfx::AlphaTest::None;
            pass(all, kSeeThrough);
            movingPass(kSeeThrough);
            call = before;
        }
        call.state.src = call.state.dst = gfx::Blend::One;
        call.state.depthWrite = false;
        call.state.alphaTest = gfx::AlphaTest::None;
        pass(all, kAdditive);
        movingPass(kAdditive);
    }

    drawMarks(eye, right, up);
    if (marksShown_)
        updateSounds(eye, right);
    drawParticles();

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(index_ + 1) + "/" + std::to_string(tracks_.size()) +
                                      "  " + tracks_[index_] + "  " + trackName(tracks_[index_]) + "  " +
                                      kVariantNames[variant_]};
    if (!error_.empty()) {
        lines.push_back(error_);
    } else {
        char where[200];
        std::snprintf(where, sizeof where, "block %d/%zu  at %.0f %.0f %.0f  %s detail%s  set %d%s  %zu quads  %zu moving",
                      here, track_.blocks.size(), eye_[0], eye_[1], -eye_[2], kDetailNames[detail_],
                      allBlocks_ ? ", all blocks" : "", textureSet_ % 2, textureSet_ >= 2 ? " (8-bit)" : "", drawn,
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
        if (lightIntensity_ < 1)
            line += "  lights " + std::to_string(static_cast<int>(lightIntensity_ * 100)) + "%";
        if (mirrored_)
            line += "  mirrored";
        if (variant_ % 2 == 1) {
            static constexpr const char* kBeams[] = {"headlights off", "low beams", "high beams"};
            line += std::string("  ") + kBeams[beams_] + (beams_ && !projected_ ? " (not projected)" : "");
        }
        lines.push_back(line);
        if (!hitText_.empty())
            lines.push_back(hitText_ + "  (" + std::to_string(particles_.live()) + " particles)");
        if (!skyError_.empty())
            lines.push_back(skyError_);
        if (!collisionError_.empty())
            lines.push_back(collisionError_);
    }
    lines.push_back("PgUp/PgDn: track  WASD/arrows/drag: move  N/B: block  1-3: detail  H: sky  I: lights  P: wall hit  E: headlights  U: mirror  T V L O G X F Y M: toggles");
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
