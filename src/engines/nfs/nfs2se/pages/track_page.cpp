// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/pages/track_page.h"

#include "common/fs.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/nfs2se/pages/chroma_key.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace reluva::nfs::nfs2se {

namespace {

// QuadMesh's texture numbers from here on are animated textures': this
// plus their place in the .col's table.
constexpr int kAnimated = 1 << 20;
// And painted lines': this plus the number of their lin picture.
constexpr int kLine = 1 << 21;

// The game's world is left-handed and the renderer's right-handed: the
// view mirrored left to right, as the hosted game shows it, the road's
// across vector to the right (col.md, The virtual road).
const gfx::Mat4 kMirror = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr const char *kLevelNames[] = {"low", "medium", "high"};

// The file beside `ref` named `name` (in its folder, any case), or null.
const FileRef *beside(const std::vector<FileRef> &all, const FileRef &ref, const std::string &name) {
    const std::string folder = fs::toLower(std::filesystem::path(ref.name).parent_path().generic_string());
    for (const FileRef &f : all)
        if (f.inner.empty() && fs::toLower(f.name) == folder + "/" + fs::toLower(name))
            return &f;
    return nullptr;
}

// Where a painted line's stripes lie across its quad, 0 at the record's
// vertex and 1 at the vertex moved across the road (the right), and along
// it, 0 at this record and 1 at the next; by type, 5 to 9 as 0 to 4
// (0x448a24). The 5/16 inset is the drawers' (0x449ff0); the double and
// mixed lines' stripes along the edges *(inferred)*.
struct Stripe {
    float from;
    float to;
    float start;
    bool second; // the second picture (0x4c4134)
};
const std::vector<Stripe> &stripes(int kind) {
    static const std::vector<Stripe> kStripes[5] = {
        {{5 / 16.0f, 11 / 16.0f, 0, false}},
        {{5 / 16.0f, 11 / 16.0f, 0.5f, false}},
        {{0, 5 / 16.0f, 0, false}, {11 / 16.0f, 1, 0, true}},
        {{11 / 16.0f, 1, 0, false}, {0, 5 / 16.0f, 0.5f, true}},
        {{11 / 16.0f, 1, 0.5f, false}, {0, 5 / 16.0f, 0, true}},
    };
    return kStripes[kind];
}

// About the distance, as the game reckons it for its placed sounds
// (0x42df30): the largest of each pair plus a quarter of the other.
float roughDistance(float dx, float dy, float dz) {
    const float ax = std::abs(dx);
    const float az = std::abs(dz);
    const float across = std::max(ax, az) + std::min(ax, az) / 4;
    const float ay = std::abs(dy);
    return std::max(across, ay) + std::min(across, ay) / 4;
}

} // namespace

