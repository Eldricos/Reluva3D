// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <functional>
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
// The cars' sphere map (0x47ac30 loads it into 0x6ff9a0, 0x563a90).
constexpr const char* kEnvPicture[] = {"CHRD", "CHRN", "CHRW", "CHNW"};
// The game's ticks (0x7d3684 counts them): 641 in 10 seconds of a race.
// Moving objects' key delays and the clouds' drift are in them.
constexpr int kGameTicksPerSecond = 64;
// The game's screen at 640 x 480 (viewScreen: 0x4dbce0, 0x4bf260): 90
// degrees across (sx 320) and 72 up (sy -330.33), so its pixels are
// 330.33 / 320 taller than wide. The tangents of the half angles, and the
// vertical field of view (radians) as gfx::perspective has it.
const ViewScreen kScreen = viewScreen(0, 0, kFrameW, kFrameH);
const float kTanAcross = static_cast<float>(kFrameW) / 2 / kScreen.sx; // 1
const float kTanUp = static_cast<float>(kFrameH) / 2 / -kScreen.sy;    // tan 36 degrees
const float kFieldOfView = 2 * std::atan(kTanUp);

std::string lower(const std::string& s) { return fs::toLower(s); }

void setColour(gfx::Vertex& v, const Horizon::Rgb& c) {
    for (int i = 0; i < 3; ++i)
        v.color[i] = static_cast<uint8_t>(std::clamp(c[i], 0, 255));
    v.color[3] = 255;
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
    if (renderer_ && horizonMesh_)
        renderer_->destroyMesh(horizonMesh_);
    skyMesh_ = boltMesh_ = horizonMesh_ = 0;
    boltIndices_ = 0;
    skyBlack_ = false;
    skyVertices_.clear();
    skyHeights_.clear();
    bolts_.clear();
    sunRotate_ = skyChanged_ = false;
    horizonPicture_ = sunPicture_ = cloudPicture_ = envPicture_ = 0; // owned by textures_
    sunFirst_ = sunIndices_ = cloudFirst_ = cloudIndices_ = 0;
}

void TrackPage::release() {
    releaseSky();
    stopSounds();
    soundData_.clear();
    soundBank_ = {};
    soundSlots_.clear();
    marks_.clear();
    glowPicture_ = glow1Picture_ = glow4Picture_ = shadowPicture_ = 0; // owned by textures_
    if (renderer_ && markMesh_)
        renderer_->destroyMesh(markMesh_);
    markMesh_ = 0;
    particles_.clear();
    pendingHits_.clear();
    hitText_.clear();
    particlePictures_.clear(); // owned by textures_
    particlePictureSizes_.clear();
    particleRuns_.clear();
    if (renderer_ && particleMesh_)
        renderer_->destroyMesh(particleMesh_);
    particleMesh_ = 0;
    if (renderer_ && weatherMesh_)
        renderer_->destroyMesh(weatherMesh_);
    weatherMesh_ = 0;
    weatherDrops_ = weatherSplashes_ = 0;
    weatherStarted_ = false;
    if (renderer_ && lightMesh_)
        renderer_->destroyMesh(lightMesh_);
    lightMesh_ = 0;
    lightCapacity_ = 0;
    lightRuns_.clear();
    headlightPicture_ = 0; // owned by textures_
    releaseCars();
    movingFrame_.clear();
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
    if (shape && !decodeShape(*shape, img) &&
        !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id)) {
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
            if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id))
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
    // Clamped: each tile's quad takes its own part, edge to edge.
    if (renderer_->createTexture(strip.w, strip.h, strip.rgba.data(), glideTextureParams(true), id))
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
    weatherStarted_ = false; // set up again from this sky's weather (0x4dcdf0)
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
    envPicture_ = skyPicture(kEnvPicture[variant_], nullptr);
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
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;

    // The sky's shading, the earth's and the horizon picture: 8 quads each,
    // in front of the camera, placed each frame (updateHorizon).
    if (h.hasPixmap)
        horizonPicture_ = horizonStrip();
    {
        std::vector<gfx::Vertex> quads(3 * 8 * 4);
        std::vector<uint32_t> quadIndices;
        for (uint32_t q = 0; q < 3 * 8; ++q)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                quadIndices.push_back(q * 4 + k);
        if (renderer_->createMesh(quads, quadIndices, horizonMesh_))
            horizonMesh_ = 0;
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
                v.color[0] = v.color[1] = v.color[2] = b;
                v.color[3] = cloudAlpha(h, b, direct3d_);
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
    skyChanged_ = true; // the clouds' colours as the look and the intensity have them

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
            c[0] = c[1] = c[2] = cloudBase_[i];
            c[3] = cloudAlpha(horizon_, cloudBase_[i], direct3d_);
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
        // The added clouds (type 1) under Direct3D with the effects
        // intensity under 1: every point's colour dimmed, after the flash
        // (0x47b850 calls 0x47b7d0 on the 169 points).
        if (horizon_.cloudType == 1 && effectsIntensity() < 1)
            for (size_t i = 0; i < cloudBase_.size(); ++i) {
                uint8_t* c = skyVertices_[cloudVertex_ + i].color;
                const uint32_t argb = dimByIntensity(uint32_t(c[3]) << 24 | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2],
                                                     effectsIntensity());
                c[0] = static_cast<uint8_t>(argb >> 16);
                c[1] = static_cast<uint8_t>(argb >> 8);
                c[2] = static_cast<uint8_t>(argb);
                c[3] = static_cast<uint8_t>(argb >> 24);
            }
    }
    renderer_->setMeshVertices(skyMesh_, skyVertices_);

    if (boltMesh_) {
        std::vector<gfx::Vertex> quads(240 * 4);
        uint32_t q = 0;
        for (const LightningBolt& bolt : bolts_) {
            // 0x4946e0: each point's lines to the points after it in the
            // colour 0xE2E2FF with alpha life x 510 / 24 (at most 255: full
            // for 12 ticks, then fading), added (flags word 5, bit 0: blend
            // 1, source alpha and one); the first point's (its +4 word set)
            // in colour 0, so the lines out of it add nothing.
            const uint8_t alpha = static_cast<uint8_t>(boltFlash(bolt));
            for (const LightningBolt::Point& p : bolt.points) {
                if (p.parent <= 0 || q >= 240)
                    continue;
                // Track coordinates to the page's (z negated); a pixel of the
                // game's 640 x 480 wide (the game's quad is the line and the
                // line moved by a pixel right and down), across the line as
                // seen.
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
                const float width = distance * (0.5f / 320) / length;
                const gfx::Vec3* ends[4] = {&from, &to, &to, &from};
                for (int k = 0; k < 4; ++k) {
                    gfx::Vertex& v = quads[q * 4 + uint32_t(k)];
                    const float s = k == 0 || k == 1 ? -width : width;
                    for (int i = 0; i < 3; ++i)
                        v.pos[i] = (*ends[k])[size_t(i)] + side[size_t(i)] * s;
                    // 0xe2e2ff, pale blue-white (0x4946fd).
                    v.color[0] = v.color[1] = 0xe2;
                    v.color[2] = 0xff;
                    v.color[3] = alpha;
                }
                ++q;
            }
        }
        boltIndices_ = q * 6;
        if (q)
            renderer_->setMeshVertices(boltMesh_, quads);
    }
    skyChanged_ = !bolts_.empty();
}

// The sky's rows as the game lays them out each frame (skyRow; 0x479680,
// 0x4799d0, 0x479b90): the sky's shading from the shaded area's midpoint
// to its top, its colours at the bottom by slot edge and the top colour
// above; the earth's from the base to the midpoint, its top colour above
// its base colour; the horizon picture between its bottom and top, a tile
// a slot, white. Heights are the world's less the camera's.
void TrackPage::updateHorizon(const gfx::Vec3& forward) {
    if (!horizonMesh_ || !skyError_.empty())
        return;
    const Horizon& h = horizon_;
    const std::array<float, 3> ahead{forward[0], forward[1], -forward[2]};
    const float base = static_cast<float>(h.shadedBase) - eye_[1];
    const float mid = static_cast<float>(h.shadedMidpoint), top = static_cast<float>(h.shadedHeight);
    const std::array<Horizon::Rgb, 16> edge = skyBaseColours(h);
    const Horizon::Rgb white{255, 255, 255};
    std::vector<gfx::Vertex> quads(3 * 8 * 4);
    auto put = [&](size_t quad, const std::array<std::array<float, 3>, 4>& at,
                   const std::array<const Horizon::Rgb*, 4>& colour, const std::array<float, 8>& uv) {
        for (size_t k = 0; k < 4; ++k) {
            gfx::Vertex& v = quads[quad * 4 + k];
            v.pos[0] = at[k][0];
            v.pos[1] = at[k][1];
            v.pos[2] = -at[k][2];
            v.normal[1] = 1;
            setColour(v, *colour[k]);
            v.uv[0] = uv[2 * k];
            v.uv[1] = uv[2 * k + 1];
        }
    };
    constexpr std::array<float, 8> kNoUv{};
    // Corners in each quad: bottom left, top left, top right, bottom right.
    const SkyRow sky = skyRow(h, ahead, base + mid, top - mid);
    for (size_t i = 0; i < 8; ++i) {
        const int slot = (sky.firstSlot + static_cast<int>(i)) % 16;
        put(i, {sky.bottom[i], sky.top[i], sky.top[i + 1], sky.bottom[i + 1]},
            {&edge[size_t(slot)], &h.skyTop, &h.skyTop, &edge[size_t((slot + 1) % 16)]}, kNoUv);
    }
    const SkyRow earth = skyRow(h, ahead, base, mid);
    for (size_t i = 0; i < 8; ++i)
        put(8 + i, {earth.bottom[i], earth.top[i], earth.top[i + 1], earth.bottom[i + 1]},
            {&h.earthBase, &h.earthTop, &h.earthTop, &h.earthBase}, kNoUv);
    const float bottom = static_cast<float>(h.pixmapBottom), height = static_cast<float>(h.pixmapTop - h.pixmapBottom);
    const SkyRow picture = skyRow(h, ahead, base + bottom, height);
    for (size_t i = 0; i < 8; ++i) {
        const int slot = (picture.firstSlot + static_cast<int>(i)) % 16;
        bool flipped = false;
        const float tile = static_cast<float>(horizonTile(h, slot, &flipped));
        // The tile's top corners; the top left pushed out by a hundredth of
        // the tile's width (0x479b90).
        std::array<float, 3> left = picture.top[i], right = picture.top[i + 1];
        for (size_t k = 0; k < 3; ++k)
            left[k] += (left[k] - right[k]) * 0.01f;
        const float u0 = (tile + (flipped ? 1 : 0)) / 8, u1 = (tile + (flipped ? 0 : 1)) / 8;
        put(16 + i, {picture.bottom[i], left, right, picture.bottom[i + 1]}, {&white, &white, &white, &white},
            {u0, 1, u0, 0, u1, 0, u1, 1});
    }
    renderer_->setMeshVertices(horizonMesh_, quads);
}

