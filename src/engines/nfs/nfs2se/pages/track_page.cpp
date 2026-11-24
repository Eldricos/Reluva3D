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

} // namespace

TrackPage::TrackPage(const GameFiles &files) : _files(files) {
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
    _renderer = nullptr;
}

void TrackPage::release() {
    if (_renderer) {
        if (_mesh)
            _renderer->destroyMesh(_mesh);
        for (gfx::TextureId t : _textures)
            if (t)
                _renderer->destroyTexture(t);
    }
    _mesh = 0;
    _textures.clear();
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
    if (const FileRef *hrz = beside(_files.files(), ref, "3" + stem + ".hrz"))
        if (!_files.read(*hrz, data))
            _hasSky = !parseSky(std::string(data.begin(), data.end()), _sky);
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
            quads.add(picture(q.texture), corners, pictureCorners(orientation));
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
    _quads = quads.quads();
    if (_renderer)
        if (Error err = quads.build(*_renderer, _mesh, _runs))
            _error = err.message;
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
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(1.1f, float(kFrameW) / float(kFrameH), 0.1f, 2000.0f),
                                             gfx::lookAlong(eye, ahead, {0, 1, 0}));
    // The clear colour: the 3dfx sky's base, toward the sun.
    const Color clear =
        _hasSky ? Color{uint8_t(_sky.skyBaseSun[0]), uint8_t(_sky.skyBaseSun[1]), uint8_t(_sky.skyBaseSun[2])}
                : Color{70, 74, 82};
    _renderer->beginFrame(viewProj, eye, clear);
    if (_mesh) {
        gfx::DrawCall call;
        call.mesh = _mesh;
        call.rgbSource = gfx::ColorSource::kConstant;
        // Which way the quads face isn't worked out: both sides drawn.
        call.state.cull = gfx::Cull::kNone;
        call.state.alphaTest = gfx::AlphaTest::kGe128;
        for (const QuadMesh::Run &r : _runs) {
            int picture = r.texture;
            // The game's: a picture of the animation each `ticksPerFrame`
            // of its 64 ticks a second (col.md, Textures).
            if (picture >= kAnimated) {
                const Collision::Texture &t = _collision.textures[size_t(picture - kAnimated)];
                picture = t.picture + int(nowMs * 64 / 1000 / std::max<uint8_t>(t.ticksPerFrame, 1) % t.frames);
            }
            const bool known = picture >= 0 && picture < static_cast<int>(_textures.size());
            call.texture = _textured && known ? _textures[size_t(picture)] : 0;
            call.color =
                _textured && known ? std::array<float, 4>{1, 1, 1, 1} : std::array<float, 4>{0.6f, 0.6f, 0.6f, 1};
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
    lines.push_back("PgUp/PgDn: track  Up/Down: road  Left/Right: look  1-3: level  T: textures  O: objects  V: above");
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