TrackPage::TrackPage(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {
    // The tracks the game races on, not TmpTrk's stand-in.
    for (const FileRef &f : files.withExtensions({".trk"}))
        if (fs::toLower(f.name).find("tracks/se/") != std::string::npos)
            _tracks.push_back(f);
}

TrackPage::~TrackPage() {
    release();
}

void TrackPage::activate(System &sys) {
    if (Error err = sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _unavailable = err.message;
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    const char *view = std::getenv("RELUVA_NFS2SE_TRACK_VIEW");
    int track = _index;
    int point = 0;
    int level = -1;
    const int got = view ? std::sscanf(view, "%d %d %d", &track, &point, &level) : 0;
    if (got >= 1 && !_tracks.empty())
        _index = std::clamp(track, 0, static_cast<int>(_tracks.size()) - 1);
    if (level >= 0 && level <= 2)
        _level = level;
    load(_index);
    if (got >= 2 && !_collision.road.empty())
        _point = std::clamp(point, 0, static_cast<int>(_collision.road.size()) - 1);
}

void TrackPage::deactivate() {
    release();
    stopSounds();
    _renderer = nullptr;
}

void TrackPage::release() {
    if (_renderer) {
        for (gfx::MeshId m : {_mesh, _skyMesh, _domeMesh})
            if (m)
                _renderer->destroyMesh(m);
        if (_domeTexture)
            _renderer->destroyTexture(_domeTexture);
        for (const auto *list : {&_textures, &_lines})
            for (gfx::TextureId t : *list)
                if (t)
                    _renderer->destroyTexture(t);
    }
    _mesh = 0;
    _skyMesh = 0;
    _domeMesh = 0;
    _domeTexture = 0;
    _domeIndices = 0;
    _textures.clear();
    _lines.clear();
    _runs.clear();
}

void TrackPage::load(int index) {
    release();
    _index = index;
    _error.clear();
    _track = {};
    _collision = {};
    _hasSky = false;
    _point = 0;
    _look = 0;
    const FileRef &ref = _tracks[size_t(index)];
    std::vector<uint8_t> data;
    if (Error err = _files.read(ref, data)) {
        _error = err.message;
        return;
    }
    if (Error err = parseTrack(data, _track)) {
        _error = err.message;
        return;
    }
    // trNN.trk's .col, its 3dfx pictures (trNN0.qfs) and sky (3trNN.hrz).
    const std::string stem = std::filesystem::path(ref.name).stem().string();
    if (const FileRef *col = beside(_files.files(), ref, stem + ".col")) {
        if (Error err = _files.read(*col, data))
            _error = err.message;
        else if (Error err2 = parseCollision(data, _collision))
            _error = err2.message;
    } else {
        _error = "no " + stem + ".col beside it: no textures or road";
    }
    if (const FileRef *qfs = beside(_files.files(), ref, stem + "0.qfs")) {
        ShapeBank bank;
        if (!_files.read(*qfs, data) && !parseShapeBank(std::move(data), bank))
            for (const Shape &s : bank.shapes) {
                image::Image img;
                gfx::TextureId id = 0;
                if (!isPaletteType(s.type) && !decodeShape(s, img)) {
                    clearChromaKey(s, img);
                    _renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), id);
                }
                _textures.push_back(id);
            }
    }
    // The painted lines' pictures, lin0 to lin7, by their number.
    for (const FileRef &f : _files.withExtensions({".fsh"})) {
        if (fs::toLower(f.name).find("render/pc/dtexture.fsh") == std::string::npos)
            continue;
        ShapeBank bank;
        if (_files.read(f, data) || parseShapeBank(std::move(data), bank))
            break;
        _lines.assign(8, 0);
        for (const Shape &s : bank.shapes) {
            image::Image img;
            if (s.name.size() == 4 && s.name.compare(0, 3, "lin") == 0 && s.name[3] >= '0' && s.name[3] <= '7' &&
                !decodeShape(s, img))
                _renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(),
                                         _lines[size_t(s.name[3] - '0')]);
        }
        break;
    }
    loadSounds(fs::toLower(stem));
    _trackNumber = stem.size() > 2 ? std::atoi(stem.c_str() + 2) : 0;
    if (const FileRef *hrz = beside(_files.files(), ref, "3" + stem + ".hrz"))
        if (!_files.read(*hrz, data))
            _hasSky = !parseSky(std::string(data.begin(), data.end()), _sky);
    // The cloud dome's picture, CLDn of Tracks/SE/sky.fsh, on tracks 2 to 7
    // only (a table, NFS2SEA.EXE 0x4eda10).
    if (_hasSky && _trackNumber >= 2 && _trackNumber <= 7)
        if (const FileRef *fsh = beside(_files.files(), ref, "sky.fsh")) {
            ShapeBank bank;
            if (!_files.read(*fsh, data) && !parseShapeBank(std::move(data), bank))
                for (const Shape &s : bank.shapes) {
                    image::Image img;
                    if (fs::toLower(s.name) == "cld" + std::to_string(_trackNumber) && !decodeShape(s, img))
                        _renderer->createTexture(img.w, img.h, img.rgba.data(), glideTextureParams(), _domeTexture);
                }
        }
    buildDome();
    build();
}