// The virtual road's slice nearest the camera (the game goes by its car's,
// 0x4217e0), or -1 without a road.
int TrackPage::roadSliceAt() const {
    int best = -1;
    float bestDistance = 1e30f;
    for (size_t i = 0; i < collision_.road.size(); ++i) {
        const auto& p = collision_.road[i].position;
        const float dx = p[0] - eye_[0], dy = p[1] - eye_[1], dz = -p[2] - eye_[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void TrackPage::fogAt(int slice, Horizon::Rgb& colour, float& density) const {
    const Horizon::FogPoint f = fogAtSlice(horizon_, slice);
    colour = f.colour;
    density = static_cast<float>(f.density);
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
                moving_.push_back({own ? static_cast<int>(block) : -1, o.keys, o.keyDelay, {}, gi,
                                   static_cast<size_t>(&o - track_.extraObjects[gi].data())});
                movingGroups.emplace_back();
                add(-1, movingGroups.back(), o.vertices, o.colours, {}, 1, true, o.polygons);
            }
        }
    }

    // The virtual road's walls: a barrier along each side, red on the left
    // and green on the right, from each road point to the next (a guide
    // for checking the .col data against the scenery); blue where the
    // side's code (col.md, the side codes: the point's byte 14, high
    // nibble left) is 3 and white where it is 5, the two dust codes.
    // Lower and yellow, the road's edges from its lane counts and widths.
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
                const int code = (a.unknown >> 16 & 0xff) >> (right ? 0 : 4) & 15;
                out.color[0] = edge || !right ? 255 : 40;
                out.color[1] = edge || right ? 255 : 40;
                out.color[2] = 40;
                if (!edge && code == 3) {
                    out.color[0] = out.color[1] = 40;
                    out.color[2] = 255;
                } else if (!edge && code == 5) {
                    out.color[0] = out.color[1] = out.color[2] = 235;
                }
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
    // RELUVA_NFS_TRACK_CAMERA="x y z yaw pitch": the camera placed as a
    // view of the game's (track coordinates; yaw atan2(x, z) of its forward,
    // pitch, in radians), for comparing with nfs-host's frames.
    if (const char* camera = std::getenv("RELUVA_NFS_TRACK_CAMERA")) {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
        if (std::sscanf(camera, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5) {
            eye_ = {x, y, -z};
            yaw_ = yaw;
            pitch_ = pitch;
        }
    }
    // RELUVA_NFS_TRACK_CARS=<file>: the cars placed from a dump of the
    // game's car records (0x5e1120, 0x9AC bytes a car), shown at once.
    if (const char* step = std::getenv("RELUVA_NFS_TRACK_STEP"))
        fixedStep_ = std::atoll(step);
    carDump_.clear();
    // RELUVA_NFS_TRACK_CARS_SETUP=<file>: a dump of the race's set-up
    // records (0x6fd52c, 0x6C bytes a car), whose keys (the cars' serial
    // numbers) give each dumped car its own model.
    carSetup_.clear();
    if (const char* setup = std::getenv("RELUVA_NFS_TRACK_CARS_SETUP"))
        if (FILE* f = std::fopen(setup, "rb")) {
            uint8_t record[0x6c];
            while (std::fread(record, 1, sizeof record, f) == sizeof record) {
                std::array<uint8_t, 16> key{};
                std::copy(record, record + 16, key.begin());
                carSetup_.push_back(key);
            }
            std::fclose(f);
        }
    if (const char* dump = std::getenv("RELUVA_NFS_TRACK_CARS")) {
        std::vector<uint8_t> bytes;
        if (FILE* f = std::fopen(dump, "rb")) {
            uint8_t buffer[4096];
            for (size_t got; (got = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
                bytes.insert(bytes.end(), buffer, buffer + got);
            std::fclose(f);
        }
        carDump_ = parseCarDump(bytes);
        if (!carDump_.empty())
            computerCars_ = true;
    }
    // RELUVA_NFS_TRACK_PARTICLES=<file>: the particles as a dump of the
    // game's 600 slots (0x7ddfd0, 128 bytes each) has them, held there (as
    // a car dump holds the cars) for comparing with the frame.
    particlesHeld_ = false;
    if (const char* dump = std::getenv("RELUVA_NFS_TRACK_PARTICLES")) {
        particlesHeld_ = true;
        std::vector<uint8_t> bytes;
        if (FILE* f = std::fopen(dump, "rb")) {
            uint8_t buffer[4096];
            for (size_t got; (got = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
                bytes.insert(bytes.end(), buffer, buffer + got);
            std::fclose(f);
        }
        const std::vector<Particle> loaded = parseParticleDump(bytes);
        particles_.clear();
        for (size_t i = 0; i < loaded.size(); ++i)
            particles_.particles()[i] = loaded[i];
    }
    if (computerCars_) {
        loadCars();
        placeCars();
    }
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
// keeps only its polygons whose first corner is within it. A moving
// object's batches go by its corners as placed by its matrix (`model`):
// the game moves its vertices (0x41a9b0) and queues it with the placed
// objects (0x41ca40, 0x41b640), which drops a polygon whose first corner,
// so placed, is beyond the reach as for any other.
void TrackPage::cutFar(std::vector<Batch>& list, const gfx::Vec3& eye, const gfx::Vec3& forward,
                       const gfx::Mat4* model) {
    for (Batch& b : list) {
        auto ahead = [&](const float* p) {
            gfx::Vec3 at{p[0], p[1], p[2]};
            if (model)
                at = gfx::transformPoint(*model, at);
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
//
// With the computer cars (J) their lights follow the camera's car's in the
// pass, as the race's cars (0x5efac8, in order): each car within 150 of the
// block whose light isn't behind the camera by more than its reach
// (0x41bc50) is lit with the intensity headlightIntensity() gives it, the
// cars' count being all of them. A corner in several lights gets the
// highest of their colours, byte by byte.
void TrackPage::prepareHeadlights(int here, const gfx::Vec3& eye, const gfx::Vec3& forward) {
    lightRuns_.clear();
    const bool night = variant_ == 1 || variant_ == 3;
    struct Car {
        std::array<float, 3> at{}, right{}, up{}, forward{};
        float halfLength = 0;
        int beams = 0;
        bool siren = false;
    };
    std::vector<Car> cars;
    // The car at the camera, upright, as long as the Corvette (half
    // length 2.40644, car.fce); its light where the game puts it.
    // A dump's human car stands for it where the dump has it.
    const std::array<float, 3> look{std::sin(yaw_), 0, std::cos(yaw_)};
    if (humanPose_.set && computerCars_)
        cars.push_back({humanPose_.at, humanPose_.right, humanPose_.up, humanPose_.forward, humanPose_.halfLength, beams_});
    else
        cars.push_back({{eye_[0], eye_[1], -eye_[2]}, {look[2], 0, -look[0]}, {0, 1, 0}, look, kVirtualCarHalfLength, beams_});
    // Each computer car with its own rows and half length (+0x11C, the
    // model's), the lights it has on.
    if (computerCars_)
        for (const ComputerCar& c : cars_)
            cars.push_back({c.at, c.right, c.up, c.forward, carModels_[size_t(c.model)].halfSize[2], c.beams, c.siren});
    bool any = false;
    for (const Car& c : cars)
        any = any || c.beams != 0 || c.siren;
    if (!night || !any || track_.blocks.empty() || !headlightPicture_)
        return;
    const std::string sheet = textureSheet();
    float ambient[3] = {1, 1, 1};
    if (skyError_.empty())
        for (int i = 0; i < 3; ++i)
            ambient[i] = static_cast<float>(horizon_.ambient[size_t(i)]) / 100.0f;
    std::vector<gfx::Vertex> projectedTris; // the projected picture's triangles
    // The colour raised, by texture and two-sidedness (flag 0x10).
    std::map<std::pair<gfx::TextureId, bool>, std::vector<gfx::Vertex>> floors;
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
        const int level = detail_ < 3 ? detail_ : blockDetail;
        std::vector<const std::vector<Track::Polygon>*> lists = {&b.polygons[size_t(2 * level)],
                                                               &b.polygons[size_t(2 * level + 1)]};
        for (size_t layer = 0; layer < 3 && layer < b.objectLayers.size(); ++layer)
            for (const Track::BlockObject& o : b.objectLayers[layer])
                lists.push_back(&o.polygons);
        if (level == 2 && lanes_)
            lists.push_back(&b.polygons[6]);
        auto entryOf = [&](const Track::Polygon& p) -> const Track::Texture& {
            const uint16_t e = p.textureAt(skyTick_);
            return track_.textures[e < track_.textures.size() ? e : p.texture];
        };
        // Each corner's raised colour: the highest of the lights' it is in.
        std::vector<bool> raised(b.vertices.size(), false);
        std::vector<uint32_t> raisedTo(b.vertices.size(), 0);
        std::vector<HeadlightPoint> points(b.vertices.size());
        int lit = 0;
        // One light's pass over the block's lists: a roof light's (0x41ab40)
        // raises the colours of every polygon's corners in it, but not the
        // lane markings'; a headlight's (0x41ae90) projects its picture.
        auto pass = [&](const Headlight& light, bool roofLight) {
            // 0x41bc50: not when the light is behind the camera by more than
            // its reach; 0x41bf30 (0x41bc90): nor when the block is outside it.
            const float behind = (light.at[0] - eye[0]) * forward[0] + (light.at[1] - eye[1]) * forward[1] +
                                 (-light.at[2] - eye[2]) * forward[2];
            if (behind < -light.reach)
                return false;
            if (lightTest_ && lightMissesBlock(light, b.corners, 1, roofLight))
                return false;
            for (size_t i = 0; i < b.vertices.size(); ++i)
                points[i] = headlightAt(light, b.vertices[i]);
            for (const auto* list : lists) {
                if (roofLight && list == &b.polygons[6])
                    continue;
                for (const Track::Polygon& p : *list) {
                    if (p.vertices[0] >= b.vertices.size() || p.vertices[1] >= b.vertices.size() ||
                        p.vertices[2] >= b.vertices.size() || p.vertices[3] >= b.vertices.size())
                        continue;
                    const Track::Vec3& c0 = b.vertices[p.vertices[0]];
                    if (!polygonInReach((c0[0] - eye[0]) * forward[0] + (c0[1] - eye[1]) * forward[1] +
                                        (-c0[2] - eye[2]) * forward[2]))
                        continue;
                    if (!roofLight && projected_ && !(entryOf(p).flags & 0x04)) {
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
                        for (uint16_t vertex : p.vertices) {
                            if (points[vertex].outside != 0)
                                continue;
                            raised[vertex] = true;
                            uint32_t& to = raisedTo[vertex];
                            for (int shift = 0; shift < 32; shift += 8)
                                if (((points[vertex].colour >> shift) & 0xff) > ((to >> shift) & 0xff))
                                    to = (to & ~(0xffu << shift)) | (points[vertex].colour & (0xffu << shift));
                        }
                    }
                }
            }
            return true;
        };
        for (const Car& car : cars) {
            const float dx = car.at[0] - b.centre[0], dz = car.at[2] - b.centre[2];
            if (dx * dx + dz * dz > 150.0f * 150.0f) // 0x41c6fe
                continue;
            if (car.siren) // 0x41c7b6: the roof light first, at full strength
                pass(sirenLight(car.at, car.right, car.up, car.forward, static_cast<int>(skyTick_)), true);
            if (car.beams == 0)
                continue;
            Headlight light = carHeadlight(car.beams, projected_, car.at, car.right, car.up, car.forward,
                                           headlightIntensity(lit, static_cast<int>(cars.size())));
            light.at = headlightPlace(light, car.at, car.up, car.halfLength);
            if (pass(light, false))
                ++lit;
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
                auto& into = floors[{id, (p.flags & Track::Polygon::kTwoSided) != 0}];
                for (int k : {0, 2, 1, 0, 3, 2}) {
                    const uint16_t vertex = p.vertices[size_t(k)];
                    gfx::Vertex v = vertexAt(b.vertices[vertex]);
                    v.uv[0] = tex.corners[size_t(2 * k)];
                    v.uv[1] = tex.corners[size_t(2 * k + 1)];
                    if (raised[vertex]) {
                        const uint32_t baked = b.colours[vertex], lit = raisedTo[vertex];
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
    // The placed objects (0x41c310, from 0x41ca40 for each object of the
    // block's groups drawn: 0-2 by night): for each car within 150 of the
    // object's place (0x536cb4), its roof light and then its headlights,
    // each at full strength (0x41a800 with 1) and skipped as for the block
    // (0x41bc50, 0x41bf30 / 0x41bc90 with the block's outline), raise the
    // colour of each of the object's vertices fully inside the light, as
    // the block's pass raises the corners of polygons with texture flag
    // 0x04, whatever the object's textures and the projtex option. Not in
    // the mirror (view 1). The still objects (type 4) at their reference
    // point; the moving ones (type 3) as placed at their key this frame
    // (0x41ca40 calls 0x41c310 with the key's matrix and position from
    // 0x40f4b0, the distance taken from that position), the others at the
    // origin.
    // place: an object vertex into track coordinates; centre: the
    // object's place on the ground; outline: the group's block (null for
    // the last group, drawn from everywhere).
    auto lightObject = [&](const Track::ExtraObject& o, const std::function<Track::Vec3(const Track::Vec3&)>& place,
                           const Track::Vec3& centre, const Track::Block* outline) {
        if (o.vertices.empty() || o.colours.size() < o.vertices.size())
            return;
        std::vector<bool> raised(o.vertices.size(), false);
        std::vector<uint32_t> raisedTo(o.vertices.size(), 0);
        std::vector<Track::Vec3> at(o.vertices.size());
        for (size_t i = 0; i < o.vertices.size(); ++i)
            at[i] = place(o.vertices[i]);
        bool anyRaised = false;
        auto pass = [&](const Headlight& light, bool roofLight) {
            const float behind = (light.at[0] - eye[0]) * forward[0] + (light.at[1] - eye[1]) * forward[1] +
                                 (-light.at[2] - eye[2]) * forward[2];
            if (behind < -light.reach)
                return;
            if (lightTest_ && outline && lightMissesBlock(light, outline->corners, 1, roofLight))
                return;
            for (size_t i = 0; i < o.vertices.size(); ++i) {
                const HeadlightPoint pt = headlightAt(light, at[i]);
                if (pt.outside != 0)
                    continue;
                raised[i] = anyRaised = true;
                uint32_t& to = raisedTo[i];
                for (int shift = 0; shift < 32; shift += 8)
                    if (((pt.colour >> shift) & 0xff) > ((to >> shift) & 0xff))
                        to = (to & ~(0xffu << shift)) | (pt.colour & (0xffu << shift));
            }
        };
        for (const Car& car : cars) {
            const float dx = car.at[0] - centre[0], dz = car.at[2] - centre[2];
            if (dx * dx + dz * dz > 150.0f * 150.0f)
                continue;
            if (car.siren)
                pass(sirenLight(car.at, car.right, car.up, car.forward, static_cast<int>(skyTick_)), true);
            if (car.beams == 0)
                continue;
            Headlight light = carHeadlight(car.beams, projected_, car.at, car.right, car.up, car.forward, 1);
            light.at = headlightPlace(light, car.at, car.up, car.halfLength);
            pass(light, false);
        }
        if (!anyRaised)
            return;
        for (const Track::Polygon& p : o.polygons) {
            bool any = false, whole = true;
            for (uint16_t vertex : p.vertices) {
                whole = whole && vertex < o.vertices.size();
                any = any || (vertex < raised.size() && raised[vertex]);
            }
            if (!any || !whole)
                continue;
            // The first corner as queued (the file's vertex 1) within reach.
            const Track::Vec3& c0 = at[p.vertices[size_t(polygonSlot(0, true))]];
            if (!polygonInReach((c0[0] - eye[0]) * forward[0] + (c0[1] - eye[1]) * forward[1] +
                                (-c0[2] - eye[2]) * forward[2]))
                continue;
            const Track::Texture& tex =
                track_.textures[p.textureAt(skyTick_) < track_.textures.size() ? p.textureAt(skyTick_) : p.texture];
            bool additive = false;
            const gfx::TextureId id = tex.lane ? picture(kLaneSheet, tex.picture, true, &additive)
                                               : picture(sheet, tex.picture, false, &additive);
            if (additive || (tex.flags & Track::Texture::kSeeThrough))
                continue;
            auto& into = floors[{id, (p.flags & Track::Polygon::kTwoSided) != 0}];
            for (int k : {0, 2, 1, 0, 3, 2}) {
                const uint16_t vertex = p.vertices[size_t(polygonSlot(k, true))];
                gfx::Vertex v = vertexAt(at[vertex]);
                v.uv[0] = tex.corners[size_t(2 * k)];
                v.uv[1] = tex.corners[size_t(2 * k + 1)];
                if (raised[vertex]) {
                    const uint32_t baked = o.colours[vertex], lit = raisedTo[vertex];
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
    };
    if (objects_) {
        for (const auto& [block, blockDetail] : blocksInView(here)) {
            (void)blockDetail;
            const Track::Block& b = track_.blocks[size_t(block)];
            for (size_t g = 0; g < 3; ++g) {
                const size_t group = static_cast<size_t>(block) * 4 + g;
                if (group >= track_.extraObjects.size())
                    break;
                for (const Track::ExtraObject& o : track_.extraObjects[group])
                    if (o.type == 4)
                        lightObject(
                            o,
                            [&](const Track::Vec3& v) {
                                return Track::Vec3{v[0] + o.reference[0], v[1] + o.reference[1], v[2] + o.reference[2]};
                            },
                            o.reference, &b);
            }
        }
        // The moving objects as drawn this frame (movingFrame_, from
        // blocks shown), their matrix in the world (z negated).
        for (const MovingFrame& m : movingFrame_) {
            if (m.moving < 0 || static_cast<size_t>(m.moving) >= moving_.size())
                continue;
            const Moving& mv = moving_[size_t(m.moving)];
            if (mv.group >= track_.extraObjects.size() || mv.object >= track_.extraObjects[mv.group].size())
                continue;
            if (mv.block >= 0 && mv.group % 4 == 3)
                continue; // the fourth group: by day only
            const Track::ExtraObject& o = track_.extraObjects[mv.group][mv.object];
            const Track::Block* outline = mv.block >= 0 ? &track_.blocks[size_t(mv.block)] : nullptr;
            // The mesh keeps a moving object's vertices unmirrored (added
            // with mirror 1), the key's matrix mirroring them into the
            // world; lit at those same places. Until November 2026 they
            // were mirrored first, so the light was worked out and drawn at
            // the object's mirror image (frd.md, The placed objects).
            lightObject(
                o,
                [&](const Track::Vec3& v) {
                    const gfx::Vec3 w = gfx::transformPoint(m.model, {v[0], v[1], v[2]});
                    return Track::Vec3{w[0], w[1], -w[2]};
                },
                Track::Vec3{m.model[12], m.model[13], -m.model[14]}, outline);
        }
    }
    std::vector<gfx::Vertex> vertices = std::move(projectedTris);
    if (!vertices.empty())
        lightRuns_.push_back({headlightPicture_, true, 0, static_cast<uint32_t>(vertices.size())});
    for (auto& [key, list] : floors) {
        lightRuns_.push_back(
            {key.first, false, static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(list.size()), key.second});
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

namespace {
// carp.txt's first value, "Serial Number(0)" (the car's key, net.md); -1
// without it.
int carpSerial(const std::string& text) {
    const size_t at = text.find("Serial Number");
    const size_t line = at == std::string::npos ? at : text.find('\n', at);
    return line == std::string::npos ? -1 : std::atoi(text.c_str() + line + 1);
}
} // namespace

// The computer cars' models: the Corvette and the police Corvette (and
// any other car a dump's set-up names), the parts of each of the four
// details (carDetailParts: fce.md, Vertices and parts) with their
// car00.tga, recoloured in their paint as the game's loader does
// (carSkin), z negated as on the Cars page.
void TrackPage::loadCarModel(CarModel& cm) {
    if (!cm.vertices.empty() || !cm.error.empty())
        return;
    const std::string folder = cm.folder;
    Model model;
    for (const FileRef& f : files_.files()) {
        const std::string name = lower(f.name);
        std::vector<uint8_t> data;
        if (name == folder + "car.fce") {
            if (Error err = files_.read(f, data))
                cm.error = err.message;
            else if (Error err2 = parseModel(data, model))
                cm.error = err2.message;
        } else if (name == folder + "carp.txt") {
            if (!files_.read(f, data)) {
                const std::string text(data.begin(), data.end());
                (void)parseCarpAi(text, cm.ai);
                cm.serial = carpSerial(text);
            }
        } else if (name == folder + "car00.tga" && cm.skinImage.rgba.empty()) {
            if (!files_.read(f, data) && !image::decode(data, cm.skinImage))
                skinRowsAsStored(data, cm.skinImage);
        }
    }
    if (model.parts.empty()) {
        if (cm.error.empty())
            cm.error = "No " + folder.substr(18, 4) + " model for the computer cars";
        return;
    }
    cm.halfSize = model.halfSize;
    cm.dummies = model.dummies;
    for (size_t w = 0; w < 4 && w + 1 < model.parts.size(); ++w)
        cm.wheels[w] = {model.parts[w + 1].position[0], model.parts[w + 1].position[2]};
    cm.paintValue = model.primaryColours.empty() ? 0 : static_cast<int>(std::min<uint32_t>(model.primaryColours[0].hsbx[2], 255));
    cm.colours.primaryColours = model.primaryColours;
    cm.colours.secondaryColours = model.secondaryColours;
    // reflects: the detail's first body part and the headlights (0x4bae00,
    // 0x4bab70, 0x4baa50 pass the sphere map with them, not the wheels).
    auto addPart = [&](size_t pi, bool reflects) {
        const Model::Part& part = model.parts[pi];
        for (uint32_t t = part.firstTriangle;
             t < part.firstTriangle + part.triangleCount && t < model.triangles.size(); ++t) {
            const Model::Triangle& tri = model.triangles[t];
            bool whole = true;
            for (uint32_t vi : tri.vertices)
                whole = whole && vi < model.vertices.size();
            if (!whole)
                continue;
            for (int k : carTriangleCorners(tri.flags)) {
                if (reflects && !(tri.flags & Model::Triangle::kNoReflection)) {
                    cm.envFrom.push_back(static_cast<uint32_t>(cm.vertices.size()));
                    cm.envStrong.push_back((tri.flags & Model::Triangle::kStrongReflection) != 0);
                }
                const uint32_t vi = tri.vertices[size_t(k)];
                gfx::Vertex v{};
                v.pos[0] = model.vertices[vi][0] + part.position[0];
                v.pos[1] = model.vertices[vi][1] + part.position[1];
                v.pos[2] = -(model.vertices[vi][2] + part.position[2]);
                v.uv[0] = tri.u[size_t(k)];
                v.uv[1] = tri.v[size_t(k)];
                v.normal[1] = 1;
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                cm.vertices.push_back(v);
                cm.normals.push_back(vi < model.normals.size() ? model.normals[vi] : std::array<float, 3>{0, 1, 0});
            }
        }
    };
    for (int d = 0; d < 4; ++d) {
        CarModel::Detail& at = cm.details[size_t(d)];
        const CarDetailParts parts = carDetailParts(d, model.parts.size());
        at.first = static_cast<uint32_t>(cm.vertices.size());
        at.envFirst = static_cast<uint32_t>(cm.envFrom.size());
        for (size_t i = 0; i < parts.body.size(); ++i) {
            addPart(size_t(parts.body[i]), i == 0);
            if (i == 0)
                at.depthPart = static_cast<uint32_t>(cm.vertices.size()) - at.first;
        }
        at.body = static_cast<uint32_t>(cm.vertices.size()) - at.first;
        at.envBody = static_cast<uint32_t>(cm.envFrom.size()) - at.envFirst;
        if (parts.headlights)
            addPart(12, true);
        at.lights = static_cast<uint32_t>(cm.vertices.size()) - at.first - at.body;
        at.envLights = static_cast<uint32_t>(cm.envFrom.size()) - at.envFirst - at.envBody;
    }
}

// The car model whose carp.txt has this serial number (the set-up's car
// key, net.md: the Corvette's 0x13), loaded once; -1 without one.
int TrackPage::carModelForSerial(int serial) {
    for (size_t m = 0; m < carModels_.size(); ++m)
        if (carModels_[m].serial == serial && carModels_[m].error.empty())
            return static_cast<int>(m);
    for (const FileRef& f : files_.files()) {
        const std::string name = lower(f.name);
        const std::string head = "gamedata/carmodel/", tail = "/car.viv/carp.txt";
        if (name.rfind(head, 0) != 0 || name.size() <= head.size() + tail.size() ||
            name.compare(name.size() - tail.size(), tail.size(), tail) != 0)
            continue;
        std::vector<uint8_t> data;
        if (files_.read(f, data) || carpSerial(std::string(data.begin(), data.end())) != serial)
            continue;
        const std::string folder = name.substr(0, name.size() - std::string("carp.txt").size());
        for (size_t m = 0; m < carModels_.size(); ++m)
            if (carModels_[m].folder == folder)
                return carModels_[m].error.empty() ? static_cast<int>(m) : -1;
        CarModel cm;
        cm.folder = folder;
        cm.serial = serial;
        loadCarModel(cm);
        if (!cm.error.empty())
            return -1;
        carModels_.push_back(std::move(cm));
        return static_cast<int>(carModels_.size() - 1);
    }
    return -1;
}

gfx::TextureId TrackPage::carSkin(size_t model, size_t paint) {
    if (model >= carModels_.size())
        return 0;
    CarModel& cm = carModels_[model];
    if (auto it = cm.skins.find(paint); it != cm.skins.end())
        return it->second;
    gfx::TextureId id = 0;
    if (!cm.skinImage.rgba.empty()) {
        const Model& m = cm.colours;
        const Model::Colour* primary = paint < m.primaryColours.size() ? &m.primaryColours[paint] : nullptr;
        const Model::Colour* second = nullptr;
        if (primary && !m.secondaryColours.empty()) {
            const size_t k = secondaryFor(m, {static_cast<int>(primary->hsbx[0] & 0xff), static_cast<int>(primary->hsbx[1] & 0xff),
                                             static_cast<int>(primary->hsbx[2] & 0xff)});
            second = &m.secondaryColours[k];
        }
        std::vector<uint8_t> rgba = cm.skinImage.rgba;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            const auto t = skinTexel({rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]}, primary, second);
            std::copy(t.begin(), t.end(), rgba.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (!renderer_->createTexture(cm.skinImage.w, cm.skinImage.h, rgba.data(), glideTextureParams(), id))
            textures_.push_back(id);
        else
            id = 0;
    }
    cm.skins[paint] = id;
    return id;
}

void TrackPage::loadCars() {
    static constexpr const char* kFolders[] = {"gamedata/carmodel/corv/car.viv/", "gamedata/carmodel/vcop/car.viv/"};
    if (carModels_.size() < 2)
        carModels_.resize(2);
    for (size_t m = 0; m < 2; ++m) {
        carModels_[m].folder = kFolders[m];
        loadCarModel(carModels_[m]);
    }
    loadOfficer();
    // The speed tables, for the cars' speeds.
    const std::string trackFolder = "gamedata/tracks/" + tracks_[index_] + "/";
    speedsForward_.clear();
    speedsBackward_.clear();
    for (const FileRef& f : files_.files()) {
        const std::string name = lower(f.name);
        if (name == trackFolder + "speedsf.bin")
            (void)files_.read(f, speedsForward_);
        else if (name == trackFolder + "speedsr.bin")
            (void)files_.read(f, speedsBackward_);
    }
}

// The officer's model (0x4b7c60 loads it with 0x49d450: the .fce and its
// .art's pages): its triangles by page, z negated as the cars'.
void TrackPage::loadOfficer() {
    const int track = std::atoi(tracks_[index_].substr(3).c_str());
    const int which = track == 0 || track == 4 ? 0 : track == 2 || track == 6 ? 2 : track == 3 || track == 7 ? 3 : track == 8 ? 4 : 1;
    const std::string stem = "gamedata/render/pc/cop" + std::to_string(which);
    std::vector<uint8_t> fce, art;
    for (const FileRef& f : files_.files()) {
        const std::string name = lower(f.name);
        if (name == stem + ".fce")
            (void)files_.read(f, fce);
        else if (name == stem + ".art")
            (void)files_.read(f, art);
    }
    Model model;
    std::vector<image::Image> pages;
    if (fce.empty() || parseModel(fce, model) || model.parts.empty() || art.empty() || parseArt(art, pages))
        return;
    officerHalfHeight_ = model.halfSize[1];
    officerHasDummy_ = !model.dummies.empty();
    officerDummy_ = officerHasDummy_ ? model.dummies.front().position : std::array<float, 3>{};
    std::vector<gfx::TextureId> textures;
    for (const image::Image& img : pages) {
        gfx::TextureId id = 0;
        if (!renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id))
            textures_.push_back(id);
        textures.push_back(id);
    }
    officerVertices_.clear();
    officerRuns_.clear();
    officerEnvFrom_.clear();
    officerEnvStrong_.clear();
    for (size_t page = 0; page < pages.size(); ++page) {
        OfficerRun run;
        run.texture = textures[page];
        run.first = static_cast<uint32_t>(officerVertices_.size());
        for (const Model::Part& part : model.parts)
            for (uint32_t t = part.firstTriangle; t < part.firstTriangle + part.triangleCount && t < model.triangles.size(); ++t) {
                const Model::Triangle& tri = model.triangles[t];
                if (tri.texturePage != page)
                    continue;
                bool whole = true;
                for (uint32_t vi : tri.vertices)
                    whole = whole && vi < model.vertices.size();
                if (!whole)
                    continue;
                // The reflection (0x49de20 with draw_officer's sphere map):
                // the triangles without fce flag 1.
                if (!(tri.flags & Model::Triangle::kNoReflection))
                    for (uint32_t k = 0; k < 3; ++k) {
                        officerEnvFrom_.push_back(static_cast<uint32_t>(officerVertices_.size()) + k);
                        officerEnvStrong_.push_back((tri.flags & Model::Triangle::kStrongReflection) != 0);
                    }
                for (size_t k = 0; k < 3; ++k) {
                    const uint32_t vi = tri.vertices[k];
                    gfx::Vertex v{};
                    v.pos[0] = model.vertices[vi][0] + part.position[0];
                    v.pos[1] = model.vertices[vi][1] + part.position[1];
                    v.pos[2] = -(model.vertices[vi][2] + part.position[2]);
                    v.uv[0] = tri.u[k];
                    v.uv[1] = tri.v[k];
                    const auto& nm = vi < model.normals.size() ? model.normals[vi] : Model::Vec3{0, 1, 0};
                    v.normal[0] = nm[0];
                    v.normal[1] = nm[1];
                    v.normal[2] = -nm[2];
                    v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                    officerVertices_.push_back(v);
                }
            }
        run.count = static_cast<uint32_t>(officerVertices_.size()) - run.first;
        if (run.count)
            officerRuns_.push_back(run);
    }
    std::vector<uint32_t> indices(officerVertices_.size());
    for (size_t i = 0; i < indices.size(); ++i)
        indices[i] = static_cast<uint32_t>(i);
    if (officerVertices_.empty() || renderer_->createMesh(officerVertices_, indices, officerMesh_))
        officerMesh_ = 0;
    if (!officerEnvFrom_.empty()) {
        std::vector<gfx::Vertex> env(officerEnvFrom_.size());
        std::vector<uint32_t> envIndices(env.size());
        for (size_t i = 0; i < env.size(); ++i) {
            env[i] = officerVertices_[officerEnvFrom_[i]];
            envIndices[i] = static_cast<uint32_t>(i);
        }
        if (renderer_->createMesh(env, envIndices, officerEnvMesh_))
            officerEnvMesh_ = 0;
    }
}

// The officer on foot (placed and lit in prepareCars): his vertices lit as
// a car's (carShade of his colour, carVertexColour by each normal turned
// with him against the horizon's light direction, 0x49d8f0, 0x49da30, with
// 0x6fbc20 on, as from 167 MHz up); drawn within 100 m (draw_officer
// 0x4b9bc0). Then the sky's sphere map on the triangles that reflect
// (cop2's), in the two colours draw_officer gives 0x49d800: 0x4F4F4F4F
// and, for the strong ones, 0x7F7F7F7F (the effects intensity applied).
void TrackPage::drawOfficers(const gfx::DrawCall& base) {
    if (!officerMesh_ || officerRuns_.empty() || !officer_.shown)
        return;
    const OfficerShown& o = officer_;
    const std::array<float, 3> up{0, 1, 0};
    const std::array<float, 3> light = carLightDirection(skyError_.empty() ? horizon_.rotation : 0);
    const CarShade shade = carShade(o.colour);
    auto worldNormal = [&](const gfx::Vertex& v) {
        const float nz = -v.normal[2];
        return std::array<float, 3>{o.right[0] * v.normal[0] + o.forward[0] * nz, v.normal[1],
                                    o.right[2] * v.normal[0] + o.forward[2] * nz};
    };
    std::vector<gfx::Vertex> vertices = officerVertices_;
    for (gfx::Vertex& v : vertices) {
        const auto w = worldNormal(v);
        const uint32_t c = carVertexColour(shade, w[0] * light[0] + w[1] * light[1] + w[2] * light[2]);
        v.color[0] = static_cast<uint8_t>(c >> 16);
        v.color[1] = static_cast<uint8_t>(c >> 8);
        v.color[2] = static_cast<uint8_t>(c);
        v.color[3] = 255;
    }
    renderer_->setMeshVertices(officerMesh_, vertices);
    gfx::DrawCall call = base;
    call.mesh = officerMesh_;
    call.rgbSource = gfx::ColorSource::Vertex;
    call.color = {1, 1, 1, 1};
    call.state.cull = gfx::Cull::None;
    call.state.alphaTest = gfx::AlphaTest::Ge128; // the art's keyed texels
    gfx::Mat4 m = gfx::identity();
    for (int k = 0; k < 3; ++k) {
        const float flip = k == 2 ? -1.0f : 1.0f;
        m[size_t(k)] = o.right[size_t(k)] * flip;
        m[size_t(4 + k)] = up[size_t(k)] * flip;
        m[size_t(8 + k)] = -o.forward[size_t(k)] * flip;
    }
    m[12] = o.at[0];
    m[13] = o.at[1];
    m[14] = -o.at[2];
    call.model = m;
    for (const OfficerRun& run : officerRuns_) {
        call.texture = textured_ ? run.texture : 0;
        call.firstIndex = run.first;
        call.indexCount = run.count;
        renderer_->draw(call);
    }
    if (officerEnvMesh_ && envPicture_ && textured_) {
        const std::array<float, 3> camera{eye_[0], eye_[1], -eye_[2]};
        const auto frame = reflectionFrame({camera[0] - o.at[0], camera[1] - o.at[1], camera[2] - o.at[2]});
        const uint32_t normal = dimLight(0x4f4f4f4fu, effectsIntensity()), strong = dimLight(0x7f7f7f7fu, effectsIntensity());
        std::vector<gfx::Vertex> env(officerEnvFrom_.size());
        for (size_t i = 0; i < env.size(); ++i) {
            env[i] = officerVertices_[officerEnvFrom_[i]];
            const auto w = worldNormal(env[i]);
            float axis[3];
            for (int a = 0; a < 3; ++a)
                axis[a] = w[0] * frame[size_t(a)][0] + w[1] * frame[size_t(a)][1] + w[2] * frame[size_t(a)][2];
            const auto uv = reflectionUv(axis[0], axis[1], axis[2]);
            const uint32_t c = officerEnvStrong_[i] ? strong : normal;
            env[i].uv[0] = uv[0];
            env[i].uv[1] = uv[1];
            env[i].color[0] = static_cast<uint8_t>(c >> 16);
            env[i].color[1] = static_cast<uint8_t>(c >> 8);
            env[i].color[2] = static_cast<uint8_t>(c);
            env[i].color[3] = static_cast<uint8_t>(c >> 24);
        }
        renderer_->setMeshVertices(officerEnvMesh_, env);
        gfx::DrawCall e = call;
        e.mesh = officerEnvMesh_;
        e.texture = envPicture_;
        e.alphaSource = gfx::ColorSource::Vertex;
        e.state.alphaTest = gfx::AlphaTest::None;
        e.state.src = gfx::Blend::SrcAlpha;
        e.state.dst = gfx::Blend::One;
        e.state.depthWrite = false;
        e.firstIndex = 0;
        e.indexCount = static_cast<uint32_t>(env.size());
        renderer_->draw(e);
    }
}

void TrackPage::releaseCars() {
    for (ComputerCar& c : cars_)
        for (gfx::MeshId m : {c.mesh, c.envMesh})
            if (renderer_ && m)
                renderer_->destroyMesh(m);
    cars_.clear();
    carModels_.assign(2, CarModel{}); // skins owned by textures_
    for (gfx::MeshId m : {carGlowMesh_, carFlareMesh_, carBeamMesh_, stripMesh_, officerMesh_, officerEnvMesh_, shadowMesh_})
        if (renderer_ && m)
            renderer_->destroyMesh(m);
    officerMesh_ = officerEnvMesh_ = 0;
    officer_ = {};
    officerRuns_.clear(); // pages owned by textures_
    officerVertices_.clear();
    carGlowMesh_ = carFlareMesh_ = carBeamMesh_ = stripMesh_ = 0;
    stripQuads_ = stripCapacity_ = 0;
    shadowMesh_ = 0;
    shadowQuads_ = shadowCapacity_ = 0;
    carGlowQuads_ = carGlowCapacity_ = carFlareQuads_ = carFlareCapacity_ = carBeamQuads_ = carBeamCapacity_ = 0;
    glow3Picture_ = 0; // owned by textures_
    carError_.clear();
    speedsForward_.clear();
    speedsBackward_.clear();
}

// Three cars on the virtual road ahead of the camera, from its nearest
// slice the way it looks along the road: two going its way 15 and 35
// units on, in the first and second lanes on their right (the second if
// the road has one, else the first), and a police car coming the other
// way 25 on, in the first lane on its right, its siren on. Each starts
// with the lights a computer car has (startingBeams).
void TrackPage::placeCars() {
    for (ComputerCar& c : cars_)
        for (gfx::MeshId m : {c.mesh, c.envMesh})
            if (m)
                renderer_->destroyMesh(m);
    cars_.clear();
    humanPose_ = {};
    followCop_ = orderedCop_ = -1;
    carError_.clear();
    for (const CarModel& cm : carModels_)
        if (!cm.error.empty())
            carError_ = cm.error;
    const auto& road = collision_.road;
    const int here = roadSliceAt();
    if (here < 0 || road.size() < 30 || !carError_.empty())
        return;
    if (!carDump_.empty()) {
        placeDumpedCars();
        return;
    }
    const int n = static_cast<int>(road.size());
    const auto& rp = road[size_t(here)];
    const float look = std::sin(yaw_) * rp.forward[0] + std::cos(yaw_) * rp.forward[2];
    const int way = look >= 0 ? 1 : -1;
    const bool night = variant_ == 1 || variant_ == 3, weather = variant_ >= 2;
    const int track = std::atoi(tracks_[index_].substr(3).c_str());
    aiTime_ = 0;
    auto put = [&](float distance, int heading, int nth, int model, bool siren, const AiDriver& driver) {
        int at = here;
        for (float gone = 0; gone < distance;) {
            const int next = ((at + way) % n + n) % n;
            const auto& a = road[size_t(at)].position;
            const auto& b = road[size_t(next)].position;
            gone += std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
            at = next;
            if (at == here)
                break;
        }
        const auto& p = road[size_t(at)];
        ComputerCar c;
        c.model = model;
        c.siren = siren;
        c.slice = static_cast<float>(at);
        c.way = heading;
        // Lanes count out from the middle: 7, 8... on the right, 6, 5... on
        // the left; a car's right is the road's when it goes the slices' way.
        const int lanes = heading > 0 ? p.rightLanes() : p.leftLanes();
        const int k = nth < lanes ? nth : 0;
        c.lane = heading > 0 ? 7 + k : 6 - k;
        c.beams = startingBeams(night, weather, false);
        const CarModel& cm = carModels_[size_t(model)];
        // As the computer drives it (aiStep): at its lane's middle, at the
        // speed it wants here.
        c.ai.slice = at;
        c.ai.way = heading;
        c.ai.halfWidth = cm.halfSize[0];
        c.ai.halfHeight = cm.halfSize[1];
        c.ai.halfLength = cm.halfSize[2];
        c.ai.mass = aiCarMass(cm.ai.mass, !siren); // a racer's over 1.2 (0x42bcb0)
        c.ai.driver = driver;
        c.ai.cop = siren;
        c.ai.siren = siren;
        c.ai.racer = !siren;
        c.ai.phase = static_cast<int>(cars_.size());
        c.ai.wheels = cm.wheels;
        // The police cars' own indices (+0x714): the first even, its strip
        // in lane 7, the second odd, in lane 6 (0x403d30).
        for (const ComputerCar& o : cars_)
            c.ai.index += siren && o.ai.cop ? 1 : 0;
        c.ai.scale = std::min(aiDriverFactor(driver, aiStep_) * aiTrackFactor(track), 1.0f);
        c.ai.lane = c.ai.laneLeft = c.ai.laneRight = c.ai.pick = c.lane;
        c.ai.offset = c.ai.target = aiLaneOffset(p, c.lane, cm.halfSize[0]);
        c.ai.beams = c.beams;
        const auto& speeds = heading > 0 || speedsBackward_.empty() ? speedsForward_ : speedsBackward_;
        c.ai.speed = c.ai.wanted = siren ? std::min(trackSectionSpeed(track, at) * 2.0f, cm.ai.maxVelocity)
                                         : std::min(aiSlicesSpeed(speeds, at), cm.ai.topSpeedCap * aiTrackFactor(track)) *
                                               c.ai.scale;
        c.speed = c.ai.speed;
        if (!cm.envFrom.empty()) {
            std::vector<gfx::Vertex> env(cm.envFrom.size());
            std::vector<uint32_t> indices(env.size());
            for (size_t i = 0; i < env.size(); ++i) {
                env[i] = cm.vertices[cm.envFrom[i]];
                indices[i] = static_cast<uint32_t>(i);
            }
            if (renderer_->createMesh(env, indices, c.envMesh))
                c.envMesh = 0;
        }
        if (renderer_->createMesh(cm.vertices, [&] {
                std::vector<uint32_t> indices(cm.vertices.size());
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = static_cast<uint32_t>(i);
                return indices;
            }(), c.mesh))
            c.mesh = 0;
        carRows(c);
        cars_.push_back(c);
    };
    // Drivers from the game's table (0x55226c): Lead Foot (skill 2) behind
    // Mulligan (skill 0, surging), so the first catches the second up; the
    // police car with Smooth's.
    put(15, way, 0, 0, false, {2, false, 2});
    put(25, -way, 0, 1, true, {2, false, 2});
    put(35, way, 1, 0, false, {0, true, 4});
    // A second police car 800 m on, going the camera's way on patrol, its
    // siren off: a free cop for the chasing one's calls (cop_ai_call_strip
    // 0x4039c0, cop_call_backup 0x4043a0) and the key C's orders.
    put(800, way, 0, 1, true, {2, false, 2});
    cars_.back().siren = cars_.back().ai.siren = false;
    cars_.back().ai.copState = 1;
}

// The cars of a dump of the game's car records (RELUVA_NFS_TRACK_CARS),
// each where the game has it: its position, rows, lights, brake and
// siren, the fog beams' scroll; a police car (+0x200 bit 0x20) as the
// police Corvette, the others as the Corvette. The human's car (+0x200 bit
// 4) is left out, as the camera stands for it, unless
// RELUVA_NFS_TRACK_CARS_HUMAN is set; so are cars out of play or hidden.
// They stay where the dump has them.
void TrackPage::placeDumpedCars() {
    const int n = static_cast<int>(collision_.road.size());
    const bool withHuman = std::getenv("RELUVA_NFS_TRACK_CARS_HUMAN") != nullptr;
    humanPose_ = {};
    for (const DumpedCar& d : carDump_) {
        if (d.inPlay && d.human && !humanPose_.set) {
            humanPose_.set = true;
            humanPose_.at = d.at;
            humanPose_.right = d.right;
            humanPose_.up = d.up;
            humanPose_.forward = d.forward;
            humanPose_.halfLength = kVirtualCarHalfLength;
            if (d.setup >= 0 && static_cast<size_t>(d.setup) < carSetup_.size() && carSetup_[size_t(d.setup)][0] != 0xff) {
                const int m = carModelForSerial(carSetup_[size_t(d.setup)][0]);
                if (m >= 0)
                    humanPose_.halfLength = carModels_[size_t(m)].halfSize[2];
            }
        }
        if (!d.inPlay || d.hidden || (d.human && !withHuman))
            continue;
        ComputerCar c;
        c.model = d.cop ? 1 : 0;
        if (d.setup >= 0 && static_cast<size_t>(d.setup) < carSetup_.size() && carSetup_[size_t(d.setup)][0] != 0xff) {
            const int m = carModelForSerial(carSetup_[size_t(d.setup)][0]);
            if (m >= 0)
                c.model = m;
        }
        // Its paint: the model's primary colour nearest the dump's (+0x8A8),
        // and the value the reflection takes (+0x8B0).
        c.paint = primaryFor(carModels_[size_t(c.model)].colours, d.paint);
        c.paintValue = std::clamp(d.paint[2], 0, 255);
        c.siren = d.siren;
        c.slice = n > 0 ? static_cast<float>(((d.slice % n) + n) % n) : 0;
        const auto& p = collision_.road[size_t(c.slice)];
        c.way = d.forward[0] * p.forward[0] + d.forward[1] * p.forward[1] + d.forward[2] * p.forward[2] >= 0 ? 1 : -1;
        c.speed = d.speed;
        c.beams = d.beams;
        c.beamScroll = d.beamScroll;
        c.ai.slice = static_cast<int>(c.slice);
        c.ai.way = c.way;
        c.ai.speed = d.speed;
        c.ai.offset = c.ai.target = c.ai.side = c.ai.lateral = d.offset;
        c.ai.sideKept = true;
        c.ai.beams = d.beams;
        c.ai.braking = d.braking;
        c.ai.cop = d.cop;
        c.ai.siren = d.siren;
        c.at = d.at;
        c.right = d.right;
        c.up = d.up;
        c.forward = d.forward;
        c.dumpedOfficer = d.officer;
        c.officerAt = d.officerAt;
        c.officerRight = d.officerRight;
        c.officerForward = d.officerForward;
        // The tyres (car_spiked_effects 0x42a560): each wheel's skid level
        // and the surface under it (0x429b40: its road polygon's, else the
        // car's +0x18C).
        c.velocity = d.velocity;
        c.wheelAt = d.wheelAt;
        c.weather = d.weather;
        for (int w = 0; w < 4; ++w) {
            c.skid[size_t(w)] = wheelSkidLevel(w, d.frontSkid, d.rearSkid, d.airborne, d.upright);
            int block = -1;
            size_t index = 0;
            c.wheelSurface[size_t(w)] = roadPolygonAt(d.wheelAt[size_t(w)], block, index)
                                            ? roadSurface(track_.blocks[size_t(block)], index)
                                            : d.surface;
        }
        const CarModel& cm = carModels_[size_t(c.model)];
        if (!cm.envFrom.empty()) {
            std::vector<gfx::Vertex> env(cm.envFrom.size());
            std::vector<uint32_t> indices(env.size());
            for (size_t i = 0; i < env.size(); ++i) {
                env[i] = cm.vertices[cm.envFrom[i]];
                indices[i] = static_cast<uint32_t>(i);
            }
            if (renderer_->createMesh(env, indices, c.envMesh))
                c.envMesh = 0;
        }
        std::vector<uint32_t> indices(cm.vertices.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = static_cast<uint32_t>(i);
        if (renderer_->createMesh(cm.vertices, indices, c.mesh))
            c.mesh = 0;
        cars_.push_back(c);
    }
}

// Where a car is on the road this frame (track coordinates): between its
// slice's point and the next, in the middle of its lane, its box on the
// road. The computer aims a car's side at the lane's middle (0x40daf0): a
// lane is the slice's lane width / 2 (col.md) wide, lane 7 from the
// middle to the right; kept within the walls less the car's half width.
// The box rests on the road's plane (car_clearance, 0x4a0440, is 0 when
// its lowest corner touches): the car's position is its half height
// (+0x118, the model's) along the road's normal from the road, its rows
// the road's, forward the way it drives.
void TrackPage::carRows(ComputerCar& car) const {
    const auto& road = collision_.road;
    const int n = static_cast<int>(road.size());
    if (n < 2)
        return;
    const int i = static_cast<int>(std::floor(car.slice)) % n;
    const float t = car.slice - std::floor(car.slice);
    const auto& a = road[size_t(i)];
    const auto& b = road[size_t((i + 1) % n)];
    auto mix = [&](const std::array<float, 3>& x, const std::array<float, 3>& y) {
        return std::array<float, 3>{x[0] + (y[0] - x[0]) * t, x[1] + (y[1] - x[1]) * t, x[2] + (y[2] - x[2]) * t};
    };
    auto unit = [](std::array<float, 3> v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0)
            for (float& c : v)
                c /= l;
        return v;
    };
    auto cross = [](const std::array<float, 3>& x, const std::array<float, 3>& y) {
        return std::array<float, 3>{x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
    };
    const std::array<float, 3> position = mix(a.position, b.position);
    const std::array<float, 3> normal = unit(mix(a.normal, b.normal));
    const std::array<float, 3> roadRight = unit(mix(a.right, b.right));
    std::array<float, 3> forward = unit(mix(a.forward, b.forward));
    for (float& v : forward)
        v *= static_cast<float>(car.way);
    const CarModel& cm = carModels_[size_t(car.model)];
    const float halfWidth = cm.halfSize[0];
    // Its side offset as driven (+0x7B8, aiMove: following the aim,
    // which moves 0.2 a step towards a new lane's middle), kept within
    // the walls.
    float side = car.ai.sideKept ? car.ai.lateral : car.ai.offset;
    side = std::max(std::min(side, a.rightWall - halfWidth), halfWidth - a.leftWall);
    // Across the road at a roadblock (0x403180: a quarter turn, 0.75 of a
    // turn about y, on its slice): facing its left.
    if (car.ai.across) {
        const std::array<float, 3> r = unit(cross(normal, forward));
        forward = {-r[0], -r[1], -r[2]};
    }
    car.up = normal;
    car.right = unit(cross(normal, forward)); // left-handed: up x forward
    car.forward = cross(car.right, normal);
    // The roll and pitch a hit left it (aiCollide, brought back by aiTilt):
    // the pitch about its right, then the roll about its forward.
    if (car.ai.roll != 0 || car.ai.pitch != 0) {
        const float p = car.ai.pitch * 2 * kPi, r = car.ai.roll * 2 * kPi;
        const float cp = std::cos(p), sp = std::sin(p), cr = std::cos(r), sr = std::sin(r);
        std::array<float, 3> f{}, u{};
        for (size_t k = 0; k < 3; ++k) {
            f[k] = car.forward[k] * cp + car.up[k] * sp;
            u[k] = car.up[k] * cp - car.forward[k] * sp;
        }
        std::array<float, 3> rr{}, uu{};
        for (size_t k = 0; k < 3; ++k) {
            uu[k] = u[k] * cr + car.right[k] * sr;
            rr[k] = car.right[k] * cr - u[k] * sr;
        }
        car.forward = f;
        car.up = uu;
        car.right = rr;
    }
    for (int k = 0; k < 3; ++k)
        car.at[size_t(k)] = position[size_t(k)] + roadRight[size_t(k)] * side + normal[size_t(k)] * cm.halfSize[1];
}

void TrackPage::dispatchCop() {
    const int n = static_cast<int>(collision_.road.size());
    if (!computerCars_ || !carDump_.empty() || n < 2)
        return;
    // The first free police car (no target), else the first.
    size_t pickCop = cars_.size();
    for (size_t i = 0; i < cars_.size(); ++i)
        if (cars_[i].ai.cop && (pickCop == cars_.size() || (cars_[pickCop].ai.chasing >= 0 && cars_[i].ai.chasing < 0)))
            pickCop = i;
    for (size_t ci = pickCop; ci < cars_.size(); ci = cars_.size()) {
        ComputerCar& c = cars_[ci];
        orderedCop_ = static_cast<int>(ci);
        if (followCop_ >= 0)
            followCop_ = orderedCop_;
        auto sliceDiff = [&](int a, int b) {
            int d = a - b;
            if (d > n / 2)
                d -= n;
            else if (d < -(n / 2))
                d += n;
            return d;
        };
        int racer = -1, best = n;
        for (size_t i = 0; i < cars_.size(); ++i)
            if (cars_[i].ai.racer && std::abs(sliceDiff(cars_[i].ai.slice, c.ai.slice)) < best) {
                best = std::abs(sliceDiff(cars_[i].ai.slice, c.ai.slice));
                racer = static_cast<int>(i);
            }
        // Orders go to a free cop: the chase is dropped.
        c.ai.chasing = -1;
        c.ai.holding = c.ai.reversing = c.ai.across = false;
        c.ai.copState = 1;
        switch (dispatchNext_) {
        case 0:
            c.ai.stripOn = false;
            aiOrderStrip(c.ai, racer, n);
            c.ai.radio = 0x40b910;
            break;
        case 1:
            aiOrderRoadblock(c.ai, racer, ((c.ai.slice + 20 * c.ai.way) % n + n) % n);
            c.ai.radio = 0x40b860;
            break;
        default:
            c.ai.stripOn = c.ai.stripWanted = c.ai.blockWatch = false;
            c.ai.siren = false;
            aiOrderPatrol(c.ai);
            c.ai.radio = 0;
            break;
        }
        c.ai.radioStep = aiStep_;
        dispatchNext_ = (dispatchNext_ + 1) % 3;
    }
}

// The cars driven as the computer drives (aiStep), 64 steps a second, in
// the list's order: the speed and lane each step (speedsF.bin's speeds
// going the slices' way, speedsR.bin's the other), the model, the hits,
// then each car moved by its world velocity (aiAdvance).
namespace {
// A computer car's place along the slices as the page keeps it (its slice
// and how far on to the next, held under 1 for drawing).
float aiSliceAt(const AiCar& c, int n) {
    float at = static_cast<float>(c.slice) + std::clamp(c.along, 0.0f, 0.999f);
    if (at >= static_cast<float>(n))
        at -= static_cast<float>(n);
    return at;
}
} // namespace

void TrackPage::moveCars(float dt) {
    const auto& road = collision_.road;
    const int n = static_cast<int>(road.size());
    if (n < 2)
        return;
    if (!carDump_.empty())
        return; // as the dump has them
    const int track = std::atoi(tracks_[index_].substr(3).c_str());
    const bool weather = variant_ >= 2;
    if (carsMoving_)
        aiTime_ = std::min(aiTime_ + dt, 0.25f);
    std::vector<AiCar> ai;
    auto sliceDiff = [&](int a, int b) { // 0x40e120
        int d = a - b;
        if (d > n / 2)
            d -= n;
        else if (d < -(n / 2))
            d += n;
        return d;
    };
    // The camera's car is the race's human (0x56ebb8): the racers' race
    // distances are taken from its slice along their way.
    const int human = roadSliceAt();
    for (; carsMoving_ && aiTime_ >= 1.0f / kGameTicksPerSecond; aiTime_ -= 1.0f / kGameTicksPerSecond) {
        ++aiStep_;
        ai.clear();
        for (ComputerCar& c : cars_) {
            // Its place as aiAdvance left it (`along` may be a little over 1
            // in the gap between two slices' runs on a bend), unless it was
            // put somewhere else.
            if (std::fabs(c.slice - aiSliceAt(c.ai, n)) > 1e-3f) {
                c.ai.slice = static_cast<int>(std::floor(c.slice)) % n;
                c.ai.along = c.slice - std::floor(c.slice);
            }
            ai.push_back(c.ai);
        }
        // The catch-up (0x404c80, 0x4090a0): how many racers are ahead of
        // the human, and each racer's place.
        AiRace race;
        race.human = human >= 0;
        race.track = track;
        std::vector<int> distance(cars_.size(), 0), racerDistances;
        for (size_t i = 0; i < cars_.size(); ++i)
            if (ai[i].racer && human >= 0) {
                distance[i] = sliceDiff(ai[i].slice, human) * ai[i].way;
                racerDistances.push_back(distance[i]);
            }
        race.ahead = aiRacersAhead(0, racerDistances);
        // Each car's road right (+0x124: the road polygon's under it), which
        // its side offset is measured along.
        for (AiCar& a : ai)
            aiRoadFrame(track_, collision_, a);
        // The game's tick (Sc32, the even steps): the race task measures
        // every car (aiRaceTick: +0x7B8, +0x21C, +0x638), then every car's
        // driver acts, then each car's model; the hits after them all.
        aiRaceTick(ai, collision_, aiStep_);
        for (size_t i = 0; i < cars_.size(); ++i) {
            const ComputerCar& c = cars_[i];
            const CarModel& cm = carModels_[size_t(c.model)];
            ai[i].raceDistance = distance[i]; // from the camera's car, the race's human
            if (ai[i].cop) {
                aiCopStep(ai, i, collision_, cm.ai, aiStep_, track, weather, AiPhase::Drive);
            } else {
                int place = 1 + (race.human && 0 > distance[i] ? 1 : 0);
                for (int d : racerDistances)
                    place += d > distance[i] ? 1 : 0;
                ai[i].scale = std::min(aiDriverFactor(ai[i].driver, aiStep_) * aiCatchUpFactor(race, distance[i], place) *
                                           aiTrackFactor(track),
                                       1.0f);
                aiStep(ai, i, collision_, c.way > 0 || speedsBackward_.empty() ? speedsForward_ : speedsBackward_, cm.ai,
                       aiStep_, track, weather, AiPhase::Drive);
            }
        }
        for (size_t i = 0; i < cars_.size(); ++i) {
            ComputerCar& c = cars_[i];
            const CarModel& cm = carModels_[size_t(c.model)];
            if (ai[i].cop)
                aiCopStep(ai, i, collision_, cm.ai, aiStep_, track, weather, AiPhase::Move);
            else
                aiStep(ai, i, collision_, c.way > 0 || speedsBackward_.empty() ? speedsForward_ : speedsBackward_, cm.ai,
                       aiStep_, track, weather, AiPhase::Move);
            c.ai = ai[i];
            // Put back on the road (aiPutBack) it is somewhere else.
            c.slice = aiSliceAt(ai[i], n);
            c.way = c.ai.way;
            c.speed = c.ai.speed;
            c.lane = c.ai.lane;
            c.beams = c.ai.beams;
            if (c.ai.cop)
                c.siren = c.ai.siren;
            // 0x4b8740: the fog beams' picture moves by int(speed + 1) a step.
            c.beamScroll = (c.beamScroll + static_cast<int>(c.speed + 1)) & 0x7ff;
        }
        // The cars against each other (car_collisions_task 0x4311d0, 32 a
        // second after their models): what the hits did to their places,
        // speeds and yaw.
        aiCollide(ai, collision_, aiStep_, &carHits_);
        // Then every car's place by its velocity (car_move 0x4a2c60, the
        // Sc64 task, last). What a cop's step did to the cars stepped
        // before it (a racer pulled over by its strip, its chase record)
        // is kept too.
        for (size_t i = 0; i < cars_.size(); ++i) {
            aiAdvance(ai[i], collision_);
            cars_[i].ai = ai[i];
            cars_[i].slice = aiSliceAt(ai[i], n);
            cars_[i].speed = ai[i].speed;
        }
    }
    for (ComputerCar& c : cars_)
        carRows(c);
}

Headlight TrackPage::cameraLight() const {
    if (humanPose_.set && computerCars_) {
        const HumanPose& h = humanPose_;
        Headlight light = carHeadlight(beams_ ? beams_ : 1, projected_, h.at, h.right, h.up, h.forward);
        light.at = headlightPlace(light, h.at, h.up, h.halfLength);
        return light;
    }
    const std::array<float, 3> at{eye_[0], eye_[1], -eye_[2]};
    Headlight light = carHeadlight(beams_ ? beams_ : 1, projected_, at, yaw_);
    light.at = headlightPlace(light, at, kVirtualCarHalfLength);
    return light;
}

bool TrackPage::roadPolygonAt(const std::array<float, 3>& at, int& block, size_t& index) const {
    // The blocks around the nearest one (by its centre on the ground).
    int nearest = -1;
    float best = 1e30f;
    for (size_t i = 0; i < track_.blocks.size(); ++i) {
        const auto& c = track_.blocks[i].centre;
        const float d = (c[0] - at[0]) * (c[0] - at[0]) + (c[2] - at[2]) * (c[2] - at[2]);
        if (d < best) {
            best = d;
            nearest = static_cast<int>(i);
        }
    }
    if (nearest < 0)
        return false;
    std::vector<int> blocks{nearest};
    for (int16_t nb : track_.blocks[size_t(nearest)].neighbours)
        if (nb >= 0 && nb < static_cast<int>(track_.blocks.size()) && nb != nearest)
            blocks.push_back(nb);
    for (int bi : blocks) {
        const Track::Block& b = track_.blocks[size_t(bi)];
        for (size_t pi = 0; pi < b.polygons[4].size(); ++pi) {
            const Track::Polygon& p = b.polygons[4][pi];
            bool inside = true, any = false;
            float sign = 0;
            for (int k = 0; k < 4 && inside; ++k) {
                const uint16_t v0 = p.vertices[size_t(k)], v1 = p.vertices[size_t((k + 1) % 4)];
                if (v0 >= b.vertices.size() || v1 >= b.vertices.size()) {
                    inside = false;
                    break;
                }
                const auto& p0 = b.vertices[v0];
                const auto& p1 = b.vertices[v1];
                const float c = (p1[0] - p0[0]) * (at[2] - p0[2]) - (p1[2] - p0[2]) * (at[0] - p0[0]);
                if (std::fabs(c) < 1e-6f)
                    continue;
                if (any && (c > 0) != (sign > 0))
                    inside = false;
                sign = c;
                any = true;
            }
            if (inside && any) {
                block = bi;
                index = pi;
                return true;
            }
        }
    }
    return false;
}

float TrackPage::roadHeightAt(int block, size_t index, float x, float z) const {
    const Track::Block& b = track_.blocks[size_t(block)];
    const Track::Polygon& p = b.polygons[4][index];
    const auto& v0 = b.vertices[p.vertices[0]];
    const auto& v1 = b.vertices[p.vertices[1]];
    const auto& v2 = b.vertices[p.vertices[2]];
    const auto& v3 = b.vertices[p.vertices[3]];
    // The plane through the corners' middle, across the diagonals.
    const std::array<float, 3> a{v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]}, c{v3[0] - v1[0], v3[1] - v1[1], v3[2] - v1[2]};
    const std::array<float, 3> nrm{a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0]};
    const float mx = (v0[0] + v1[0] + v2[0] + v3[0]) / 4, my = (v0[1] + v1[1] + v2[1] + v3[1]) / 4,
                mz = (v0[2] + v1[2] + v2[2] + v3[2]) / 4;
    if (std::fabs(nrm[1]) < 1e-6f)
        return my;
    return my - (nrm[0] * (x - mx) + nrm[2] * (z - mz)) / nrm[1];
}

bool TrackPage::roadColoursAt(const std::array<float, 3>& at, std::array<uint32_t, 4>& corners) const {
    int block = -1;
    size_t index = 0;
    if (!roadPolygonAt(at, block, index))
        return false;
    std::array<int, 3> ambient{100, 100, 100};
    if (skyError_.empty())
        ambient = horizon_.ambient;
    const Track::Block& b = track_.blocks[size_t(block)];
    const Track::Polygon& p = b.polygons[4][index];
    for (int k = 0; k < 4; ++k) {
        const uint16_t v = p.vertices[size_t(k)];
        corners[size_t(k)] = ambientScaled(v < b.colours.size() ? b.colours[v] : 0xff7f7f7fu, ambient);
    }
    return true;
}

uint32_t TrackPage::uploadQuads(std::vector<gfx::Vertex>& quads, gfx::MeshId& mesh, uint32_t& capacity) {
    const uint32_t count = static_cast<uint32_t>(quads.size() / 4);
    if (count == 0)
        return 0;
    if (count > capacity || !mesh) {
        if (mesh)
            renderer_->destroyMesh(mesh);
        mesh = 0;
        capacity = std::max<uint32_t>(count * 2, 32);
        std::vector<uint32_t> indices;
        for (uint32_t i = 0; i < capacity; ++i)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(4 * i + k);
        quads.resize(size_t(capacity) * 4);
        if (renderer_->createMesh(quads, indices, mesh)) {
            mesh = 0;
            capacity = 0;
            return 0;
        }
        return count;
    }
    quads.resize(size_t(capacity) * 4);
    renderer_->setMeshVertices(mesh, quads);
    return count;
}

// Each car's colour, as the game's (car_draw_wheels and the drawing
// functions beside it, 0x4bab70): the track's shading under it
// (carShadeColour; 0x4ba810 averages it under the car and its wheels,
// here under the car), by night with the other cars' lights on it
// (carLitBy, 0x4b97b0; the camera's car's too), each vertex lit from the
// horizon's light direction (carVertexColour). Its reflection (0x4ba350,
// 0x49de20): the body's and headlights' corners on the sky's sphere map
// by their normals in the frame of the camera's view of the car
// (reflectionFrame), in carReflection's colours. And its light glows
// (carLightGlow) as quads facing the camera, turned by their place across
// the screen as 0x492370 turns them; in the weather each glow's flare on
// a wet road (rainFlare, 0x491bc0), in the fog each headlight's beam
// (fogBeam, 0x492980).
void TrackPage::prepareCars(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right,
                            const gfx::Vec3& up, float fogDensity) {
    carGlowQuads_ = carFlareQuads_ = carBeamQuads_ = shadowQuads_ = 0;
    carSort_.clear();
    if (!computerCars_ || cars_.empty())
        return;
    const bool night = variant_ == 1 || variant_ == 3, weather = variant_ >= 2;
    const bool effects = sfxLevel_ != 2; // the Special Effects option not Off
    const std::array<float, 3> light = carLightDirection(skyError_.empty() ? horizon_.rotation : 0);
    // The race's lights, in the list's order: the camera's car's, then each car's.
    std::vector<CarLights> lights;
    if (night) {
        CarLights camera;
        camera.headlight = cameraLight();
        camera.headlightOn = beams_ != 0;
        lights.push_back(camera);
        for (const ComputerCar& c : cars_) {
            CarLights l;
            l.headlightOn = c.beams != 0;
            l.headlight = carHeadlight(c.beams ? c.beams : 1, projected_, c.at, c.right, c.up, c.forward);
            l.headlight.at = headlightPlace(l.headlight, c.at, c.up, carModels_[size_t(c.model)].halfSize[2]);
            l.sirenOn = c.siren;
            l.siren = sirenLight(c.at, c.right, c.up, c.forward, static_cast<int>(skyTick_));
            lights.push_back(l);
        }
    }
    // The sky's colour the reflection takes (0x6ff9f8, the clear colour).
    std::array<int, 3> sky{0, 0, 0};
    if (skyError_.empty() && !skyBlack_)
        sky = horizon_.skyTop;
    const std::array<float, 3> camera{eye[0], eye[1], -eye[2]};
    const int n = static_cast<int>(collision_.road.size());
    std::vector<gfx::Vertex> glows, flares, beams, shadows;
    auto vertex = [](const std::array<float, 3>& track, float u, float v, uint32_t colour) {
        gfx::Vertex out{};
        out.pos[0] = track[0];
        out.pos[1] = track[1];
        out.pos[2] = -track[2];
        out.uv[0] = u;
        out.uv[1] = v;
        out.color[0] = static_cast<uint8_t>(colour >> 16);
        out.color[1] = static_cast<uint8_t>(colour >> 8);
        out.color[2] = static_cast<uint8_t>(colour);
        out.color[3] = static_cast<uint8_t>(colour >> 24);
        return out;
    };
    // A glow quad (0x492370): only for a centre from 0 to 300 deep and on
    // the screen; projected at the light's place, sized by its depth, but
    // with the depth given (`depth`, or with `own` the light's own less it),
    // so neither the body hides a car's lights nor they show through what
    // is in front of the car. Here the quad is moved along the ray to that
    // depth and scaled with it, the same on the screen. Its corners take
    // the picture from texel centre to texel centre (u, v 1/64 to 63/64
    // of glw3's 32).
    auto glowQuad = [&](const std::array<float, 3>& at, float size, uint32_t colour, bool own, float depthGiven) {
        const gfx::Vec3 lamp{at[0], at[1], -at[2]};
        const gfx::Vec3 d{lamp[0] - eye[0], lamp[1] - eye[1], lamp[2] - eye[2]};
        const float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        const float y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
        const float z = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
        if (z <= 0 || z > 300 || std::fabs(x) > kTanAcross * z || std::fabs(y) > kTanUp * z)
            return;
        const float depth = own ? z - depthGiven : depthGiven;
        if (depth <= 0)
            return;
        const float scale = depth / z;
        const gfx::Vec3 centre{eye[0] + d[0] * scale, eye[1] + d[1] * scale, eye[2] + d[2] * scale};
        // Turned by a quarter of a turn times (x / z + 1) (0x53bd00).
        const float angle = (x / z + 1) * 0.25f * 2 * kPi;
        for (int corner = 0; corner < 4; ++corner) {
            const float a = angle + static_cast<float>(corner) * kPi / 2;
            const float cx = std::cos(a) * size * scale, cy = std::sin(a) * size * scale;
            gfx::Vertex v{};
            for (int k = 0; k < 3; ++k)
                v.pos[k] = centre[size_t(k)] + right[size_t(k)] * cx + up[size_t(k)] * cy;
            v.uv[0] = corner == 1 || corner == 2 ? 0.984375f : 0.015625f;
            v.uv[1] = corner >= 2 ? 0.984375f : 0.015625f;
            v.color[0] = static_cast<uint8_t>(colour >> 16);
            v.color[1] = static_cast<uint8_t>(colour >> 8);
            v.color[2] = static_cast<uint8_t>(colour);
            v.color[3] = static_cast<uint8_t>(colour >> 24);
            glows.push_back(v);
        }
    };
    for (size_t ci = 0; ci < cars_.size(); ++ci) {
        ComputerCar& c = cars_[ci];
        const CarModel& cm = carModels_[size_t(c.model)];
        std::array<uint32_t, 4> corners{};
        c.colour = roadColoursAt(c.at, corners) ? carShadeColour(corners) : 0xff7f7f7fu;
        // The detail (0x4bb5d0): the mean view depth of the box's eight
        // corners (+0x114-+0x11C either way along the rows from +0x98),
        // which is the depth of its centre; the car detail option taken
        // as High (0, as nfs-host's config.dat has it).
        {
            const float depth = (c.at[0] - eye[0]) * forward[0] + (c.at[1] - eye[1]) * forward[1] +
                                (-c.at[2] - eye[2]) * forward[2];
            c.detail = carDetailLevel(depth, 0, night);
            // Its sort depth without the z-buffer (0x49c420 -> 0x4c7400 with
            // +0x8B8 - 9.5 + 9.5, in quarter-metre buckets) is that mean;
            // the box's nearest corner and its screen bounds go with it.
            CarSort sort;
            sort.mean = depth;
            sort.nearest = depth;
            sort.x0 = sort.y0 = 1e9f;
            sort.x1 = sort.y1 = -1e9f;
            bool behind = false;
            for (int k = 0; k < 8; ++k) {
                const float sx = k & 1 ? 1.0f : -1.0f, sy = k & 2 ? 1.0f : -1.0f, sz = k & 4 ? 1.0f : -1.0f;
                float d[3];
                for (size_t a = 0; a < 3; ++a) {
                    const float p = c.at[a] + c.right[a] * sx * cm.halfSize[0] + c.up[a] * sy * cm.halfSize[1] +
                                    c.forward[a] * sz * cm.halfSize[2];
                    d[a] = (a == 2 ? -p : p) - eye[a];
                }
                const float z = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
                sort.nearest = std::min(sort.nearest, z);
                if (z <= 0.1f) {
                    behind = true;
                    continue;
                }
                const float x = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / z,
                            y = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / z;
                sort.x0 = std::min(sort.x0, x);
                sort.x1 = std::max(sort.x1, x);
                sort.y0 = std::min(sort.y0, y);
                sort.y1 = std::max(sort.y1, y);
            }
            if (!behind && c.detail >= 0)
                carSort_.push_back(sort);
        }
        if (night) {
            std::vector<CarLights> others = lights;
            others.erase(others.begin() + static_cast<std::ptrdiff_t>(ci + 1));
            c.colour = carLitBy(c.colour, c.at, others);
            // At the nearest detail (0x4bae00) the track's lights too
            // (0x4b9550).
            if (n > 0 && c.detail == 0)
                c.colour = carLitByTrackLights(c.colour, c.at, static_cast<int>(std::floor(c.slice)) % n, n, track_,
                                               skyTick_, !effects);
        }
        // The shadow (0x4b81a0): the box's bottom corners on the road, cast
        // away from the light by day out of the weather; the page has no
        // mirror view, and its cars are never off the ground (+0x184) or
        // without a road under them (+0x20), which leave it out.
        {
            int block = -1;
            size_t index = 0;
            RoadFrame frame;
            const bool onRoad = roadPolygonAt(c.at, block, index);
            const bool own = onRoad && polygonRoadFrame(track_.blocks[size_t(block)], index, frame);
            std::array<std::array<float, 3>, 4> corners{};
            const float w = cm.halfSize[0], h = cm.halfSize[1], l = cm.halfSize[2];
            const std::array<std::array<float, 2>, 4> box{{{-w, l}, {w, l}, {w, -l}, {-w, -l}}};
            for (size_t k = 0; k < 4; ++k) {
                auto& p = corners[k];
                for (size_t a = 0; a < 3; ++a)
                    p[a] = c.at[a] + c.right[a] * box[k][0] - c.up[a] * h + c.forward[a] * box[k][1];
                // 0x4968a0 on the car's road frame, then the road found
                // there (trackpos_build) when it is up to 0.25 above.
                float plane = own ? roadFrameHeight(frame, p) : onRoad ? roadHeightAt(block, index, p[0], p[2]) : p[1];
                int b2 = -1;
                size_t i2 = 0;
                if (roadPolygonAt({p[0], plane, p[2]}, b2, i2)) {
                    const float road = roadHeightAt(b2, i2, p[0], p[2]);
                    if (road > plane && road < plane + 0.25f)
                        plane = road;
                }
                p[1] = plane;
            }
            const auto quad = carShadowQuad(corners, light, h, !night && !weather);
            for (size_t k = 0; k < 4; ++k)
                shadows.push_back(vertex(quad[k], k == 1 || k == 2 ? 0.984375f : 0.015625f, k >= 2 ? 0.984375f : 0.015625f,
                                         kCarShadowColour));
        }
        const CarShade shade = carShade(c.colour);
        auto normalOf = [&](uint32_t i) {
            const auto& nm = cm.normals[i];
            std::array<float, 3> w{};
            for (int k = 0; k < 3; ++k)
                w[size_t(k)] = c.right[size_t(k)] * nm[0] + c.up[size_t(k)] * nm[1] + c.forward[size_t(k)] * nm[2];
            return w;
        };
        if (c.mesh) {
            std::vector<gfx::Vertex> vertices = cm.vertices;
            for (size_t i = 0; i < vertices.size(); ++i) {
                const std::array<float, 3> w = normalOf(static_cast<uint32_t>(i));
                const uint32_t colour = carVertexColour(shade, w[0] * light[0] + w[1] * light[1] + w[2] * light[2]);
                vertices[i].color[0] = static_cast<uint8_t>(colour >> 16);
                vertices[i].color[1] = static_cast<uint8_t>(colour >> 8);
                vertices[i].color[2] = static_cast<uint8_t>(colour);
                vertices[i].color[3] = 255;
            }
            renderer_->setMeshVertices(c.mesh, vertices);
        }
        if (c.envMesh && reflections_ && envPicture_) {
            const int slice = n > 0 ? static_cast<int>(std::floor(c.slice)) % n : 0;
            const CarReflection refl =
                carReflection(c.colour, sky, c.paintValue >= 0 ? c.paintValue : cm.paintValue,
                              n > 0 ? coverDepth(collision_, slice, c.at) : 0);
            const uint32_t normal = dimLight(refl.normal, effectsIntensity()), strong = dimLight(refl.strong, effectsIntensity());
            const auto frame = reflectionFrame({camera[0] - c.at[0], camera[1] - c.at[1], camera[2] - c.at[2]});
            std::vector<gfx::Vertex> env(cm.envFrom.size());
            for (size_t i = 0; i < env.size(); ++i) {
                const std::array<float, 3> w = normalOf(cm.envFrom[i]);
                float axis[3];
                for (int a = 0; a < 3; ++a)
                    axis[a] = w[0] * frame[size_t(a)][0] + w[1] * frame[size_t(a)][1] + w[2] * frame[size_t(a)][2];
                const auto uv = reflectionUv(axis[0], axis[1], axis[2]);
                const uint32_t colour = cm.envStrong[i] ? strong : normal;
                env[i] = cm.vertices[cm.envFrom[i]];
                env[i].uv[0] = uv[0];
                env[i].uv[1] = uv[1];
                env[i].color[0] = static_cast<uint8_t>(colour >> 16);
                env[i].color[1] = static_cast<uint8_t>(colour >> 8);
                env[i].color[2] = static_cast<uint8_t>(colour);
                env[i].color[3] = static_cast<uint8_t>(colour >> 24);
            }
            renderer_->setMeshVertices(c.envMesh, env);
        }
        // The body's nearest depth (0x49de20 for part 0 gives the least
        // view depth of its vertices, 0x49d9d0), the depth the tail lights'
        // and headlights' glows are drawn at (0x4bb200 -> 0x4b8740 ->
        // 0x492370's sixth argument).
        const std::array<float, 3> view{forward[0], forward[1], -forward[2]};
        float bodyDepth = 65535.0f;
        const CarModel::Detail& shown = cm.details[size_t(std::max(c.detail, 0))];
        for (uint32_t i = shown.first; i < shown.first + shown.depthPart; ++i) {
            const float* v = cm.vertices[i].pos;
            float depth = 0;
            for (int k = 0; k < 3; ++k) {
                const float track = c.at[size_t(k)] + c.right[size_t(k)] * v[0] + c.up[size_t(k)] * v[1] -
                                    c.forward[size_t(k)] * v[2] + (k == 1 ? 0.145f : 0.0f);
                depth += (track - camera[size_t(k)]) * view[size_t(k)];
            }
            bodyDepth = std::min(bodyDepth, depth);
        }
        // The glows: each dummy where the model is drawn (the position
        // plus 0.145 up, 0x540120).
        const float brightness = carGlowBrightness(c.colour);
        const float phase = static_cast<float>(skyTick_ % 32) * 0.03125f;
        const char flash = c.siren ? (phase <= 0.5f ? 'O' : 'E') : '\0';
        const uint32_t beamColour = effects ? dimLight(fogBeamColour(fogDensity, c.beams), effectsIntensity()) : 0;
        const bool covered = n > 0 && pointCovered(collision_.road[size_t(static_cast<int>(std::floor(c.slice)) % n)], c.at);
        for (const Model::Dummy& dm : cm.dummies) {
            std::array<float, 3> at{};
            for (int k = 0; k < 3; ++k)
                at[size_t(k)] = c.at[size_t(k)] + c.right[size_t(k)] * dm.position[0] + c.up[size_t(k)] * dm.position[1] +
                                c.forward[size_t(k)] * dm.position[2];
            at[1] += 0.145f;
            // The beam in the fog, for a headlight that is on, facing the
            // camera or not.
            if (beamColour && !dm.name.empty() && dm.name[0] == 'H' &&
                (c.beams != 0 || (flash != '\0' && dm.name.size() > 3 && dm.name[3] == flash))) {
                const FogBeam beam = fogBeam(at, c.right, c.up, c.forward, camera, beamColour);
                const float scroll = static_cast<float>(c.beamScroll) / 2048.0f;
                const size_t segments = beam.shown ? beam.nearRing.size() - 1 : 0;
                for (size_t i = 0; i < segments; ++i) {
                    const float u0 = static_cast<float>(i) / static_cast<float>(segments),
                                u1 = static_cast<float>(i + 1) / static_cast<float>(segments);
                    beams.push_back(vertex(beam.farRing[i + 1], u1, -scroll, 0));
                    beams.push_back(vertex(beam.farRing[i], u0, -scroll, 0));
                    beams.push_back(vertex(beam.nearRing[i], u0, 1 - scroll, beam.colour));
                    beams.push_back(vertex(beam.nearRing[i + 1], u1, 1 - scroll, beam.colour));
                }
            }
            std::array<float, 3> toCamera{camera[0] - at[0], camera[1] - at[1], camera[2] - at[2]};
            const float distance = std::sqrt(toCamera[0] * toCamera[0] + toCamera[1] * toCamera[1] + toCamera[2] * toCamera[2]);
            if (distance <= 0)
                continue;
            const float facing =
                (c.forward[0] * toCamera[0] + c.forward[1] * toCamera[1] + c.forward[2] * toCamera[2]) / distance;
            const CarGlow g =
                carLightGlow(dm.name, facing, c.beams, c.ai.braking, c.siren, static_cast<int>(skyTick_), brightness);
            if (!g.shown || g.size <= 0)
                continue;
            const uint32_t colour = dimLight(g.colour, effectsIntensity());
            // The rain's flare under a tail light or headlight (not a roof
            // light) whose dummy's fifth letter isn't N, on a wet road out
            // of cover.
            if (effects && weather && !covered && (dm.name[0] == 'T' || dm.name[0] == 'H') &&
                !(dm.name.size() > 4 && dm.name[4] == 'N')) {
                int block = -1;
                size_t index = 0;
                if (roadPolygonAt(at, block, index) && wetSurface(roadSurface(track_.blocks[size_t(block)], index))) {
                    const float way = dm.name[0] == 'T' ? -1.0f : 1.0f;
                    const RainFlare f = rainFlare(at, camera, {c.forward[0] * way, c.forward[1] * way, c.forward[2] * way},
                                                  g.size, dimLight(g.colour, effectsIntensity()));
                    // The heights (0x491bc0): the light put on the road by
                    // trackpos_build (0x4969c0), each corner at that height
                    // taken onto the road the same way (0x4968a0), 0.125 above
                    // it; with the road point's frame instead of the
                    // polygon's (no records), at the light's road height.
                    RoadFrame frame;
                    const bool own = polygonRoadFrame(track_.blocks[size_t(block)], index, frame);
                    const float ground = own ? roadFrameHeight(frame, at) : roadHeightAt(block, index, at[0], at[2]);
                    if (f.shown)
                        for (int k = 0; k < 4; ++k) {
                            const auto& p = f.corners[size_t(k)];
                            const float h = own ? roadFrameHeight(frame, {p[0], ground, p[1]}) + 0.125f : ground;
                            flares.push_back(vertex({p[0], h, p[1]}, k == 1 || k == 2 ? 1.0f : 0.0f,
                                                    k >= 2 ? 1.0f : 0.0f, f.colour));
                        }
                }
            }
            // 0x492370 (glowQuad): the body's nearest depth for the tail
            // lights and headlights, the roof lights' own less 0.75.
            glowQuad(at, g.size, colour, g.nearer > 0, g.nearer > 0 ? g.nearer : bodyDepth);
        }
    }
    // The officer on foot (cop_officer_place 0x401c20 and
    // cop_officer_spot 0x401b50 as the stop begins, draw_officer 0x4b9bc0):
    // 2 m from the racer along its road's right, the side by where it is
    // across its slice and which way it faces (towards the road's middle),
    // kept only on the slice's road polygons with a surface not 0 or 14
    // (0x401d78, officerSpotOnRoad), else the other side; on the road
    // there (the racer's half height down), his model raised by its own;
    // upright, turned to the racer's heading (mat_rot_y_turns of +0x210).
    // Lit as a car (0x49d8f0, 0x49da30, the horizon's light direction)
    // from the road's colour under the racer (0x4206f0 at its track
    // position), by night with the cars' lights on the racer (0x4b97b0)
    // and the track's (0x4b9550); and on track 8 his red light (the
    // model's dummy, 0x492370 in 0xFFFF0000, size 0.1 x how far he faces
    // the camera, at his nearest depth).
    officer_ = {};
    for (size_t ci = 0; ci < cars_.size() && officerMesh_ && n > 0; ++ci) {
        const ComputerCar& cop = cars_[ci];
        size_t ti = ci;
        std::array<float, 3> at{}, f{0, 0, 1}, r{1, 0, 0};
        if (cop.dumpedOfficer) {
            // A dump's officer: where the game put him (on the road), his
            // rows; draw_officer raises him by his half height.
            at = cop.officerAt;
            f = cop.officerForward;
            r = cop.officerRight;
        } else {
            if (!cop.ai.cop || cop.ai.copState != 6 || cop.ai.chasing < 0 || static_cast<size_t>(cop.ai.chasing) >= cars_.size())
                continue;
            ti = static_cast<size_t>(cop.ai.chasing);
            const ComputerCar& t = cars_[ti];
            const CarModel& tm = carModels_[size_t(t.model)];
            const int slice = ((t.ai.slice % n) + n) % n;
            const auto& rp = collision_.road[size_t(slice)];
            auto spotAt = [&](float side) {
                std::array<float, 3> spot{};
                for (size_t k = 0; k < 3; ++k)
                    spot[k] = t.at[k] - t.up[k] * tm.halfSize[1] + rp.right[k] * side;
                return spot;
            };
            float side = t.ai.side > 0 ? -2.0f : 2.0f;
            at = spotAt(side);
            if (!officerSpotOnRoad(track_, slice, at))
                at = spotAt(-side);
            f = {t.forward[0], 0, t.forward[2]};
            const float fl = std::sqrt(f[0] * f[0] + f[2] * f[2]);
            if (fl > 0)
                f = {f[0] / fl, 0, f[2] / fl};
            else
                f = {0, 0, 1};
            r = {f[2], 0, -f[0]};
        }
        const ComputerCar& t = cars_[ti];
        const int slice = ((t.ai.slice % n) + n) % n;
        at[1] += officerHalfHeight_;
        const float dx = at[0] - eye_[0], dy = at[1] - eye_[1], dz = -at[2] - eye_[2];
        if (dx * dx + dy * dy + dz * dz > 100.0f * 100.0f)
            continue;
        officer_.shown = true;
        officer_.at = at;
        officer_.forward = f;
        officer_.right = r;
        std::array<uint32_t, 4> corners{};
        uint32_t colour = roadColoursAt(t.at, corners) ? carShadeColour(corners) : 0xff7f7f7fu;
        if (night) {
            std::vector<CarLights> others = lights;
            others.erase(others.begin() + static_cast<std::ptrdiff_t>(ti + 1));
            colour = carLitBy(colour, t.at, others);
            colour = carLitByTrackLights(colour, t.at, slice, n, track_, skyTick_, !effects);
        }
        officer_.colour = colour;
        if (officerHasDummy_ && std::atoi(tracks_[index_].substr(3).c_str()) == 8) {
            const std::array<float, 3> view{forward[0], forward[1], -forward[2]};
            const float d = f[0] * view[0] + f[1] * view[1] + f[2] * view[2];
            if (d < 0) {
                const auto& r = officer_.right;
                const auto& p = officerDummy_;
                const std::array<float, 3> lamp{at[0] + r[0] * p[0] + f[0] * p[2], at[1] + p[1],
                                                at[2] + r[2] * p[0] + f[2] * p[2]};
                // His nearest view depth (0x49d5c0's least of his parts').
                float nearest = 65535.0f;
                for (const gfx::Vertex& v : officerVertices_) {
                    const float mz = -v.pos[2];
                    const std::array<float, 3> w{at[0] + r[0] * v.pos[0] + f[0] * mz, at[1] + v.pos[1],
                                                 at[2] + r[2] * v.pos[0] + f[2] * mz};
                    nearest = std::min(nearest, (w[0] - camera[0]) * view[0] + (w[1] - camera[1]) * view[1] +
                                                    (w[2] - camera[2]) * view[2]);
                }
                if (nearest > 0)
                    glowQuad(lamp, -d * 0.1f, dimLight(0xffff0000u, effectsIntensity()), false, nearest);
            }
        }
        break; // one mesh: the first stop's officer
    }
    carGlowQuads_ = uploadQuads(glows, carGlowMesh_, carGlowCapacity_);
    carFlareQuads_ = uploadQuads(flares, carFlareMesh_, carFlareCapacity_);
    // The strips laid (spike_lay 0x4c9dd0): a bar across the lane's middle,
    // 1.5 lane widths long, on the road at the strip's slice.
    std::vector<gfx::Vertex> strips;
    for (const ComputerCar& c : cars_) {
        if (!c.ai.stripOn || n < 2)
            continue;
        const auto& p = collision_.road[size_t(((c.ai.stripSlice % n) + n) % n)];
        float from = 0, to = 0;
        aiStripSpan(p, c.ai.stripLane, from, to);
        for (const auto& [side, along] : {std::pair{from, -0.3f}, std::pair{to, -0.3f}, std::pair{to, 0.3f}, std::pair{from, 0.3f}}) {
            std::array<float, 3> at{};
            for (size_t k = 0; k < 3; ++k)
                at[k] = p.position[k] + p.right[k] * side + p.forward[k] * along + p.normal[k] * 0.05f;
            strips.push_back(vertex(at, 0, 0, 0xff303030u));
        }
    }
    stripQuads_ = uploadQuads(strips, stripMesh_, stripCapacity_);
    shadowQuads_ = uploadQuads(shadows, shadowMesh_, shadowCapacity_);
    carBeamQuads_ = uploadQuads(beams, carBeamMesh_, carBeamCapacity_);
}

// The cars as lit (prepareCars), their headlights' part only with the
// lights on (0x4bab70 draws part 12 only then).
void TrackPage::drawCars(const gfx::DrawCall& base) {
    if (!computerCars_)
        return;
    if (stripMesh_ && stripQuads_) {
        gfx::DrawCall strip = base;
        strip.mesh = stripMesh_;
        strip.model = gfx::identity();
        strip.texture = 0;
        strip.rgbSource = gfx::ColorSource::Vertex;
        strip.color = {1, 1, 1, 1};
        strip.state.cull = gfx::Cull::None;
        strip.state.depthBias = true;
        strip.firstIndex = 0;
        strip.indexCount = stripQuads_ * 6;
        renderer_->draw(strip);
    }
    gfx::DrawCall car = base;
    car.rgbSource = gfx::ColorSource::Vertex;
    car.color = {1, 1, 1, 1};
    // Facing away culled as 0x49dbf0 does (the two-sided triangles are in
    // the mesh both ways round), and its skin's alpha (the reflection's
    // share, fce.md) not tested.
    car.state.cull = mirrored_ ? gfx::Cull::Front : gfx::Cull::Back;
    car.state.alphaTest = gfx::AlphaTest::None;
    for (const ComputerCar& c : cars_) {
        if (!c.mesh)
            continue;
        const CarModel& cm = carModels_[size_t(c.model)];
        car.mesh = c.mesh;
        car.texture = textured_ ? carSkin(size_t(c.model), c.paint) : 0;
        // Track rows into the world (z negated); the model's z is stored
        // negated, so its column is the forward negated, then flipped.
        gfx::Mat4 m = gfx::identity();
        for (int k = 0; k < 3; ++k) {
            const float flip = k == 2 ? -1.0f : 1.0f;
            m[size_t(k)] = c.right[size_t(k)] * flip;
            m[size_t(4 + k)] = c.up[size_t(k)] * flip;
            m[size_t(8 + k)] = -c.forward[size_t(k)] * flip;
        }
        m[12] = c.at[0];
        m[13] = c.at[1] + 0.145f; // the model drawn 0.145 above the position (0x540120)
        m[14] = -c.at[2];
        car.model = m;
        if (c.detail < 0)
            continue;
        const CarModel::Detail& d = cm.details[size_t(c.detail)];
        car.firstIndex = d.first;
        car.indexCount = d.body + (c.beams ? d.lights : 0);
        renderer_->draw(car);
        // The reflection (0x49dbf0 queues it with the polygon): the sphere
        // map added by the corner colour's alpha (THRASH blend 1), the
        // headlights' only with them on.
        if (c.envMesh && reflections_ && envPicture_ && textured_) {
            gfx::DrawCall env = car;
            env.mesh = c.envMesh;
            env.texture = envPicture_;
            env.alphaSource = gfx::ColorSource::Vertex;
            env.state.src = gfx::Blend::SrcAlpha;
            env.state.dst = gfx::Blend::One;
            env.state.depthWrite = false;
            env.firstIndex = d.envFirst;
            env.indexCount = d.envBody + (c.beams ? d.envLights : 0);
            if (env.indexCount)
                renderer_->draw(env);
        }
    }
    drawOfficers(car);
}

// The cars' shadows (prepareCars): over the road and its markings, by the
// texel's alpha times the colour's (0x6F), source alpha and its inverse
// as the THRASH list has them (blend on, mode 0), not writing the depth.
void TrackPage::drawShadows(const gfx::DrawCall& base) {
    if (!computerCars_ || !shadowMesh_ || !shadowQuads_ || !shadowPicture_ || !textured_)
        return;
    gfx::DrawCall shadow = base;
    shadow.mesh = shadowMesh_;
    shadow.model = gfx::identity();
    shadow.texture = shadowPicture_;
    shadow.rgbSource = shadow.alphaSource = gfx::ColorSource::Vertex;
    shadow.color = {1, 1, 1, 1};
    shadow.state.cull = gfx::Cull::None;
    shadow.state.alphaTest = gfx::AlphaTest::None;
    shadow.state.depthWrite = false;
    shadow.state.depthBias = true;
    shadow.state.src = gfx::Blend::SrcAlpha;
    shadow.state.dst = gfx::Blend::OneMinusSrcAlpha;
    shadow.firstIndex = 0;
    shadow.indexCount = shadowQuads_ * 6;
    renderer_->draw(shadow);
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
    // The glows, beams, flares and the headlights' picture are point-sampled
    // without mipmaps as everything the game's Glide draws: their batches
    // in a race's THRASH list (d7-night1, d5-fog) are textured with them
    // like any other, and no state 7, 8 or 0xb is ever sent (thrash.md,
    // Texture filtering).
    bool additive = false;
    picture(kLaneSheet, -1, false, &additive); // loads the sheet
    for (const Shape& shape : banks_[kLaneSheet].shapes)
        if (shape.name == "glw0") {
            image::Image img;
            if (!decodeForGlide(shape, img, true) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), glowPicture_))
                textures_.push_back(glowPicture_);
        } else if (shape.name == "glw3") {
            // The cars' glows (0x492370 draws with 0x8b4840, which 0x4d5370
            // loads with glw3).
            image::Image img;
            if (!decodeForGlide(shape, img, true) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), glow3Picture_))
                textures_.push_back(glow3Picture_);
        } else if (shape.name == "shad") {
            // The cars' shadows (0x4b81a0 draws with 0x8b4898, which 0x4d5370
            // loads with shad: 32 x 32, a dark blue with soft-edged alpha).
            image::Image img;
            if (!decodeForGlide(shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), shadowPicture_))
                textures_.push_back(shadowPicture_);
        } else if (shape.name == "glw1" || shape.name == "glw4") {
            // The fog's beams (0x492980 draws with 0x8b47e8, glw1) and the
            // rain's flares (0x491bc0 with 0x8b486c, glw4).
            image::Image img;
            gfx::TextureId& id = shape.name == "glw1" ? glow1Picture_ : glow4Picture_;
            if (!decodeForGlide(shape, img, true) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id))
                textures_.push_back(id);
        } else if (shape.name == "glw2") {
            // The headlights' picture (0x4d5538 loads it into 0x8b4814,
            // which 0x41a120 draws with); clamped, its edges black.
            image::Image img;
            const gfx::TextureParams params = glideTextureParams(true);
            if (!decodeForGlide(shape, img, true) &&
                !renderer_->createTexture(img.w, img.h, img.rgba.data(), params, headlightPicture_))
                textures_.push_back(headlightPicture_);
        }
    if (marks_.empty())
        return;
    // Two quads a mark: the glw0 glows (or the sounds' diamonds), then
    // the lights' glw3 quads.
    std::vector<gfx::Vertex> vertices(marks_.size() * 8);
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < 2 * marks_.size(); ++i)
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(4 * i + k);
    if (Error err = renderer_->createMesh(vertices, indices, markMesh_))
        markMesh_ = 0;
    loadSounds();
}

// Each mark as a quad facing the camera: a light as 0x491190 draws it
// (lightQuads: colour, size and blinking at the sky's ticks as its type
// has them), glw0 at a fixed size on the screen, its colour fading with
// the depth to nothing at 256, and glw3 in perspective, both turned by a
// quarter of a turn times (x / z + 1) and only while the light's centre
// is on the screen at a depth of 1 to 256, none with the Special Effects
// option Off; a small cyan diamond for a sound.
void TrackPage::drawMarks(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right, const gfx::Vec3& up) {
    if (!markMesh_ || !marksShown_)
        return;
    std::vector<gfx::Vertex> vertices(marks_.size() * 8);
    const size_t second = marks_.size() * 4; // the glw3 quads
    for (size_t mi = 0; mi < marks_.size(); ++mi) {
        const Mark& m = marks_[mi];
        if (!m.light) {
            for (int corner = 0; corner < 4; ++corner) {
                const float x = corner == 1 || corner == 2 ? 1.0f : -1.0f, y = corner >= 2 ? 1.0f : -1.0f;
                gfx::Vertex& v = vertices[mi * 4 + size_t(corner)];
                // The diamond: the quad's corners pulled in to its edges' middles.
                for (int i = 0; i < 3; ++i)
                    v.pos[i] = m.at[size_t(i)] + (corner % 2 ? right[size_t(i)] * x : up[size_t(i)] * y) * 0.6f;
                v.color[0] = 60;
                v.color[1] = v.color[2] = v.color[3] = 255;
            }
            continue;
        }
        const gfx::Vec3 d{m.at[0] - eye[0], m.at[1] - eye[1], m.at[2] - eye[2]};
        const float z = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
        const float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        const float y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
        // On the screen: 90 degrees across, 72 up (kScreen).
        if (sfxLevel_ == 2 || z <= 0 || std::fabs(x) > kTanAcross * z || std::fabs(y) > kTanUp * z)
            continue;
        const LightQuads q = lightQuads(lightLook(static_cast<uint32_t>(m.value), skyTick_), z);
        if (!q.shown)
            continue;
        const float angle = (x / z + 1) * 0.25f * 2 * kPi;
        auto quad = [&](size_t first, float half, uint32_t colour) {
            colour = dimLight(colour, effectsIntensity());
            for (int corner = 0; corner < 4; ++corner) {
                const float a = angle + static_cast<float>(corner) * kPi / 2;
                const float cx = std::cos(a) * half, cy = std::sin(a) * half;
                gfx::Vertex& v = vertices[first + size_t(corner)];
                for (int i = 0; i < 3; ++i)
                    v.pos[i] = m.at[size_t(i)] + right[size_t(i)] * cx + up[size_t(i)] * cy;
                v.uv[0] = corner == 1 || corner == 2 ? 1.0f : 0.0f;
                v.uv[1] = corner >= 2 ? 1.0f : 0.0f;
                v.color[0] = static_cast<uint8_t>(colour >> 16);
                v.color[1] = static_cast<uint8_t>(colour >> 8);
                v.color[2] = static_cast<uint8_t>(colour);
                v.color[3] = static_cast<uint8_t>(colour >> 24);
            }
        };
        // A fixed size on the screen: the game's corners are screenHalf x
        // sx pixels across and x sy up, here screenHalf x z units, which
        // the view's own scales (kScreen) turn into the same pixels, 3 %
        // taller than wide.
        quad(mi * 4, q.screenHalf * z, q.glowColour);
        quad(second + mi * 4, q.worldHalf, q.flareColour);
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
    call.state.src = gfx::Blend::SrcAlpha;
    call.state.dst = gfx::Blend::One;
    const uint32_t marks = static_cast<uint32_t>(marks_.size());
    for (const auto& [texture, offset] : {std::pair{glowPicture_, 0u}, std::pair{glow3Picture_, marks}}) {
        if (!texture)
            continue;
        call.texture = texture;
        for (const auto& [from, to] : lights) {
            call.firstIndex = 6 * (offset + from);
            call.indexCount = 6 * (to - from);
            renderer_->draw(call);
        }
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
            break;
        }
    // The race's own banks for the weather and the hits (0x413bc0): rain
    // (entry 4, 0x41447d) and Gen3d, the 3D one as the track's (entry 2,
    // 0x414246; Gen without 3D sound).
    loadEffectBank(rainSounds_, {"GameData/Audio/SFX/rain.bnk"});
    loadEffectBank(hitSounds_, {"GameData/Audio/SFX/Gen3d.bnk", "GameData/Audio/SFX/Gen.bnk"});
}

void TrackPage::loadEffectBank(EffectBank& b, const std::vector<std::string>& names) {
    if (!b.data.empty())
        return; // the same for every track and sky
    for (const std::string& name : names)
        for (const FileRef& f : files_.files())
            if (lower(f.name) == lower(name)) {
                if (files_.read(f, b.data) || parseSoundBank(b.data, b.bank)) {
                    b.data.clear();
                    b.bank = {};
                    break;
                }
                return;
            }
}

// A sound of one of the race's banks, once (0x4182b0): the volume 0-127
// and the pan byte (0 left, 64 ahead, 127 right; soundPan).
void TrackPage::playEffect(EffectBank& b, int slot, int volume, int pan) {
    if (!mixer_ || b.data.empty() || slot < 0 || slot >= static_cast<int>(b.bank.slots.size()) ||
        !b.bank.slots[size_t(slot)])
        return;
    auto& sound = b.decoded[slot];
    if (!sound) {
        sound = std::make_shared<audio::Sound>();
        if (decodeBankSound(b.data, *b.bank.slots[size_t(slot)], *sound))
            sound = std::make_shared<audio::Sound>();
    }
    if (sound->samples.empty() || volume <= 0)
        return;
    const int p = std::clamp((pan - 64) * audio::kMaxPan / 63, -audio::kMaxPan, audio::kMaxPan);
    mixer_->play(sound, audio::SoundType::Effects, std::min(volume, 127) * audio::kMaxVolume / 127, p, false);
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

void TrackPage::tickCarEffects(uint32_t tick) {
    const int n = static_cast<int>(collision_.road.size());
    const bool dumped = !carDump_.empty();
    auto velocityOf = [&](const ComputerCar& c) -> ParticleVec {
        return dumped ? c.velocity : aiVelocity(collision_, c.ai);
    };
    // The hits between cars (0x42ac40, type 5): sparks and dust at the
    // point, the smoke started, and the hit's sound (category 5, material
    // 5) from Gen3d.bnk at the camera's distance.
    for (const AiHit& h : carHits_) {
        if (h.car >= cars_.size())
            continue;
        ComputerCar& c = cars_[h.car];
        const ParticleVec v = velocityOf(c);
        carHitParticles(particles_, h.point, c.at, v, h.strength, tick, particleRandom_);
        startCarSmoke(c.smoke, h.strength, !c.ai.racer && !c.ai.cop, h.point, c.at, c.right, c.up, c.forward);
        if (h.strength > 5) {
            char text[120];
            std::snprintf(text, sizeof text, "car hit: car %zu, strength %.1f%s", h.car, h.strength,
                          c.smoke.timer > 0 ? ", smoking" : "");
            hitText_ = text;
            // Queued at strength x 0.25 (0x5372e0), sound_play's slot by
            // that x 127 / 10; the volume only by the distance.
            const int loudness = std::min(static_cast<int>(h.strength * 0.25f * 127.0f / 10.0f), 127);
            const int slot = carHitSoundSlot(loudness, hitSoundLast_, effectSoundRandom_);
            const ParticleVec d{h.point[0] - eye_[0], h.point[1] - eye_[1], h.point[2] + eye_[2]};
            // Its azimuth from the camera (0x410d30: across its right, along
            // its forward; the track's z is the page's -z).
            const float across = d[0] * std::cos(yaw_) - d[2] * std::sin(yaw_),
                        ahead = d[0] * std::sin(yaw_) + d[2] * std::cos(yaw_);
            const uint32_t azimuth = static_cast<uint32_t>(std::lround(std::atan2(across, ahead) / (2 * kPi) * 65536.0)) & 0xffff;
            playEffect(hitSounds_, slot, hitSoundVolume(soundDistance(d)), soundPan(azimuth));
        }
    }
    carHits_.clear();
    const bool weather = variant_ >= 2;
    for (ComputerCar& c : cars_) {
        const ParticleVec v = velocityOf(c);
        // The hit's smoke. The game's spin (+0xE4) is a vector in the world,
        // turns a second; the page's car spins by its yaw, roll and pitch
        // rates about its up, forward and right.
        ParticleVec spin{};
        for (size_t k = 0; k < 3; ++k)
            spin[k] = c.up[k] * c.ai.yawRate + c.forward[k] * c.ai.rollRate + c.right[k] * c.ai.pitchRate;
        carSmokeTick(particles_, c.smoke, c.at, c.right, c.up, c.forward, v, c.ai.airTicks > 0, c.up[1], spin[0],
                     spin[2], tick, particleRandom_);
        // The wheels (car_spiked_effects 0x42a560): nothing in the air or
        // tipped. A dump's cars as it has them; the computer's from their
        // box: each wheel's point (0x4299a0) the car's place a 32nd of a
        // second on (0x537228) plus x 0.75 of the half width (0x537218),
        // the half height down and x 0.5 of the half length (0x537220)
        // along its rows; the surface under it; the weather (+0x160,
        // car_road_contact_task 0x4a08c0) the race's kind, none under
        // cover; and no skid (the computer's cars here have no tyres).
        if (!dumped && (c.ai.airTicks > 0 || c.up[1] < 0.9f))
            continue;
        const float speed = std::max(std::fabs(v[0]), std::fabs(v[2])) + 0.25f * std::min(std::fabs(v[0]), std::fabs(v[2]));
        int carWeather = c.weather;
        if (!dumped) {
            const int slice = n > 0 ? static_cast<int>(std::floor(c.slice)) % n : -1;
            carWeather = weather && weatherStarted_ && !(slice >= 0 && pointCovered(collision_.road[size_t(slice)], c.at))
                             ? weather_.setup().kind
                             : 0;
        }
        const std::array<float, 3>& half = carModels_[size_t(c.model)].halfSize;
        for (int w = 0; w < 4; ++w) {
            ParticleVec at = c.wheelAt[size_t(w)];
            int surface = c.wheelSurface[size_t(w)];
            float level = c.skid[size_t(w)];
            if (!dumped) {
                const float x = (w == 0 || w == 3 ? -0.75f : 0.75f) * half[0], z = (w < 2 ? 0.5f : -0.5f) * half[2];
                for (size_t k = 0; k < 3; ++k)
                    at[k] = c.at[k] + v[k] * 0.03125f + c.right[k] * x - c.up[k] * half[1] + c.forward[k] * z;
                int block = -1;
                size_t index = 0;
                surface = roadPolygonAt(at, block, index) ? roadSurface(track_.blocks[size_t(block)], index) : 0;
                level = 0;
            }
            if (wheelSmokes(w, level, surface, carWeather))
                tyreSmoke(particles_, w, at, v, c.right, tick, particleRandom_);
            wheelSpray(particles_, w, level, surface, carWeather, at, v, c.right, speed, tick, particleRandom_);
        }
    }
}

// The particles at the game's rate: each 64 Hz tick the hits waiting,
// then the particles' step (0x4cb2c0 runs after the cars in the "Sc64"
// list). Not more than a second's worth at once.
void TrackPage::tickParticles(uint64_t nowMs) {
    if (particlesHeld_)
        return;
    if (!particleStartMs_ || nowMs < particleStartMs_)
        particleStartMs_ = nowMs;
    const uint64_t target = (nowMs - particleStartMs_) * kGameTicksPerSecond / 1000;
    if (target > particleTick_ + kGameTicksPerSecond)
        particleTick_ = target - kGameTicksPerSecond;
    if (!weatherStarted_ && skyError_.empty()) {
        const Horizon::Weather& w = horizon_.weather;
        weather_.start(weatherSetup(w.type, w.startSlice, w.endSlice, w.fade, w.stayOn, w.fadeOff, w.stayOff, w.fadeOn),
                       {eye_[0], eye_[1], -eye_[2]}, static_cast<int>(particleTick_), weatherRandom_);
        weatherStarted_ = true;
    }
    particles_.direct3d = direct3d_;
    for (; particleTick_ < target; ++particleTick_) {
        const uint32_t tick = static_cast<uint32_t>(particleTick_);
        for (const WallHit& hit : pendingHits_)
            wallHitParticles(particles_, hit, tick, particleRandom_);
        pendingHits_.clear();
        if (computerCars_)
            tickCarEffects(tick);
        else
            carHits_.clear();
        particles_.step(tick, particleRandom_);
        // The weather's drops (0x4dd5e0 works every other tick), round the
        // camera, hidden as they come back into the box under cover.
        if (weatherStarted_) {
            const ParticleVec eye{eye_[0], eye_[1], -eye_[2]};
            const int slice = roadSliceAt();
            const int n = static_cast<int>(collision_.road.size());
            const bool covered =
                slice >= 0 && n > 0 && pointCovered(collision_.road[size_t(slice)], {eye_[0], eye_[1], -eye_[2]});
            weatherActive_ = weather_.active(slice, n, static_cast<int>(tick), sfxLevel_);
            weather_.update(static_cast<int>(tick), weatherActive_, eye, covered, weatherRandom_);
        }
    }
}

// A kind's picture (0x4cb590): SPnn of sfx.fsh by day, SNnn at night (the
// night flag 0x6fd4c8 set: 0x4cb64a jumps to the SP name when it is 0).
gfx::TextureId TrackPage::particlePicture(int kind) {
    char name[8];
    std::snprintf(name, sizeof name, "%s%02d", variant_ % 2 ? "SN" : "SP", kind % 100);
    if (auto it = particlePictures_.find(name); it != particlePictures_.end())
        return it->second;
    gfx::TextureId id = 0;
    // Point-sampled without mipmaps, as all the game's Glide pictures
    // (thrash.md, Texture filtering).
    bool additive = false;
    picture(kLaneSheet, -1, false, &additive); // loads the sheet
    for (const Shape& shape : banks_[kLaneSheet].shapes)
        if (shape.name == name) {
            image::Image img;
            if (!decodeForGlide(shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id)) {
                textures_.push_back(id);
                particlePictureSizes_[id] = {img.w, img.h};
            } else {
                id = 0;
            }
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
    const float focal = kScreen.sx; // pixels
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
    // The pictures' corners at the texels' centres, half a texel in from
    // each edge of the picture: 0x4d3f50 keeps 1 / (2 x its width) and
    // 1 / (2 x its height) and their complements in the picture's record
    // (+0x08-+0x24), and the polygon drawers (0x433840, 0x433bb0) give
    // every quad those unless its flags have bit 1, so streaks too (1/16
    // to 15/16 of the 8 x 8 sparks; 1/64 to 63/64 of the 32 x 32 smoke,
    // as e1-t8s0's THRASH list has it).
    static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (size_t q = 0; q < shown.size(); ++q) {
        Particle& p = *shown[q].p;
        const ParticleTemplate& t = *particleTemplate(p.kind);
        const float pixel = shown[q].depth / focal;
        std::array<ParticleVec, 4> corners =
            t.streak ? streakCorners(p, r, u, pixel, p.size / pixel) : pictureCorners(p, r, u, p.size, -kScreen.sy / kScreen.sx);
        // Without the z-buffer (projected_ off, as nfs-host runs the game)
        // the particles' quads and the cars go into the same depth buckets
        // (0x4c7480 and 0x4c7400: round(4 x depth), the quad's the mean of
        // its corners', the car's the mean of its box's), drawn back to
        // front with no depth test (setstate 4 0): a particle in a bucket
        // no farther than a car's covers it. The page's depth test gets the
        // same by putting such a quad, along the lines from the eye, just in
        // front of the nearest corner of each car it overlaps on the screen.
        if (!projected_) {
            const float depth = shown[q].depth;
            float to = depth;
            float px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f;
            for (const ParticleVec& c : corners) {
                const float d[3] = {c[0] - eye[0], c[1] - eye[1], -c[2] - eye[2]};
                const float z = std::max(d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2], 0.1f);
                const float x = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / z,
                            y = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / z;
                px0 = std::min(px0, x);
                px1 = std::max(px1, x);
                py0 = std::min(py0, y);
                py1 = std::max(py1, y);
            }
            for (const CarSort& c : carSort_)
                if (std::lround(4 * depth) <= std::lround(4 * c.mean) && c.nearest < to && px1 > c.x0 && px0 < c.x1 &&
                    py1 > c.y0 && py0 < c.y1)
                    to = std::max(c.nearest - 0.01f, 0.2f);
            if (to < depth) {
                const float k = to / depth;
                for (ParticleVec& c : corners) {
                    const float w[3] = {c[0], c[1], -c[2]};
                    for (size_t a = 0; a < 3; ++a)
                        c[a] = eye[a] + (w[a] - eye[a]) * k;
                    c[2] = -c[2];
                }
            }
        }
        const gfx::TextureId texture = particlePicture(p.kind);
        // Under Direct3D with the effects intensity under 1, the kinds with
        // flag bit 0 (added) dimmed (0x4cbf2f).
        const uint32_t colour = t.flags & 1 ? dimParticle(p.colour, effectsIntensity()) : p.colour;
        std::array<float, 2> inset{0.015625f, 0.015625f};
        if (auto it = particlePictureSizes_.find(texture); it != particlePictureSizes_.end())
            for (int i = 0; i < 2; ++i)
                inset[size_t(i)] = it->second[size_t(i)] > 0 ? 0.5f / static_cast<float>(it->second[size_t(i)]) : 0;
        for (int k = 0; k < 4; ++k) {
            gfx::Vertex& v = vertices[q * 4 + size_t(k)];
            v.pos[0] = corners[size_t(k)][0];
            v.pos[1] = corners[size_t(k)][1];
            v.pos[2] = -corners[size_t(k)][2];
            v.uv[0] = kUv[k][0] == 0 ? inset[0] : 1 - inset[0];
            v.uv[1] = kUv[k][1] == 0 ? inset[1] : 1 - inset[1];
            v.normal[1] = 1;
            v.color[0] = static_cast<uint8_t>(colour >> 16);
            v.color[1] = static_cast<uint8_t>(colour >> 8);
            v.color[2] = static_cast<uint8_t>(colour);
            v.color[3] = static_cast<uint8_t>(colour >> 24);
        }
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
// Fogged as the scenery: e1-t8s0's THRASH list sends the smoke's quads with
// the frame's fog state (475, as the track's).
void TrackPage::drawParticles(float fog, const std::array<float, 3>& fogColour) {
    if (particleRuns_.empty())
        return;
    gfx::DrawCall call;
    if (fog > 0) {
        call.tableFog = fog;
        call.tableFogFromDepth = true;
        call.tableFogColor = fogColour;
    }
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

// A picture of weather.fsh by name (0x4dcd20 loads DRPn and SPLn, n the
// weather type), point-sampled as the game's Glide pictures.
gfx::TextureId TrackPage::weatherPicture(const char* name) {
    static constexpr const char* kSheet = "GameData/Render/pc/weather.fsh";
    const std::string key = std::string("weather/") + name;
    if (auto it = particlePictures_.find(key); it != particlePictures_.end())
        return it->second;
    gfx::TextureId id = 0;
    bool additive = false;
    picture(kSheet, -1, false, &additive); // loads the sheet
    for (const Shape& shape : banks_[kSheet].shapes)
        if (shape.name == name) {
            image::Image img;
            if (!decodeForGlide(shape, img) && !renderer_->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id))
                textures_.push_back(id);
            else
                id = 0;
        }
    particlePictures_[key] = id;
    return id;
}

// The weather's polygons (Weather::draw) this frame, from the game's
// screen back into the page's world: each corner on the line through its
// pixel, at the polygon's depth, so the page's projection puts it on the
// same pixel and its fog is the depth's. The pass's own view is 120
// degrees across (0x4dbda0), so the drops are drawn nearer the screen's
// middle than the scene's 90-degree view would put them.
void TrackPage::prepareWeather(const gfx::Vec3& eye, const gfx::Vec3& forward, const gfx::Vec3& right,
                               const gfx::Vec3& up) {
    weatherDrops_ = weatherSplashes_ = 0;
    if (!renderer_ || !weatherStarted_ || weather_.setup().kind == 0)
        return;
    constexpr uint32_t kQuads = Weather::kDrops + Weather::kSplashes;
    if (!weatherMesh_) {
        std::vector<gfx::Vertex> vertices(kQuads * 4);
        std::vector<uint32_t> indices;
        for (uint32_t i = 0; i < kQuads; ++i)
            for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                indices.push_back(4 * i + k);
        if (renderer_->createMesh(vertices, indices, weatherMesh_))
            weatherMesh_ = 0;
        if (!weatherMesh_)
            return;
    }
    static const ViewScreen kWeatherScreen = viewScreen(0, 0, kFrameW, kFrameH, 120);
    WeatherView view;
    view.eye = {eye[0], eye[1], -eye[2]};
    view.right = {right[0], right[1], -right[2]};
    view.up = {up[0], up[1], -up[2]};
    view.forward = {forward[0], forward[1], -forward[2]};
    view.sx = kWeatherScreen.sx;
    view.sy = kWeatherScreen.sy;
    view.cx = kWeatherScreen.cx;
    view.cy = kWeatherScreen.cy;
    view.left = kWeatherScreen.left;
    view.rightEdge = kWeatherScreen.right;
    view.top = kWeatherScreen.top;
    view.bottom = kWeatherScreen.bottom;
    // A camera cut (0x4dece0: 0x4dd230 before the move, done here before
    // the drawing), then the rain's tails (0x4dd510; the page's camera is
    // never in a car).
    if (cameraCut_) {
        const int slice = roadSliceAt();
        const bool covered =
            slice >= 0 && pointCovered(collision_.road[size_t(slice)], {eye[0], eye[1], -eye[2]});
        weather_.cut(view.eye, covered, weatherRandom_);
    }
    if (weather_.setup().kind == 1)
        weather_.tails(false, cameraCut_);
    cameraCut_ = false;
    std::vector<WeatherPoly> polys;
    weather_.draw(view, weatherActive_, sfxLevel_ != 2, weatherRandom_, polys, effectsIntensity());
    // The rain's splash sounds (0x414f90): rain.bnk's sound 0-3, volume
    // 90-119, at a random azimuth.
    for (int i = weather_.takeSplashSounds(); i > 0; --i) {
        const SplashSound s = splashSound(effectSoundRandom_);
        playEffect(rainSounds_, s.sound, s.volume, soundPan(s.azimuth));
    }
    // Drops first, then splashes (the game's two lists, 0x4bc375 and
    // 0x4bc39b).
    std::stable_partition(polys.begin(), polys.end(), [](const WeatherPoly& p) { return !p.splash; });
    std::vector<gfx::Vertex> vertices(kQuads * 4);
    // The pictures' corners (0, 0), (1, 0), (1, 1), (0, 1), at the texels'
    // centres as the particles' are (inferred: no weather in a list yet).
    const bool rain = weather_.setup().kind == 1;
    const float dropW = 8, dropH = rain ? 32.0f : 8.0f; // DRP1 8 x 32, DRP2 8 x 8; SPLn 32 x 32
    size_t q = 0;
    for (const WeatherPoly& p : polys) {
        if (q >= kQuads)
            break;
        const float tw = p.splash ? 32.0f : dropW, th = p.splash ? 32.0f : dropH;
        const float u0 = 0.5f / tw, u1 = 1 - 0.5f / tw, v0 = 0.5f / th, v1 = 1 - 0.5f / th;
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        for (int k = 0; k < 4; ++k) {
            const int c = k < p.corners ? k : p.corners - 1; // a triangle's last corner twice
            const float a = (p.at[size_t(c)][0] - kScreen.cx) / kScreen.sx * p.depth,
                        b = (p.at[size_t(c)][1] - kScreen.cy) / kScreen.sy * p.depth;
            gfx::Vertex& v = vertices[q * 4 + size_t(k)];
            for (int i = 0; i < 3; ++i)
                v.pos[i] = eye[i] + forward[i] * p.depth + right[i] * a + up[i] * b;
            v.uv[0] = uv[c][0];
            v.uv[1] = uv[c][1];
            v.normal[1] = 1;
            v.color[0] = static_cast<uint8_t>(p.colour >> 16);
            v.color[1] = static_cast<uint8_t>(p.colour >> 8);
            v.color[2] = static_cast<uint8_t>(p.colour);
            v.color[3] = static_cast<uint8_t>(p.colour >> 24);
        }
        ++q;
        if (p.splash)
            ++weatherSplashes_;
        else
            ++weatherDrops_;
    }
    if (renderer_->setMeshVertices(weatherMesh_, vertices))
        weatherDrops_ = weatherSplashes_ = 0;
}

// Over everything, without a depth test (the game draws the two lists
// after its sorted scene, 0x4bc030), added by their alpha (the polygons'
// flags 1: THRASH blend 1, source alpha and one, 0x433530), fogged by
// their depth as the scene.
void TrackPage::drawWeather(float fog, const std::array<float, 3>& fogColour) {
    if (!weatherDrops_ && !weatherSplashes_)
        return;
    const bool rain = weather_.setup().kind == 1;
    gfx::DrawCall call;
    if (fog > 0) {
        call.tableFog = fog;
        call.tableFogFromDepth = true;
        call.tableFogColor = fogColour;
    }
    call.mesh = weatherMesh_;
    call.rgbSource = call.alphaSource = gfx::ColorSource::Vertex;
    call.state.cull = gfx::Cull::None;
    call.state.depthTest = call.state.depthWrite = false;
    call.state.src = gfx::Blend::SrcAlpha;
    call.state.dst = gfx::Blend::One;
    if (weatherDrops_) {
        call.texture = weatherPicture(rain ? "DRP1" : "DRP2");
        call.firstIndex = 0;
        call.indexCount = 6 * weatherDrops_;
        renderer_->draw(call);
    }
    if (weatherSplashes_) {
        call.texture = weatherPicture(rain ? "SPL1" : "SPL2");
        call.firstIndex = 6 * weatherDrops_;
        call.indexCount = 6 * weatherSplashes_;
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
    cameraCut_ = true;
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
        case 'c':
            if (ev.mods & keymod::Shift) {
                // Follow a police car (the last ordered, or the first) or stop.
                if (followCop_ >= 0) {
                    followCop_ = -1;
                } else {
                    followCop_ = orderedCop_;
                    for (size_t i = 0; followCop_ < 0 && i < cars_.size(); ++i)
                        if (cars_[i].ai.cop)
                            followCop_ = static_cast<int>(i);
                }
                cameraCut_ = true;
            } else {
                dispatchCop();
            }
            break;
        case 'z': reflections_ = !reflections_; break;
        case 'q': sfxLevel_ = (sfxLevel_ + 1) % 3; break;
        case 'j':
            // Computer cars on the road ahead; Shift+J stops or starts them.
            if (ev.mods & keymod::Shift) {
                carsMoving_ = !carsMoving_;
            } else {
                computerCars_ = !computerCars_;
                if (computerCars_ && renderer_) {
                    loadCars();
                    placeCars();
                }
            }
            break;
        case 'h':
            variant_ = (variant_ + 1) % 4;
            // The lights a race starts with (startingBeams): the camera's
            // car is a human's, the computer cars not.
            beams_ = startingBeams(variant_ % 2 == 1, variant_ >= 2, true);
            for (ComputerCar& c : cars_) {
                if (!carDump_.empty())
                    break; // the lights as the dump has them
                c.beams = c.ai.beams = startingBeams(variant_ % 2 == 1, variant_ >= 2, false);
                c.ai.flashing = false;
            }
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
        case 'y':
            if (ev.mods & keymod::Shift) {
                direct3d_ = !direct3d_;
                skyChanged_ = true;
            } else {
                fog_ = !fog_;
            }
            break;
        case 'p': hitWall((ev.mods & keymod::Shift) != 0); break;
        case 'i':
            lightIntensity_ = lightIntensity_ <= 0.25f ? 1.0f : lightIntensity_ - 0.25f;
            skyChanged_ = true; // the added clouds' colours
            break;
        case 'm':
            marksShown_ = !marksShown_;
            if (!marksShown_)
                stopSounds();
            break;
        case 'e':
            // Shift+E: the z-buffer and the projected picture (the projtex
            // option) on or off (projected_);
            // E: off, low and high beams in turn, as the game's key (0x40de50).
            // Ctrl+E: the game's test of each block against each light.
            if (ev.mods & keymod::Shift)
                projected_ = !projected_;
            else if (ev.mods & keymod::Ctrl)
                lightTest_ = !lightTest_;
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
                if (computerCars_)
                    placeCars();
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
    // Shift+C: behind and above the police car followed, along its way.
    if (followCop_ >= 0 && static_cast<size_t>(followCop_) < cars_.size() && computerCars_) {
        // Along the road its way (it may stand across it).
        const ComputerCar& c = cars_[size_t(followCop_)];
        const auto& rf = collision_.road[size_t(c.ai.slice) % collision_.road.size()].forward;
        const float flat = std::max(std::hypot(rf[0], rf[2]), 1e-3f);
        const float fx = rf[0] * static_cast<float>(c.way) / flat, fz = rf[2] * static_cast<float>(c.way) / flat;
        // Stopping a racer: nearer, between the two, to see the officer.
        std::array<float, 3> at = c.at;
        float back = 22, up = 11;
        if (c.ai.copState == 6 && c.ai.chasing >= 0 && static_cast<size_t>(c.ai.chasing) < cars_.size()) {
            for (size_t k = 0; k < 3; ++k)
                at[k] = (at[k] + cars_[size_t(c.ai.chasing)].at[k]) * 0.5f;
            back = 11;
            up = 5;
        }
        float dirX = fx, dirZ = fz;
        // A strip out: past it, looking back at it the way its racer comes.
        if (c.ai.stripOn && c.ai.watching >= 0 && static_cast<size_t>(c.ai.watching) < cars_.size()) {
            const auto quad = aiStripQuad(collision_.road[size_t(c.ai.stripSlice) % collision_.road.size()], c.ai.stripLane);
            for (size_t k = 0; k < 3; ++k)
                at[k] = (quad[0][k] + quad[1][k]) * 0.5f;
            const float w = static_cast<float>(-cars_[size_t(c.ai.watching)].way);
            const auto& sf = collision_.road[size_t(c.ai.stripSlice) % collision_.road.size()].forward;
            const float sl = std::max(std::hypot(sf[0], sf[2]), 1e-3f);
            dirX = sf[0] * w / sl;
            dirZ = sf[2] * w / sl;
            back = 14;
            up = 6;
        }
        eye_ = {at[0] - dirX * back, at[1] + up, -(at[2] - dirZ * back)};
        yaw_ = std::atan2(dirX, dirZ);
        pitch_ = -0.36f;
    }
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
        gfx::multiply(gfx::perspective(kFieldOfView, kTanAcross / kTanUp, 0.5f, 6000.0f),
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
    fogAt(roadSliceAt(), fog, fogDensity); // by the camera's road slice (0x478e10)
    if (!fog_)
        fogDensity = 0;
    tickSky(nowMs);
    updateSky(forward, right);
    updateHorizon(forward);
    skyBlack_ = skyError_.empty() &&
                skyBlack(horizon_, std::atoi(tracks_[index_].substr(3).c_str()), roadSliceAt());
    const gfx::Vec3 up{-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_), std::sin(pitch_) * std::cos(yaw_)};
    tickParticles(nowMs);

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
        // Moving objects at the key for now, between it and the next: as
        // nfs3.exe (0x40f4b0), a key every `delay` ticks (1-400, else 6),
        // round keys - 1 of them (the last is the first again), blended
        // towards the next by the ticks since.
        movingFrame_.clear();
        if (objects_)
            for (size_t mi = 0; mi < moving_.size(); ++mi) {
                const Moving& m = moving_[mi];
                if ((m.block >= 0 && !shown[size_t(m.block)]) || m.keys.size() < 2)
                    continue;
                const int delay = m.keyDelay >= 1 && m.keyDelay <= 400 ? m.keyDelay : 6;
                // RELUVA_NFS_TRACK_STEP=<g_step> holds them at the game's
                // step (0x7d3684), for comparing with nfs-host's frames.
                const float ticks = fixedStep_ >= 0 ? static_cast<float>(fixedStep_)
                                                    : static_cast<float>(nowMs % 100000000) * kGameTicksPerSecond / 1000.0f;
                const float t = ticks / static_cast<float>(delay);
                const size_t k = static_cast<size_t>(t) % (m.keys.size() - 1);
                movingFrame_.push_back(
                    {keyMatrix(m.keys[k], m.keys[k + 1], t - std::floor(t)), m.batches, static_cast<int>(mi)});
            }
        cutIndices_.clear();
        if (!allBlocks_) {
            cutFar(solid, eye, forward);
            cutFar(lanes, eye, forward);
            for (MovingFrame& m : movingFrame_)
                cutFar(m.batches, eye, forward, &m.model);
        }
        if (!cutIndices_.empty()) {
            if (cutMesh_ ? renderer_->setMeshIndices(cutMesh_, cutIndices_)
                         : renderer_->createIndexedMesh(mesh_, cutIndices_, cutMesh_)) {
                // Without it, drawn whole.
                for (auto* list : {&solid, &lanes})
                    for (Batch& b : *list)
                        b.cut = false;
                for (MovingFrame& m : movingFrame_)
                    for (Batch& b : m.batches)
                        b.cut = false;
            }
        }
        moveCars(dt);
        prepareCars(eye, forward, right, up, fogDensity);
        prepareHeadlights(here, eye, forward);
    }
    // After the cars: their sort depths (carSort_) place the particles.
    prepareParticles(eye, forward, right, up);
    prepareWeather(eye, forward, right, up);
    // The clear colour: the sky's top (THRASH state 3, 0x47a4c0), black
    // where the game draws no sky, and without a sky the fog's.
    Horizon::Rgb clear = fog;
    if (skyError_.empty())
        clear = skyBlack_ ? Horizon::Rgb{0, 0, 0} : horizon_.skyTop;
    renderer_->beginFrame(viewProj, eye,
                          {static_cast<uint8_t>(clear[0]), static_cast<uint8_t>(clear[1]), static_cast<uint8_t>(clear[2])});
    // Fog as the game has the card draw it (thrash.md, Fog): its curve over
    // the depth, with the values it sends for the scenery and for the sky.
    const bool fogged = fog_ && !direct3d_ && skyError_.empty();
    const float sceneryFog = fogged ? static_cast<float>(sceneryFogValue(static_cast<int>(fogDensity + 0.5f))) : 0;
    const float skyFog = fogged ? static_cast<float>(skyFogValue(horizon_, static_cast<int>(fogDensity + 0.5f))) : 0;
    auto fogDraw = [&](gfx::DrawCall& call, float value) {
        call.tableFog = value;
        call.tableFogFromDepth = true;
        call.tableFogColor = {fog[0] / 255.0f, fog[1] / 255.0f, fog[2] / 255.0f};
    };

    // The sky first, around the camera, behind everything, as the game
    // draws it (0x47a880): the sky's shading (unfogged), the sun and
    // clouds, then the earth's shading and the horizon picture, fogged as
    // the sky; none of it where the game clears to black.
    if (skyMesh_ && !skyBlack_) {
        gfx::DrawCall sky;
        sky.mesh = skyMesh_;
        sky.followCamera = true;
        sky.rgbSource = gfx::ColorSource::Vertex;
        sky.state.cull = gfx::Cull::None;
        sky.state.depthTest = sky.state.depthWrite = false;
        gfx::DrawCall row = sky;
        row.mesh = horizonMesh_;
        if (horizonMesh_) {
            row.firstIndex = 0;
            row.indexCount = 8 * 6;
            renderer_->draw(row);
        }
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
            bolts.alphaSource = gfx::ColorSource::Vertex;
            bolts.state.src = gfx::Blend::SrcAlpha;
            bolts.state.dst = gfx::Blend::One;
            fogDraw(bolts, skyFog);
            renderer_->draw(bolts);
        }
        // INFRONT: the sun over the clouds, still behind the horizon.
        if (sunInFront_)
            drawSun();
        if (horizonMesh_) {
            fogDraw(row, skyFog);
            row.firstIndex = 8 * 6;
            renderer_->draw(row);
            if (horizonPicture_) {
                row.firstIndex = 16 * 6;
                row.texture = horizonPicture_;
                row.state.src = gfx::Blend::SrcAlpha;
                row.state.dst = gfx::Blend::OneMinusSrcAlpha;
                renderer_->draw(row);
            }
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
        // Moving objects as placed this frame, cut at the reach.
        auto movingPass = [&](Pass which) {
            for (const MovingFrame& m : movingFrame_) {
                call.model = m.model;
                pass(m.batches, which);
            }
            call.model = gfx::identity();
            call.mesh = mesh_;
        };
        // Solid first, then the lane markings pulled onto the road, then
        // additive pictures (lights, glows) over everything.
        pass(solid, kSolid);
        movingPass(kSolid);
        // The computer cars, lit as the game lights them (prepareCars).
        drawCars(call);
        call.state.depthBias = true;
        pass(lanes, kSolid);
        call.state.depthBias = false;
        drawShadows(call);
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
                // Fogged as the scenery: the game's raised corners are the
                // polygon's own colours, fogged with it (black here, as this
                // is added over the fogged polygon); the projected picture
                // is a polygon of its own, which Glide mixes towards the fog
                // colour before adding it. Until November 2026 neither was
                // fogged, so by night the lit scenery lacked the fog's
                // darkening (d5-wheel: 25-35 % too bright at 43 m in
                // Hometown's density 60).
                if (sceneryFog > 0) {
                    fogDraw(light, sceneryFog);
                    if (!run.picture)
                        light.tableFogColor = {0, 0, 0};
                }
                light.texture = run.picture || textured_ ? run.texture : 0;
                light.alphaSource = run.picture ? gfx::ColorSource::Vertex : gfx::ColorSource::Constant;
                light.state.src = run.picture ? gfx::Blend::SrcAlpha : gfx::Blend::One;
                light.state.dst = gfx::Blend::One;
                light.state.alphaTest = run.picture ? gfx::AlphaTest::None : gfx::AlphaTest::Ge128;
                light.state.cull = cullBack_ && !run.twoSided ? backFaces : gfx::Cull::None;
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

    drawMarks(eye, forward, right, up);
    // The cars' glows (prepareCars), added over the scene but not through
    // it, as the track's lights are.
    if (carGlowMesh_ && carGlowQuads_ && glow3Picture_) {
        gfx::DrawCall glow;
        glow.mesh = carGlowMesh_;
        glow.rgbSource = glow.alphaSource = gfx::ColorSource::Vertex;
        glow.texture = glow3Picture_;
        glow.state.cull = gfx::Cull::None;
        glow.state.depthWrite = false;
        glow.state.src = gfx::Blend::SrcAlpha;
        glow.state.dst = gfx::Blend::One;
        glow.firstIndex = 0;
        glow.indexCount = carGlowQuads_ * 6;
        // Fogged as the scenery (the game sends them with the frame's fog
        // state, d5-fog's THRASH list: fog 75 on the glows and flares as on
        // the car): Glide mixes each pixel towards the fog colour before
        // adding it, so in fog a glow's dark edges add the fog's colour.
        if (sceneryFog > 0)
            fogDraw(glow, sceneryFog);
        renderer_->draw(glow);
    }
    // The rain's flares on the road (glw4) and the fog's beams (glw1),
    // added the same way.
    for (const auto& [mesh, quads, texture] :
         {std::tuple{carFlareMesh_, carFlareQuads_, glow4Picture_}, std::tuple{carBeamMesh_, carBeamQuads_, glow1Picture_}}) {
        if (!mesh || !quads || !texture)
            continue;
        gfx::DrawCall add;
        add.mesh = mesh;
        add.rgbSource = add.alphaSource = gfx::ColorSource::Vertex;
        add.texture = texture;
        add.state.cull = gfx::Cull::None;
        add.state.depthWrite = false;
        add.state.depthBias = true;
        add.state.src = gfx::Blend::SrcAlpha;
        add.state.dst = gfx::Blend::One;
        add.firstIndex = 0;
        add.indexCount = quads * 6;
        if (sceneryFog > 0)
            fogDraw(add, sceneryFog);
        renderer_->draw(add);
    }
    if (marksShown_)
        updateSounds(eye, right);
    drawParticles(sceneryFog, {fog[0] / 255.0f, fog[1] / 255.0f, fog[2] / 255.0f});
    drawWeather(sceneryFog, {fog[0] / 255.0f, fog[1] / 255.0f, fog[2] / 255.0f});

    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(index_ + 1) + "/" + std::to_string(tracks_.size()) +
                                      "  " + tracks_[index_] + "  " + trackName(tracks_[index_]) + "  " +
                                      kVariantNames[variant_] + (direct3d_ ? "  (Direct3D look)" : "")};
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
            line += "  effects intensity " + std::to_string(static_cast<int>(lightIntensity_ * 100)) + "%" +
                    (direct3d_ ? "" : " (Direct3D only)");
        if (sfxLevel_ != 0)
            line += sfxLevel_ == 1 ? "  effects low" : "  effects off";
        if (!reflections_)
            line += "  no reflections";
        if (mirrored_)
            line += "  mirrored";
        if (computerCars_) {
            int braking = 0, flashing = 0;
            for (const ComputerCar& c : cars_) {
                braking += c.ai.braking ? 1 : 0;
                flashing += c.ai.flashing ? 1 : 0;
            }
            line += carError_.empty() ? "  " + std::to_string(cars_.size()) + " computer cars" + (carsMoving_ ? "" : " (stopped)") +
                                            (braking ? ", " + std::to_string(braking) + " braking" : "") +
                                            (flashing ? ", " + std::to_string(flashing) + " flashing" : "")
                                      : "  " + carError_;
        }
        if (variant_ % 2 == 1) {
            static constexpr const char* kBeams[] = {"headlights off", "low beams", "high beams"};
            line += std::string("  ") + kBeams[beams_] + (beams_ && !projected_ ? " (not projected)" : "") +
                    (lightTest_ ? "" : " (no light test)");
        }
        lines.push_back(line);
        if (computerCars_ && carDump_.empty())
            for (const ComputerCar& c : cars_) {
                if (!c.ai.cop)
                    continue;
                // The police car's state (+0x70C) and its last radio call,
                // for 5 s (the stand-in plays no speech).
                static constexpr const char* kStates[] = {"",
                                                          "patrol",
                                                          "to its order",
                                                          "to the roadside",
                                                          "stopped at the roadside",
                                                          "chasing",
                                                          "stopping a racer",
                                                          "run with the siren",
                                                          "to a roadblock",
                                                          "slowing for it",
                                                          "at the roadblock",
                                                          "across the road"};
                std::string cop = std::string("police: ") + kStates[std::clamp(c.ai.copState, 0, 11)];
                if (c.ai.reversing)
                    cop += ", reversing";
                if (c.ai.chasing >= 0 && static_cast<size_t>(c.ai.chasing) < cars_.size()) {
                    // Its target: caught by a strip (+0x621 bit 0x40), pulling
                    // over (5) or stopped (6).
                    const AiCar& t = cars_[size_t(c.ai.chasing)].ai;
                    if (t.spiked)
                        cop += ", its racer ran over the strip";
                    if (t.pursuit == 5)
                        cop += ", the racer pulling over";
                    else if (t.pursuit == 6)
                        cop += ", the racer stopped";
                }
                if (c.ai.stripOn)
                    cop += ", strip out at slice " + std::to_string(c.ai.stripSlice);
                int tickets = 0;
                for (const ComputerCar& o : cars_)
                    tickets += o.ai.tickets;
                if (tickets)
                    cop += ", " + std::to_string(tickets) + " ticket" + (tickets > 1 ? "s" : "");
                if (c.ai.radio && aiStep_ - c.ai.radioStep < 5 * kGameTicksPerSecond)
                    cop += std::string("  radio: ") + aiRadioCall(c.ai.radio);
                lines.push_back(cop);
            }
        if (!hitText_.empty())
            lines.push_back(hitText_ + "  (" + std::to_string(particles_.live()) + " particles)");
        if (!skyError_.empty())
            lines.push_back(skyError_);
        if (!collisionError_.empty())
            lines.push_back(collisionError_);
    }
    lines.push_back("PgUp/PgDn: track  WASD/arrows/drag: move  N/B: block  1-3: detail  H: sky  I: effects intensity  P: wall hit  E: headlights  J: cars  C: police orders (Shift: follow)  Z: reflections  Q: effects  U: mirror  T V L O G X F Y M: toggles (Shift+Y: Direct3D look)");
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