void TrackPage::build() {
    if (_renderer && _mesh)
        _renderer->destroyMesh(_mesh);
    _mesh = 0;
    QuadMesh quads;
    // A polygon's texture is a place in the .col's table, which gives its
    // picture's place in the .qfs; an animated one's quads go apart, under
    // kAnimated + its place, their picture chosen as they're drawn.
    auto picture = [&](uint16_t texture) {
        if (texture >= _collision.textures.size())
            return -1;
        const Collision::Texture &t = _collision.textures[texture];
        return (t.flags & Collision::kAnimated) && t.frames > 1 ? kAnimated + texture : static_cast<int>(t.picture);
    };
    auto add = [&](const std::vector<Quad> &list, const std::vector<Point> &vertices, const Point &at) {
        for (const Quad &q : list) {
            std::array<Point, 4> corners;
            for (int k = 0; k < 4; ++k)
                for (int c = 0; c < 3; ++c)
                    corners[size_t(k)][size_t(c)] = vertices[q.vertices[size_t(k)]][size_t(c)] + at[size_t(c)];
            const uint8_t orientation =
                q.texture < _collision.textures.size() ? _collision.textures[q.texture].orientation : 0;
            const int texture = picture(q.texture);
            // The 3dfx program's greys; an animated picture's all 7's (0x4468d7).
            std::array<uint8_t, 4> greys{255, 255, 255, 255};
            if (_shaded)
                for (int k = 0; k < 4; ++k)
                    greys[size_t(k)] = texture >= kAnimated ? cornerGrey(Quad{0, 0xffff, {}}, 0) : cornerGrey(q, k);
            quads.add(texture, corners, pictureCorners(orientation), greys);
        }
    };
    auto place = [&](const std::vector<Mesh> &meshes, const std::vector<Placement> &placements) {
        for (const Placement &p : placements) {
            // An animated one where its animation starts.
            const Point at = p.type == 3 && !p.keys.empty() ? p.keys[0].position : p.position;
            add(meshes[p.mesh].quads, meshes[p.mesh].vertices, at);
        }
    };
    for (const TrackBlock &b : _track.blocks) {
        const TrackBlock::Level &level = b.levels[size_t(_level)];
        add(level.first, b.vertices, {0, 0, 0});
        add(level.second, b.vertices, {0, 0, 0});
        if (_objects)
            place(b.meshes, b.placements);
    }
    if (_objects)
        place(_collision.meshes, _collision.placements);
    if (_painted)
        addLines(quads);
    _quads = quads.quads();
    if (_renderer)
        if (Error err = quads.build(*_renderer, _mesh, _runs))
            _error = err.message;
}

// The painted lines (extrablock 9; trk.md): a quad from each record to
// the next, from its vertex to that moved by the road point's across
// vector, each end pushed out along the road by half its forward vector
// (0x4353fc); the stripes as its type draws them close up. Lifted 2 cm
// off the road, where the game draws them after it.
void TrackPage::addLines(QuadMesh &quads) const {
    const auto &road = _collision.road;
    std::vector<int> first(_track.blocks.size(), -1);
    for (size_t i = 0; i < road.size(); ++i)
        if (road[i].block < first.size() && first[road[i].block] < 0)
            first[road[i].block] = static_cast<int>(i);
    // Their pictures' numbers: by the colour and the vertex's place
    // (0x4c412c), the second stripe's two on from the first's.
    constexpr int kPictures[10] = {2, 3, 4, 5, 6, 7, 4, 5, 2, 3};
    for (size_t b = 0; b < _track.blocks.size(); ++b) {
        const TrackBlock &block = _track.blocks[b];
        if (first[b] < 0)
            continue;
        struct End {
            Point left, right;
        };
        auto end = [&](const RoadLine &l, float push, End &out) {
            const size_t at = size_t(first[b]) + l.roadPoint;
            if (at >= road.size() || l.vertex >= block.vertices.size())
                return false;
            const Collision::RoadPoint &r = road[at];
            for (int c = 0; c < 3; ++c) {
                const float base = block.vertices[l.vertex][size_t(c)] + push * r.forward[size_t(c)] / 512.0f +
                                   0.02f * r.up[size_t(c)] / 127.0f;
                out.left[size_t(c)] = base;
                out.right[size_t(c)] = base + r.across[size_t(c)] / 256.0f;
            }
            return true;
        };
        const auto &lines = block.roadLines;
        for (size_t i = 0; i + 1 < lines.size(); ++i) {
            const RoadLine &line = lines[i];
            End from;
            End to;
            if (line.type > 9 || !end(line, -1, from) || !end(lines[i + 1], 1, to))
                continue;
            const int colour = line.type > 4 ? 1 : 0;
            const int z = static_cast<int>(std::floor(block.vertices[line.vertex][2] * 256));
            const int picture = kPictures[(z & 6) | colour];
            auto at = [&](float across, float along) {
                Point p;
                for (int c = 0; c < 3; ++c) {
                    const float a = from.left[size_t(c)] + (from.right[size_t(c)] - from.left[size_t(c)]) * across;
                    const float e = to.left[size_t(c)] + (to.right[size_t(c)] - to.left[size_t(c)]) * across;
                    p[size_t(c)] = a + (e - a) * along;
                }
                return p;
            };
            for (const Stripe &s : stripes(line.type % 5))
                quads.add(kLine + (s.second ? kPictures[picture + 2] : picture),
                          {at(s.from, s.start), at(s.to, s.start), at(s.to, 1), at(s.from, 1)}, {0, 1, 2, 3});
        }
    }
}

// The track's own bank of placed sounds: TRAMBnn.BNK for trNN.
void TrackPage::loadSounds(const std::string &stem) {
    stopSounds();
    _soundData.clear();
    _soundBank = {};
    _sounds.clear();
    if (!_mixer || stem.size() < 4)
        return;
    const std::string name = "audio/sfx/pc/tramb" + stem.substr(2) + ".bnk";
    for (const FileRef &f : _files.withExtensions({".bnk"})) {
        if (fs::toLower(f.name).find(name) == std::string::npos)
            continue;
        if (_files.read(f, _soundData) || parseSoundBank(_soundData, _soundBank)) {
            _soundData.clear();
            _soundBank = {};
        }
        break;
    }
}

void TrackPage::stopSounds() {
    if (_mixer && _loop != audio::kNoSound)
        _mixer->stop(_loop);
    _loop = audio::kNoSound;
    _loopSound = -1;
}

// The placed sounds as the game plays them (0x434030): the volume 2^18 /
// the distance squared, at most 127; the pan by how far to the side. One
// loop at a time, on its own voice, the game's the first it comes to, this
// page's the nearest; a one-shot when the program's frame count, in
// eighths of 64, comes round to a number picked at random 0 to 7, picked
// again after it plays. The count steps 30 times a second here.
void TrackPage::updateSounds(const gfx::Vec3 &eye, const gfx::Vec3 &right) {
    if (!_mixer || _soundData.empty())
        return;
    auto decoded = [&](int slot) -> std::shared_ptr<audio::Sound> {
        if (slot < 0 || slot >= static_cast<int>(_soundBank.slots.size()) || !_soundBank.slots[size_t(slot)])
            return nullptr;
        auto &sound = _sounds[slot];
        if (!sound) {
            sound = std::make_shared<audio::Sound>();
            if (decodeBankSound(_soundData, *_soundBank.slots[size_t(slot)], *sound))
                sound = std::make_shared<audio::Sound>();
        }
        return sound->samples.empty() ? nullptr : sound;
    };
    const bool oneShotDue = ((_soundFrame & 63) >> 3) == static_cast<uint32_t>(_oneShotPhase);
    bool oneShotPlayed = false;
    int loop = -1;
    float loopDistance = 0;
    int loopVolume = 0;
    int loopPan = 0;
    for (const TrackBlock &b : _track.blocks)
        for (const AmbientSound &s : b.sounds) {
            const float dx = s.position[0] - eye[0];
            const float dy = s.position[1] - eye[1];
            const float dz = s.position[2] - eye[2];
            const float d = roughDistance(dx, dy, dz);
            const int volume = d > 0 ? static_cast<int>(std::min(127.0f, 262144.0f / (d * d))) : 127;
            if (volume < 1)
                continue;
            const float side = d > 0 ? std::clamp((dx * right[0] + dz * right[2]) / d, -1.0f, 1.0f) : 0;
            const int pan = static_cast<int>(side * audio::kMaxPan);
            if (s.random == 1) {
                if (oneShotDue && !oneShotPlayed)
                    if (auto sound = decoded(s.sound)) {
                        _mixer->play(sound, audio::SoundType::kEffects, volume * audio::kMaxVolume / 127, pan, false);
                        oneShotPlayed = true;
                    }
            } else if (loop < 0 || d < loopDistance) {
                loop = s.sound;
                loopDistance = d;
                loopVolume = volume * audio::kMaxVolume / 127;
                loopPan = pan;
            }
        }
    if (oneShotPlayed) {
        _soundRandom = _soundRandom * 1103515245u + 12345u;
        _oneShotPhase = static_cast<int>(_soundRandom >> 16 & 7);
    }
    if (loop != _loopSound) {
        stopSounds();
        if (auto sound = decoded(loop)) {
            _loop = _mixer->play(sound, audio::SoundType::kEffects, loopVolume, loopPan, true);
            _loopSound = loop;
        }
    } else if (_loop != audio::kNoSound) {
        _mixer->setVolume(_loop, loopVolume);
        _mixer->setPan(_loop, loopPan);
    }
}

// The 3dfx program's cloud dome (0x42be50): a 13 × 13 grid of points
// round the eye, x and z -1,000 to 800 by 150, at the file's offset +
// its height × cos(2π r / 4,000), r the distance from the middle; its
// picture once to each cell.
void TrackPage::buildDome() {
    if (!_renderer || !_domeTexture)
        return;
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    constexpr float kPi = 3.14159265f;
    auto point = [&](float x, float z, float u, float v) {
        gfx::Vertex out{};
        const float r = std::sqrt(x * x + z * z);
        out.pos[0] = x;
        out.pos[1] = static_cast<float>(_sky.cloudDomeOffset) +
                     static_cast<float>(_sky.cloudDomeHeight) * std::cos(2 * kPi * r / 4000);
        out.pos[2] = z;
        out.uv[0] = u;
        out.uv[1] = v;
        out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
        vertices.push_back(out);
    };
    for (int j = 0; j < 12; ++j)
        for (int i = 0; i < 12; ++i) {
            const auto base = static_cast<uint32_t>(vertices.size());
            const float x = -1000.0f + 150.0f * static_cast<float>(j);
            const float z = -1000.0f + 150.0f * static_cast<float>(i);
            point(x, z, 0, 0);
            point(x + 150, z, 1, 0);
            point(x + 150, z + 150, 1, 1);
            point(x, z + 150, 0, 1);
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    if (!_renderer->createMesh(vertices, indices, _domeMesh))
        _domeIndices = static_cast<uint32_t>(indices.size());
}

// The 3dfx program's sky in its main view (0x42c010; hrz.md, The 3dfx
// sky): a ring of 16 segments round the eye, turned with the view but
// not moved with it, each point drawn as if at the flat distance ahead.
// Bottom to top: the earth's two colours to the midpoint, the ring's
// colours there to the sky's top, and the horizon's pictures, the track's
// first 8, between their two heights; the cloud dome after the shaded
// sky. None of it tests or writes depth. Fogged as the program fogs it:
// the shaded sky as if 1 unit away, the earth and pictures 100 (1 on
// track 0), the dome by its depth.
void TrackPage::drawSky(const gfx::Vec3 &eye, const gfx::Vec3 &ahead, const GlideFog &fog) {
    if (!_hasSky || _sky.kind != Sky::kGlide || _sky.blackHorizon || !_skyShown)
        return;
    auto cross = [](const gfx::Vec3 &a, const gfx::Vec3 &b) {
        return gfx::Vec3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    auto dot = [](const gfx::Vec3 &a, const gfx::Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const gfx::Vec3 &f = ahead;
    gfx::Vec3 side = cross(f, {0, 1, 0});
    const float length = std::sqrt(dot(side, side));
    if (length <= 0)
        return;
    for (float &c : side)
        c /= length;
    const gfx::Vec3 up = cross(side, f);
    constexpr float kPi = 3.14159265f;
    const auto radius = static_cast<float>(_sky.radius);
    const auto flat = static_cast<float>(_sky.flatDistance);
    const auto base = static_cast<float>(_sky.base);
    const auto height = static_cast<float>(_sky.height);
    // The program keeps these heights as fractions of the shaded area's (0x42d07b).
    const float fraction = height > 0 ? std::floor(65536 / height) / 65536 : 0;
    auto row = [&](int v) { return base + static_cast<float>(v) * fraction * height; };
    const float mid = row(_sky.midpoint);
    const float pictureBottom = row(_sky.pixmapBottom);
    const float pictureTop = row(_sky.pixmapTop);
    auto ring = [&](int i) {
        const float turn = 2 * kPi * (static_cast<float>(_sky.angle) / 360 + static_cast<float>(i) / 16);
        return gfx::Vec3{radius * std::sin(turn), 0, radius * std::cos(turn)};
    };
    auto at = [&](int i, float y) {
        gfx::Vec3 p = ring(i);
        p[1] = y;
        const float x = dot(p, side);
        const float v = dot(p, up);
        return gfx::Vec3{eye[0] + side[0] * x + up[0] * v + f[0] * flat, eye[1] + side[1] * x + up[1] * v + f[1] * flat,
                         eye[2] + side[2] * x + up[2] * v + f[2] * flat};
    };
    const std::array<Rgb, 17> colours = skyRingColours(_sky);
    const float pictureFog = _trackNumber == 0 ? 1.0f : 100.0f;
    std::vector<gfx::Vertex> vertices(16 * 12);
    auto put = [&](size_t n, const gfx::Vec3 &p, const Rgb &c, float u, float v, float w) {
        gfx::Vertex &out = vertices[n];
        for (int k = 0; k < 3; ++k) {
            out.pos[k] = p[size_t(k)];
            out.color[k] = static_cast<uint8_t>(std::clamp(c[size_t(k)], 0, 255));
        }
        out.color[3] = 255;
        out.uv[0] = u;
        out.uv[1] = v;
        out.uv2[0] = w;
    };
    const Rgb white{255, 255, 255};
    for (int c = 0; c < 16; ++c) {
        // Only the segments reaching a quarter of the radius ahead (0x42c38f).
        auto ahead4 = [&](int i) {
            gfx::Vec3 p = ring(i);
            p[1] = base;
            return dot(p, f) >= radius / 4;
        };
        if (!ahead4(c) && !ahead4(c + 1))
            continue;
        const auto g = size_t(4 * c);
        put(g, at(c, mid), colours[size_t(c)], 0, 0, 1);
        put(g + 1, at(c + 1, mid), colours[size_t(c + 1)], 0, 0, 1);
        put(g + 2, at(c + 1, base + height), _sky.skyTop, 0, 0, 1);
        put(g + 3, at(c, base + height), _sky.skyTop, 0, 0, 1);
        const size_t e = 64 + g;
        put(e, at(c, base), _sky.earthBase, 0, 0, pictureFog);
        put(e + 1, at(c + 1, base), _sky.earthBase, 0, 0, pictureFog);
        put(e + 2, at(c + 1, mid), _sky.earthTop, 0, 0, pictureFog);
        put(e + 3, at(c, mid), _sky.earthTop, 0, 0, pictureFog);
        // With the mirror flag the second half takes the first's pictures
        // the other way round (0x42cd2e); the top right corner a little
        // past the next segment's (0x42c024).
        const bool turned = _sky.mirror && c >= 8;
        const float left = turned ? 1.0f : 0.0f;
        const gfx::Vec3 topLeft = at(c, pictureTop);
        const gfx::Vec3 topNext = at(c + 1, pictureTop);
        gfx::Vec3 topRight;
        for (int k = 0; k < 3; ++k)
            topRight[size_t(k)] = topLeft[size_t(k)] + 1.03f * (topNext[size_t(k)] - topLeft[size_t(k)]);
        const size_t q = 128 + g;
        put(q, at(c, pictureBottom), white, left, 1, pictureFog);
        put(q + 1, at(c + 1, pictureBottom), white, 1 - left, 1, pictureFog);
        put(q + 2, topRight, white, 1 - left, 0, pictureFog);
        put(q + 3, topLeft, white, left, 0, pictureFog);
    }
    if (!_skyMesh) {
        std::vector<uint32_t> indices;
        for (uint32_t b = 0; b < vertices.size(); b += 4)
            indices.insert(indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        if (_renderer->createMesh(vertices, indices, _skyMesh))
            return;
    } else if (_renderer->setMeshVertices(_skyMesh, vertices)) {
        return;
    }
    gfx::DrawCall call;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    call.rgbSource = gfx::ColorSource::kVertex;
    if (_fogged && fog.density > 0) {
        call.tableFog = 1 / fog.density;
        for (int k = 0; k < 3; ++k)
            call.tableFogColor[size_t(k)] = static_cast<float>(fog.colour[size_t(k)]) / 255;
    }
    auto draw = [&](uint32_t first, uint32_t count, gfx::TextureId texture) {
        call.mesh = _skyMesh;
        call.firstIndex = first;
        call.indexCount = count;
        call.texture = texture;
        _renderer->draw(call);
    };
    draw(0, 16 * 6, 0);
    if (_domeMesh) {
        // Added to the sky on tracks 2 to 6, half over it on 7 (0x42b4d0).
        gfx::DrawCall dome = call;
        dome.mesh = _domeMesh;
        dome.firstIndex = 0;
        dome.indexCount = _domeIndices;
        dome.texture = _domeTexture;
        dome.model = gfx::identity();
        dome.model[12] = eye[0];
        dome.model[13] = eye[1];
        dome.model[14] = eye[2];
        dome.rgbSource = gfx::ColorSource::kConstant;
        dome.state.src = gfx::Blend::kSrcAlpha;
        dome.state.dst = _trackNumber == 7 ? gfx::Blend::kOneMinusSrcAlpha : gfx::Blend::kOne;
        dome.color = {1, 1, 1, _trackNumber == 7 ? 0.5f : 1.0f};
        dome.tableFogFromDepth = true;
        _renderer->draw(dome);
    }
    draw(16 * 6, 16 * 6, 0);
    call.state.alphaTest = gfx::AlphaTest::kGe128;
    for (int c = 0; c < 16; ++c) {
        const int slice = _sky.mirror && c >= 8 ? 15 - c : c % 8;
        draw(static_cast<uint32_t>((32 + c) * 6), 6,
             _textured && slice < static_cast<int>(_textures.size()) ? _textures[size_t(slice)] : 0);
    }
}

void TrackPage::handleKey(uint32_t key, uint16_t mods) {
    const int n = static_cast<int>(_tracks.size());
    const int points = static_cast<int>(_collision.road.size());
    const int step = (mods & keymod::kShift) ? 10 : 1;
    switch (key) {
    case key::kPageDown:
        if (_renderer)
            load((_index + 1) % n);
        break;
    case key::kPageUp:
        if (_renderer)
            load((_index + n - 1) % n);
        break;
    case key::kUp:
        if (points)
            _point = (_point + step) % points;
        break;
    case key::kDown:
        if (points)
            _point = ((_point - step) % points + points) % points;
        break;
    case key::kLeft:
        _look -= 0.2f;
        break;
    case key::kRight:
        _look += 0.2f;
        break;
    case '1':
    case '2':
    case '3':
        _level = static_cast<int>(key - '1');
        build();
        break;
    case 't':
        _textured = !_textured;
        break;
    case 'o':
        _objects = !_objects;
        build();
        break;
    case 'v':
        _above = !_above;
        break;
    case 's':
        _shaded = !_shaded;
        build();
        break;
    case 'l':
        _painted = !_painted;
        build();
        break;
    case 'h':
        _skyShown = !_skyShown;
        break;
    case 'f':
        _fogged = !_fogged;
        break;
    default:
        break;
    }
}

void TrackPage::draw(Surface &s) {
    std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
    font::drawText(s, 16, 16, "The Tracks page needs 3D rendering, which isn't available:", page::kWarnColor);
    font::drawText(s, 16, 16 + font::kLineHeight, _unavailable, page::kTextColor);
}

void TrackPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    // The camera on the road point, 1.5 above it, looking along the road
    // (turned by _look); or high above it, looking down ahead.
    gfx::Vec3 eye{0, 2, 0};
    gfx::Vec3 ahead{0, 0, 1};
    if (!_collision.road.empty()) {
        const Collision::RoadPoint &r = _collision.road[size_t(_point)];
        const float c = std::cos(_look);
        const float s = std::sin(_look);
        const float fx = r.forward[0] / 127.0f;
        const float fz = r.forward[2] / 127.0f;
        ahead = {fx * c - fz * s, r.forward[1] / 127.0f, fx * s + fz * c};
        eye = {r.position[0], r.position[1] + 1.5f, r.position[2]};
        if (_above) {
            eye = {r.position[0] - ahead[0] * 60, r.position[1] + 80, r.position[2] - ahead[2] * 60};
            ahead = {ahead[0], -1.0f, ahead[2]};
        }
    }
    const float length = std::sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1] + ahead[2] * ahead[2]);
    for (float &v : ahead)
        v /= length > 0 ? length : 1;
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::multiply(gfx::perspective(1.1f, float(kFrameW) / float(kFrameH), 0.1f, 2000.0f), kMirror),
                      gfx::lookAlong(eye, ahead, {0, 1, 0}));
    // The clear colour: the 3dfx sky's top, or black with a black horizon
    // (NFS2SEA.EXE 0x427440).
    Color clear{70, 74, 82};
    if (_hasSky)
        clear = _sky.blackHorizon ? Color{0, 0, 0}
                                  : Color{uint8_t(_sky.skyTop[0]), uint8_t(_sky.skyTop[1]), uint8_t(_sky.skyTop[2])};
    const GlideFog fog = glideFog(_trackNumber, _point);
    // The screen's right, level: as the road's across vector is to its
    // forward one.
    const float flat = std::sqrt(ahead[0] * ahead[0] + ahead[2] * ahead[2]);
    const gfx::Vec3 right = flat > 0 ? gfx::Vec3{ahead[2] / flat, 0, -ahead[0] / flat} : gfx::Vec3{1, 0, 0};
    const auto frame = static_cast<uint32_t>(nowMs * 30 / 1000);
    if (frame != _soundFrame) {
        _soundFrame = frame;
        updateSounds(eye, right);
    }
    _renderer->beginFrame(viewProj, eye, clear);
    drawSky(eye, ahead, fog);
    if (_mesh) {
        gfx::DrawCall call;
        call.mesh = _mesh;
        // The track fogged by its depth along the view, as Glide's table fog.
        if (_fogged && fog.density > 0) {
            call.tableFog = 1 / fog.density;
            call.tableFogFromDepth = true;
            for (int k = 0; k < 3; ++k)
                call.tableFogColor[size_t(k)] = static_cast<float>(fog.colour[size_t(k)]) / 255;
        }
        call.rgbSource = gfx::ColorSource::kConstant;
        // Which way the quads face isn't worked out: both sides drawn.
        call.state.cull = gfx::Cull::kNone;
        call.state.alphaTest = gfx::AlphaTest::kGe128;
        for (const QuadMesh::Run &r : _runs) {
            int picture = r.texture;
            // The game's: a picture of the animation each `ticksPerFrame`
            // of its 64 ticks a second (col.md, Textures).
            if (picture >= kLine) {
                const size_t k = size_t(picture - kLine);
                call.texture = _textured && k < _lines.size() ? _lines[k] : 0;
                call.rgbSource = gfx::ColorSource::kConstant;
                call.color = {1, 1, 1, 1};
                call.firstIndex = r.firstIndex;
                call.indexCount = r.indexCount;
                _renderer->draw(call);
                continue;
            }
            if (picture >= kAnimated) {
                const Collision::Texture &t = _collision.textures[size_t(picture - kAnimated)];
                picture = t.picture + int(nowMs * 64 / 1000 / std::max<uint8_t>(t.ticksPerFrame, 1) % t.frames);
            }
            const bool known = picture >= 0 && picture < static_cast<int>(_textures.size());
            call.texture = _textured && known ? _textures[size_t(picture)] : 0;
            call.color =
                _textured && known ? std::array<float, 4>{1, 1, 1, 1} : std::array<float, 4>{0.6f, 0.6f, 0.6f, 1};
            call.rgbSource = _shaded ? gfx::ColorSource::kVertex : gfx::ColorSource::kConstant;
            call.firstIndex = r.firstIndex;
            call.indexCount = r.indexCount;
            _renderer->draw(call);
        }
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    std::vector<std::string> lines = {"Tracks  " + std::to_string(_index + 1) + "/" + std::to_string(_tracks.size()) +
                                      "  " + _tracks[size_t(_index)].name};
    lines.push_back(!_error.empty()
                        ? _error
                        : std::to_string(_track.blocks.size()) + " blocks, " + kLevelNames[_level] + " detail, " +
                              std::to_string(_quads) + " polygons" + (_objects ? " with objects" : "") +
                              "  road point " + std::to_string(_point) + "/" + std::to_string(_collision.road.size()));
    lines.push_back("PgUp/PgDn: track  Up/Down: road  Left/Right: look  1-3: level  T: textures  O: objects  V: above"
                    "  S: shades  L: lines  H: sky  F: fog");
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y =
            i + 1 == lines.size() ? kFrameH - font::kLineHeight - 4 : 4 + static_cast<int>(i) * font::kLineHeight;
        for (int yy = y - 2; yy < y + font::kLineHeight - 1; ++yy)
            for (int x = 0; x < kFrameW; ++x)
                _overlay.row(yy)[x * 4 + 3] = 140;
        font::drawText(_overlay, 6, y, lines[i], i == 1 && !_error.empty() ? page::kWarnColor : page::kTextColor);
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::nfs::nfs2se
