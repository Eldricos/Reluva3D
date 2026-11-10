// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/frd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace reluva::nfs {

namespace {

// Reads little-endian values in order, failing (once) past the end.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : d_(data) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    bool has(size_t n) const { return ok_ && pos_ + n <= d_.size(); }

    const uint8_t* take(size_t n) {
        if (!has(n)) {
            ok_ = false;
            return nullptr;
        }
        const uint8_t* p = d_.data() + pos_;
        pos_ += n;
        return p;
    }
    uint32_t u32() {
        const uint8_t* p = take(4);
        return p ? p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                       static_cast<uint32_t>(p[3]) << 24
                 : 0;
    }
    uint16_t u16() {
        const uint8_t* p = take(2);
        return p ? static_cast<uint16_t>(p[0] | p[1] << 8) : 0;
    }
    uint8_t u8() {
        const uint8_t* p = take(1);
        return p ? *p : 0;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    Track::Vec3 vec3() {
        const float x = f32(), y = f32(), z = f32();
        return {x, y, z};
    }
    std::vector<uint8_t> bytes(size_t n) {
        const uint8_t* p = take(n);
        return p ? std::vector<uint8_t>(p, p + n) : std::vector<uint8_t>{};
    }
    // A count about to be followed by `size`-byte records: fails rather
    // than allocating for a count the rest of the file can't hold.
    uint32_t count(size_t size) {
        const uint32_t n = u32();
        if (ok_ && size && static_cast<uint64_t>(n) * size > d_.size() - pos_)
            ok_ = false;
        return ok_ ? n : 0;
    }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
    bool ok_ = true;
};

constexpr size_t kHeaderSize = 28;
constexpr size_t kPolygonSize = 14;
constexpr int kNeighbours = 300;

void readPolygons(Reader& r, uint32_t n, std::vector<Track::Polygon>& out) {
    out.resize(n);
    for (Track::Polygon& p : out) {
        for (uint16_t& v : p.vertices)
            v = r.u16();
        p.texture = r.u16();
        p.animationBase = r.u16();
        p.flags = r.u8();
        p.animation = r.u8();
    }
}

} // namespace

Error parseTrack(const std::vector<uint8_t>& data, Track& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .frd track: " + why); };
    Reader r(data);
    r.take(kHeaderSize);
    const uint32_t blocks = r.count(84) + 1; // stored as one less
    if (!r.ok())
        return bad("too short");

    // The blocks: geometry, neighbours and their records.
    out.blocks.resize(blocks);
    for (Track::Block& b : out.blocks) {
        b.centre = r.vec3();
        for (Track::Vec3& c : b.corners)
            c = r.vec3();
        const uint32_t vertices = r.u32();
        b.highResVertices = r.u32();
        b.lowResVertices = r.u32();
        b.mediumResVertices = r.u32();
        b.duplicateVertices = r.u32();
        b.objectVertices = r.u32();
        if (!r.has(16ull * vertices))
            return bad("a block's vertices run past the end");
        b.vertices.resize(vertices);
        for (Track::Vec3& v : b.vertices)
            v = r.vec3();
        b.colours.resize(vertices);
        for (uint32_t& c : b.colours)
            c = r.u32();
        for (int i = 0; i < kNeighbours; ++i) {
            const auto block = static_cast<int16_t>(r.u16());
            r.u16();
            if (block >= 0)
                b.neighbours.push_back(block);
        }
        b.startPosition = r.u32();
        const uint32_t positions = r.u32(), polygons = r.u32(), road = r.u32(), refs = r.u32(), polyObjects = r.u32(),
                       sounds = r.u32(), lights = r.u32();
        if (!r.has(8ull * positions + 8ull * polygons + 12ull * road + 20ull * refs + 20ull * polyObjects +
                   16ull * sounds + 16ull * lights))
            return bad("a block's records run past the end");
        b.positions = r.bytes(8ull * positions);
        b.polygonRoad = r.bytes(8ull * polygons);
        b.virtualRoad = r.bytes(12ull * road);
        b.objectRefs = r.bytes(20ull * refs);
        b.polygonObjects = r.bytes(20ull * polyObjects);
        b.sounds = r.bytes(16ull * sounds);
        b.lights = r.bytes(16ull * lights);
        if (!r.ok())
            return bad("a block runs past the end");
        // Lights and sounds: a position (3 x s32, 16.16), then the type, or
        // the slot and a flag.
        auto position = [](const std::vector<uint8_t>& bytes, size_t at) {
            Track::Vec3 v;
            for (int i = 0; i < 3; ++i) {
                int32_t fixed;
                std::memcpy(&fixed, bytes.data() + at + 4 * size_t(i), 4);
                v[size_t(i)] = static_cast<float>(fixed) / 65536.0f;
            }
            return v;
        };
        for (size_t at = 0; at + 16 <= b.lights.size(); at += 16) {
            Track::Block::Light l;
            l.position = position(b.lights, at);
            std::memcpy(&l.type, b.lights.data() + at + 12, 4);
            b.lightList.push_back(l);
        }
        for (size_t at = 0; at + 16 <= b.sounds.size(); at += 16) {
            Track::Block::PlacedSound snd;
            snd.position = position(b.sounds, at);
            std::memcpy(&snd.slot, b.sounds.data() + at + 12, 2);
            std::memcpy(&snd.kind, b.sounds.data() + at + 14, 2);
            b.soundList.push_back(snd);
        }
    }

    // Each block's polygons: seven lists, each a count, then (if not 0)
    // the count again and the polygons; then four object layers, each a
    // polygon total, then (if not 0) the objects.
    for (size_t bi = 0; bi < out.blocks.size(); ++bi) {
        Track::Block& b = out.blocks[bi];
        for (auto& list : b.polygons) {
            const uint32_t n = r.count(kPolygonSize);
            if (n) {
                if (r.u32() != n)
                    return bad("block " + std::to_string(bi) + "'s polygon counts disagree");
                readPolygons(r, n, list);
            }
        }
        for (auto& layer : b.objectLayers) {
            if (!r.u32())
                continue;
            const uint32_t objects = r.count(4);
            layer.resize(objects);
            for (Track::BlockObject& o : layer) {
                o.type = r.u32();
                if (o.type == 1)
                    readPolygons(r, r.count(kPolygonSize), o.polygons);
            }
        }
        auto inside = [&](const std::vector<Track::Polygon>& list) {
            for (const Track::Polygon& p : list)
                for (uint16_t v : p.vertices)
                    if (v >= b.vertices.size())
                        return false;
            return true;
        };
        for (const auto& list : b.polygons)
            if (!inside(list))
                return bad("block " + std::to_string(bi) + " has a polygon outside its vertices");
        for (const auto& layer : b.objectLayers)
            for (const Track::BlockObject& o : layer)
                if (!inside(o.polygons))
                    return bad("block " + std::to_string(bi) + " has an object polygon outside its vertices");
        if (!r.ok())
            return bad("the polygons run past the end");
    }

    // Objects placed on the track: four groups per block and one more.
    out.extraObjects.resize(4ull * blocks + 1);
    for (auto& group : out.extraObjects) {
        group.resize(r.count(28));
        for (Track::ExtraObject& o : group) {
            o.type = r.u32();
            if (o.type == 4) {
                r.take(8);
                o.reference = r.vec3();
                r.take(4);
            } else if (o.type == 3) {
                r.take(8 + 12 + 8);
                const uint16_t keys = r.u16();
                o.keyDelay = static_cast<int16_t>(r.u16());
                o.keys.resize(keys);
                for (auto& k : o.keys) {
                    for (int32_t& p : k.position)
                        p = static_cast<int32_t>(r.u32());
                    for (int16_t& q : k.rotation)
                        q = static_cast<int16_t>(r.u16());
                }
            } else {
                return bad("an object of unknown type " + std::to_string(o.type));
            }
            const uint32_t vertices = r.count(16);
            o.vertices.resize(vertices);
            for (Track::Vec3& v : o.vertices)
                v = r.vec3();
            o.colours.resize(vertices);
            for (uint32_t& c : o.colours)
                c = r.u32();
            readPolygons(r, r.count(kPolygonSize), o.polygons);
            for (const Track::Polygon& p : o.polygons)
                for (uint16_t v : p.vertices)
                    if (v >= o.vertices.size())
                        return bad("an object has a polygon outside its vertices");
            if (!r.ok())
                return bad("the objects run past the end");
        }
    }

    // The texture table, to the end of the file.
    out.textures.resize(r.count(47));
    for (Track::Texture& t : out.textures) {
        t.width = r.u16();
        t.height = r.u16();
        t.handle = r.u32();
        for (float& c : t.corners)
            c = r.f32();
        t.flags = r.u32();
        t.lane = r.u8() != 0;
        t.picture = r.u16();
    }
    if (!r.ok())
        return bad("the texture table runs past the end");
    if (r.pos() != data.size())
        return bad(std::to_string(data.size() - r.pos()) + " bytes left after the texture table");
    for (const Track::Block& b : out.blocks)
        for (const auto& list : b.polygons)
            for (const Track::Polygon& p : list)
                if (p.texture >= out.textures.size())
                    return bad("a polygon uses a texture past the table");
    return Error::success();
}

uint16_t Track::Polygon::textureAt(uint64_t tick) const {
    if (!(flags & kAnimated) || frameDelay() == 0 || frameCount() == 0)
        return texture;
    return static_cast<uint16_t>(animationBase + (tick / static_cast<uint64_t>(frameDelay())) % frameCount());
}

int blockDetailAt(float distance, bool night, int viewLevel) {
    // 16.16 scales from the tables at 0x41a8bc (day) and 0x41a8b0 (night).
    static constexpr int kDay[3] = {65536, 36044, 26214}, kNight[3] = {49152, 26869, 19660};
    const int level = std::clamp(viewLevel - 1, 0, 2);
    const float scale = static_cast<float>((night ? kNight : kDay)[level]) / 65536.0f;
    const float far = 500 * scale;
    // View level 0: every block in reach at high detail (0x41d6e9).
    const float medium = viewLevel == 0 ? far : 150 * scale, high = viewLevel == 0 ? far : 110 * scale;
    if (distance > far)
        return -1;
    return distance > medium ? 0 : distance > high ? 1 : 2;
}

int placedSoundVolume(float distance) {
    const int64_t d = static_cast<int64_t>(distance * 65536); // 16.16
    const int64_t square = ((d >> 12) * (d >> 12)) >> 10;
    return square == 0 ? 127 : static_cast<int>(std::min<int64_t>(0x10000 / square, 127));
}

uint32_t scaleArgb(uint32_t colour, uint32_t k) {
    return (((colour & 0xff00ff00u) >> 8) * k & 0xff00ff00u) | ((colour & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu);
}

Headlight carHeadlight(int beams, bool projected, const std::array<float, 3>& at, const std::array<float, 3>& right,
                       const std::array<float, 3>& up, const std::array<float, 3>& forward, float intensity) {
    Headlight h;
    h.at = at;
    // 0x42d253-0x42d31b.
    float tilt = 0; // turns
    if (beams == 1) {
        h.fadeFrom = projected ? 10.0f : 20.0f;
        h.reach = 40;
        h.colour = projected ? 0xB0B0B0B0u : 0xFFFFFFFFu;
        tilt = 0.0625f;
    } else {
        h.fadeFrom = projected ? 20.0f : 40.0f;
        h.reach = 60;
        h.colour = projected ? 0xE0E0E0E0u : 0xFFFFFFFFu;
        tilt = 0.03125f;
    }
    h.nearPlane = 1;
    // 0x41a800: the fade starts at fadeFrom x intensity and ends at the
    // reach x intensity, or a unit after the start if that's further.
    h.fadeFrom *= intensity;
    h.reach = std::max(h.fadeFrom + 1, h.reach * intensity);
    // The car's rows with up and forward turned about the right by the
    // tilt (0x4ea9e0): forward goes down.
    constexpr float kTau = 6.28318530718f;
    const float s = std::sin(tilt * kTau), c = std::cos(tilt * kTau);
    h.right = right;
    for (int i = 0; i < 3; ++i) {
        h.up[size_t(i)] = forward[size_t(i)] * s + up[size_t(i)] * c;
        h.forward[size_t(i)] = forward[size_t(i)] * c - up[size_t(i)] * s;
    }
    return h;
}

Headlight carHeadlight(int beams, bool projected, const std::array<float, 3>& at, float heading, float intensity) {
    const std::array<float, 3> forward{std::sin(heading), 0, std::cos(heading)};
    // Left-handed: the right of a forward (x, 0, z) is (z, 0, -x).
    return carHeadlight(beams, projected, at, {forward[2], 0, -forward[0]}, {0, 1, 0}, forward, intensity);
}

Headlight sirenLight(const std::array<float, 3>& carAt, const std::array<float, 3>& right,
                     const std::array<float, 3>& up, const std::array<float, 3>& forward, int step) {
    Headlight h;
    // 0x42d020: the place, the car's position plus its up row (0x4dffb0).
    for (int i = 0; i < 3; ++i)
        h.at[size_t(i)] = carAt[size_t(i)] + up[size_t(i)];
    // The turn (0x42cff0: (step mod 32) x 1/32), about the up (0x4eaab0):
    // rows right and forward mixed by its sine and cosine.
    constexpr float kTau = 6.28318530718f;
    const float turn = static_cast<float>(((step % 32) + 32) % 32) * 0.03125f;
    const float s = std::sin(turn * kTau), c = std::cos(turn * kTau);
    for (int i = 0; i < 3; ++i) {
        h.right[size_t(i)] = right[size_t(i)] * c - forward[size_t(i)] * s;
        h.forward[size_t(i)] = right[size_t(i)] * s + forward[size_t(i)] * c;
    }
    h.up = up;
    h.nearPlane = 3.5f;
    h.fadeFrom = 15;
    h.reach = 30;
    h.colour = 0xFF8080FFu;
    h.backColour = 0xFFFF8080u;
    return h;
}

ViewScreen viewScreen(int left, int top, int width, int height, int viewAngle, bool mirrored) {
    // 0x4dbce0: the half angles in whole degrees, (a x 13) >> 4 rounded
    // towards zero.
    const int across = viewAngle / 2, up = across * 13 / 16;
    // 0x4bf260: tan of the angle x 0x5401c4 (pi / 180 as a float) on the
    // x87, then x 0x5401c0 (0.5).
    const double degree = static_cast<double>(0.017453279f);
    const double w = width, h = height;
    double sx = w, sy = w; // with no angle across: the window's width
    if (across != 0) {
        const double t = std::tan(across * degree);
        sx = w / t;
        sy = up != 0 ? h / std::tan(up * degree) : w / t;
    }
    ViewScreen s;
    s.sx = static_cast<float>(sx) * 0.5f;
    s.sy = -static_cast<float>(sy) * 0.5f;
    if (mirrored)
        s.sx = -s.sx;
    s.cx = static_cast<float>(width) * 0.5f + static_cast<float>(left);
    s.cy = static_cast<float>(height) * 0.5f + static_cast<float>(top);
    s.left = static_cast<float>(left);
    s.right = static_cast<float>(left + width);
    s.top = static_cast<float>(top);
    s.bottom = static_cast<float>(top + height);
    return s;
}

bool lightMissesBlock(const Headlight& light, const std::array<std::array<float, 3>, 4>& corners, float zoom,
                      bool bothWays, const ViewScreen& screen) {
    zoom = std::min(zoom, 1.0f);
    uint8_t shared = 0xff;
    for (const auto& corner : corners) {
        const std::array<float, 3> d{corner[0] - light.at[0], corner[1] - light.at[1], corner[2] - light.at[2]};
        auto dot = [&](const std::array<float, 3>& v) { return d[0] * v[0] + d[1] * v[1] + d[2] * v[2]; };
        const float x = dot(light.right), y = dot(light.up);
        float z = dot(light.forward);
        if (std::fabs(z) == 0)
            z = 1.5259022e-05f;
        // Outcodes as for drawing, against the screen's window: the
        // floats' bits compared as integers (0x41c0a6), so a negative
        // value is outside below the bound.
        const float inverse = 1 / z;
        uint8_t code = 0;
        if (!(inverse >= 0) || inverse >= screen.inverseDepth) {
            code = 0x10;
        } else {
            const float sxp = screen.cx + screen.sx * inverse * x, syp = screen.cy + screen.sy * inverse * y;
            if (syp < 0 || syp < screen.top)
                code |= 8;
            else if (syp > screen.bottom)
                code |= 4;
            if (sxp < 0 || sxp < screen.left)
                code |= 1;
            else if (sxp > screen.right)
                code |= 2;
        }
        shared &= code;
        // 0x41c138: a corner no wider than it is deep keeps the block.
        if (std::fabs(x) * zoom <= (bothWays ? std::fabs(z) : z))
            return false;
    }
    return shared != 0;
}

std::array<float, 3> headlightPlace(const Headlight& light, const std::array<float, 3>& carAt,
                                    const std::array<float, 3>& carUp, float halfLength, float upright) {
    const float along = halfLength * 0.25f; // 0x5373b0
    std::array<float, 3> out{};
    for (int i = 0; i < 3; ++i)
        out[size_t(i)] = carAt[size_t(i)] + carUp[size_t(i)] * upright + light.forward[size_t(i)] * along;
    return out;
}

std::array<float, 3> headlightPlace(const Headlight& light, const std::array<float, 3>& carAt, float halfLength,
                                    float upright) {
    return headlightPlace(light, carAt, {0, 1, 0}, halfLength, upright);
}

HeadlightPoint headlightAt(const Headlight& light, const std::array<float, 3>& point) {
    const std::array<float, 3> d{point[0] - light.at[0], point[1] - light.at[1], point[2] - light.at[2]};
    auto dot = [&](const std::array<float, 3>& v) { return d[0] * v[0] + d[1] * v[1] + d[2] * v[2]; };
    float x = dot(light.right), y = dot(light.up), z = dot(light.forward);
    HeadlightPoint p;
    if (std::fabs(z) == 0)
        z = 1.5259022e-05f;
    // 1 / z, or 1 below a unit ahead (0x41a5f5; also behind).
    const float inverse = z < 1 ? 1.0f : 1 / z;
    p.u = (x * inverse + 1) * 0.5f;
    p.v = (y * inverse + 1) * 0.5f;
    uint32_t colour = light.colour;
    if (z <= 0) {
        // A car's light has no colour behind (+0x74 is 0); a roof light
        // takes the point as one ahead, negated, in its back colour.
        if (light.backColour == 0) {
            p.outside = 0x10;
            return p;
        }
        x = -x;
        y = -y;
        z = -z;
        colour = light.backColour;
    }
    if (z < light.nearPlane)
        p.outside |= 0x10;
    if (z > light.reach)
        p.outside |= 0x20;
    if (y > z)
        p.outside |= 8;
    else if (y < -z)
        p.outside |= 4;
    if (x > z)
        p.outside |= 2;
    else if (x < -z)
        p.outside |= 1;
    if (z < light.reach) {
        // 0x41a3e0: dimmer off the axis (a diamond), and fading past fadeFrom.
        const float across = std::clamp(std::fabs(x) / z, 0.0f, 1.0f), high = std::clamp(std::fabs(y) / z, 0.0f, 1.0f);
        const float fade = z > light.fadeFrom ? 1 - (z - light.fadeFrom) / (light.reach - light.fadeFrom) : 1.0f;
        const long k = std::lround((1 - (across + high) * 0.5f) * fade * 65536.0f);
        p.colour = scaleArgb(colour, static_cast<uint32_t>(std::clamp(k, 0L, 0xffffL)) >> 8);
    }
    return p;
}

bool headlightTriangle(const HeadlightPoint& a, const HeadlightPoint& b, const HeadlightPoint& c) {
    if ((a.outside & b.outside & c.outside) != 0 || ((a.outside | b.outside | c.outside) & 0x20) != 0)
        return false;
    return ((a.colour | b.colour | c.colour) >> 24) >= 8;
}

uint32_t carLitBy(uint32_t colour, const std::array<float, 3>& carAt, const std::vector<CarLights>& others) {
    int sum[3] = {0, 0, 0}, count = 0;
    auto add = [&](uint32_t c, float k) {
        ++count;
        for (int i = 0; i < 3; ++i)
            sum[i] += static_cast<int>(std::lrint(static_cast<float>((c >> (16 - 8 * i)) & 0xff) * k));
    };
    auto inFrame = [&](const Headlight& l, float& x, float& y, float& z) {
        const std::array<float, 3> d{carAt[0] - l.at[0], carAt[1] - l.at[1], carAt[2] - l.at[2]};
        x = d[0] * l.right[0] + d[1] * l.right[1] + d[2] * l.right[2];
        y = d[0] * l.up[0] + d[1] * l.up[1] + d[2] * l.up[2];
        z = d[0] * l.forward[0] + d[1] * l.forward[1] + d[2] * l.forward[2];
    };
    for (const CarLights& o : others) {
        bool headlights = true;
        if (o.sirenOn) {
            headlights = false;
            float x, y, z;
            inFrame(o.siren, x, y, z);
            uint32_t c = o.siren.colour;
            if (z < 0) {
                z = -z;
                c = o.siren.backColour;
            }
            if (o.siren.nearPlane <= z && z <= o.siren.reach && std::fabs(x) <= z && std::fabs(y) <= z) {
                add(c, 1 - z / o.siren.reach);
                headlights = true; // 0x4b9a7f: on to the headlights
            }
        }
        if (headlights && o.headlightOn) {
            const Headlight& l = o.headlight;
            float x, y, z;
            inFrame(l, x, y, z);
            if (l.nearPlane <= z && z <= l.reach && std::fabs(x) <= z && std::fabs(y) <= z)
                add(l.colour, (1 - std::fabs(x) / z) * (1 - z / l.reach));
        }
    }
    if (count == 0)
        return colour;
    uint32_t out = 0xff000000u;
    for (int i = 0; i < 3; ++i) {
        const int shift = 16 - 8 * i;
        const int c = static_cast<int>((colour >> shift) & 0xff) + sum[i] / count;
        out |= static_cast<uint32_t>(std::clamp(c, 0, 255)) << shift;
    }
    return out;
}

int lightDistance(const std::array<int32_t, 3>& a, const std::array<int32_t, 3>& b) {
    // 0x496550: with its own abs (x < 1 negated) on each difference.
    auto absd = [](int32_t x) { return x < 1 ? -x : x; };
    int dx = absd(a[0] - b[0]), dy = absd(a[1] - b[1]), dz = absd(a[2] - b[2]);
    int m = dy < dx ? dx + (dy >> 2) : dy + (dx >> 2);
    if (dz < m)
        dz >>= 2;
    else
        m >>= 2;
    return m + dz;
}

uint32_t carLitByTrackLights(uint32_t colour, const std::array<float, 3>& carAt, int slice, int slices,
                             const Track& track, uint64_t tick, bool effectsOff) {
    if (slices <= 0 || effectsOff)
        return colour;
    const std::array<int32_t, 3> at{static_cast<int32_t>(std::lrint(double(carAt[0]) * 65536.0)),
                                    static_cast<int32_t>(std::lrint(double(carAt[1]) * 65536.0)),
                                    static_cast<int32_t>(std::lrint(double(carAt[2]) * 65536.0))};
    int sum[3] = {0, 0, 0}, count = 0;
    for (int k = -3; k < 4; ++k) {
        int s = slice + k;
        if (s < 0)
            s += slices;
        else if (s >= slices)
            s -= slices;
        const size_t block = static_cast<size_t>(s >> 3);
        if (block >= track.blocks.size())
            continue;
        for (const Track::Block::Light& l : track.blocks[block].lightList) {
            const LightLook look = lightLook(l.type & 0xffff, tick);
            if (!look.shown || look.color == 0)
                continue;
            const std::array<int32_t, 3> p{static_cast<int32_t>(std::lrint(double(l.position[0]) * 65536.0)),
                                           static_cast<int32_t>(std::lrint(double(l.position[1]) * 65536.0)),
                                           static_cast<int32_t>(std::lrint(double(l.position[2]) * 65536.0))};
            const int d = lightDistance(at, p);
            if (d >= 0x100000)
                continue;
            const int w = 0x10000 - (d >> 4);
            for (int i = 0; i < 3; ++i)
                sum[i] += static_cast<int>((static_cast<int64_t>(w) * ((look.color >> (16 - 8 * i)) & 0xff)) >> 16);
            ++count;
        }
    }
    if (count == 0)
        return colour;
    uint32_t out = 0xff000000u;
    for (int i = 0; i < 3; ++i) {
        const int shift = 16 - 8 * i;
        const int c = static_cast<int>((colour >> shift) & 0xff) + sum[i] / count;
        out |= static_cast<uint32_t>(std::clamp(c, 0, 255)) << shift;
    }
    return out;
}

int carDetailLevel(float meanDepth, int option, bool night, bool mirror) {
    static constexpr float kTable[3][4] = {{30, 60, 120, 300}, {0, 45, 90, 300}, {0, 20, 60, 300}};
    if (option < 0 || option > 2)
        return -1;
    float t[4];
    for (int i = 0; i < 4; ++i) {
        t[i] = kTable[option][i];
        if (night && i < 3)
            t[i] *= 0.75f;
        if (mirror)
            t[i] *= 0.6f;
    }
    if (t[0] != 0 && meanDepth < t[0])
        return 0;
    for (int i = 1; i < 4; ++i)
        if (meanDepth < t[i])
            return i;
    return -1;
}

float aiSlicesSpeed(const std::vector<uint8_t>& speeds, int slice) {
    const size_t at = 16 + static_cast<size_t>(std::max(slice, 0) / 2);
    return at < speeds.size() ? static_cast<float>(speeds[at]) : 64.0f;
}

LightLook lightLook(uint32_t type, uint64_t tick) {
    struct Entry {
        uint32_t color;
        bool blinks;
        uint8_t shift, phase;
        float size;
    };
    static constexpr Entry kTypes[16] = {
        {0xffdeeaeb, false, 0, 0, 5},     {0xb9ffffff, false, 0, 0, 4.5f}, {0xffffffd2, false, 0, 0, 5},
        {0xff80e5f0, false, 0, 0, 4.5f},  {0xffd9c45e, false, 0, 0, 5},    {0xffdf1616, true, 6, 0, 5},
        {0xffdf1616, true, 5, 27, 5},     {0xffff0000, true, 6, 0, 3.125f}, {0xffa3b1be, false, 0, 0, 3.75f},
        {0xffdf1616, false, 0, 0, 3.125f}, {0xbadf1616, false, 0, 0, 2.5f}, {0xff1e95e3, false, 0, 0, 2.5f},
        {0xff1e95e3, true, 6, 0, 3.125f}, {0xffe0e027, false, 0, 0, 3.75f}, {0xffdeeaeb, false, 0, 0, 5},
        {0xffdeeaeb, false, 0, 0, 5},
    };
    const Entry& e = kTypes[type & 15];
    // Hidden while bit `shift` of the tick plus the phase is set (0x4911d9).
    return {e.color, e.size, !(e.blinks && ((tick + e.phase) >> e.shift) & 1)};
}

uint32_t dimLight(uint32_t color, float intensity) {
    if (intensity >= 1)
        return color;
    const uint32_t k = static_cast<uint32_t>(std::min(static_cast<int>(intensity * 65536), 0xffff)) >> 8;
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= (((color >> shift) & 0xff) * k >> 8) << shift;
    return out;
}

LightQuads lightQuads(const LightLook& look, float depth) {
    LightQuads q;
    // 0x49126b: the depth's bits between 1.0 and 256.0, as integers.
    if (!look.shown || !(depth >= 1.0f && depth <= 256.0f))
        return q;
    q.shown = true;
    // 0x491364: (0x100 - round(z)) x 0x100, at most 0xFFFF, >> 8.
    const int k = std::min((0x100 - static_cast<int>(std::nearbyint(depth))) * 0x100, 0xffff) >> 8 & 0xff;
    q.glowColour = scaleArgb(look.color, static_cast<uint32_t>(k));
    q.screenHalf = 0.025f * look.size;
    q.flareColour = look.color;
    q.worldHalf = 1.05f * 0.25f * look.size;
    return q;
}

bool pointCovered(const TrackCollision::RoadPoint& p, const std::array<float, 3>& at) {
    const uint8_t codes = static_cast<uint8_t>(p.unknown >> 16);
    float side = 0;
    for (int k = 0; k < 3; ++k)
        side += p.right[size_t(k)] * (at[size_t(k)] - p.position[size_t(k)]);
    const int right = codes & 15, left = codes >> 4;
    if (side > 0)
        return (right != 0 ? right : left) == 2;
    return (left != 0 ? left : right) == 2;
}

int coverDepth(const TrackCollision& col, int slice, const std::array<float, 3>& at) {
    const int n = static_cast<int>(col.road.size());
    if (n == 0)
        return 0;
    auto point = [&](int s) -> const TrackCollision::RoadPoint& { return col.road[size_t(((s % n) + n) % n)]; };
    if (!pointCovered(point(slice), at))
        return 0;
    int i = 1;
    while (pointCovered(point(slice + i), at) && pointCovered(point(slice - i), at)) {
        if (++i > 7)
            break;
    }
    return i;
}

int roadSurface(const Track::Block& b, size_t index) {
    return 8 * index + 1 < b.polygonRoad.size() ? b.polygonRoad[8 * index + 1] & 0x3f : 0;
}

int sliceRunSide(const Track::Block& b, int sliceInBlock, const std::array<float, 3>& point, size_t& polygon) {
    const size_t at = 8 * static_cast<size_t>(sliceInBlock);
    if (sliceInBlock < 0 || at + 3 > b.positions.size())
        return -1;
    const int first = b.positions[at] | b.positions[at + 1] << 8, count = b.positions[at + 2];
    const auto& list = b.polygons[4];
    if (count <= 0 || first + count > static_cast<int>(list.size()))
        return -1;
    // Which side of the line a-b a point is on, in plan.
    auto cross = [](const Track::Vec3& a, const Track::Vec3& c, float x, float z) {
        return (c[0] - a[0]) * (z - a[2]) - (c[2] - a[2]) * (x - a[0]);
    };
    auto past = [&](const Track::Polygon& p, int e0, int e1, int o0, int o1) {
        const auto& v = b.vertices;
        const auto &a = v[p.vertices[size_t(e0)]], &c = v[p.vertices[size_t(e1)]];
        const float inside = cross(a, c, v[p.vertices[size_t(o0)]][0], v[p.vertices[size_t(o0)]][2]) +
                             cross(a, c, v[p.vertices[size_t(o1)]][0], v[p.vertices[size_t(o1)]][2]);
        const float here = cross(a, c, point[0], point[2]);
        return inside * here < 0;
    };
    int i = first + count / 2;
    for (int steps = 0; steps <= count; ++steps) {
        const Track::Polygon& p = list[size_t(i)];
        bool whole = true;
        for (uint16_t vi : p.vertices)
            whole = whole && vi < b.vertices.size();
        if (!whole)
            return -1;
        if (past(p, 0, 3, 1, 2)) { // past the edge it shares with i - 1
            if (i == first)
                return polygon = size_t(i), 1;
            --i;
        } else if (past(p, 1, 2, 0, 3)) { // with i + 1
            if (i == first + count - 1)
                return polygon = size_t(i), 2;
            ++i;
        } else {
            polygon = size_t(i);
            return 0;
        }
    }
    return -1;
}

bool roadSearch(const Track& track, RoadSearch& s, float x, float z) {
    s.found = false;
    s.foundSlice = s.polygon = -1;
    s.beyondBlock = false;
    const int slices = static_cast<int>(track.blocks.size()) * 8;
    if (slices <= 0)
        return false;
    struct Run {
        const Track::Block* block = nullptr;
        int first = 0, count = 0;
    };
    auto runOf = [&](int slice) {
        Run r;
        const Track::Block& b = track.blocks[size_t(slice >> 3)];
        const size_t at = 8 * static_cast<size_t>(slice & 7);
        if (at + 3 <= b.positions.size()) {
            r.block = &b;
            r.first = b.positions[at] | b.positions[at + 1] << 8;
            r.count = b.positions[at + 2];
        }
        return r;
    };
    // A polygon's corners as 0x41e6a0 takes them (+0x54-+0x60: vertices 1,
    // 0, 3, 2) and its surface; false past the block's list.
    struct Poly {
        std::array<Track::Vec3, 4> c{};
        int surface = 0;
    };
    auto polyOf = [&](const Run& r, int index, Poly& out) {
        const auto& list = r.block->polygons[4];
        const int at = r.first + index;
        if (at < 0 || at >= static_cast<int>(list.size()))
            return false;
        const Track::Polygon& p = list[size_t(at)];
        static constexpr int kOrder[4] = {1, 0, 3, 2};
        for (int k = 0; k < 4; ++k) {
            const uint16_t v = p.vertices[size_t(kOrder[k])];
            if (v >= r.block->vertices.size())
                return false;
            out.c[size_t(k)] = r.block->vertices[v];
        }
        out.surface = 8 * size_t(at) + 1 < r.block->polygonRoad.size() ? r.block->polygonRoad[8 * size_t(at) + 1] & 0xf : 0;
        return true;
    };
    // The point on the inner side of the edge from a to b (the x87
    // compares as 0x41ea86 does).
    auto inner = [&](const Track::Vec3& a, const Track::Vec3& b) {
        return (b[2] - a[2]) * (x - a[0]) >= (b[0] - a[0]) * (z - a[2]);
    };
    auto inside = [&](const Poly& p) {
        return inner(p.c[2], p.c[1]) && inner(p.c[1], p.c[0]) && inner(p.c[3], p.c[2]) && inner(p.c[0], p.c[3]);
    };
    auto distance = [&](const Poly& p) {
        // +0x54 and +0x5C: the polygon's vertices 1 and 3.
        return static_cast<int>(std::fabs(z - p.c[0][2]) + std::fabs(x - p.c[0][0]) + std::fabs(z - p.c[2][2]) +
                                std::fabs(x - p.c[2][0]));
    };
    static constexpr int kSteps[11] = {1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11};
    int slice = ((s.slice % slices) + slices) % slices;
    const Run run0 = runOf(slice);
    int start = s.index;
    if (start < 0 || run0.count - 1 <= start || s.side != 0)
        start = (run0.count - 1) / 2;
    s.side = 0;
    for (int step = 0; step < 11; ++step) {
        const Run r = runOf(slice);
        if (r.block && r.count > 0) {
            Poly p;
            // Up.
            int i = start > r.count - 1 ? r.count - 2 : start;
            int best = 0x7fffffff;
            while (i <= r.count - 1) {
                if (!polyOf(r, i, p))
                    return s.beyondBlock = true, false;
                if (inside(p) && p.surface != 14) {
                    s.found = true;
                    s.foundSlice = slice;
                    s.polygon = r.first + i;
                    s.index = i;
                    s.side = 0;
                    return true;
                }
                if (i == r.count - 1) {
                    if (s.side == 0 && inner(p.c[2], p.c[1]))
                        s.side = 2;
                    break;
                }
                const int d = distance(p);
                if (best < d && p.surface != 14)
                    break;
                best = d;
                ++i;
            }
            // Down, from the start as it is.
            i = start - 1;
            best = 0x7fffffff;
            while (i >= 0) {
                if (i >= r.count)
                    ++s.pastRun;
                if (!polyOf(r, i, p))
                    return s.beyondBlock = true, false;
                if (inside(p) && p.surface != 14) {
                    s.found = true;
                    s.foundSlice = slice;
                    s.polygon = r.first + i;
                    s.index = i;
                    s.side = 0;
                    return true;
                }
                if (i == 0) {
                    if (s.side == 0 && inner(p.c[0], p.c[3]))
                        s.side = 1;
                    break;
                }
                const int d = distance(p);
                if (best < d && p.surface != 14)
                    break;
                best = d;
                --i;
            }
            s.index = i;
        }
        slice = (((slice + kSteps[step]) % slices) + slices) % slices;
    }
    return false;
}

bool officerSpotOnRoad(const Track& track, int slice, const std::array<float, 3>& point) {
    if (slice < 0 || static_cast<size_t>(slice >> 3) >= track.blocks.size())
        return false;
    const Track::Block& b = track.blocks[size_t(slice >> 3)];
    size_t polygon = 0;
    if (sliceRunSide(b, slice & 7, point, polygon) != 0 || 8 * polygon + 1 >= b.polygonRoad.size())
        return false;
    const int surface = roadSurface(b, polygon);
    return surface != 0 && surface != 14;
}

bool polygonRoadFrame(const Track::Block& b, size_t index, RoadFrame& out) {
    if (8 * index >= b.polygonRoad.size() || index >= b.polygons[4].size())
        return false;
    const size_t record = b.polygonRoad[8 * index];
    if (12 * record + 12 > b.virtualRoad.size())
        return false;
    const Track::Polygon& p = b.polygons[4][index];
    if (p.vertices[1] >= b.vertices.size())
        return false;
    auto s16 = [&](size_t k) {
        const size_t at = 12 * record + 2 * k;
        return static_cast<float>(static_cast<int16_t>(b.virtualRoad[at] | (b.virtualRoad[at + 1] << 8))) / 32768.0f;
    };
    out.up = {s16(0), s16(1), s16(2)};
    out.forward = {s16(3), s16(4), s16(5)};
    const auto& u = out.up;
    const auto& f = out.forward;
    out.right = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
    out.point = b.vertices[p.vertices[1]];
    return true;
}

float roadFrameHeight(const RoadFrame& f, const std::array<float, 3>& p) {
    const std::array<float, 3> d{p[0] - f.point[0], p[1] - f.point[1], p[2] - f.point[2]};
    const float along = d[0] * f.right[0] + d[1] * f.right[1] + d[2] * f.right[2];
    const float ahead = d[0] * f.forward[0] + d[1] * f.forward[1] + d[2] * f.forward[2];
    return f.point[1] + along * f.right[1] + ahead * f.forward[1];
}

bool wetSurface(int surface) {
    // 0x55308c + surface x 0x24 + kind x 0x240, kinds 1 and 2.
    static constexpr int kWet[16] = {0, 10, 11, 11, 10, 11, 10, 10, 0, 1, 10, 10, 10, 11, 0, 1};
    return surface >= 0 && surface < 16 && kWet[surface] == 10;
}

bool parseCarpAi(const std::string& text, AiCarData& out) {
    // Each field is a line "name(n)" and its values on the next line.
    auto values = [&](int number, std::vector<float>& into) {
        const std::string tag = "(" + std::to_string(number) + ")";
        for (size_t at = text.find(tag); at != std::string::npos; at = text.find(tag, at + 1)) {
            const size_t eol = text.find('\n', at);
            if (eol == std::string::npos)
                return false;
            if (text.find_first_not_of(" \t\r", at + tag.size()) != eol)
                continue; // not the end of a name line
            const size_t end = text.find('\n', eol + 1);
            std::string line = text.substr(eol + 1, end == std::string::npos ? std::string::npos : end - eol - 1);
            into.clear();
            const char* c = line.c_str();
            while (*c) {
                char* next = nullptr;
                const float v = std::strtof(c, &next);
                if (next == c) {
                    ++c;
                    continue;
                }
                into.push_back(v);
                c = next;
            }
            return !into.empty();
        }
        return false;
    };
    std::vector<float> v;
    if (!values(15, v))
        return false;
    out.topSpeedCap = v[0];
    if (!values(14, v))
        return false;
    out.maxVelocity = v[0];
    if (!values(18, v))
        return false;
    out.maxBrake = v[0];
    if (values(30, v))
        out.latGrip = v[0];
    if (values(2, v))
        out.mass = v[0];
    if (values(31, v))
        out.downforce = v[0];
    for (int row = 0; row < 8; ++row) {
        if (!values(67 + row, v) || v.size() < 14)
            return false;
        for (int i = 0; i < 14; ++i)
            out.accel[size_t(row * 14 + i)] = v[size_t(i)];
    }
    return true;
}

float aiDriverFactor(const AiDriver& d, int step) {
    float f = d.skill == 0 ? 0.94f : d.skill < 2 ? 0.97f : 1.0f;
    const int quarter = (step >> 7) & 3;
    if (d.surges && quarter == 1)
        f *= 1.05f;
    if (d.surges && quarter == 3)
        f *= 0.95f;
    if (d.personality == 0)
        f *= 0.95f;
    else if (d.personality == 1)
        f *= 0.97f;
    return f;
}

float aiTrackFactor(int track) {
    return track == 0 || track == 3 || track == 5 || track == 7 ? 0.82f : 0.84f;
}

int aiLaneAt(const TrackCollision::RoadPoint& p, float offset) {
    const bool right = offset >= 0;
    const int width = right ? p.extra[2] : p.extra[1];
    // 1 / (k / 4), k the lane width in quarters (0x56f0e0, 0 for none).
    const int k = static_cast<int>(std::nearbyint(static_cast<float>(width << 15) / 65536.0f * 4.0f));
    const float inverse = k > 0 ? 1.0f / (static_cast<float>(k) * 0.25f) : 0.0f;
    const int lane = (right ? 7 : 6) + static_cast<int>(std::nearbyint(inverse * offset));
    return std::clamp(lane, 0, 13);
}

bool aiLaneOpen(const TrackCollision::RoadPoint& p, int lane) {
    const int bit = 15 - std::clamp(lane + 1, 0, 15);
    return ((p.unknown & 0xffff) >> bit & 1) != 0;
}

float aiLaneOffset(const TrackCollision::RoadPoint& p, int lane, float halfWidth, int nudge) {
    const int left = p.leftLanes(), right = p.rightLanes();
    const float leftWidth = static_cast<float>(p.extra[1]) * 0.5f, rightWidth = static_cast<float>(p.extra[2]) * 0.5f;
    float t = 0;
    if (lane == 6 - left || lane == right + 7)
        t = lane < 7 ? -static_cast<float>(6 - lane) * leftWidth - halfWidth : static_cast<float>(lane - 7) * rightWidth + halfWidth;
    else
        t = lane < 7 ? -static_cast<float>(6 - lane) * leftWidth - leftWidth * 0.5f
                     : static_cast<float>(lane - 7) * rightWidth + rightWidth * 0.5f;
    t = std::min(t, p.rightWall - halfWidth);
    t = std::max(t, halfWidth - p.leftWall);
    return t + static_cast<float>(nudge) * halfWidth;
}

int aiLineLane(const std::vector<uint8_t>& speeds, int slice, int sliceCount) {
    const size_t at = 16 + static_cast<size_t>((sliceCount + 1) / 2) + static_cast<size_t>(std::max(slice, 0));
    return speeds.size() > 16 && at < speeds.size() ? speeds[at] >> 4 : 7;
}

namespace {

// The computer's model across the road (car_ai_model 0x408600, a 32 Hz
// task, then ai_integrate 0x4071a0), on the car's world velocity (+0xA4,
// AiCar::vel) and its body's yaw in the world (AiCar::yaw): `sideSpeed`
// is the velocity along the slice's right, `heading` the yaw from the way
// it runs, towards its right, `yawRate` the spin about the up (+0x4FC as
// the controllers read it), `carSide` the velocity along its own right
// (+0x4EC). As the world velocity is kept from slice to slice, on a bend
// the car runs wide unless pushed back. The controller (car_ai_model):
// ai_ctl_slow (0x407c30) reversing or with +0x4F4 under 0; ai_ctl_slide
// (0x407fa0) on a side of the slice with no lanes (aiModelSteer), more
// than 0.04 x the surface's grip
// off the point it steers at (dry: 1), under 0.5 m/s or slipping sideways
// (+0xB8 over 2.5 and |+0x4EC| over half of +0x4F4);
// 2.5 m/s); ai_ctl_slow under 13.41 m/s; else ai_ctl_rails (0x4076b0).
// Gains from ai_gains_init (0x405e60).
constexpr float kPi = 3.14159265358979f;

// The point steered at (0x406e50 -> 0x406d40 with 0x406980): from 4
// slices on, stepping one at a time towards the look-ahead slice
// (0x40e1b0: speed x 1.7 / 6 rounded to the nearest, fistp, held to 4-20,
// behind the car while it reverses), and stopping at the first slice
// whose forward in x and z (the .col bytes, 0x40ef70) has a dot of 0.7 or
// less, either way (|dot|), with the car's slice's: the first such slice
// itself. On Hometown (track 0) a car on slices 0x225-0x235 takes the
// look-ahead slice as it is (0x4069c2-0x4069dc).
} // namespace

int aiSlice(const AiCar& c) { return c.roadSlice >= 0 ? c.roadSlice : c.slice; }

namespace {

int aiSteerSlice(const TrackCollision& col, const AiCar& me, int track) {
    const int n = static_cast<int>(col.road.size());
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    auto at = [&](int s) -> const TrackCollision::RoadPoint& { return col.road[size_t(wrap(s))]; };
    const int slice = aiSlice(me);
    const auto& here = at(slice);
    int count = std::clamp(static_cast<int>(std::nearbyint(me.speed * 1.7f / 6.0f)), 4, 20);
    if (me.reversing)
        count = -count;
    const int far = wrap(slice + me.way * count);
    if (track == 0 && slice > 0x224 && slice < 0x236)
        return far;
    int k = wrap(slice + me.way * 4);
    float d = 1;
    for (int guard = 0; guard < n && k != far && std::fabs(d) > 0.7f; ++guard) {
        k = wrap(k + me.way);
        const auto& f = at(k).forward;
        d = f[0] * here.forward[0] + f[2] * here.forward[2];
    }
    return k;
}

float dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> unit3(std::array<float, 3> v) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 0)
        for (float& x : v)
            x /= l;
    return v;
}

std::array<float, 3> cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The frame the views are taken in: the slice's forward along the way,
// its up, and the way's right square to both (up x forward, as the
// road polygons' and the boxes' are), so a heading of h faces
// forward cos h + right sin h.
void aiWayFrame(const TrackCollision& col, int slice, int way, std::array<float, 3>& f, std::array<float, 3>& r,
                std::array<float, 3>& u) {
    const int n = static_cast<int>(col.road.size());
    const auto& a = col.road[size_t(((slice % n) + n) % n)];
    u = unit3(a.normal);
    if (dot3(u, u) == 0)
        u = {0, 1, 0};
    const float w = way < 0 ? -1.0f : 1.0f;
    f = {a.forward[0] * w, a.forward[1] * w, a.forward[2] * w};
    const float k = dot3(f, u);
    f = unit3({f[0] - k * u[0], f[1] - k * u[1], f[2] - k * u[2]});
    r = unit3(cross3(u, f));
}

// The heading as a view: the body's yaw against the slice's way.
float aiHeadingOf(const TrackCollision& col, const AiCar& c, float yaw) {
    std::array<float, 3> f{}, r{}, u{};
    aiWayFrame(col, c.slice, c.way, f, r, u);
    const std::array<float, 3> h{std::sin(yaw), 0, std::cos(yaw)};
    return std::atan2(dot3(h, r), dot3(h, f));
}

float aiYawOf(const TrackCollision& col, const AiCar& c, float heading) {
    std::array<float, 3> f{}, r{}, u{};
    aiWayFrame(col, c.slice, c.way, f, r, u);
    const float ch = std::cos(heading), sh = std::sin(heading);
    return std::atan2(f[0] * ch + r[0] * sh, f[2] * ch + r[2] * sh);
}

// Whether the views were written since they were last taken from the
// world state (or it was never set).
bool aiViewsWritten(const AiCar& c) {
    return !c.worldKept || c.speed != c.keptSpeed || c.sideSpeed != c.keptSide || c.way != c.keptWay;
}

std::array<float, 3> aiVelocityFromViews(const TrackCollision& col, const AiCar& c) {
    // The inverse of aiViews: along the road's forward the speed, and
    // square to it, along the slice's right, what gives the side speed.
    const int n = static_cast<int>(col.road.size());
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const float w = c.way < 0 ? -1.0f : 1.0f;
    const std::array<float, 3> rf = unit3(c.roadRightSet ? c.roadForward : a.forward);
    const float k = dot3(a.right, rf);
    const std::array<float, 3> r = unit3({a.right[0] - k * rf[0], a.right[1] - k * rf[1], a.right[2] - k * rf[2]});
    const float rr = dot3(r, a.right);
    const float across = rr != 0 ? (c.sideSpeed - w * c.speed * k) / rr : 0;
    std::array<float, 3> v{};
    for (size_t i = 0; i < 3; ++i)
        v[i] = rf[i] * w * c.speed + r[i] * across;
    return v;
}

// The views from the world state: +0x7C0 (0x42cdd0, the velocity along
// the road polygon's forward +0x13C) along the way, the velocity along
// the slice's right, and the heading.
void aiViews(const TrackCollision& col, AiCar& c) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return;
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const float w = c.way < 0 ? -1.0f : 1.0f;
    const std::array<float, 3> rf = unit3(c.roadRightSet ? c.roadForward : a.forward);
    c.speed = w * dot3(c.vel, rf);
    c.sideSpeed = dot3(c.vel, a.right);
    c.heading = aiHeadingOf(col, c, c.yaw);
    c.keptSpeed = c.speed;
    c.keptSide = c.sideSpeed;
    c.keptHeading = c.heading;
    c.keptWay = c.way;
    c.keptSlice = c.slice;
    c.worldKept = true;
}

// The world state from views written since (see AiCar::vel).
void aiWorldIn(const TrackCollision& col, AiCar& c) {
    if (col.road.size() < 2)
        return;
    const bool velocity = aiViewsWritten(c);
    const bool heading = !c.worldKept || c.heading != c.keptHeading || c.way != c.keptWay;
    if (!velocity && !heading)
        return;
    if (velocity)
        c.vel = aiVelocityFromViews(col, c);
    if (heading)
        c.yaw = aiYawOf(col, c, c.heading);
    aiViews(col, c);
}

// The body's axes from its yaw on the slice's up (the roll and pitch a hit
// leaves are turned in by whoever needs them).
void aiBodyAxesOf(const TrackCollision& col, const AiCar& c, float yaw, std::array<float, 3>& forward,
                  std::array<float, 3>& right, std::array<float, 3>& up) {
    std::array<float, 3> f{}, r{};
    aiWayFrame(col, c.slice, c.way, f, r, up);
    const std::array<float, 3> h{std::sin(yaw), 0, std::cos(yaw)};
    const float k = dot3(h, up);
    forward = unit3({h[0] - k * up[0], h[1] - k * up[1], h[2] - k * up[2]});
    right = unit3(cross3(up, forward));
}

// The aim and the speed (0x408870, 0x4066c0 with 0x4064f0 and 0x4063e0),
// for one step of 64 a second. The computer's model (car_ai_physics
// 0x4088f0) is a 32 Hz task: on the even steps only, its time step
// 0x56ec58 1/32 s, so the aim moves 0.2 every other step (3.2 in the
// half second, as t6-log's dumps have it). The car's own motion (its
// side offset, heading) every step.
float aiChord(const TrackCollision& col, int slice);

} // namespace

std::array<float, 3> aiVelocity(const TrackCollision& col, const AiCar& c) {
    if (col.road.size() < 2)
        return {};
    return aiViewsWritten(c) ? aiVelocityFromViews(col, c) : c.vel;
}

void aiBodyAxes(const TrackCollision& col, const AiCar& c, std::array<float, 3>& forward, std::array<float, 3>& right,
                std::array<float, 3>& up) {
    if (col.road.size() < 2) {
        forward = {0, 0, 1};
        right = {1, 0, 0};
        up = {0, 1, 0};
        return;
    }
    const bool heading = !c.worldKept || c.heading != c.keptHeading || c.way != c.keptWay;
    aiBodyAxesOf(col, c, heading ? aiYawOf(col, c, c.heading) : c.yaw, forward, right, up);
}

void aiSetWorld(AiCar& c, const TrackCollision& col, const std::array<float, 3>& velocity,
                const std::array<float, 3>& forward) {
    if (col.road.size() < 2)
        return;
    c.vel = velocity;
    c.yaw = std::atan2(forward[0], forward[2]);
    aiViews(col, c);
}

void aiSetWorld(AiCar& c, const TrackCollision& col, const std::array<float, 3>& velocity,
                const std::array<float, 3>& forward, const std::array<float, 3>& up) {
    aiSetWorld(c, col, velocity, forward);
    if (col.road.size() >= 2)
        aiSetTilt(c, col, up);
}

void aiLocate(AiCar& c, const TrackCollision& col, const std::array<float, 3>& p) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return;
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    // In a slice's run the place is its point + lateral x its right +
    // along x (the next point - it). On a bend the runs' rights turn, so
    // on the inside two runs overlap and on the outside there is a gap
    // between them: a place in the gap is past the one run's end and
    // before the next's start, and stays on the first (along over 1)
    // until it is in the next.
    auto solve = [&](int slice, float& along, float& lateral) {
        const auto& s0 = col.road[size_t(slice)];
        const auto& s1 = col.road[size_t(wrap(slice + 1))];
        std::array<float, 3> e2{}, w{};
        for (size_t k = 0; k < 3; ++k) {
            e2[k] = s1.position[k] - s0.position[k];
            w[k] = p[k] - s0.position[k];
        }
        const auto& e1 = s0.right;
        const float a11 = dot3(e1, e1), a12 = dot3(e1, e2), a22 = dot3(e2, e2);
        const float det = a11 * a22 - a12 * a12;
        if (det == 0)
            return false;
        lateral = (dot3(e1, w) * a22 - dot3(e2, w) * a12) / det;
        along = (dot3(e2, w) * a11 - dot3(e1, w) * a12) / det;
        return true;
    };
    int slice = wrap(c.slice);
    float along = c.along, lateral = c.lateral;
    if (!solve(slice, along, lateral))
        return;
    for (int tries = 0; tries < 6; ++tries) {
        int next = slice;
        if (along >= 1)
            next = wrap(slice + 1);
        else if (along < 0)
            next = wrap(slice - 1);
        else
            break;
        float a2 = 0, l2 = 0;
        if (!solve(next, a2, l2))
            break;
        // The gap: on to the next run it is before its start (or back to
        // the one before, past its end); it stays where it is.
        if ((along >= 1 && a2 < 0) || (along < 0 && a2 >= 1))
            break;
        slice = next;
        along = a2;
        lateral = l2;
    }
    int moved = slice - wrap(c.slice);
    if (moved > n / 2)
        moved -= n;
    else if (moved < -(n / 2))
        moved += n;
    c.distance += moved * (c.way < 0 ? -1 : 1);
    c.slice = slice;
    c.along = along;
    c.lateral = lateral;
}
float aiSegmentLength(const TrackCollision& col, int slice, float lateral) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return 1;
    const auto& a = col.road[size_t(((slice % n) + n) % n)];
    const auto& b = col.road[size_t((((slice + 1) % n) + n) % n)];
    std::array<float, 3> d{};
    for (size_t k = 0; k < 3; ++k)
        d[k] = b.position[k] - a.position[k] + lateral * (b.right[k] - a.right[k]);
    return std::max(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 0.01f);
}

namespace {
float aiCentreLift(const TrackCollision& col, const AiCar& c);
}

float aiMeasuredSide(const TrackCollision& col, const AiCar& c) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return c.lateral;
    if (!c.roadRightSet)
        return c.lateral + c.along * aiChord(col, c.slice);
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const auto& b = col.road[size_t((((c.slice + 1) % n) + n) % n)];
    const auto& o = col.road[size_t(((aiSlice(c) % n) + n) % n)]; // +0x1C's point
    float k1 = 0, k2 = 0, k0 = 0;
    for (size_t k = 0; k < 3; ++k) {
        k0 += (a.position[k] - o.position[k]) * c.roadRight[k];
        k1 += a.right[k] * c.roadRight[k];
        k2 += (b.position[k] - a.position[k]) * c.roadRight[k];
    }
    return k0 + c.lateral * k1 + c.along * k2 + aiCentreLift(col, c);
}

float aiLateralFor(const TrackCollision& col, const AiCar& c, float side) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return side;
    if (!c.roadRightSet)
        return side - c.along * aiChord(col, c.slice);
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const auto& b = col.road[size_t((((c.slice + 1) % n) + n) % n)];
    const auto& o = col.road[size_t(((aiSlice(c) % n) + n) % n)];
    float k1 = 0, k2 = 0, k0 = 0;
    for (size_t k = 0; k < 3; ++k) {
        k0 += (a.position[k] - o.position[k]) * c.roadRight[k];
        k1 += a.right[k] * c.roadRight[k];
        k2 += (b.position[k] - a.position[k]) * c.roadRight[k];
    }
    side -= aiCentreLift(col, c);
    return std::fabs(k1) > 0.1f ? (side - k0 - c.along * k2) / k1 : side - c.along * aiChord(col, c.slice);
}

// The body's axes as the model has them (its yaw on the slice's up,
// aiBodyAxes), then rolled about its forward and pitched about its right
// by what a hit left (+0xC0, +0xCC, +0xD8).
namespace {
// The unit axes (the rows' orthonormal part, Q).
void aiUnitAxes(const TrackCollision& col, const AiCar& c, std::array<float, 3>& f1, std::array<float, 3>& r1,
                std::array<float, 3>& u1) {
    constexpr float kTurn = 2 * 3.14159265358979f;
    std::array<float, 3> forward{}, up{}, right{};
    aiBodyAxes(col, c, forward, right, up);
    const float cr = std::cos(c.roll * kTurn), sr = std::sin(c.roll * kTurn);
    const float cp = std::cos(c.pitch * kTurn), sp = std::sin(c.pitch * kTurn);
    for (size_t k = 0; k < 3; ++k) {
        r1[k] = right[k] * cr + up[k] * sr;
        u1[k] = up[k] * cr - right[k] * sr;
    }
    for (size_t k = 0; k < 3; ++k) {
        f1[k] = forward[k] * cp + u1[k] * sp;
        u1[k] = u1[k] * cp - forward[k] * sp;
    }
    // The forward keeps the yaw's direction across the world (+0x210 is
    // taken from it): the yaw's line laid on the tilted up.
    const std::array<float, 3> h{std::sin(c.yaw), 0, std::cos(c.yaw)};
    const bool heading = !c.worldKept || c.heading != c.keptHeading || c.way != c.keptWay;
    if (!heading && (c.roll != 0 || c.pitch != 0)) {
        const float k = dot3(h, u1);
        const std::array<float, 3> g = unit3({h[0] - k * u1[0], h[1] - k * u1[1], h[2] - k * u1[2]});
        if (dot3(g, g) > 0) {
            f1 = g;
            r1 = unit3(cross3(u1, f1));
        }
    }
}

// The rows as the game has them: P times the unit rows (AiCar::rowShape).
void aiShapeRows(const AiCar& c, std::array<float, 3>& f, std::array<float, 3>& r, std::array<float, 3>& u) {
    if (!c.rowsShaped)
        return;
    const auto& p = c.rowShape;
    std::array<float, 3> r2{}, u2{}, f2{};
    for (size_t k = 0; k < 3; ++k) {
        r2[k] = p[0] * r[k] + p[1] * u[k] + p[2] * f[k];
        u2[k] = p[3] * r[k] + p[4] * u[k] + p[5] * f[k];
        f2[k] = p[6] * r[k] + p[7] * u[k] + p[8] * f[k];
    }
    r = r2;
    u = u2;
    f = f2;
}
} // namespace

void aiTiltedAxes(const TrackCollision& col, const AiCar& c, std::array<float, 3>& f1, std::array<float, 3>& r1,
                  std::array<float, 3>& u1) {
    aiUnitAxes(col, c, f1, r1, u1);
    aiShapeRows(c, f1, r1, u1);
}

namespace {
// +0x7B8 is measured from +0x98 (0x4a2bc0), the body's centre, which
// car_on_road (0x4a1610) keeps with its box's lowest corner on the road
// polygon's plane (clearance +0x15C = (+0x98 - the point +0x148) . the up
// +0x130 less |up . right| half width + |up . up| half height + |up .
// forward| half length, car_clearance 0x4a0440; under 0 the centre is
// pushed out along +0x130). The page's place is that centre brought
// square onto the slice's plane (its right and the chord to the next
// point), so the centre is above it along the plane's normal by H, with
// (place + H normal - point) . up = the box's reach: the side as measured
// gains H x (normal . the road's right). Along the polygon's up itself
// the road's right (up x forward, 0x420230) measures nothing.
// The body's centre over the page's place, along the normal `m` of the
// slice's plane: the place + H x m puts the box's lowest corner on the
// road polygon's plane (car_clearance 0x4a0440 at 0). 0 unset.
float aiCentreHeight(const TrackCollision& col, const AiCar& c, std::array<float, 3>& m) {
    const int n = static_cast<int>(col.road.size());
    m = {0, 1, 0};
    if (n < 2 || !c.roadUpSet || !c.roadRightSet)
        return 0;
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const auto& b = col.road[size_t((((c.slice + 1) % n) + n) % n)];
    const std::array<float, 3> chord{b.position[0] - a.position[0], b.position[1] - a.position[1],
                                     b.position[2] - a.position[2]};
    m = unit3(cross3(chord, a.right));
    if (dot3(m, a.normal) < 0)
        m = {-m[0], -m[1], -m[2]};
    const float mn = dot3(m, c.roadUp);
    if (mn < 0.5f)
        return 0;
    std::array<float, 3> f1{}, r1{}, u1{};
    aiTiltedAxes(col, c, f1, r1, u1);
    const float reach = std::fabs(dot3(c.roadUp, r1)) * c.halfWidth + std::fabs(dot3(c.roadUp, u1)) * c.halfHeight +
                        std::fabs(dot3(c.roadUp, f1)) * c.halfLength;
    std::array<float, 3> q{};
    for (size_t k = 0; k < 3; ++k)
        q[k] = a.position[k] + c.lateral * a.right[k] + c.along * chord[k];
    const std::array<float, 3> d{q[0] - c.roadPoint[0], q[1] - c.roadPoint[1], q[2] - c.roadPoint[2]};
    return (reach - dot3(d, c.roadUp)) / mn;
}

float aiCentreLift(const TrackCollision& col, const AiCar& c) {
    std::array<float, 3> m{};
    const float h = aiCentreHeight(col, c, m);
    return h * dot3(m, c.roadRight);
}
} // namespace

float aiCarExtent(const TrackCollision& col, const AiCar& c) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return c.halfWidth;
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    std::array<float, 3> r1{}, u1{}, f1{};
    aiTiltedAxes(col, c, f1, r1, u1);
    const std::array<float, 3>& road = c.roadRightSet ? c.roadRight : a.right;
    auto dot = [](const std::array<float, 3>& x, const std::array<float, 3>& y) {
        return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    };
    return std::fabs(dot(road, r1)) * c.halfWidth + std::fabs(dot(road, u1)) * c.halfHeight +
           std::fabs(dot(road, f1)) * c.halfLength;
}

void aiRaceTick(std::vector<AiCar>& cars, const TrackCollision& col, int step) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || (step & 1) != 0)
        return;
    for (AiCar& c : cars) {
        // +0x7C0 / +0x7C4 from the velocity along the road polygon's
        // forward as it is now (0x42cdd0).
        if (c.sideKept) {
            aiWorldIn(col, c);
            aiViews(col, c);
        }
        c.extent = aiCarExtent(col, c);
        if (c.sideKept)
            c.side = aiMeasuredSide(col, c);
        c.lane = aiLaneAt(col.road[size_t(((aiSlice(c) % n) + n) % n)], c.side);
    }
}

void aiRoadFrame(const Track& track, const TrackCollision& col, AiCar& c) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || track.blocks.empty())
        return;
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const auto& b = col.road[size_t((((c.slice + 1) % n) + n) % n)];
    c.roadTrack = &track;
    const float lateral = c.sideKept ? c.lateral : aiLateralFor(col, c, c.offset);
    std::array<float, 3> at{};
    for (size_t k = 0; k < 3; ++k)
        at[k] = a.position[k] + a.right[k] * lateral + (b.position[k] - a.position[k]) * c.along;
    // The search starts from the slice it was on (+0x10, kept); its slice
    // is the one whose polygons it is in.
    c.road.slice = c.roadSlice >= 0 && std::abs(((c.roadSlice - c.slice) % n + n + n / 2) % n - n / 2) <= 2
                       ? c.roadSlice
                       : ((c.slice % n) + n) % n;
    if (roadSearch(track, c.road, at[0], at[2])) {
        c.roadSlice = c.road.foundSlice;
        RoadFrame f;
        if (static_cast<size_t>(c.road.foundSlice >> 3) < track.blocks.size() &&
            polygonRoadFrame(track.blocks[size_t(c.road.foundSlice >> 3)], size_t(c.road.polygon), f)) {
            c.roadRight = f.right;
            c.roadForward = f.forward;
            c.roadUp = f.up;
            c.roadPoint = f.point;
            c.roadUpSet = true;
            c.roadRightSet = true;
            return;
        }
    }
    // Not found inside a polygon of the runs searched (off the road's side,
    // +0x6C set, or past them): the search walks +0x10 back to the slice it
    // started from and clears +0x14 (0x41f018-0x41f078), so
    // trackpos_road_frame (0x420230) takes that slice's road point's own
    // vectors and point (its bytes +0x0D-+0x17), as car_road_frame
    // (0x49ffa0) does for +0x124 and +0x148; the polygon found before
    // doesn't stay.
    const auto& from = col.road[size_t(((c.road.slice % n) + n) % n)];
    c.roadRight = from.right;
    c.roadForward = from.forward;
    c.roadUp = from.normal;
    c.roadPoint = from.position;
    c.roadUpSet = true;
    c.roadRightSet = true;
    c.roadSlice = c.road.slice;
}

namespace {

std::array<float, 3> aiPlace(const TrackCollision& col, const AiCar& c);

// What the dynamics and car_ai_physics hand the model on an even step:
// whether it ran, the velocity along the body's forward (+0x4F4) and the
// cheap ground speed (+0xB8).
struct AiDynamics {
    bool ran = false;
    // car_ai_physics (0x4088f0) left the model out (tipped: the body's
    // friction instead), or car_ai_model (0x408600) coasted it out of the
    // race (ai_finished_coast): no controller, no forces.
    bool tipped = false, coast = false;
    float forward = 0, ground = 0;
    // ai_ctl_slide's side force (+0x4E0, before the start's push) and the
    // body's up's y, for the start's push (aiMove).
    float slideSide = 0, upY = 1;
    // For the skid levels (aiSkid): the rails' side speed 2 slices on
    // (0x406a80) and the velocity along the car's right (+0x4EC) as the
    // controller found them.
    float railsSide = 0, carSide = 0;
};

// car_on_road's frame (0x4a1610). Rows as the game keeps them: right, up,
// forward (+0x124, +0x130, +0x13C).
using AiRows = std::array<float, 9>;

// 0x4b73c0: a quaternion (x, y, z, w) from the rows, the trace's way, else
// from the largest diagonal (next axis 1, 2, 0: 0x4b6a1c).
std::array<float, 4> aiQuatOfRows(const AiRows& m) {
    std::array<float, 4> q{};
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0) {
        const float r = std::sqrt(trace + 1.0f);
        const float f = 0.5f / r;
        q[3] = r * 0.5f;
        q[0] = (m[5] - m[7]) * f;
        q[1] = (m[6] - m[2]) * f;
        q[2] = (m[1] - m[3]) * f;
        return q;
    }
    static constexpr int kNext[3] = {1, 2, 0};
    int i = m[4] > m[0] ? 1 : 0;
    if (m[8] > m[size_t(4 * i)])
        i = 2;
    const int j = kNext[i], k = kNext[j];
    auto at = [&](int r, int c) { return m[size_t(3 * r + c)]; };
    float r = std::sqrt(at(i, i) - (at(j, j) + at(k, k)) + 1.0f);
    q[size_t(i)] = r * 0.5f;
    if (r != 0)
        r = 0.5f / r;
    q[3] = (at(j, k) - at(k, j)) * r;
    q[size_t(j)] = (at(i, j) + at(j, i)) * r;
    q[size_t(k)] = (at(i, k) + at(k, i)) * r;
    return q;
}

// 0x4b71e0: the turn t of the way from a to b, the shorter way round; when
// the two are within 1e-4 (0x53ff60) of each other, straight between them
// (not brought back to a unit quaternion).
std::array<float, 4> aiQuatBetween(const std::array<float, 4>& a, std::array<float, 4> b, float t) {
    double c = double(a[3] * b[3] + a[2] * b[2] + a[0] * b[0] + a[1] * b[1]);
    if (c < 0) {
        c = -c;
        for (float& x : b)
            x = -x;
    }
    double wa = 1.0 - t, wb = t;
    if (1.0 - c > 1e-4) {
        const double angle = std::acos(c), sine = std::sin(angle);
        wa = std::sin((1.0 - t) * angle) / sine;
        wb = std::sin(t * angle) / sine;
    }
    const float fa = static_cast<float>(wa), fb = static_cast<float>(wb);
    return {b[0] * fb + a[0] * fa, b[1] * fb + a[1] * fa, b[2] * fb + a[2] * fa, fb * b[3] + a[3] * fa};
}

// 0x4b7590: the rows of a quaternion.
AiRows aiRowsOfQuat(const std::array<float, 4>& q) {
    const float y2 = q[1] * 2, z2 = q[2] * 2, wx2 = q[3] * q[0] * 2, xx = 1.0f - q[0] * q[0] * 2;
    AiRows m{};
    m[1] = q[0] * y2 + q[3] * z2;
    m[2] = q[0] * z2 - q[3] * y2;
    m[3] = q[0] * y2 - q[3] * z2;
    m[5] = q[1] * z2 + wx2;
    m[6] = q[0] * z2 + q[3] * y2;
    m[7] = q[1] * z2 - wx2;
    m[0] = (1.0f - q[1] * y2) - q[2] * z2;
    m[4] = xx - q[2] * z2;
    m[8] = xx - q[1] * y2;
    return m;
}

} // namespace

std::array<float, 9> aiHalfTurn(const std::array<float, 9>& from, const std::array<float, 9>& to) {
    return aiRowsOfQuat(aiQuatBetween(aiQuatOfRows(from), aiQuatOfRows(to), 0.5f));
}

std::array<float, 3> aiContactVelocity(const std::array<float, 3>& v, const std::array<float, 9>& rows) {
    const float a = v[0] * rows[0] + v[1] * rows[1] + v[2] * rows[2];
    float into = v[0] * rows[3] + v[1] * rows[4] + v[2] * rows[5];
    const float b = v[0] * rows[6] + v[1] * rows[7] + v[2] * rows[8];
    if (into < 0.5f)
        into = 0;
    std::array<float, 3> out{};
    for (size_t k = 0; k < 3; ++k)
        out[k] = b * rows[6 + k] + into * rows[3 + k] + a * rows[k];
    return out;
}

namespace {

// The frame car_on_road (0x4a1610-0x4a1a2a) takes the velocity's part
// into the road out of: the car's road rows (+0x124) turned half way
// (0x4b7680) to the road polygon's at the box's bottom one tick ahead,
// on the side it moves to: the centre + the velocity / 32 (0x53c824) +
// the half width x (v^ . right) along the body's right - the half height
// along its up + the half length x (v^ . forward) along its forward (v^
// the velocity made unit, 0x4972f0). That point's track position
// (trackpos_update from the car's own) must be inside a polygon of the
// run (+0x6C 0) whose surface is neither 0 nor 14, and another polygon
// than the car's (+0x6A, set when the slice or polygon changed); and the
// turn is taken
// unless the road there falls away: the ups' y more than 0.13 apart
// (1 - |n.y - n'.y| under 0x5efdd8, 0.87), the velocity into the road
// ahead and its up lower (then 0x42efe0 pushes the car over the crest,
// not ported, and the car's rows stay). Not found, the car's rows.
bool aiContactFrame(const TrackCollision& col, const AiCar& c, AiRows& out) {
    if (!c.roadUpSet || !c.roadRightSet)
        return false;
    for (size_t k = 0; k < 3; ++k) {
        out[k] = c.roadRight[k];
        out[3 + k] = c.roadUp[k];
        out[6 + k] = c.roadForward[k];
    }
    if (!c.roadTrack || c.roadTrack->blocks.empty())
        return true;
    std::array<float, 3> m{};
    const float h = aiCentreHeight(col, c, m);
    std::array<float, 3> centre = aiPlace(col, c);
    for (size_t k = 0; k < 3; ++k)
        centre[k] += h * m[k];
    std::array<float, 3> v = c.vel;
    const float speed = std::sqrt(dot3(v, v));
    if (speed != 0)
        for (float& x : v)
            x /= speed;
    std::array<float, 3> f1{}, r1{}, u1{};
    aiTiltedAxes(col, c, f1, r1, u1);
    const float across = c.halfWidth * dot3(v, r1), ahead = c.halfLength * dot3(v, f1);
    std::array<float, 3> p{};
    for (size_t k = 0; k < 3; ++k)
        p[k] = c.vel[k] * 0.03125f + centre[k] + (ahead * f1[k] - c.halfHeight * u1[k] + across * r1[k]);
    RoadSearch search = c.road;
    if (c.road.found)
        search.slice = c.road.foundSlice;
    if (!roadSearch(*c.roadTrack, search, p[0], p[2]) || search.side != 0)
        return true;
    // trackpos_update (0x41f6e0) keeps the car's polygon while the point is
    // inside it and clears +0x6A, which car_on_road asks to be set: the
    // point in the car's own polygon, the car's rows as they are.
    if (c.road.found && search.foundSlice == c.road.foundSlice && search.polygon == c.road.polygon)
        return true;
    const size_t block = static_cast<size_t>(search.foundSlice >> 3);
    if (block >= c.roadTrack->blocks.size())
        return true;
    const Track::Block& b = c.roadTrack->blocks[block];
    const size_t index = static_cast<size_t>(search.polygon);
    if (8 * index + 1 >= b.polygonRoad.size())
        return true;
    const int surface = b.polygonRoad[8 * index + 1] & 0x3f;
    RoadFrame f;
    if (surface == 0 || surface == 14 || !polygonRoadFrame(b, index, f))
        return true;
    const float nearness = 1.0f - std::fabs(c.roadUp[1] - f.up[1]);
    if (nearness < 0.87f && dot3(c.vel, f.up) < 0 && c.roadUp[1] > f.up[1])
        return true;
    AiRows there{};
    for (size_t k = 0; k < 3; ++k) {
        there[k] = f.right[k];
        there[3 + k] = f.up[k];
        there[6 + k] = f.forward[k];
    }
    out = aiHalfTurn(out, there);
    return true;
}

// A yaw force (+0x2F8, about the body's up row) into the world's spins
// through the body's rows (ai_integrate 0x4071a0), over 1/32 s: the yaw
// (+0xE8) gains it x the up's y, the spins about x and z (+0xE4, +0xEC) x
// its x and z (up to 2e-4 a call). car_integrate_turn (0x4a2e20) turns the
// body by all three before car_on_road sets the last two again (aiTilt),
// so those turn the roll and pitch here (as aiTilt's rates do).
void aiYawForce(AiCar& me, const std::array<float, 3>& f, const std::array<float, 3>& r, const std::array<float, 3>& up,
                float force) {
    me.yawRate += force * up[1] / 32.0f;
    const float wx = force * up[0] / 32.0f, wz = force * up[2] / 32.0f;
    me.spinX += wx;
    me.spinZ += wz;
    me.roll += (wx * f[0] + wz * f[2]) / 32.0f;
    me.pitch -= (wx * r[0] + wz * r[2]) / 32.0f;
}

// The body's friction on the road (0x4aa8e7, for a tipped car, on the
// ground): with the clearance under 0.2 (+0x15C as an int under
// 0x3e4cccce) and the road's normal y (+0x130) at least 0.5 (0x53dcc4), the
// velocity's part along the road's plane x 0.94 and the spin about its
// normal x 0.98. (In the air, +0x164 set, x 0.98 and x 0.998 when an axis
// of the body is near the normal; the page doesn't run this then.)
void aiBodyFriction(AiCar& me, const std::array<float, 3>& n) {
    if (!(me.lift < 0.2f) || n[1] < 0.5f)
        return;
    const float into = dot3(n, me.vel);
    for (size_t k = 0; k < 3; ++k)
        me.vel[k] = (me.vel[k] - into * n[k]) * 0.94f + into * n[k];
    std::array<float, 3> spin{me.spinX, me.yawRate, me.spinZ};
    const float about = dot3(n, spin);
    for (size_t k = 0; k < 3; ++k)
        spin[k] = spin[k] - about * n[k] + about * n[k] * 0.98f;
    me.spinX = spin[0];
    me.yawRate = spin[1];
    me.spinZ = spin[2];
}

// The model's half across the road on an even step (car_dynamics 0x4a2830,
// car_ai_physics 0x4088f0, car_ai_model 0x408600 to the controller and
// ai_integrate 0x4071a0's side and yaw parts), on the world velocity.
// `ramp`: ai_lane_ramp (0x408870) and ai_reverse_end (0x4060d0) first, as
// car_ai_model calls them before the heading error.
AiDynamics aiModelSteer(AiCar& me, const TrackCollision& col, int step, int track, bool ramp) {
    AiDynamics out;
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return out;
    const TrackCollision::RoadPoint* here = &col.road[size_t(((me.slice % n) + n) % n)];
    if (!me.sideKept) {
        me.side = me.offset;
        me.lateral = aiLateralFor(col, me, me.side);
        me.sideSpeed = me.carSide = me.heading = me.yawRate = 0;
                    me.worldKept = false;
        me.velSlice = me.slice;
        me.sideKept = true;
        me.worldKept = false;
    }
    aiWorldIn(col, me);
    me.velSlice = me.slice;
    // Before the race's start (0x7d3680 0): car_ai_physics zeroes the
    // velocity and the spins, and nothing else moves the car (car_dynamics
    // 0x4a2830 waits for g_step 0x200, car_integrate_turn for 0x1FF).
    if (step < me.raceStartStep) {
        me.vel = {0, 0, 0};
        me.yawRate = me.spinX = me.spinZ = me.rollRate = me.pitchRate = 0;
        me.braking = true; // the brake held before the start (physics.md, Controls)
        aiViews(col, me);
        return out;
    }
    // Put in the air by a hard hit (aiCollide): the dynamics' landing
    // (0x4a1ce0) takes each spin x (0.5 + e), e 0.1 x the Suspension
    // Stiffness (P +0x2E0, 1) x its slider (1 for a computer car), and
    // ends the air ticks once every spin is under 0.6 (0x4a2068); until
    // then car_ai_physics (0x4088f0) skips the model. On the road the
    // roll and pitch go back (aiTilt).
    aiTilt(me, step, col);
    if ((step & 1) == 0)
        me.turnDue = true; // car_integrate_turn after the move (aiAdvance)
    if ((step & 1) != 0 || me.airTicks != 0)
        return out;
    out.ran = true;
    // car_dynamics (0x4a2830): +0xB8, the cheap ground speed the
    // controllers clamp by, max(|vx|, |vz|) + min / 4 of the world
    // velocity (0.88 of the speed along a diagonal, up to 1.03); then
    // gravity (k_gravity_tick 0x53c8dc, -0.30656 m/s a tick) through the
    // rows at +0x190 (0x4a2a0c-0x4a2b19), each x its own y. For a computer
    // car standing up on the road (+0x10C, the body's up . the road's
    // normal, over 0.9: 0x4a0f30 from car_road_dots 0x4a2690) those rows
    // are the body's own (+0xC0, +0xD8) with the road's normal for the up:
    // the up part goes into the road, which the contact takes back
    // (car_on_road 0x4a1610), and the forward part along the body's own
    // forward, not the road's: a car turned or tipped on a slope is pulled
    // along it by its own rise. With the road's normal y under 0.87
    // (0x5efdd8) the right part as well; not upright, neither.
    std::array<float, 3> nu{};
    {
        const std::array<float, 3> rf = unit3(me.roadRightSet ? me.roadForward : here->forward);
        const std::array<float, 3> rr = me.roadRightSet ? me.roadRight : here->right;
        nu = unit3(cross3(rf, rr));
        if (nu[1] < 0)
            nu = {-nu[0], -nu[1], -nu[2]};
    }
    {
        std::array<float, 3> gf{}, gr{}, gu{};
        aiTiltedAxes(col, me, gf, gr, gu);
        // car_downforce (0x4a2260), first: upright (+0x10C over 0.75,
        // 0x53c8a4) and +0xB8 (as the last tick left it) over 10 m/s, the
        // velocity along the body's up loses the carp's downforce (+0x24C)
        // x 2 for a car not the player's (0x53c8ac) x the velocity along
        // the body's forward, when that is over 0. The contact (car_on_road,
        // below) takes the part into the road back out; what is left is
        // along the road where the body is pitched or rolled on it.
        {
            const float ax = std::fabs(me.vel[0]), az = std::fabs(me.vel[2]);
            if (me.groundSpeed < 0)
                me.groundSpeed = std::max(ax, az) + std::min(ax, az) * 0.25f;
        }
        if (me.downforce > 0 && dot3(gu, nu) > 0.75f && me.groundSpeed > 10.0f) {
            const float along = dot3(me.vel, gf);
            if (along > 0)
                for (size_t k = 0; k < 3; ++k)
                    me.vel[k] -= me.downforce * 2.0f * along * gu[k];
        }
        const float ax = std::fabs(me.vel[0]), az = std::fabs(me.vel[2]);
        out.ground = std::max(ax, az) + std::min(ax, az) * 0.25f;
        me.groundSpeed = out.ground;
        // The up row's part always (0x4a2a4c-0x4a2aa5): into the road,
        // which the contact takes back out when the car is on it.
        for (size_t k = 0; k < 3; ++k)
            me.vel[k] += -0.30656251f * nu[1] * nu[k];
        if (nu[1] < 0.87f) {
            for (size_t k = 0; k < 3; ++k)
                me.vel[k] += -0.30656251f * (gf[1] * gf[k] + gr[1] * gr[k]);
        } else if (dot3(gu, nu) > 0.9f) {
            for (size_t k = 0; k < 3; ++k)
                me.vel[k] += -0.30656251f * gf[1] * gf[k];
        }
        // car_on_road (0x4a1610) on the road: the velocity into the frame
        // of aiContactFrame (the transposed rows, 0x4a1a46), its part along
        // the up dropped when under 0.5 m/s (0x5efddc), and back. Mostly the
        // frame is the car's own polygon's 16.16 rows, a little short of
        // unit length, so the rebuild takes about 0.002 m/s a call off the
        // speed at 20 m/s; for the ticks the box's front is on the next
        // polygon, the rows turned half way to that polygon's.
        // car_dynamics (0x4a2b28-0x4a2b78) calls car_on_road with the
        // clearance under 0.6 (else the car is in the air, +0x8B), and that
        // pushes the car out and takes the velocity only with the clearance
        // 0 or less (0x4a1a2a): over a crest the car runs on along the slope
        // it left, a little over the road, until gravity's part along the
        // up brings it down (in t6-log 20 % of the calls at 15 m/s, half of
        // them over 28 m/s).
        // The clearance: the moves since the last tick along the road
        // polygon's up as it is now.
        // car_clearance (0x4a0440) is the centre's height over the polygon's
        // plane less the box's reach along its up, so the reach changing as
        // the body turns (the roll, pitch and yaw since the last tick) moves
        // it too, as the plane's up is now.
        if (me.roadUpSet) {
            if (me.liftPlaneSet && (me.liftUp != me.roadUp || me.liftPoint != me.roadPoint)) {
                // The same centre over the new plane: the page's centre is put
                // with the box's lowest corner on the plane (aiCentreHeight), so
                // it moves by the two heights' difference along the slice's
                // normal; the lift kept along the old up.
                AiCar was = me;
                was.roadUp = me.liftUp;
                was.roadPoint = me.liftPoint;
                std::array<float, 3> m0{}, m1{};
                const float h0 = aiCentreHeight(col, was, m0), h1 = aiCentreHeight(col, me, m1);
                if (h0 != 0 && h1 != 0)
                    me.lift = me.lift * dot3(me.liftUp, me.roadUp) + (h0 - h1) * dot3(m1, me.roadUp);
            }
            me.liftUp = me.roadUp;
            me.liftPoint = me.roadPoint;
            me.liftPlaneSet = true;
            me.lift += dot3(me.liftMove, me.roadUp);
            if (me.liftRowsSet) {
                auto reach = [&](const std::array<float, 3>& r, const std::array<float, 3>& u,
                                 const std::array<float, 3>& f) {
                    return std::fabs(dot3(me.roadUp, r)) * me.halfWidth + std::fabs(dot3(me.roadUp, u)) * me.halfHeight +
                           std::fabs(dot3(me.roadUp, f)) * me.halfLength;
                };
                const auto& w = me.liftRows;
                me.lift -= reach(gr, gu, gf) - reach({w[0], w[1], w[2]}, {w[3], w[4], w[5]}, {w[6], w[7], w[8]});
            }
        }
        me.liftMove = {};
        me.liftRows = {gr[0], gr[1], gr[2], gu[0], gu[1], gu[2], gf[0], gf[1], gf[2]};
        me.liftRowsSet = true;
        AiRows frame{};
        if (me.lift > 0) {
            me.contactUpSet = false;
        } else if (aiContactFrame(col, me, frame)) {
            me.lift = 0;
            me.vel = aiContactVelocity(me.vel, frame);
            me.contactUp = {frame[3], frame[4], frame[5]};
            me.contactUpSet = true;
        } else {
            me.lift = 0;
            const float into = dot3(me.vel, nu);
            if (into < 0.5f)
                for (size_t k = 0; k < 3; ++k)
                    me.vel[k] -= into * nu[k];
        }
    }
    // car_ai_physics: the velocity into the body's frame (+0x4EC its right,
    // +0x4F4 its forward).
    std::array<float, 3> bf{}, br{}, bu{};
    aiBodyAxesOf(col, me, me.yaw, bf, br, bu);
    aiShapeRows(me, bf, br, bu);
    me.carSide = dot3(me.vel, br);
    out.forward = dot3(me.vel, bf);
    out.carSide = me.carSide;
    // +0x4FC: the spins (+0xE4, +0xE8, +0xEC) along the body's up row
    // (+0xCC, the tilted body's), which the controllers steer by; their yaw
    // force (+0x2F8, about that up) goes back into the world's spins
    // through the body's rows (ai_integrate 0x4071a0): the yaw gains it x
    // the up's y. (Its x and z parts, a few 1e-4 a call, are left out:
    // car_on_road sets those spins again the next tick.)
    std::array<float, 3> tf{}, tr{}, tu{};
    aiTiltedAxes(col, me, tf, tr, tu);
    const float spinUp = me.spinX * tu[0] + me.yawRate * tu[1] + me.spinZ * tu[2];
    out.upY = tu[1];
    // car_ai_physics (0x4088f0): tipped (+0x10C, the body's up row . the
    // road's normal, under 0.9: 0x5363d4) the skid levels 0 and, instead of
    // car_ai_model, the body's friction on the road (0x4aa8e7); so too in
    // car_ai_model for a body up row's y under 0.3 (+0xD0 as an int under
    // 0x3e99999a, 0x408613).
    if (dot3(tu, nu) < 0.9f || tu[1] < 0.3f) {
        me.skidFront = me.skidRear = 0;
        aiBodyFriction(me, nu);
        out.tipped = true;
        return out;
    }
    // car_ai_model: a racer (+0x200 bit 1) out of the race (+0x2C4 3 or 4,
    // 0x408639) only coasts (ai_finished_coast 0x407380, on the ground and
    // upright, which car_ai_physics has seen to): no controller, no forces
    // (ai_integrate isn't called), its brake 255. The velocity and spins
    // into the body's rows (+0x4EC, +0x4F0, +0x4F4; +0x4F8-+0x500), the
    // forward, side and yaw parts damped (aiFinishedCoast), and back
    // through the same rows.
    if (me.racer && !me.cop && (me.raceState == 3 || me.raceState == 4)) {
        AiCoast coast;
        coast.forward = dot3(me.vel, tf);
        coast.right = dot3(me.vel, tr);
        const float up = dot3(me.vel, tu);
        const std::array<float, 3> spin{me.spinX, me.yawRate, me.spinZ};
        const float s0 = dot3(spin, tr), s2 = dot3(spin, tf);
        coast.yawRate = dot3(spin, tu);
        coast.ground = out.ground;
        aiFinishedCoast(coast);
        for (size_t k = 0; k < 3; ++k) {
            me.vel[k] = coast.forward * tf[k] + up * tu[k] + coast.right * tr[k];
        }
        me.spinX = s2 * tf[0] + coast.yawRate * tu[0] + s0 * tr[0];
        me.yawRate = s2 * tf[1] + coast.yawRate * tu[1] + s0 * tr[1];
        me.spinZ = s2 * tf[2] + coast.yawRate * tu[2] + s0 * tr[2];
        me.skidFront = coast.skidFront;
        me.skidRear = coast.skidRear;
        me.braking = true;
        me.carSide = coast.right;
        out.coast = true;
        return out;
    }
    if (ramp) {
        // ai_lane_ramp (0x408870): the aim 0.2 a call towards the offset
        // wanted; ai_reverse_end (0x4060d0): reversing, the aim is the
        // road's point (a timed reversing ends past its time).
        if (me.offset < me.target)
            me.offset = std::min(me.offset + 0.2f, me.target);
        else
            me.offset = std::max(me.offset - 0.2f, me.target);
        if (me.reversing) {
            if (me.reverseTimed && me.reverseUntil < step)
                aiReverseSet(me, 1, 0, step);
            else
                me.offset = 0;
        }
    }
    // ai_heading_error (0x4070c0) with ai_target_point (0x406e50).
    // ai_target_point, for a car with no target (+0x624): off the road's
    // lanes (0x40f110: +0x638 not within one lane of them) +0x7D8 counts
    // the ticks under 3 m/s along its forward (+0x4F4, 0x5362c4); while
    // its bit 0x40 is set it steers at its own slice's point
    // (ai_point_own_slice 0x406c00), and over 1000 it is put back on the
    // road (0x429100, 0x406fd3). On them +0x7D8 is cleared and, over 10
    // m/s (+0x7C4, 0x5362cc), the reversing count; at most that, a car
    // that has started reversing more than 8 times is put back (0x407023,
    // the count cleared), more than 4 and in the lane past the road's
    // lanes it steers at its own slice's point.
    bool ownSlice = false;
    if (me.chasing < 0) {
        const auto& lanesAt = col.road[size_t(((aiSlice(me) % n) + n) % n)];
        const int lo = 6 - lanesAt.leftLanes(), hi = lanesAt.rightLanes() + 7;
        auto on = [&](int l) { return l >= lo && l <= hi && l >= 0 && l < 14; };
        const bool offRoad = me.lane < 0 || me.lane >= 14 || !(on(me.lane) || on(me.lane - 1) || on(me.lane + 1));
        if (offRoad) {
            if (std::fabs(out.forward) < 3.0f)
                ++me.offRoadTicks;
            ownSlice = (me.offRoadTicks & 0x40) != 0;
            if (me.offRoadTicks > 1000) {
                me.putBackDue = true;
                me.offRoadTicks = 0;
            }
        } else {
            me.offRoadTicks = 0;
            if (std::fabs(me.speed) > 10.0f)
                me.reverseCount = 0;
            else if (me.reverseCount > 8) {
                me.putBackDue = true;
                me.reverseCount = 0;
            } else if (me.reverseCount > 4)
                ownSlice = me.lane < lo || me.lane > hi;
        }
    }
    // The point (0x406d40): the steered slice's point moved by the aim
    // along the car's road right (+0x124; 10 more to the left on Hometown's
    // slices 0x205-0x20F with the aim under 0, 0x406d5c-0x406d85), less the
    // car's place (+0x98: `lateral` across the line from its slice's
    // point to the next's, `along` it). The direction to it and the car's
    // heading are taken in the world, in turns: atan2 of x and z (0x4e06d9,
    // fpatan) x 1 / 2 pi, under 0 + 1; the heading is +0x210, the body's
    // forward's (+0x1A8) atan2_turns x 1024 chopped (0x42ce70, fistp after
    // 0x4dfd56's chop), as h / 1024 for h over 0 and (h + 512) / 1024 +
    // 0.5 else; the error kept within +-0.5.
    const int k = ownSlice ? (((aiSlice(me) % n) + n) % n) : aiSteerSlice(col, me, track);
    const auto& p = col.road[size_t(k)];
    const auto& next = col.road[size_t((((me.slice + 1) % n) + n) % n)];
    const float aim =
        ownSlice ? 0.0f : me.offset + (track == 0 && k > 0x204 && k < 0x210 && me.offset < 0 ? -10.0f : 0.0f);
    const std::array<float, 3>& aimRight = me.roadRightSet ? me.roadRight : here->right;
    std::array<float, 3> d{};
    for (size_t c = 0; c < 3; ++c) {
        const float car =
            here->position[c] + here->right[c] * me.lateral + (next.position[c] - here->position[c]) * me.along;
        d[c] = p.position[c] + aimRight[c] * aim - car;
    }
    float toward = std::atan2(d[0], d[2]) * 0.159154958f;
    if (toward < 0)
        toward += 1;
    const int h1024 = static_cast<int>(std::atan2(bf[0], bf[2]) * 0.159154958f * 1024.0f); // chopped
    const float headingTurns = h1024 > 0 ? static_cast<float>(h1024) * 0.00097656f
                                         : static_cast<float>(h1024 + 512) * 0.0009765625f + 0.5f;
    float error = headingTurns - toward;
    if (error < -0.5f)
        error += 1;
    if (error > 0.5f)
        error -= 1;
    me.headingError = error;
    const auto& lanesAt = col.road[size_t(((aiSlice(me) % n) + n) % n)]; // +0x1C's
    // car_ai_model's "off the lanes" (0x408703-0x40876f): the lanes x the
    // width byte x 0x8000, the edge in 16.16, loaded as an integer (fild)
    // and compared with +0x7B8 in metres: never true but on a side with
    // no lanes (or a width of 0), where any side offset that way is.
    const bool offLanes = me.side < static_cast<float>(-(lanesAt.leftLanes() * (lanesAt.extra[1] << 15))) ||
                          me.side > static_cast<float>(lanesAt.rightLanes() * (lanesAt.extra[2] << 15));
    const bool slipping = out.ground > 2.5f && std::fabs(me.carSide) > out.forward * 0.5f;
    auto clampTo = [](float x, float limit) { return std::clamp(x, -limit, limit); };
    const float ground = out.ground;
    if (me.reversing || out.forward < 0) {
        me.steer = AiCar::Slow;
    } else if (offLanes || std::fabs(error) > 0.04f || std::fabs(me.speed) < 0.5f || step < me.slideUntil ||
               slipping) {
        me.steer = AiCar::Slide;
    } else if (std::fabs(me.speed) < 13.4109f) {
        me.steer = AiCar::Slow;
    } else {
        me.steer = AiCar::Rails;
    }
    float sideForce = 0; // along the body's right (+0x4E0) for the slow and sliding controllers
    if (me.steer == AiCar::Rails) {
        // ai_ctl_rails: the side speed wanted -5 x (+0x7B8 - aim), in
        // +-32 and +-half the ground speed (under 22.35 m/s +-0.129 x
        // the speed), against the velocity along the right of the road 2
        // slices on (0x406a80); the force through the road's rows.
        const auto& two = col.road[size_t((((aiSlice(me) + 2 * me.way) % n) + n) % n)];
        const float a4 = dot3(me.vel, two.right);
        out.railsSide = a4;
        float wantedSide = clampTo(clampTo(-(me.side - me.offset) * 5.0f, 32.0f), ground * 0.5f);
        float wantedYaw = clampTo(-error * 2.5f, 0.8f);
        if (me.speed < 22.35f) {
            wantedSide = clampTo(wantedSide, me.speed * 0.129f);
            wantedYaw = clampTo(wantedYaw, me.speed * 0.012f);
        }
        // x (1 + 0.0015 x the speed along the body's forward, +0x4F4, not
        // +0x7C4: 0x4078ea).
        const float k15 = 1 + 0.0015f * out.forward;
        const float force = clampTo(-(a4 - wantedSide) * 4.0f * k15, 38.0f * k15);
        me.sideForce = force;
        // Along the road polygon's right (+0x124, ai_integrate 0x4071a0).
        const std::array<float, 3>& rr = me.roadRightSet ? me.roadRight : here->right;
        for (size_t c = 0; c < 3; ++c)
            me.vel[c] += force / 32.0f * rr[c];
        aiYawForce(me, tf, tr, tu, clampTo(-(spinUp - wantedYaw) * 5.0f, 0.8f));
    } else if (me.steer == AiCar::Slow) {
        // ai_ctl_slow: the yaw from the heading error, the side speed
        // damped, in the car's frame. The side speed wanted is sin(error)
        // x +0x4F4 for an error of 0 or more, and -sin((1 + error) x 2 pi)
        // x it for one under 0 (0x407d09-0x407d35), the same size and sign:
        // it never goes to the left.
        const float wantedYaw = clampTo(clampTo(-error * 2.5f, 1.6f), ground * 0.02f);
        const float wantedSide =
            clampTo(clampTo(std::sin(std::fabs(error) * 2 * kPi) * out.forward, 16.0f), ground * 0.01f);
        aiYawForce(me, tf, tr, tu, clampTo(-(spinUp - wantedYaw) * 10.0f, 0.8f));
        sideForce = clampTo(-(me.carSide - wantedSide) * 3.0f, 38.0f);
    } else {
        // ai_ctl_slide: the same on the side speed, towards the point.
        const float turn = clampTo(-error * 3.0f, 3.0f);
        const float wantedYaw = clampTo(turn, ground * 0.04f);
        const float wantedSide = clampTo(clampTo(clampTo(turn * 10.0f, 32.0f), ground), ground * 0.2f);
        aiYawForce(me, tf, tr, tu, clampTo(-(spinUp - wantedYaw) * 10.0f * 0.5f, 1.0f));
        sideForce = clampTo(-(me.carSide - wantedSide) * 6.0f, 40.0f);
        out.slideSide = sideForce;
    }
    if (sideForce != 0) {
        for (size_t c = 0; c < 3; ++c)
            me.vel[c] += sideForce / 32.0f * br[c];
        me.carSide += sideForce / 32.0f;
    }
    return out;
}

} // namespace

void aiAdvance(AiCar& me, const TrackCollision& col) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || !me.sideKept)
        return;
    aiWorldIn(col, me);
    // car_move (0x4a2c60): the place += the velocity / 64; the body turned
    // by its yaw rate (turns a second).
    std::array<float, 3> at = aiPlace(col, me);
    for (size_t k = 0; k < 3; ++k) {
        at[k] += me.vel[k] / 64.0f;
        me.liftMove[k] += me.vel[k] / 64.0f;
    }
    // The body's up is the world's (car_integrate_turn 0x4a2e20 turns the
    // body's rows by its spins about the world's axes, nothing lays it on
    // a slice): turned with the yaw about the world's y, it is kept while
    // the slice under it changes, so the roll and pitch (against the yaw
    // on the slice's up) are taken again for the new slice.
    std::array<float, 3> bf{}, br{}, bu{};
    aiUnitAxes(col, me, bf, br, bu);
    const float turn = me.yawRate * 2 * kPi / 64.0f;
    me.yaw += turn;
    me.yaw = std::remainder(me.yaw, 2 * kPi);
    const float ct = std::cos(turn), st = std::sin(turn);
    bu = {bu[0] * ct + bu[2] * st, bu[1], -bu[0] * st + bu[2] * ct};
    aiLocate(me, col, at);
    aiSetTilt(me, col, bu);
    // Not through the walls (the car against the track, 0x4304b0 with
    // 0x430210): its sides kept within the slice's walls, the velocity
    // across the slice taken out.
    const auto& here = col.road[size_t(((me.slice % n) + n) % n)];
    const float low = me.halfWidth - here.leftWall, high = here.rightWall - me.halfWidth;
    if (low < high && (me.lateral < low || me.lateral > high)) {
        me.lateral = std::clamp(me.lateral, low, high);
        std::array<float, 3> f{}, r{}, u{};
        aiWayFrame(col, me.slice, me.way, f, r, u);
        const float across = dot3(me.vel, r);
        for (size_t k = 0; k < 3; ++k)
            me.vel[k] -= across * r[k];
        me.carSide = 0;
    }
    me.side = aiMeasuredSide(col, me);
    aiViews(col, me);
    // car_integrate_turn (0x4a2e20, 32 a second): +0x94 gains the turn of
    // the call, (|+0xE4| + |+0xE8| + |+0xEC|) / 32 (0x4a2cc0); when any
    // spin turns the body, the rows are made orthonormal again (0x4b7840:
    // M (M^T M)^(-1/2), its series to 10 terms, 0x55fdcc) once +0x94 is over
    // 1/32 (0x53c8f4) or +0x8C, taken down by one, is 0; then +0x94 0 and
    // +0x8C 32. The turn itself keeps P (AiCar::rowShape).
    if (me.turnDue) {
        me.turnDue = false;
        const float sx = std::fabs(me.spinX / 32.0f), sy = std::fabs(me.yawRate / 32.0f),
                    sz = std::fabs(me.spinZ / 32.0f);
        me.turnSum = sx + sy + sz + me.turnSum;
        if (sx > 0 || sy > 0 || sz > 0) {
            bool again = me.turnSum > 0.03125f;
            if (!again) {
                me.turnCount = (me.turnCount - 1) & 0xff;
                again = me.turnCount == 0;
            }
            if (again) {
                me.rowShape = {1, 0, 0, 0, 1, 0, 0, 0, 1};
                me.rowsShaped = false;
                me.turnSum = 0;
                me.turnCount = 32;
            }
        }
    }
}

void aiSteerStep(AiCar& me, const TrackCollision& col, int step, int track) {
    const bool kept = me.sideKept;
    aiModelSteer(me, col, step, track, false);
    if (kept)
        aiAdvance(me, col);
}

// The skid levels a controller leaves (+0x5C0, +0x5C4). ai_ctl_rails
// (0x407b5c-0x407bbc): `side` the velocity across the road 2 slices on;
// over 7 m/s (0x56ebd0, ai_gains_init 0x405e60) the front is the excess
// x 0.15 (0x53636c), the rear x 0.5 (0x53634c) while braking, else 0.
// ai_ctl_slide (0x408467-0x4084ba): `side` the velocity along the car's
// right (+0x4EC); both the excess over 7. ai_ctl_slow (0x407c30) leaves
// them as they were.
void aiSkid(AiCar& me, float side, bool braking) {
    const float over = std::fabs(side) - 7.0f;
    if (me.steer == AiCar::Rails) {
        me.skidFront = over > 0 ? over * 0.15f : 0.0f;
        me.skidRear = over > 0 && braking ? over * 0.5f : 0.0f;
    } else if (me.steer == AiCar::Slide) {
        me.skidFront = me.skidRear = over > 0 ? over : 0.0f;
    }
}

void AiStartRandom::seed(uint32_t value) {
    // 0x4371d0: the value and 3 x it made odd; the value times 3 x it,
    // then times the value 7 more times, each kept to 16 bits but the last.
    uint32_t p = value, u = value * 3;
    if ((p & 1) == 0)
        ++p;
    if ((u & 1) == 0)
        ++u;
    uint32_t s = (p * u) & 0xffff;
    for (int i = 0; i < 6; ++i)
        s = (s * p) & 0xffff;
    s *= p;
    low = s & 0xffff;
    mult = p;
}

int AiStartRandom::pick() {
    const uint32_t s = low * mult; // 0x554f14
    low = s & 0xffff;
    return static_cast<int>(((s & 0xffff00) >> 8) * 14 >> 16);
}

AiStartRandom& aiStartRandom() {
    static AiStartRandom random = [] {
        AiStartRandom r;
        r.seed(0x2b5d1e0u); // any start-up tick count
        return r;
    }();
    return random;
}

bool aiStartSpin(const AiCar& car, const AiCarData& data, float accel, AiStartRandom& random) {
    if (!car.racer || car.cop || car.distance >= 20 || accel != data.accel[0])
        return false;
    return random.pick() == car.slot;
}

void aiFinishedCoast(AiCoast& c) {
    c.skidFront = c.skidRear = 0;
    c.forward = static_cast<float>(static_cast<double>(c.forward) * (c.ground >= 22.3515 ? 0.98 : 0.95));
    c.right = static_cast<float>(static_cast<double>(c.right) * 0.85);
    c.yawRate = static_cast<float>(static_cast<double>(c.yawRate) * 0.5);
    if (c.forward >= 5.0f || c.forward <= -10.0f)
        c.skidRear = static_cast<float>(static_cast<double>(std::fabs(c.forward)) * 0.1);
    else
        c.forward = static_cast<float>(static_cast<double>(c.forward) * 0.5);
    if (std::fabs(c.yawRate) > 0.2)
        c.skidRear += std::fabs(c.yawRate);
    if (std::fabs(c.right) > std::fabs(c.forward)) {
        c.skidFront = c.skidRear = std::fabs(c.right);
        c.right = static_cast<float>(static_cast<double>(c.right) * 0.2);
    }
}

void aiLaps(AiCar& c, int slices, int step, bool backward) {
    if (c.laps <= 0 || slices <= 0)
        return;
    const int slice = aiSlice(c);
    if (c.slicePrev < 0)
        c.slicePrev = slice;
    if (slice != c.slicePrev) {
        // car_laps_owed (0x42cc20), with the race task's gate (0x42cfbe: the
        // track position's slice changed).
        const int prev = c.slicePrev;
        if (!backward) {
            if (slice > 500 && prev == 0)
                ++c.lapsOwed;
            else if (slice >= 0 && slice < 51 && prev > 500) {
                if (c.lapsOwed < 1)
                    ++c.lap;
                else
                    --c.lapsOwed;
            }
        } else if (slice >= 0 && slice < 51 && prev > 500) {
            ++c.lapsOwed;
        } else if (prev < 51 && slice > 500) {
            if (c.lapsOwed > 0)
                --c.lapsOwed;
            else
                ++c.lap;
        }
        c.slicePrev = slice;
        // car_race_distance (0x42ce10).
        c.distance = c.lapsOwed < 1 ? c.lap * slices + (backward ? slices - slice : slice) : 0;
    }
    // car_laps_task (0x4ce280): a racer's new lap counted; at the race's
    // laps (not a Knockout's out car, +0x200 bit 0x80, nor a busted one) it
    // has finished.
    if (c.racer && !c.cop && c.lap != c.lapCounted && c.raceState != 3) {
        c.lapCounted = c.lap;
        if (c.lap == c.laps && c.raceState != 4) {
            c.finishStep = step;
            c.raceState = 3;
        }
    }
    if (c.raceState != 3 && c.raceState != 4)
        c.finishStep = step;
}

int aiGridSlice(int index, int cars, int slices, bool backward) {
    const int place = backward ? cars - 1 - index : index;
    const int s = place * 3 + 1;
    return slices > 0 ? s % slices : s;
}

float aiGridOffset(const TrackCollision::RoadPoint& p, int index, bool backward) {
    // 0x42ba00 with 0x40eca0 0 (t_ai_track_rows +8, 0 on every track):
    // int(-byte 0x17 x 0x8000) / 2 or int(byte 0x1B << 15) / 2, 16.16.
    bool right = (index & 1) != 0;
    if (backward)
        right = !right;
    const int v = right ? (static_cast<int>(p.extra[2]) << 15) / 2 : (static_cast<int>(p.extra[1]) * -0x8000) / 2;
    return static_cast<float>(v) * (1.0f / 65536.0f);
}

namespace {
using Mat3 = std::array<float, 9>;
Mat3 aiMatMul(const Mat3& a, const Mat3& b) {
    Mat3 m{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                m[size_t(i * 3 + j)] += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
    return m;
}
} // namespace

void aiSetRows(AiCar& c, const TrackCollision& col, const std::array<float, 3>& velocity,
               const std::array<float, 3>& right, const std::array<float, 3>& up, const std::array<float, 3>& forward) {
    // M = P Q with P = (M M^T)^(1/2) (symmetric) and Q orthonormal, by the
    // series of (I + E)^(1/2) and (I + E)^(-1/2), E = M M^T - I (about 1e-4
    // for the 16.16 rows; the terms to E^3 leave 1e-16).
    const std::array<std::array<float, 3>, 3> rows{right, up, forward};
    Mat3 e{};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            e[i * 3 + j] = dot3(rows[i], rows[j]) - (i == j ? 1.0f : 0.0f);
    const Mat3 e2 = aiMatMul(e, e), e3 = aiMatMul(e2, e);
    Mat3 p{}, q{};
    for (size_t k = 0; k < 9; ++k) {
        const float id = (k % 4 == 0) ? 1.0f : 0.0f;
        p[k] = id + e[k] * 0.5f - e2[k] * 0.125f + e3[k] * 0.0625f;
        q[k] = id - e[k] * 0.5f + e2[k] * 0.375f - e3[k] * 0.3125f;
    }
    std::array<std::array<float, 3>, 3> unit{};
    for (size_t i = 0; i < 3; ++i)
        for (size_t k = 0; k < 3; ++k)
            unit[i][k] = q[i * 3] * rows[0][k] + q[i * 3 + 1] * rows[1][k] + q[i * 3 + 2] * rows[2][k];
    aiSetWorld(c, col, velocity, unit[2], unit[1]);
    c.rowShape = p;
    c.rowsShaped = false;
    for (size_t k = 0; k < 9; ++k)
        c.rowsShaped = c.rowsShaped || p[k] != ((k % 4 == 0) ? 1.0f : 0.0f);
}

void aiPutOnGrid(AiCar& c, const Track& track, const TrackCollision& col, int slice, float offset, bool backward) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return;
    slice = ((slice % n) + n) % n;
    // car_place_on_slice (0x4a0aa0): on the slice's point, along its right
    // by the offset (and 0.5 up, then down onto the road: the page's place
    // is on it), its track position searched there (0x4a08c0, 0x49ffa0).
    c.slice = slice;
    c.roadSlice = slice;
    c.velSlice = slice;
    c.way = backward ? -1 : 1;
    c.along = 0;
    c.lateral = offset;
    c.side = offset;
    c.sideKept = true;
    c.speed = c.sideSpeed = c.carSide = c.heading = c.yawRate = c.headingError = 0;
    c.spinX = c.spinZ = 0;
    c.vel = {0, 0, 0};
    c.worldKept = false;
    c.roll = c.pitch = c.rollRate = c.pitchRate = 0;
    c.airTicks = 0;
    c.lift = 0;
    c.liftMove = {};
    c.contactUpSet = false;
    c.roadRightSet = c.roadUpSet = false;
    c.road = RoadSearch{};
    c.lane = c.laneLeft = c.laneRight = aiLaneAt(col.road[size_t(slice)], offset);
    // 0x42bcb0 clears the race's fields: the aim and the offset wanted are
    // 0 until the driver's lane choice (as dumped on Hometown's grid).
    c.offset = c.target = 0;
    c.reversing = false;
    c.offRoadTicks = c.reverseCount = 0;
    c.skidFront = c.skidRear = 0;
    c.braking = false;
    aiRoadFrame(track, col, c);
    // 0x4a0980: the body's rows are the road polygon's 16.16 rows, the right
    // and forward turned round on a backward track.
    const float w = backward ? -1.0f : 1.0f;
    if (c.roadRightSet && c.roadUpSet) {
        aiSetRows(c, col, {0, 0, 0}, {c.roadRight[0] * w, c.roadRight[1] * w, c.roadRight[2] * w}, c.roadUp,
                  {c.roadForward[0] * w, c.roadForward[1] * w, c.roadForward[2] * w});
    } else {
        const auto& p = col.road[size_t(slice)];
        aiSetWorld(c, col, {0, 0, 0}, {p.forward[0] * w, p.forward[1] * w, p.forward[2] * w});
    }
    c.side = aiMeasuredSide(col, c);
    c.turnSum = 0; // 0x4a0d20
    c.turnCount = 0;
    // The race's start (0x42bcb0): lap 0, a lap owed backward, the slice
    // before = the slice, the race distance, racing.
    c.lap = c.lapCounted = 0;
    c.lapsOwed = backward ? 1 : 0;
    c.slicePrev = slice;
    c.distance = c.lapsOwed < 1 ? (backward ? n - slice : slice) : 0;
    c.raceState = 0;
}

namespace {

void aiMove(AiCar& me, const AiCarData& data, bool weather, int step, const TrackCollision& col, int track) {
    // +0x7C4 as the race task took it (0x42ce70), before this tick's
    // gravity and forces.
    const float raceSpeed = me.speed;
    me.downforce = data.downforce;
    const AiDynamics dyn = aiModelSteer(me, col, step, track, true);
    // car_ai_physics (0x4088f0, 0x408959): in the air or tipped (+0x10C
    // under 0.9, 0x5363d4) both levels 0 and the model not run, so no
    // controller sets them (aiModelSteer: the body's friction instead); out
    // of the race the coast sets them.
    if (me.airTicks != 0 && (step & 1) == 0)
        me.skidFront = me.skidRear = 0;
    const bool tipped = dyn.tipped;
    bool spin = false; // the start's wheelspin (aiStartSpin)
    if (!dyn.ran || dyn.tipped || dyn.coast) {
        if (dyn.ran)
            aiViews(col, me);
        return;
    }
    // The speed: the game's computer cars (car_ai_physics 0x4088f0 without
    // +0x88; 0x4066c0's "towards the speed wanted, not past it" is only
    // for a car with +0x88, which no racer in t6-log had) are pushed by a
    // forward force from the controller, integrated over 1/32 s
    // (ai_integrate 0x4071a0), with gravity along the road from the
    // dynamics task (0x4a2830, in aiModelSteer). ai_ctl_rails (0x4076b0):
    // the acceleration (0x4064f0) while the speed is under the speed
    // wanted, else minus the braking (0x4063e0), with no dead band and
    // nothing stopping it at the speed wanted, so the speed chatters just
    // under it; accelerating over 44.703 m/s (0x53635c) and faster than it
    // slips sideways, a racer (+0x200 bit 1) loses 3.45 (0x536364) / its
    // lateral grip multiplier, another car its side force / (speed x 0.5).
    // ai_ctl_slow (0x407c30, the speed wanted at most 16.41 m/s) and
    // ai_ctl_slide (0x407fa0) accelerate only when more than 1 m/s under
    // it, else brake; reversing (+0x7D4) the slow controller drives back
    // towards minus the speed wanted.
    const float speed = raceSpeed;
    const float error = me.wanted - speed;
    auto accel = [&]() {
        if (me.racer && (speed > data.topSpeedCap * me.scale || speed > data.topSpeedCap))
            return 0.0f;
        const int at = std::clamp(static_cast<int>(std::nearbyint(std::fabs(speed) - 0.5f)), 0, 111);
        float a = data.accel[size_t(at)];
        if (me.distance > 100) // ai Normal (0x6fd4f0 0) past race distance 100 (+8, 0x406591)
            a *= 0.9f;
        if (weather && at < 45)
            a *= 0.9f;
        return a;
    };
    auto brake = [&](float err) {
        // ai_brake: none for a car stopped with the brake held and nothing
        // wanted (within 0.5 m/s, 0x536234); the brake (+0x509) held only
        // when more than 5 m/s too fast.
        if (me.braking && me.wanted == 0 && std::fabs(err) < 0.5f)
            return 0.0f;
        float b = data.maxBrake;
        if (me.cop)
            b *= 2;
        if (speed < 0)
            b *= 2;
        if (me.racer && !me.cop)
            b *= 1.25f; // an AI racer (+0x200 bit 8)
        me.braking = !(speed != 0 && err > -5.0f);
        return b;
    };
    float force = 0;
    const float way = me.way < 0 ? -1.0f : 1.0f;
    const float forwardSpeed = dyn.forward; // +0x4F4
    bool alongCar = false;
    if (me.steer == AiCar::Rails) {
        if (error > 0) {
            force = accel();
            me.braking = false;
            if (force > 0 && speed > std::fabs(me.carSide) && speed > 44.703f)
                force -= me.racer && data.latGrip != 0 ? 3.45f / data.latGrip
                                                       : std::fabs(me.sideForce) / std::fabs(speed * 0.5f);
        } else {
            force = -brake(error);
        }
        if (!tipped)
            aiSkid(me, dyn.railsSide, force < 0);
        if (speed < 0)
            force = -force;
    } else {
        // ai_ctl_slow and ai_ctl_slide compare with the speed along the
        // car's own forward (+0x4F4) and push along it (+0x4E8), not along
        // the road: a car turned off its way (after a hit, sliding) runs
        // slower along the road for the same push.
        float w = std::fabs(me.wanted);
        if (me.steer == AiCar::Slow)
            w = std::min(w, 16.4109f);
        if (me.steer == AiCar::Slow && me.reversing) {
            if (-w < forwardSpeed) {
                force = -accel();
                me.braking = false;
            } else {
                force = brake(0); // 0x407f3d: the error passed as 0
            }
        } else if (w - forwardSpeed > 1.0f) {
            force = accel();
            me.braking = false;
            spin = me.steer == AiCar::Slide && aiStartSpin(me, data, force, aiStartRandom());
        } else {
            force = -brake(w - forwardSpeed);
        }
        alongCar = true;
        if (me.steer == AiCar::Slide && !tipped) {
            aiSkid(me, dyn.carSide, false);
            if (spin)
                me.skidRear = 10; // 0x4084bc
        }
        if (spin) {
            // The start's push (0x4082dc-0x4082f8): the side force (+0x4E0)
            // 0.5 (0x53638c) more, and the yaw force (+0x2F8) gains half of
            // that side force, push and all (x 1.0 after: 0x53638c is not
            // over 1); both through the body's rows (ai_integrate 0x4071a0).
            std::array<float, 3> bf{}, br{}, bu{};
            aiBodyAxesOf(col, me, me.yaw, bf, br, bu);
            aiShapeRows(me, bf, br, bu);
            for (size_t c = 0; c < 3; ++c)
                me.vel[c] += 0.5f / 32.0f * br[c];
            std::array<float, 3> tf{}, tr{}, tu{};
            aiTiltedAxes(col, me, tf, tr, tu);
            aiYawForce(me, tf, tr, tu, (dyn.slideSide + 0.5f) * 0.5f);
        }
    }
    const int n = static_cast<int>(col.road.size());
    if (alongCar) {
        std::array<float, 3> bf{}, br{}, bu{};
        aiBodyAxesOf(col, me, me.yaw, bf, br, bu);
        for (size_t c = 0; c < 3; ++c)
            me.vel[c] += force / 32.0f * bf[c];
    } else if (n > 0) {
        // ai_ctl_rails' forward force goes along the road polygon's forward
        // (+0x13C, 0x56ec74 through ai_integrate's rows), the way it runs.
        const auto& a = col.road[size_t(((me.slice % n) + n) % n)];
        const std::array<float, 3> rf = unit3(me.roadRightSet ? me.roadForward : a.forward);
        for (size_t c = 0; c < 3; ++c)
            me.vel[c] += way * force / 32.0f * rf[c];
    }
    aiViews(col, me);
}
// How far the road's line moves across from a slice's point to the
// next's, along the slice's right (unit): the measured side offset of a
// car going straight on gains that much over the slice.
float aiChord(const TrackCollision& col, int slice) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return 0;
    const auto& a = col.road[size_t(((slice % n) + n) % n)];
    const auto& b = col.road[size_t(((slice + 1) % n + n) % n)];
    float d = 0, r = 0;
    for (size_t k = 0; k < 3; ++k) {
        d += (b.position[k] - a.position[k]) * a.right[k];
        r += a.right[k] * a.right[k];
    }
    return r > 0 ? d / std::sqrt(r) : 0;
}

} // namespace

namespace {

// What a cop's driver does this call (car_driver_cop 0x4032e0): the
// section's speed x k + plus (cop_ai_drive, ai_speed_wanted_section), the
// chase's (cop_ai_chase, 0x402420), the way to the roadside (state 3), or
// only its lanes (state 10 with a racer near); or a racer pulling over
// (its state 5: 0x408b30, 0x408bd0).
struct CopCall {
    enum Kind { Section, Chase, Roadside, LanesOnly, PullOver } kind = Section;
    float k = 1.19f, plus = 0;
    bool chase = false;
};

// One step of the computer's driving: as a racer (cop null), or a cop's
// driver's call.
void aiDrive(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const std::vector<uint8_t>& speeds,
             const AiCarData& data, int step, int track, bool /*weather*/, const CopCall* cop) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || self >= cars.size())
        return;
    AiCar& me = cars[self];
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    auto point = [&](int s) -> const TrackCollision::RoadPoint& { return col.road[size_t(wrap(s))]; };
    const int mySlice = aiSlice(me); // +0x1C: the track position's
    auto ahead = [&](int k) { return wrap(mySlice + me.way * k); }; // slice_ahead (0x40e160)
    auto sliceDiff = [&](int a, int b) { // 0x40e120
        int d = a - b;
        if (d > n / 2)
            d -= n;
        else if (d < -(n / 2))
            d += n;
        return d;
    };
    const TrackCollision::RoadPoint& here = point(mySlice);
    const int leftLanes = here.leftLanes(), rightLanes = here.rightLanes();
    int votes[14][3] = {}; // 0x56ee90: left, same, right of the car's lane, by rule
    auto vote = [&](int row, int column, int amount) { votes[row][column] += amount; };

    // lane_votes_start (0x40dd60): the car's lanes (0x40e350).
    // +0x7B8 as the race task wrote it at the tick's start (aiRaceTick:
    // `extent` set); without it, measured here.
    if (!me.sideKept)
        me.side = me.lateral = me.offset;
    else if (me.extent <= 0)
        me.side = aiMeasuredSide(col, me);
    // +0x21C, the half extent across the road the race task wrote with
    // +0x7B8 (aiRaceTick); 0x5367cc 0.5, 0x5367d4 -0.5.
    const float extent = me.extent > 0 ? me.extent : me.halfWidth;
    me.lane = aiLaneAt(here, me.side);
    me.laneLeft = aiLaneAt(here, me.side - extent + 0.5f);
    me.laneRight = aiLaneAt(here, me.side + extent - 0.5f);
    const int lane = me.lane;
    // 0x40cee0: keep to the right of the way it runs (0x56f230, 1 on
    // every track); lanes 7 and up are its right going the slices' way.
    if (me.way > 0) {
        vote(0, 0, lane - 1 < 7 ? -3 : 1);
        vote(0, 1, lane < 7 ? -3 : 1);
        if (lane + 1 > 6)
            vote(0, 2, 1);
    } else {
        if (lane - 1 < 7)
            vote(0, 0, 1);
        else if (lane < 7)
            vote(0, 1, 1);
        else
            vote(0, 1, -3);
        vote(0, 2, lane + 1 < 7 ? 1 : -3);
    }
    // 0x40cfc0: the nearest car ahead and behind in each of the three
    // lanes, within the speed's reach in slices (at least 10).
    // 0x40cfd0: +0x7C4 (the speed's size) x 2 x 0.1667 rounded towards 0
    // (0x4dfd56 sets the chop mode for its frndint), at least 10.
    const int reach = std::max(static_cast<int>(std::fabs(me.speed) * 2.0 * 0.1667), 10);
    std::array<const AiCar*, 3> carAhead{}, carBehind{};
    std::array<int, 3> aheadBy{reach, reach, reach}, behindBy{-reach, -reach, -reach};
    std::array<bool, 3> aheadCentre{}, behindCentre{};
    for (size_t i = 0; i < cars.size(); ++i) {
        if (i == self)
            continue;
        const AiCar& o = cars[i];
        const int d = sliceDiff(aiSlice(o), mySlice) * me.way;
        if (std::abs(d) > reach)
            continue;
        int from, centre, to;
        if (o.laneLeft == o.laneRight)
            from = centre = to = o.lane - lane + 1;
        else {
            from = o.laneLeft - lane + 1;
            centre = o.lane - lane + 1;
            to = o.laneRight - lane + 1;
        }
        for (int c = from; c <= to; ++c) {
            if (c < 0 || c >= 3)
                continue;
            if (d < 0) {
                if (behindBy[size_t(c)] < d) {
                    behindBy[size_t(c)] = d;
                    carBehind[size_t(c)] = &o;
                    behindCentre[size_t(c)] = c == centre;
                }
            } else if (d < aheadBy[size_t(c)]) {
                aheadBy[size_t(c)] = d;
                carAhead[size_t(c)] = &o;
                aheadCentre[size_t(c)] = c == centre;
            }
        }
    }
    auto along = [](const AiCar& c) { return static_cast<float>(c.way) * c.speed; }; // +0x7C0
    const CopCall::Kind kind = cop ? cop->kind : CopCall::Section;
    // The lane picked from the sums (lane_choose_best 0x40da30, the same
    // lane on a tie) and its aim (0x40daf0).
    auto choose = [&](int nudge) {
        int sum[3] = {};
        for (const auto& row : votes)
            for (int c = 0; c < 3; ++c)
                sum[c] += row[c];
        int pick = lane, best = sum[1];
        if (best < sum[0]) {
            pick = lane - 1;
            best = sum[0];
        }
        if (best < sum[2])
            pick = lane + 1;
        me.pick = std::clamp(pick, 0, 13);
        me.target = aiLaneOffset(here, me.pick, me.halfWidth, me.way > 0 ? nudge : -nudge);
    };
    const bool atEdge = lane == 6 - leftLanes || lane == rightLanes + 7;
    if (kind == CopCall::PullOver) {
        // 0x408b30: 11 m/s until in the lane past the road's lanes, then 0;
        // 0x408bd0: +150 for the nearer edge's lane, or its own there.
        me.wanted = atEdge ? 0.0f : 11.0f;
        vote(3, atEdge ? 1 : lane < 7 ? 0 : 2, 150);
        choose(0);
        return;
    }

    // A cop's chase (0x401780): the slices to its target along its way,
    // which lane it is in (0x56ea64: 0 left, 1 the same, 2 right).
    const AiCar* target = cop && me.chasing >= 0 && static_cast<size_t>(me.chasing) < cars.size() ? &cars[size_t(me.chasing)]
                                                                                               : nullptr;
    const int toTarget = target ? sliceDiff(aiSlice(*target), mySlice) * me.way : 0;
    const int targetSide = target ? (target->lane < lane ? 0 : target->lane == lane ? 1 : 2) : 1;
    const float top = data.maxVelocity * (me.copFast ? 1.1f : 1.0f); // 0x401370
    bool chaseSection = false; // cop_ai_chase gave the section's speed x 1.19
    if (kind == CopCall::Chase && target) {
        // cop_ai_chase (0x4025c0): within 16 slices, or with the target
        // pulled over, 0x402420; else behind it the top speed, reversing
        // ended (ai_reverse_set 1); 16 or more past it the section's
        // speed x 1.19, facing the target's way (+0x621 bit 0x10 from
        // 0x56ea5c).
        const int off = std::abs(toTarget);
        if (!target->pulledOver && off >= 16) {
            if (toTarget >= 0) {
                me.wanted = top;
                me.reversing = false;
            } else {
                chaseSection = true;
                if (target->way != me.way && target->speed >= 3.0f) {
                    me.way = target->way;
                    me.speed = 0;
                    me.sideSpeed = me.carSide = me.heading = me.yawRate = 0;
                    me.worldKept = false;
                }
            }
        }
    }
    if (kind == CopCall::LanesOnly) {
        // the speed wanted as it was
    } else if (kind == CopCall::Roadside) {
        // State 3 (ai_speed_wanted_section with 0 and 6.5): 6.5 m/s.
        me.wanted = std::min(6.5f, top);
    } else if (!cop) {
        // car_ai_speed_wanted (0x409330): the table's speed for the slice
        // pair held under the top speed cap by the track's factor
        // (0x4093ab-0x4093ca), then by the scale (0x409464).
        me.wanted = std::min(aiSlicesSpeed(speeds, mySlice), data.topSpeedCap * aiTrackFactor(track)) *
                    std::min(me.scale, 1.0f);
    } else if (kind != CopCall::Chase || !target || chaseSection) {
        // ai_speed_wanted_section (0x401430): the section's speed x k + plus,
        // at most the top speed.
        const float k = chaseSection ? 1.19f : cop->k, plus = chaseSection ? 0.0f : cop->plus;
        me.wanted = std::min(trackSectionSpeed(track, mySlice) * k + plus, top);
        // 0x40bff0: behind the car ahead in its lane going its way, slower
        // than it wants, at the speed's reach in slices (at least 4) or
        // nearer, its speed less 10 x the share of that reach it is in;
        // one coming at it x 0.4.
        if (carAhead[1]) {
            const AiCar& o = *carAhead[1];
            if (o.way == me.way) {
                const int reachOf = std::max(std::abs(static_cast<int>(std::nearbyint(o.speed * 0.5793742757821553 - 0.5))), 4);
                if (me.wanted - o.speed > 0 && reachOf >= aheadBy[1] && o.speed >= 3.0f)
                    me.wanted = o.speed - static_cast<float>(reachOf - aheadBy[1]) * 10.0f / static_cast<float>(reachOf);
            } else {
                me.wanted *= 0.4f;
            }
        }
    } else if (std::abs(toTarget) < 16 || target->pulledOver) {
        // 0x402420. Reversing (+0x7D4): behind it, the target's speed along
        // the cop's way x (slices x 0.1 + 1); past it, slices x 5 (back).
        // Else: at the top speed more than 8 slices behind it or when it is
        // faster, else closing by the slices; past it, x 0.5 its speed (x
        // 0.2 for the car behind in its lane, 0x56ef54), and when under 3
        // m/s more than 7 slices past, reversing at 9.83 m/s
        // (ai_reverse_set 0, 0xc11d5ac4).
        const float targetAlong = target->speed * static_cast<float>(target->way * me.way);
        if (me.reversing) {
            if (toTarget >= 0)
                me.wanted = targetAlong * (static_cast<float>(toTarget) * 0.1f + 1.0f);
            else
                me.wanted = static_cast<float>(toTarget) * 5.0f;
        } else if (toTarget >= 0) {
            if (toTarget > 8 || top < target->speed)
                me.wanted = top;
            else
                me.wanted = static_cast<float>(toTarget) * (top - target->speed) * 0.125f + target->speed + 5.81139f;
        } else {
            me.wanted = target->speed * (carBehind[1] == target ? 0.2f : 0.5f);
            if (me.speed < 3.0f && toTarget < -7) {
                me.reversing = true;
                me.wanted = -9.834f;
            }
        }
    }
    // 0x404840: the lights flashed back (0x40deb0), then flashed again
    // when held up behind a car in its lane within 2 slices, wanting
    // more than 8.94 m/s over its speed (0x40de50: a step up for 64 steps).
    if (me.flashing && me.flashUntil < step) {
        me.flashing = false;
        if (me.beams == 1)
            me.beams = 0;
        else if (me.beams == 2)
            me.beams = 1;
    }
    // Racers past the race's start only (0x40f050), as rows 9-12 below.
    const bool atStart = !cop && aiAtRaceStart(me.distance, me.place, me.racers);
    if (!cop && !atStart && carAhead[1] && aheadBy[1] < 2 && me.speed + 8.9406f < me.wanted && !me.flashing) {
        me.flashing = true;
        me.flashUntil = step + 64;
        me.beams = me.beams == 0 ? 1 : me.beams > 1 ? 0 : 2;
    }
    // 0x4094d0: behind a slower car (not a police car) going its way in
    // its lane, its speed within 2 slices, half the difference less at 2.
    if (!cop && carAhead[1] && !carAhead[1]->cop && carAhead[1]->way == me.way) {
        const float over = me.wanted - carAhead[1]->speed;
        if (over > 0) {
            if (aheadBy[1] < 2)
                me.wanted = carAhead[1]->speed;
            else if (aheadBy[1] == 2)
                me.wanted -= over * 0.5f;
        }
    }
    // 0x409920 (personality under 3): against the lanes of slower cars.
    if (!cop && me.driver.personality < 3)
        for (int c = 0; c < 3; ++c) {
            const AiCar* o = carAhead[size_t(c)];
            if (!o)
                continue;
            const float closing = along(me) - along(*o), wantedOver = static_cast<float>(me.way) * me.wanted - along(*o);
            if (closing > 0)
                vote(8, c, static_cast<int>(std::nearbyint(closing * -0.5f)));
            if (wantedOver > 0)
                vote(8, c, static_cast<int>(std::nearbyint(wantedOver * -0.1f)));
            if (aheadBy[size_t(c)] < static_cast<int>(std::nearbyint(me.speed * 0.8f * 0.1667f - 0.5f))) {
                if (closing > 0)
                    vote(8, c, static_cast<int>(std::nearbyint(closing * -1.3f)));
                if (wantedOver > 0)
                    vote(8, c, static_cast<int>(std::nearbyint(wantedOver * -0.5f)));
            }
        }
    // 0x4097c0: for empty lanes, against lanes with a car within 3.
    for (int c = 0; c < 3 && !cop && !atStart; ++c) {
        if (!carAhead[size_t(c)])
            vote(9, c, 5);
        else if (aheadBy[size_t(c)] < 3) {
            int v = carAhead[size_t(c)]->cop ? -24 : -6;
            if (!aheadCentre[size_t(c)])
                v /= 2;
            vote(10, c, v);
        }
    }
    // ai_lane_vote_line (0x4096b0): the best line's lane 4 slices on, for
    // a car running the race's way.
    if (!cop && !atStart && me.way > 0) {
        const int line = aiLineLane(speeds, ahead(4), n);
        if (line != 0) {
            const int weight = me.driver.skill == 0 ? 6 : me.driver.personality < 3 ? 10 : 12;
            vote(11, line < lane ? 0 : line == lane ? 1 : 2, weight);
        }
    }
    // lane_votes_ahead (0x40ddb0), at the slice the speed reaches.
    int look = static_cast<int>(std::nearbyint(std::fabs(me.speed) * 2.5f * (1.0f / 6.0f) - 0.5f));
    if (look < 10)
        look = 9;
    const int far = ahead(look);
    const TrackCollision::RoadPoint& there = point(far);
    const bool roadside = kind == CopCall::Roadside;
    if (roadside) {
        // 0x402d90: +100 for the nearer road edge's lane (its own off the
        // road's lanes); the state's other rows are 0x40d720, 0x40d800 and
        // 0x40d9f0 below.
        if (6 - leftLanes <= lane && lane <= rightLanes + 7)
            vote(3, lane < 7 ? 0 : 2, 100);
        else
            vote(3, 1, 100);
    }
    // 0x40d2e0: back onto the road's lanes where they end ahead.
    if (roadside) {
    } else if (track == 6 && me.way > 0 && !cop && mySlice >= 0x127 && mySlice <= 0x168) {
        vote(2, 0, -40);
    } else if ((lane < 7 - there.leftLanes() || lane > there.rightLanes() + 6) && 7 - leftLanes <= lane &&
               lane <= rightLanes + 6) {
        vote(2, lane < 7 ? 2 : 0, 40);
    }
    // 0x40d1f0: no road in a lane here (-1000, and a nudge away), or
    // ahead (-65).
    int nudge = 0;
    if (!roadside) {
        std::vector<int> slices{mySlice}; // up to the slice reached, not it
        bool reached = false;
        if (far != mySlice)
            for (int i = 1; i < 20 && !reached; ++i) {
                reached = ahead(i) == far;
                if (!reached)
                    slices.push_back(ahead(i));
            }
        if (far != mySlice)
            for (int c = 0; c < 3; ++c) {
                if (!aiLaneOpen(here, lane - 1 + c)) {
                    vote(4, c, -1000);
                    nudge = c == 0 ? 1 : c == 2 ? -1 : 0;
                    break;
                }
                for (size_t i = 1; i < slices.size(); ++i)
                    if (!aiLaneOpen(point(slices[i]), lane - 1 + c)) {
                        vote(4, c, -65);
                        break;
                    }
            }
    }
    // 0x40d3d0: the lanes past the road's edges, and off it.
    if (!roadside) {
        const float leftRoom = here.leftWall - static_cast<float>(leftLanes) * here.extra[1] * 0.5f;
        const float rightRoom = here.rightWall - static_cast<float>(rightLanes) * here.extra[2] * 0.5f;
        auto edge = [&](int l) { return l == 6 - leftLanes || l == rightLanes + 7; };
        auto off = [&](int l) { return l < 6 - leftLanes || l > rightLanes + 7; };
        if (edge(lane - 1))
            vote(3, 0, leftRoom < me.halfWidth ? -80 : -30);
        if (edge(lane))
            vote(3, 1, (lane < 7 ? leftRoom : rightRoom) < me.halfWidth ? -80 : -30);
        if (edge(lane + 1))
            vote(3, 2, rightRoom < me.halfWidth ? -80 : -30);
        for (int c = 0; c < 3; ++c)
            if (off(lane - 1 + c))
                vote(3, c, -1000);
    }
    // 0x40d720: a lane with a strip laid 1-5 slices ahead (0x7dd850's
    // records, by its cop) -50; in its own lane the speed wanted x 0.9.
    for (const AiCar& c : cars) {
        if (!c.stripOn)
            continue;
        const int d = sliceDiff(c.stripSlice, mySlice) * me.way;
        if (d <= 0 || d >= 6)
            continue;
        for (int k = 0; k < 3; ++k)
            if (lane + k - 1 == c.stripLane) {
                vote(5, k, -50);
                if (k == 1)
                    me.wanted *= 0.9f;
            }
    }
    // 0x40d800: against lanes with slower cars ahead within 10, and cars
    // coming the other way, by the time to meet them.
    for (int c = 0; c < 3; ++c) {
        const AiCar* o = carAhead[size_t(c)];
        if (!o)
            continue;
        if (o->way == me.way) {
            const float d = me.wanted - o->speed;
            if (d > 0 && aheadBy[size_t(c)] < 10) {
                int v = d < 3 ? static_cast<int>(std::nearbyint(d * -5.0f * (1.0f / 3.0f) - 0.5f)) - 3 : -8;
                v += std::max(aheadBy[size_t(c)] - 5, 0);
                if (!aheadCentre[size_t(c)])
                    v /= 2;
                if (v < 0)
                    vote(7, c, v);
            }
        } else {
            const float closing = along(me) - along(*o);
            if (std::fabs(closing) > 1) {
                const float t = static_cast<float>(static_cast<int>(aheadBy[size_t(c)] * 6.0f * 65536.0f)) / 65536.0f / closing;
                int v = -35 - static_cast<int>(std::nearbyint(t * -35.0f * 0.125f - 0.5f));
                if (!aheadCentre[size_t(c)])
                    v /= 2;
                if (v < 0)
                    vote(6, c, v);
            }
        }
    }
    // 0x40d9f0: a car beside it ahead.
    if (carAhead[0] && aheadBy[0] < 2)
        vote(1, 0, -4);
    if (carAhead[2] && aheadBy[2] < 2)
        vote(1, 2, -4);
    // lane_votes_bonus (0x409660), the racers'.
    for (int c = 0; c < 3 && !cop; ++c)
        if (votes[3][c] != 0)
            votes[3][c] += 22;
    // The chase's votes (0x402750, rows 6 and 13): for the target's lane,
    // 70 when it comes the other way, 10 behind it or more than 5 slices
    // on, else the lane beside it 30 and its own 10 (both sides beside it
    // 30 when in the cop's lane).
    if (cop && cop->chase && target) {
        if (toTarget < 0)
            votes[13][targetSide] += 10;
        else if (toTarget == 0)
            ;
        else if (target->way != me.way)
            votes[6][targetSide] += 70;
        else if (toTarget > 5 || target->pulledOver)
            votes[13][targetSide] += 10;
        else if (targetSide == 0) {
            votes[13][1] += 30;
            votes[13][0] += 10;
        } else if (targetSide == 1) {
            votes[13][1] += 10;
            votes[13][2] += 30;
            votes[13][0] += 30;
        } else {
            votes[13][2] += 10;
            votes[13][1] += 30;
        }
    }
    // lane_choose_best (0x40da30) and the aim.
    choose(nudge);
    // A cop near its slower target steers at it (0x402750 sets +0x711
    // bit 2 under 15.65 m/s or on its slice; 0x406b60 within 4 slices):
    // its aim its side offset.
    if (cop && cop->chase && target && ((target->speed < 15.64605f && !me.reversing) || toTarget == 0) &&
        std::abs(toTarget) <= 4)
        me.target = target->side;
}

} // namespace

void aiStep(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const std::vector<uint8_t>& speeds,
            const AiCarData& data, int step, int track, bool weather, AiPhase phase) {
    if (self >= cars.size())
        return;
    if (phase == AiPhase::Move) {
        aiMove(cars[self], data, weather, step, col, track);
        if (cars[self].putBackDue)
            aiPutBack(cars, self, col);
        return;
    }
    // car_driver_ai (0x409c00) goes through the same gate as the cops'
    // driver (0x40df10 -> 0x401240): a 32 Hz task let through when
    // (step / 2 + car +0) mod 4 is 0, so it acts every 8 steps.
    AiCar& me = cars[self];
    if (aiDriverActs(me, step)) {
        auto copsOn = [&]() {
            int on = 0;
            for (const AiCar& c : cars)
                on += c.cop && c.chasing == static_cast<int>(self) ? 1 : 0;
            return on;
        };
        // Its states (+0x6E0): 1 racing until its record is pulled over
        // (cop_target_is_stopped_ 0x401500), then 5 for 320 steps at most
        // (+0x6EC), pulling over to the road's edge (0x408b30, 0x408bd0),
        // 6 stopped once under 3 m/s, back to 1 when the ticket clears the
        // record; with no cops on the record back to 1 (0x409b40).
        if (me.pursuit == 1 && me.pulledOver) {
            me.pursuit = 5;
            me.pullUntil = step + 320;
        }
        if (me.pursuit == 5) {
            CopCall pull;
            pull.kind = CopCall::PullOver;
            aiDrive(cars, self, col, speeds, data, step, track, weather, &pull);
            if (std::fabs(me.speed) < 3.0f || me.pullUntil < step) {
                if (copsOn() == 0) {
                    me.pursuit = 1;
                    me.pulledOver = false;
                } else
                    me.pursuit = 6;
            }
        } else if (me.pursuit == 6) {
            me.wanted = 0;
            me.speed *= 0.5f; // car_stop_hard: the velocity halved
            me.sideSpeed *= 0.5f;
            if (!me.pulledOver)
                me.pursuit = 1;
            if (copsOn() == 0) {
                me.pursuit = 1;
                me.pulledOver = false;
            }
        } else if (me.pursuit == 4) {
            // State 4, out of the race (0x409c00): only the speed wanted
            // (ai_out_speed_wanted 0x408c50: 11 m/s while both its sides,
            // +0x7B8 -+ the half width in 16.16, are within the slice's lanes
            // and it is on the first 20 slices, else 0; negated backward;
            // read only by the gears' sound, as car_ai_model coasts it).
            const int n = static_cast<int>(col.road.size());
            if (n > 0) {
                const int at = ((aiSlice(me) % n) + n) % n;
                const auto& p = col.road[size_t(at)];
                const bool past = me.raceBackward ? at < n - 20 : at > 20;
                const int lo = -(static_cast<int>(p.extra[1]) * 0x8000 * p.leftLanes());
                const int hi = static_cast<int>(p.extra[2]) * 0x8000 * p.rightLanes();
                const int a = static_cast<int>(std::nearbyint((me.side - me.halfWidth) * 65536.0f));
                const int b = static_cast<int>(std::nearbyint((me.side + me.halfWidth) * 65536.0f));
                const bool off = a < lo || a > hi || b < lo || b > hi || past;
                me.wanted = off ? 0.0f : (me.raceBackward ? -11.0f : 11.0f);
            }
        } else {
            aiDrive(cars, self, col, speeds, data, step, track, weather, nullptr);
            // States 1-3 (0x409d0c): a racer (+0x200 bit 1) out of the race
            // (+0x2C4 3 finished, 4 busted) goes to 4.
            if (me.racer && !me.cop && (me.raceState == 3 || me.raceState == 4))
                me.pursuit = 4;
        }
    }
    if (phase == AiPhase::Drive)
        return;
    aiMove(me, data, weather, step, col, track);
    if (me.putBackDue)
        aiPutBack(cars, self, col);
    aiAdvance(me, col); // AiPhase::Both: the place moved too (car_move)
}

bool aiAtRaceStart(int distance, int place, int racers, bool backward, int extra) {
    int start = racers < 2 ? 50 : racers < 5 ? 200 : 400;
    if (backward)
        start -= 26;
    return start + extra >= place * 14 + distance;
}

bool aiDriverActs(const AiCar& car, int step) {
    return (step & 1) == 0 && ((step / 2 + car.phase) & 3) == 0;
}

float trackSectionSpeed(int track, int slice) {
    const unsigned s = static_cast<unsigned>(slice);
    switch (track) {
    case 0:
        if (s < 0x277 && s > 0x1e8)
            return 13.4109f;
        return 22.3515f;
    case 1:
        if (s < 0x3e9) {
            if (s > 0x316)
                return 31.2921f;
            if (s > 0xaa)
                return 29.05695f;
        }
        return 24.58665f;
    case 2:
        return 22.3515f;
    case 3:
        return s < 0x56f && s > 0x528 ? 13.889f : 22.2224f;
    case 4:
        if (s < 0x154 && s > 0x127)
            return 8.9406f;
        return 22.3515f;
    case 5:
    case 6:
        return 22.3515f;
    case 7:
        return s < 0x65f && s > 0x60d ? 13.889f : 22.2224f;
    case 8:
        if (s < 0x3e9 && s > 400)
            return 15.64605f;
        return 20.11635f;
    default:
        return 20.11635f;
    }
}

int aiRacersAhead(int humanDistance, const std::vector<int>& racerDistances) {
    if (racerDistances.empty())
        return 0;
    size_t ahead = 0;
    for (int d : racerDistances)
        ahead += d > humanDistance ? 1 : 0;
    return ahead == 0 ? 0 : ahead == racerDistances.size() ? 2 : 1;
}

float aiCatchUpFactor(const AiRace& race, int distance, int place) {
    if (!race.human)
        return 1;
    const bool cup = race.raceType == 1 || race.raceType == 2; // Tournament, Knockout
    float f = 1;
    if (race.ahead == 0) {
        const int behind = cup && race.expert ? 5 : race.aggressive ? 10 : 15;
        if (behind + distance < race.humanDistance)
            f = cup ? 1.15f : 1.1f;
    } else if (race.ahead != 2) {
        const int ahead = cup && race.expert ? 100 : race.aggressive ? 70 : 50;
        if (distance >= ahead + race.humanDistance)
            f = 0.85f;
    } else if (race.aggressive) {
        f = 0.95f;
    } else if (race.raceType == 3) {
        f = 0.85f;
    } else if (place < 6 || race.link != 0) {
        f = 0.9f;
    } else {
        f = 0.8f;
    }
    if (race.raceType == 2 && race.track == 8 && f < 1)
        f = 1;
    if (race.link > 1 && f > 1)
        f = 1;
    return f;
}

namespace {

// t_ticket_times (0x5521ac), by tickets: the slices a chase is given up
// past (+4 with +0x711 bit 1), the steps a racer is then free of chases
// (+8) and the steps a ticket takes (+0xC).
struct TicketTimes {
    int giveUp, giveUpFast, free, wait;
};
constexpr TicketTimes kTicketTimes[8] = {{50, 65, 960, 384}, {50, 65, 960, 384}, {70, 75, 896, 384}, {70, 85, 768, 384},
                                         {90, 105, 640, 384}, {90, 105, 576, 448}, {90, 115, 512, 448}, {90, 115, 512, 448}};
const TicketTimes& ticketTimes(int tickets) { return kTicketTimes[std::clamp(tickets, 0, 7)]; }

// A car's place in the track (its slice's point, on towards the next by
// `along`, its side offset along the right), for the distances the cops'
// tests take (dist_approx).
std::array<float, 3> aiPlace(const TrackCollision& col, const AiCar& c) {
    const int n = static_cast<int>(col.road.size());
    const auto& a = col.road[size_t(((c.slice % n) + n) % n)];
    const auto& b = col.road[size_t(((c.slice + 1) % n + n) % n)];
    std::array<float, 3> out{};
    for (size_t k = 0; k < 3; ++k)
        out[k] = a.position[k] + (b.position[k] - a.position[k]) * c.along + a.right[k] * c.lateral;
    return out;
}

float aiDistance(const TrackCollision& col, const AiCar& a, const AiCar& b) {
    const auto p = aiPlace(col, a), q = aiPlace(col, b);
    return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}

void radioCall(AiCar& cop, uint32_t call, int step) {
    cop.radio = call;
    cop.radioStep = step;
}

} // namespace

const char* aiRadioCall(uint32_t call) {
    switch (call) {
    case 0x40b220: return "in pursuit (a cop takes a target)";
    case 0x40b670: return "suspect ticketed";
    case 0x40b720: return "requesting backup";
    case 0x40b910: return "requesting a spike strip";
    case 0x40b860: return "requesting a roadblock";
    case 0x40b9c0: return "spike strip laid";
    case 0x40bac0: return "roadblock in place";
    case 0x40bca0: return "suspect missed the strip";
    case 0x40bd00: return "suspect ran the roadblock";
    case 0x40b480: return "lost him";
    default: return "";
    }
}

void aiStripSpan(const TrackCollision::RoadPoint& p, int lane, float& from, float& to) {
    const float width = static_cast<float>(lane < 7 ? p.extra[1] : p.extra[2]) * 0.5f;
    const float middle = lane < 7 ? -(static_cast<float>(6 - lane) + 0.5f) * width : (static_cast<float>(lane - 7) + 0.5f) * width;
    from = middle - 0.75f * width;
    to = middle + 0.75f * width;
}

std::array<std::array<float, 3>, 4> aiStripQuad(const TrackCollision::RoadPoint& p, int lane) {
    float from = 0, to = 0;
    aiStripSpan(p, lane, from, to);
    std::array<std::array<float, 3>, 4> quad{};
    for (size_t k = 0; k < 3; ++k) {
        quad[0][k] = p.position[k] + p.right[k] * from;
        quad[1][k] = p.position[k] + p.right[k] * to;
    }
    quad[2] = quad[1];
    quad[3] = quad[0];
    quad[2][1] += 0.5f;
    quad[3][1] += 0.5f;
    return quad;
}

unsigned aiStripWheels(const std::array<std::array<float, 3>, 4>& quad, const std::array<float, 3>& at,
                       const std::array<float, 3>& right, const std::array<float, 3>& up,
                       const std::array<float, 3>& forward, const std::array<std::array<float, 2>, 4>& wheels) {
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    unsigned hit = 0;
    for (size_t w = 0; w < 4; ++w) {
        std::array<float, 3> wheel{};
        for (size_t k = 0; k < 3; ++k)
            wheel[k] = at[k] + right[k] * wheels[w][0] + forward[k] * wheels[w][1];
        unsigned all = 0x3f;
        for (const auto& corner : quad) {
            const std::array<float, 3> d{corner[0] - wheel[0], corner[1] - wheel[1], corner[2] - wheel[2]};
            const float c[3] = {dot(d, right), dot(d, up), dot(d, forward)};
            unsigned code = 0;
            for (int k = 0; k < 3; ++k) {
                if (c[k] > 0.5f)
                    code |= 2u << (2 * k);
                if (c[k] < -0.5f)
                    code |= 1u << (2 * k);
            }
            all &= code;
        }
        if (all == 0)
            hit |= 1u << w;
    }
    return hit;
}

int aiStopHold(bool humanTarget, int raceDistance) {
    if (!humanTarget) {
        if (raceDistance > 60)
            return 160;
        if (raceDistance < -60)
            return 224;
    }
    return 192;
}

void aiOrderStrip(AiCar& cop, int racer, int sliceCount) {
    int from = cop.slice - 6;
    if (from < 0)
        from += sliceCount;
    cop.orderKind = 0;
    cop.orderTaken = false;
    cop.orderFrom = from;
    cop.orderLength = 3;
    cop.orderAgainst = false;
    cop.watching = racer;
    cop.blockWatch = false;
    cop.stripWanted = true;
}

void aiOrderRoadblock(AiCar& cop, int racer, int slice) {
    cop.orderKind = 4;
    cop.orderTaken = false;
    cop.orderFrom = slice;
    cop.orderLength = 0;
    cop.orderAgainst = false;
    cop.watching = racer;
    cop.siren = true;
    cop.blockWatch = true;
}

void aiOrderPatrol(AiCar& cop) {
    cop.orderKind = 2;
    cop.orderTaken = false;
    cop.orderFrom = cop.orderLength = 0;
    cop.orderAgainst = (cop.index & 1) != 0;
    cop.watching = -1;
}

namespace {

using Vec3 = std::array<float, 3>;
Vec3 vAdd(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 vSub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 vScale(const Vec3& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }
float vDot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float vLength(const Vec3& a) { return std::sqrt(vDot(a, a)); }
// 0x4e0160: a x b.
Vec3 vCross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float boxDiagonal(const CarBox& c) { // +0x120 (0x4a0d20)
    return std::sqrt(c.half[0] * c.half[0] + c.half[1] * c.half[1] + c.half[2] * c.half[2]);
}

// 0x42e820 with 0x42e720: one car's share of a hit, its strength.
float carBoxImpulse(CarBox& c, Vec3 j, const Vec3& r, float f) {
    const float invMass = 1.0f / c.mass; // +0xB4
    if (c.mass * 5.0f < vLength(j)) // 0x5373bc
        j = vScale(j, 0.75f);        // 0x5537ac
    const float length = vLength(r); // 0x4972f0
    const Vec3 unit = length > 0 ? vScale(r, 1.0f / length) : r;
    float push = -vDot(j, unit) * f * invMass;
    if (push > 0) {
        const float size = vLength(j);
        if (size > 0)
            c.velocity = vAdd(c.velocity, vScale(j, push / size));
    } else {
        push = 0;
    }
    float turn = 0;
    if (length != 0) {
        const Vec3 lever = vCross(unit, j);
        turn = f * vLength(lever) * invMass;
        const float k = f / boxDiagonal(c) * 0.159154958f * invMass; // 1/2 pi, 0x5373c4
        c.spin = vAdd(c.spin, vScale(lever, k));
    }
    for (float& w : c.spin) // 0x42e720: +-1.8 (0x5537b0)
        w = std::clamp(w, -1.8f, 1.8f);
    return push + turn;
}

} // namespace

bool carBoxTouch(CarBox& a, CarBox& b, Vec3& point) {
    const Vec3* ar[3] = {&a.right, &a.up, &a.forward};
    const Vec3* br[3] = {&b.right, &b.up, &b.forward};
    float m[3][3]; // a's rows . b's rows (0x5efebc-0x5efedc)
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            m[i][k] = vDot(*ar[i], *br[k]);
    static constexpr int kFaces[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    static constexpr int kCorners[8][3] = {{1, 1, 1},  {1, 1, -1},  {1, -1, 1},  {1, -1, -1},
                                           {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1}};
    bool found = false;
    for (int round = 0; round < 4; ++round) {
        const Vec3 d = vSub(b.at, a.at); // 0x4e0000
        float da[3], db[3];              // b's centre in a's frame and in b's (0x5efe60-0x5efe74)
        for (int i = 0; i < 3; ++i) {
            da[i] = vDot(*ar[i], d);
            db[i] = vDot(*br[i], d);
        }
        // b's point (s x its half sizes) inside a's box (0x42dd60, 0x42e2c0).
        auto bInA = [&](const int* s) {
            for (int i = 0; i < 3; ++i) {
                float p = da[i];
                for (int k = 0; k < 3; ++k)
                    p += static_cast<float>(s[k]) * b.half[size_t(k)] * m[i][k];
                if (!(p <= a.half[size_t(i)] && -a.half[size_t(i)] <= p))
                    return false;
            }
            point = b.at;
            for (int k = 0; k < 3; ++k)
                point = vAdd(point, vScale(*br[k], static_cast<float>(s[k]) * b.half[size_t(k)]));
            return true;
        };
        // a's point inside b's box (0x42e010, 0x42e4f0).
        auto aInB = [&](const int* s) {
            for (int k = 0; k < 3; ++k) {
                float q = -db[k];
                for (int i = 0; i < 3; ++i)
                    q += static_cast<float>(s[i]) * a.half[size_t(i)] * m[i][k];
                if (!(q <= b.half[size_t(k)] && -b.half[size_t(k)] <= q))
                    return false;
            }
            point = a.at;
            for (int i = 0; i < 3; ++i)
                point = vAdd(point, vScale(*ar[i], static_cast<float>(s[i]) * a.half[size_t(i)]));
            return true;
        };
        bool in = false;
        for (int f = 0; f < 6 && !in; ++f)
            in = bInA(kFaces[f]);
        for (int f = 0; f < 6 && !in; ++f)
            in = aInB(kFaces[f]);
        for (int c = 0; c < 8 && !in; ++c)
            in = bInA(kCorners[c]);
        for (int c = 0; c < 8 && !in; ++c)
            in = aInB(kCorners[c]);
        if (!in)
            break;
        found = true;
        // 0x42fe10: apart along the line between the centres, 0.25
        // (0x537404) in all, each by the other's share of the masses.
        const float length = vLength(d);
        const Vec3 unit = length > 0 ? vScale(d, 1.0f / length) : d;
        const float both = a.mass + b.mass;
        a.at = vAdd(a.at, vScale(unit, -0.25f * b.mass / both));
        b.at = vAdd(b.at, vScale(unit, 0.25f * a.mass / both));
    }
    return found;
}

void carBoxHit(CarBox& a, CarBox& b, const Vec3& point) {
    constexpr float kTurn = 6.2831845f; // 0x40c90fd9
    const Vec3 ra = vSub(point, a.at), rb = vSub(point, b.at);
    const Vec3 va = vAdd(vCross(vScale(a.spin, kTurn), ra), a.velocity);
    const Vec3 vb = vAdd(vCross(vScale(b.spin, kTurn), rb), b.velocity);
    const float both = a.mass + b.mass;
    const Vec3 ja = vScale(vSub(vb, va), b.mass), jb = vScale(vSub(va, vb), a.mass);
    const Vec3 line = vSub(b.at, a.at);
    if (vDot(line, ja) < 0) {
        a.strength = carBoxImpulse(a, ja, ra, a.mass / both);
        a.hitPoint = point;
        if (a.strength > 5.0f) // 0x5373f4
            ++a.airTicks;
    }
    if (vDot(line, jb) > 0) {
        b.strength = carBoxImpulse(b, jb, rb, b.mass / both);
        b.hitPoint = point;
        if (b.strength > 5.0f)
            ++b.airTicks;
    }
}

bool carBoxCollide(CarBox& a, CarBox& b) {
    Vec3 point{}, last{};
    bool touched = false;
    for (int k = 0; k < 33 && carBoxTouch(a, b, point); ++k) {
        touched = true;
        last = point;
    }
    if (touched)
        carBoxHit(a, b, last);
    return touched;
}

int carBoxCollisions(std::vector<CarBox>& cars, const std::vector<bool>& in) {
    int hits = 0;
    std::vector<size_t> list; // 0x5efe10
    for (size_t i = 0; i < cars.size(); ++i) {
        if (i < in.size() && !in[i])
            continue;
        CarBox& me = cars[i];
        const float mine = boxDiagonal(me);
        for (size_t k : list) {
            CarBox& other = cars[k];
            const float reach = mine + boxDiagonal(other);
            if (std::fabs(me.at[2] - other.at[2]) < reach && std::fabs(me.at[0] - other.at[0]) < reach &&
                std::fabs(me.at[1] - other.at[1]) < reach)
                hits += carBoxCollide(me, other) ? 1 : 0;
        }
        list.push_back(i);
    }
    return hits;
}

void aiReverseSet(AiCar& car, int mode, int steps, int step) {
    auto start = [&]() {
        car.reversing = true;
        ++car.reverseCount;
        car.reverseTimed = steps != 0;
        if (steps != 0)
            car.reverseUntil = step + steps;
    };
    if (mode == 0) {
        start();
    } else if (mode == 1) {
        car.reversing = false;
        car.reverseTimed = false;
    } else if (car.reversing) {
        // 0x406036: off, unless timed and its time not yet up.
        car.reversing = car.reverseTimed && step < car.reverseUntil;
    } else {
        start();
    }
}

bool aiSpotOnRoad(const Track& track, int slice, const std::array<float, 3>& point) {
    // 0x428ad0 after the lane and the cars: trackpos_at_point (0x4205a0)
    // builds a track position at the point from the slice; it must be in
    // a polygon (+0x14), whose record byte 1 (trackpos_surface 0x4207e0)
    // has neither 0x40 nor 0x80 and a surface (& 0x3F) neither 0 nor 14,
    // and whose road vector's up y (s16 x 2, an int) is at least 0.8
    // (0x5371b0, a double): any up y above 0.
    RoadSearch search;
    search.slice = slice;
    if (!roadSearch(track, search, point[0], point[2]))
        return false;
    const size_t block = static_cast<size_t>(search.foundSlice >> 3);
    if (block >= track.blocks.size())
        return false;
    const Track::Block& b = track.blocks[block];
    const size_t index = static_cast<size_t>(search.polygon);
    if (8 * index + 1 >= b.polygonRoad.size())
        return false;
    const int byte1 = b.polygonRoad[8 * index + 1];
    if ((byte1 & 0xc0) != 0 || (byte1 & 0x3f) == 0 || (byte1 & 0x3f) == 14)
        return false;
    RoadFrame f;
    return polygonRoadFrame(b, index, f) && f.up[1] * 32768.0f * 2 >= 0.8f;
}

bool aiSpotFree(const std::vector<AiCar>& cars, size_t self, const TrackCollision& col, int slice, float offset) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2)
        return false;
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    const auto& p = col.road[size_t(wrap(slice))];
    const int lane = aiLaneAt(p, offset);
    if (!aiLaneOpen(p, lane))
        return false;
    for (size_t i = 0; i < cars.size(); ++i) {
        if (i == self)
            continue;
        const AiCar& o = cars[i];
        if (std::abs(o.lane - lane) > 1)
            continue;
        const int at = wrap(aiSlice(o));
        if (wrap(slice) == at || wrap(slice) == wrap(at - 1) || wrap(slice) == wrap(at + 1))
            return false;
    }
    // The road under the spot (the slice's point + the offset along its
    // right), when the car has searched the track's polygons.
    if (self < cars.size() && cars[self].roadTrack) {
        std::array<float, 3> at{};
        for (size_t k = 0; k < 3; ++k)
            at[k] = p.position[k] + p.right[k] * offset;
        return aiSpotOnRoad(*cars[self].roadTrack, wrap(slice), at);
    }
    return true;
}

void aiPutBack(std::vector<AiCar>& cars, size_t self, const TrackCollision& col) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || self >= cars.size())
        return;
    AiCar& me = cars[self];
    me.putBackDue = false;
    // 0x429100: not while it (or, for a cop, its target) is pulled over.
    if (me.pulledOver || (me.chasing >= 0 && static_cast<size_t>(me.chasing) < cars.size() && cars[size_t(me.chasing)].pulledOver))
        return;
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    // The race's way (0x429100: +1, -1 on a backward track g_backward), not
    // the car's: 5 slices on (0x5efdcc), and on from there (0x428d60).
    const int way = me.raceBackward ? -1 : 1;
    const int from = wrap(aiSlice(me) + way * 5); // 0x5efdcc
    // 0x428d60: the slice's point if free, else the lanes' middles out
    // from the point, right before left (0x428e1a, 0x428e88), on along the
    // way for up to 10 slices; none free, the first slice's point.
    int slice = from;
    float offset = 0;
    bool found = aiSpotFree(cars, self, col, from, 0);
    for (int tries = 0, s = from; !found && tries < 10; ++tries, s = wrap(s + way)) {
        const auto& p = col.road[size_t(s)];
        const int left = p.leftLanes(), right = p.rightLanes();
        const float leftWidth = static_cast<float>(p.extra[1]) * 0.5f, rightWidth = static_cast<float>(p.extra[2]) * 0.5f;
        for (int i = 0; i < std::max(left, right) && !found; ++i) {
            const float r = rightWidth * static_cast<float>(i) + rightWidth * 0.5f;
            const float l = -(leftWidth * static_cast<float>(i)) - leftWidth * 0.5f;
            if (i <= right && aiSpotFree(cars, self, col, s, r)) {
                slice = s;
                offset = r;
                found = true;
            } else if (i <= left && aiSpotFree(cars, self, col, s, l)) {
                slice = s;
                offset = l;
                found = true;
            }
        }
    }
    // 0x4a0aa0: on the slice's point, moved along its right, lined up with
    // the road and still (0x429100: the velocity 0; 0x428970: the spins,
    // the forces 0, +0x164 1, the strip's catch +0x621 bit 0x40 cleared);
    // a ghost to the other cars for 64 ticks (+0x174).
    me.slice = slice;
    me.roadSlice = slice;
    me.velSlice = slice;
    me.along = 0;
    me.lateral = offset;
    me.side = offset;
    me.sideKept = true;
    me.speed = me.sideSpeed = me.carSide = me.heading = me.yawRate = me.headingError = 0;
    me.vel = {0, 0, 0};
    me.worldKept = false;
    me.roll = me.pitch = me.rollRate = me.pitchRate = 0;
    me.airTicks = 1;
    me.spiked = false;
    me.roadRightSet = false;
    me.contactUpSet = false;
    me.lane = aiLaneAt(col.road[size_t(slice)], offset);
    me.ghostTicks = 64;
    // car_place_on_slice's rows (0x4a0980): the road polygon's 16.16 rows
    // there (here facing the car's way, as the page keeps it).
    if (me.roadTrack) {
        aiRoadFrame(*me.roadTrack, col, me);
        const float w = me.way < 0 ? -1.0f : 1.0f;
        if (me.roadRightSet && me.roadUpSet)
            aiSetRows(me, col, {0, 0, 0}, {me.roadRight[0] * w, me.roadRight[1] * w, me.roadRight[2] * w}, me.roadUp,
                      {me.roadForward[0] * w, me.roadForward[1] * w, me.roadForward[2] * w});
        me.side = aiMeasuredSide(col, me);
    }
}

void aiTilt(AiCar& car, int step) {
    if ((step & 1) != 0)
        return;
    constexpr float kTurn = 2 * 3.14159265358979f;
    if (car.airTicks > 0) {
        car.yawRate *= 0.6f;
        car.rollRate *= 0.6f;
        car.pitchRate *= 0.6f;
        if (std::fabs(car.yawRate) < 0.6f && std::fabs(car.rollRate) < 0.6f && std::fabs(car.pitchRate) < 0.6f)
            car.airTicks = 0;
    } else {
        // 0x4a1b18-0x4a1cc6: the body's up against the road's, by axis.
        const float r = std::sin(car.roll * kTurn), p = std::sin(car.pitch * kTurn);
        if (std::fabs(r) <= 0.02f && std::fabs(p) <= 0.02f) {
            car.rollRate = car.pitchRate = 0;
        } else {
            car.rollRate = -std::clamp(r, -0.5f, 0.5f);
            car.pitchRate = -std::clamp(p, -0.5f, 0.5f);
        }
    }
    car.roll += car.rollRate / 32.0f;
    car.pitch += car.pitchRate / 32.0f;
    car.roll -= std::nearbyint(car.roll);
    car.pitch -= std::nearbyint(car.pitch);
}

void aiTilt(AiCar& car, int step, const TrackCollision& col) {
    if ((step & 1) != 0)
        return;
    if (car.airTicks > 0 || col.road.size() < 2) {
        car.spinX = car.spinZ = 0;
        aiTilt(car, step);
        return;
    }
    // car_on_road (0x4a1610, 0x4a1b18-0x4a1cc6), on the world's axes: the
    // spin about x (+0xE4) is the road's normal's z less the body's up's,
    // about z (+0xEC) the body's up's x less the normal's, each held to
    // +-0.5 turn/s (0x53c84c, 0x53c854); both 0 once both are within 0.02
    // (0x53c844) with the body upright on the road (+0x10C over 0.9,
    // 0x53c83c). car_integrate_turn (0x4a2e20) turns the body by them
    // about the world's axes; here as rates about its forward and right.
    const int n = static_cast<int>(col.road.size());
    const auto& a = col.road[size_t(((car.slice % n) + n) % n)];
    const std::array<float, 3> rf = unit3(car.roadRightSet ? car.roadForward : a.forward);
    std::array<float, 3> nu = unit3(cross3(rf, car.roadRightSet ? car.roadRight : a.right));
    if (nu[1] < 0)
        nu = {-nu[0], -nu[1], -nu[2]};
    // car_on_road turns the body towards the up of the rows it took the
    // velocity through (0x4a1a3d's frame, aiContactFrame), when the box
    // touches (the clearance 0 or less); the page always.
    if (car.contactUpSet)
        nu = car.contactUp;
    std::array<float, 3> f{}, r{}, u{};
    aiTiltedAxes(col, car, f, r, u);
    const float dz = u[0] - nu[0], dx = nu[2] - u[2];
    if (dot3(u, nu) > 0.9f && std::fabs(dz) <= 0.02f && std::fabs(dx) <= 0.02f) {
        car.rollRate = car.pitchRate = 0;
        car.spinX = car.spinZ = 0;
        return;
    }
    const std::array<float, 3> w{std::clamp(dx, -0.5f, 0.5f), 0, std::clamp(dz, -0.5f, 0.5f)};
    car.spinX = w[0];
    car.spinZ = w[2];
    car.rollRate = dot3(w, f);
    car.pitchRate = -dot3(w, r);
    car.roll += car.rollRate / 32.0f;
    car.pitch += car.pitchRate / 32.0f;
    car.roll -= std::nearbyint(car.roll);
    car.pitch -= std::nearbyint(car.pitch);
}

void aiSetTilt(AiCar& c, const TrackCollision& col, const std::array<float, 3>& up) {
    // The roll and pitch that give the body this up in the world, against
    // its yaw on the slice's up (aiTiltedAxes): up = u cr cp - r sr cp - f sp.
    constexpr float kTurn = 2 * 3.14159265358979f;
    std::array<float, 3> f{}, r{}, u{};
    aiBodyAxes(col, c, f, r, u);
    const std::array<float, 3> b = unit3(up);
    const float sp = std::clamp(-dot3(b, f), -1.0f, 1.0f);
    const float cp = std::sqrt(std::max(0.0f, 1 - sp * sp));
    c.pitch = std::asin(sp) / kTurn;
    c.roll = cp > 1e-6f ? std::atan2(-dot3(b, r) / cp, dot3(b, u) / cp) / kTurn : 0;
}

float aiCarMass(float carpMass, bool racer) {
    return racer ? carpMass * (1.0f / 1.2f) : carpMass; // 1 / 0x5efdb8 (1.2, 0x3f99999a), kept as a float
}

void aiCollide(std::vector<AiCar>& cars, const TrackCollision& col, int step, std::vector<AiHit>* hits) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || (step & 1) != 0)
        return;
    auto wrap = [&](int s) { return ((s % n) + n) % n; };
    // car_collisions_task (0x4311d0): a car put back (+0x174, the ghost
    // ticks, taken down each tick first) isn't entered into the hits.
    std::vector<size_t> in;
    for (size_t i = 0; i < cars.size(); ++i) {
        if (cars[i].ghostTicks > 0)
            --cars[i].ghostTicks;
        if (cars[i].ghostTicks == 0)
            in.push_back(i);
    }
    std::vector<CarBox> boxes(in.size()), before;
    for (size_t j = 0; j < in.size(); ++j) {
        AiCar& c = cars[in[j]];
        const auto& a = col.road[size_t(wrap(c.slice))];
        const auto& b = col.road[size_t(wrap(c.slice + 1))];
        if (!c.sideKept)
            c.lateral = aiLateralFor(col, c, c.offset);
        CarBox& box = boxes[j];
        // Its world velocity and body (aiBodyAxes: its yaw on the road's
        // up; left-handed, right = up x forward).
        if (c.sideKept)
            aiWorldIn(col, c);
        const Vec3 up = unit3(a.normal);
        for (size_t k = 0; k < 3; ++k)
            box.at[k] = a.position[k] + a.right[k] * c.lateral + (b.position[k] - a.position[k]) * c.along +
                        up[k] * c.halfHeight;
        box.velocity = aiVelocity(col, c);
        aiBodyAxes(col, c, box.forward, box.right, box.up);
        box.half = {c.halfWidth, c.halfHeight, c.halfLength};
        box.mass = c.mass;
        // aiTiltedAxes pitches the up towards -forward: a turn of -pitch
        // about the right.
        box.spin = vAdd(vAdd(vScale(box.up, c.yawRate), vScale(box.forward, c.rollRate)), vScale(box.right, -c.pitchRate));
    }
    before = boxes;
    if (carBoxCollisions(boxes) == 0)
        return;
    for (size_t j = 0; j < in.size(); ++j) {
        AiCar& c = cars[in[j]];
        const CarBox& was = before[j];
        CarBox& now = boxes[j];
        if (now.at == was.at && now.velocity == was.velocity && now.spin == was.spin && now.strength == 0)
            continue;
        aiWorldIn(col, c);
        c.vel = vAdd(c.vel, vSub(now.velocity, was.velocity));
        c.yawRate += vDot(vSub(now.spin, was.spin), was.up);
        c.rollRate += vDot(vSub(now.spin, was.spin), was.forward);
        c.pitchRate -= vDot(vSub(now.spin, was.spin), was.right);
        // Its place: the point back across the line from its slice's point
        // to the next's, on to the slice it is now on.
        const Vec3 up = unit3(col.road[size_t(wrap(c.slice))].normal);
        if (now.at != was.at)
            aiLocate(c, col, vSub(now.at, vScale(up, c.halfHeight)));
        c.side = aiMeasuredSide(col, c);
        c.sideKept = true;
        aiViews(col, c);
        c.airTicks += now.airTicks;
        // The impacts task (0x42b900 -> 0x404a00): a car driven by the
        // computer (+0x200 bit 2) hit by another computer car takes the
        // sliding controller for round(strength) x 7 steps.
        // Before that (0x406120): hit under 10 m/s (+0x7C4, 0x5361ec), it
        // starts reversing for 128 steps, or stops (ai_reverse_set(2, 0x80)).
        if (now.strength != 0) {
            if (hits)
                hits->push_back({in[j], now.strength, now.hitPoint});
            if (std::fabs(c.speed) < 10.0f)
                aiReverseSet(c, 2, 128, step);
            c.slideUntil = step + static_cast<int>(std::nearbyint(now.strength)) * 7;
        }
    }
}

void aiCopStep(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const AiCarData& data, int step,
               int track, bool weather, AiPhase phase) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || self >= cars.size())
        return;
    AiCar& me = cars[self];
    if (phase == AiPhase::Move) {
        aiMove(me, data, weather, step, col, track);
        if (me.putBackDue)
            aiPutBack(cars, self, col);
        return;
    }
    auto sliceDiff = [&](int a, int b) { // 0x40e120
        int d = a - b;
        if (d > n / 2)
            d -= n;
        else if (d < -(n / 2))
            d += n;
        return d;
    };
    auto inRange = [&](int slice) { // 0x40f000: from to from + length, round the track
        const int from = me.orderFrom;
        int to = from + me.orderLength;
        if (me.orderLength < 0 && to < 0)
            to += n;
        else if (to >= n)
            to -= n;
        if (to < from)
            return slice >= from || slice <= to;
        return from <= slice && slice <= to;
    };
    // cop_ai_order_head (0x403000, 0x4028e0): facing the order's first
    // slice; the page turns the car on the spot.
    auto face = [&](bool against) {
        const int way = against ? -1 : 1;
        if (me.way != way) {
            me.way = way;
            me.speed = 0;
            me.sideSpeed = me.carSide = me.heading = me.yawRate = 0;
                    me.worldKept = false;
            me.reversing = false;
        }
    };
    auto head = [&]() { face(sliceDiff(me.orderFrom, me.slice) < 0); };
    auto target = [&]() -> AiCar* {
        return me.chasing >= 0 && static_cast<size_t>(me.chasing) < cars.size() ? &cars[size_t(me.chasing)] : nullptr;
    };
    // The cops chasing a racer (its record's +0x2C).
    auto copsOn = [&](size_t racer) {
        int on = 0;
        for (const AiCar& c : cars)
            on += c.cop && c.chasing == static_cast<int>(racer) ? 1 : 0;
        return on;
    };
    // cop_lose_target (0x401a80): the chase given up, the siren off; not
    // while the racer is pulled over (its record's +0x1C 2). The last cop
    // on it closes the record and counts a chase lost (+0x14), unless a
    // strip caught it (+0x621 bit 0x40).
    auto lose = [&]() {
        AiCar* t = target();
        if (t && t->pulledOver)
            return false;
        me.chasing = -1;
        me.siren = false;
        me.holding = false;
        me.reversing = false;
        if (t && copsOn(size_t(t - cars.data())) == 0 && !t->spiked) {
            t->recordOpen = false;
            ++t->chasesLost;
        }
        return true;
    };
    // cop_set_target (0x4018b0): the chase begins, the siren on; a cop
    // that was watching a racer for a roadblock or a strip stops (its
    // strip taken up); the record opened if it wasn't (+0x20 the step,
    // the first call for help 640 steps on); the only cop on the racer
    // radios (0x40b220 from 0x404070).
    auto setTarget = [&](int racer) {
        if (me.watching >= 0) {
            me.blockWatch = false;
            if (me.stripWanted) {
                me.stripWanted = false;
                if (static_cast<size_t>(me.watching) < cars.size())
                    cars[size_t(me.watching)].stripCop = -1;
                me.stripOn = me.stripNear = false;
            }
        }
        AiCar& t = cars[size_t(racer)];
        if (!t.recordOpen) {
            t.recordOpen = true;
            t.recordStep = 0;
            t.stopping = 0;
            t.calls = 0;
            t.caller = -1;
            t.chaseFrom = step;
            t.callAt = step + 0x280;
        }
        me.chasing = racer;
        me.copState = 5;
        me.siren = true;
        me.across = false;
        radioCall(me, 0x40b220, step);
    };
    // racer_overspeed_update (0x4014b0): +0x6F0 the most over the
    // section's speed.
    auto overspeed = [&](AiCar& o) {
        o.overspeed = std::max(o.overspeed, o.speed - trackSectionSpeed(track, o.slice));
    };
    // The strip's hits (car_spike_check 0x4297d0 each step,
    // spike_hit_test 0x4ca160): a racer in the race within a slice of the
    // strip whose wheels touch its quad (aiStripWheels) runs over it: the
    // strip cleared (0x4c9f50), the racer pulled over (spike_hit_racer
    // 0x401650: its record's +0x1C = 2, +0x621 bit 0x40) and, when it has
    // been over the section's speed by more than 2 m/s (+0x6F0), is
    // chased or has lost a chase, the strip's cop takes it as its target
    // if it has none, the record's +0x28 = step + 320 with a cop on it.
    if (me.stripOn) {
        const int at = ((me.stripSlice % n) + n) % n;
        const auto quad = aiStripQuad(col.road[size_t(at)], me.stripLane);
        for (size_t i = 0; i < cars.size(); ++i) {
            AiCar& o = cars[i];
            if (!o.racer || std::abs(sliceDiff(o.slice, at)) > 1)
                continue;
            const auto& p = col.road[size_t(((o.slice % n) + n) % n)];
            const float w = static_cast<float>(o.way);
            const std::array<float, 3> right{p.right[0] * w, p.right[1] * w, p.right[2] * w};
            const std::array<float, 3> forward{p.forward[0] * w, p.forward[1] * w, p.forward[2] * w};
            const std::array<float, 3> up{p.normal[0], p.normal[1], p.normal[2]};
            if (!aiStripWheels(quad, aiPlace(col, o), right, up, forward, o.wheels))
                continue;
            me.stripOn = me.stripNear = me.stripWanted = false;
            o.pulledOver = o.spiked = true;
            if (o.overspeed > 2.0f || copsOn(i) != 0 || o.chasesLost != 0) {
                if (!target())
                    setTarget(static_cast<int>(i));
                if (copsOn(i) != 0)
                    o.recordStep = step + 320;
            }
            o.recordOpen = true; // +0x1C 2 either way
            break;
        }
    }
    // 0x40df10 -> 0x401240: the driver acts every 8 steps, on its phase.
    if (aiDriverActs(me, step)) {
        if (me.copState == 0)
            me.copState = me.siren ? 7 : 1;
        // cop_ai_spike_watch (0x403df0): radio once as the watched racer
        // is past the strip by less than 30 slices, clear it 50 past. The
        // game counts the slices the race's way (negated with g_backward);
        // the page's racers may go either way, so by the racer's.
        if (me.stripOn && me.watching >= 0 && static_cast<size_t>(me.watching) < cars.size()) {
            const AiCar& w = cars[size_t(me.watching)];
            const int d = sliceDiff(me.stripSlice, w.slice) * w.way;
            if (d < 0 && d > -30 && !me.stripNear) {
                me.stripNear = true;
                radioCall(me, 0x40bca0, step);
            }
            if (d < -49 && d > -(n / 4)) {
                me.stripOn = me.stripWanted = false;
                aiOrderPatrol(me);
            }
        }
        // cop_ai_block_watch (0x403ed0): the watched racer 5 slices past.
        if (me.blockWatch && me.watching >= 0 && static_cast<size_t>(me.watching) < cars.size()) {
            const AiCar& w = cars[size_t(me.watching)];
            const int d = sliceDiff(me.slice, w.slice) * w.way;
            if (d < -4 && d > -(n / 4)) {
                if (d > -20 && me.copState == 10)
                    radioCall(me, 0x40bd00, step);
                me.blockWatch = false;
                me.across = false;
                aiOrderPatrol(me);
            }
        }
        // A new order (cop_ai_order_is_new, cop_ai_order_start 0x403030).
        auto orderStart = [&]() {
            switch (me.orderKind) {
            case 0:
                if (inRange(me.slice))
                    me.copState = 3;
                else {
                    head();
                    me.copState = 1;
                }
                break;
            case 1:
                me.copState = 2;
                break;
            case 3:
                if (!inRange(me.slice)) {
                    head();
                    if (me.copState != 7) {
                        me.copState = 7;
                        me.siren = true;
                    }
                }
                break;
            case 4:
                if (!inRange(me.slice)) {
                    head();
                    if (me.copState != 8) {
                        me.siren = true;
                        me.copState = 8;
                    }
                } else {
                    me.siren = true;
                    me.copState = 10;
                }
                break;
            case 2:
                face(me.orderAgainst);
                me.copState = 1;
                break;
            default:
                break;
            }
        };
        if (me.orderKind >= 0 && !me.orderTaken) {
            me.orderTaken = true;
            orderStart();
        }
        CopCall call;
        // cop_ai_pick_target (0x402e10): a racer within 16 slices, free of
        // chases (+0x634), its +0x6F0 brought up to date (0x4014b0), that
        // has lost a chase (+0x14), is over the section's speed by more
        // than 6.71 m/s or has been over it by more than 2 (+0x6F0)
        // (0x403f80) scores max(cops / 3, 1) less the cops on it; the racer
        // it watches scores the cops. With a strip out or at a roadblock it
        // waits (960 steps at most) for the racer it watches, within 4
        // slices for 256 steps.
        auto pick = [&]() {
            int best = -1, bestScore = 0, bestBy = 0;
            const int cops = static_cast<int>(std::count_if(cars.begin(), cars.end(), [](const AiCar& c) { return c.cop; }));
            for (size_t i = 0; i < cars.size(); ++i) {
                const AiCar& o = cars[i];
                if (i == self || !o.racer || o.chaseFree >= step)
                    continue;
                const int by = sliceDiff(o.slice, me.slice);
                if (std::abs(by) >= 16)
                    continue;
                overspeed(cars[i]);
                if (static_cast<int>(i) != me.watching && o.chasesLost == 0 &&
                    o.speed <= trackSectionSpeed(track, o.slice) + 6.70545f && o.overspeed <= 2.0f)
                    continue;
                const int score = static_cast<int>(i) == me.watching ? cops : std::max(cops / 3, 1) - copsOn(i);
                if (score > bestScore) {
                    best = static_cast<int>(i);
                    bestScore = score;
                    bestBy = by;
                }
            }
            if (best < 0) {
                me.waitFrom = me.waitNear = 0;
                return;
            }
            if (me.stripOn || me.blockWatch) {
                if (me.waitFrom == 0)
                    me.waitFrom = step;
                if (step - me.waitFrom < 0x3c1) {
                    if (best != me.watching || std::abs(bestBy) > 4)
                        return;
                    if (me.waitNear == 0)
                        me.waitNear = step;
                    if (step - me.waitNear < 0x101)
                        return;
                }
            }
            me.waitFrom = me.waitNear = 0;
            setTarget(best);
        };
        // cop_ai_stop_check (0x402110), chasing: with the target pulled
        // over and stopped (its state 6), within 16 m, the stop; else, within
        // 20 m (40 with 3 cops stopping it), the target at 11.18 m/s or
        // less: held aiStopHold's steps (0x40237a: 160, 192 or 224 by its
        // race distance from the leading human's), then the record's +0x1C
        // = 2, +0x28 = step + 320.
        auto stopCheck = [&](AiCar& t) {
            const bool stopped = t.pursuit == 6;
            const float d = aiDistance(col, me, t);
            if (t.pulledOver && stopped && d < 16.0f)
                return true;
            const float near = (t.stopping < 3 ? 1.0f : 2.0f) * 20.0f;
            if (d >= near || t.speed > 11.18f) {
                me.holding = false;
                return t.pulledOver && stopped && t.recordStep < step;
            }
            if (me.holding) {
                if (me.holdUntil < step) {
                    t.pulledOver = true;
                    t.recordStep = step + 320;
                    return stopped;
                }
                return false;
            }
            me.holdUntil = step + aiStopHold(false, t.raceDistance);
            me.holding = true;
            return false;
        };
        // cop_call_backup (0x4043a0): with the target still a target for
        // more cops (0x403f80's score: max(cops / 3, 1) less those on it),
        // radio once for backup (0x40b720) 640 steps into a chase with one
        // cop on it; then send the nearest free cop on the target's other
        // side within twice t_ticket_times' slices, and any free cop with
        // +0x711 bit 1 ahead of it within 8 times them, each on a run with
        // the siren (kind 3, 0x404340) to halfway between it and the target.
        auto backup = [&](AiCar& t) {
            const size_t ti = size_t(&t - cars.data());
            const int cops = static_cast<int>(std::count_if(cars.begin(), cars.end(), [](const AiCar& c) { return c.cop; }));
            if (t.chasesLost == 0 && t.speed <= trackSectionSpeed(track, t.slice) + 6.70545f && t.overspeed <= 2.0f)
                return;
            if (std::max(cops / 3, 1) - copsOn(ti) <= 0)
                return;
            if ((t.calls & 7) == 0 && step - t.chaseFrom > 0x280 && copsOn(ti) == 1) {
                radioCall(me, 0x40b720, step);
                t.calls |= 1;
            }
            auto send = [&](AiCar& c) {
                const int half = sliceDiff(t.slice, c.slice) / 2;
                c.orderKind = 3;
                c.orderTaken = false;
                c.orderFrom = ((c.slice + half) % n + n) % n;
                c.orderLength = 8;
                c.orderAgainst = false;
                c.watching = static_cast<int>(ti);
            };
            const int mine = sliceDiff(me.slice, t.slice), give = ticketTimes(t.tickets).giveUp;
            AiCar* nearest = nullptr;
            int best = 200;
            for (size_t i = 0; i < cars.size(); ++i) {
                AiCar& c = cars[i];
                if (i == self || !c.cop || c.chasing >= 0 || c.stripWanted || c.blockWatch)
                    continue;
                const int d = sliceDiff(c.slice, t.slice);
                if (!c.copFast) {
                    if ((mine ^ d) < 0 && std::abs(d) < best && std::abs(d) <= 2 * give) {
                        nearest = &c;
                        best = std::abs(d);
                    }
                } else if (c.copState != 7 && d * t.way >= 0 && std::abs(d) <= 8 * give) {
                    send(c);
                }
            }
            if (nearest && nearest->copState != 7)
                send(*nearest);
        };
        // cop_ai_call_strip (0x4039c0): with no strip ordered for the
        // target (+0x58) and its heat, chases lost + cops on it + tickets +
        // (step - the first chase's) / 2048, over 6: the first time a radio
        // call (0x40b910, +0x54 bit 2, +0x5C this cop) and no more; after
        // that the free cop nearest ahead of it, 51 slices to 3/4 of the
        // track by slice number, is ordered to the roadside from its slice
        // - 6 to lay a strip (aiOrderStrip) and noted in +0x58.
        auto callStrip = [&](AiCar& t) {
            const size_t ti = size_t(&t - cars.data());
            if (t.stripCop >= 0 ||
                t.chasesLost + copsOn(ti) + t.tickets - 3 + ((step - t.chaseFrom) >> 11) <= 3)
                return false;
            if ((t.calls & 2) == 0) {
                radioCall(me, 0x40b910, step);
                t.calls |= 2;
                t.caller = static_cast<int>(self);
                return false;
            }
            int pickCop = -1, best = n * 3 / 4;
            for (size_t i = 0; i < cars.size(); ++i) {
                const AiCar& c = cars[i];
                if (!c.cop || c.chasing >= 0 || c.stripWanted)
                    continue;
                const int d = (c.slice - t.slice) * t.way; // g_backward: by its way
                if (d > 50 && d < best) {
                    pickCop = static_cast<int>(i);
                    best = d;
                }
            }
            if (pickCop < 0)
                return false;
            aiOrderStrip(cars[size_t(pickCop)], static_cast<int>(ti), n);
            t.stripCop = pickCop;
            return true;
        };
        // The calls for help (car_driver_cop 0x4032e5): every 320 steps of
        // the chase (+0x24, the first 640 after it began). The cop that
        // asked last only lets another ask; a cop asks for a strip, else a
        // roadblock (0x403b20: for a human target only, so never here),
        // else backup.
        auto callForHelp = [&](AiCar& t) {
            if (step - t.callAt < 0x141)
                return;
            t.callAt = step;
            if (t.caller == static_cast<int>(self))
                t.caller = -1;
            else if (callStrip(t))
                return;
            backup(t);
        };
        switch (me.copState) {
        default:
            me.copState = 1;
            [[fallthrough]];
        case 1:
        case 2:
            call.k = 1.19f;
            aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            pick();
            if (target())
                me.copState = 5;
            else if (me.copState == 1 && me.orderKind >= 0)
                orderStart();
            else if (me.copState == 2) {
                if (inRange(me.slice))
                    me.headed = false;
                else if (!me.headed) {
                    head();
                    me.headed = true;
                }
            }
            break;
        case 3:
            call.kind = CopCall::Roadside;
            aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            {
                const auto& p = col.road[size_t(((me.slice % n) + n) % n)];
                // car_off_lanes (0x402f70), then the strip (cop_ai_lay_spikes
                // 0x403d30): lane 7 for an even cop, 6 for an odd one.
                if (me.lane < 7 - p.leftLanes() || me.lane > p.rightLanes() + 6) {
                    me.copState = 4;
                    if (me.stripWanted && !me.stripOn) {
                        me.stripOn = true;
                        me.stripNear = false;
                        me.stripSlice = me.slice;
                        me.stripLane = (me.index & 1) ? 6 : 7;
                        radioCall(me, 0x40b9c0, step);
                    }
                }
            }
            if (target())
                me.copState = 5;
            break;
        case 4:
        case 11:
            // Stopped (car_stop_hard: speed halved a call), at the roadside or
            // across the road.
            me.wanted = 0;
            me.speed *= me.copState == 11 ? 0.125f : 0.5f;
            me.sideSpeed *= me.copState == 11 ? 0.125f : 0.5f;
            pick();
            if (target()) {
                me.copState = 5;
                me.across = false;
            }
            break;
        case 5: {
            AiCar* t = target();
            const int give = t ? (me.copFast ? ticketTimes(t->tickets).giveUpFast : ticketTimes(t->tickets).giveUp) : 0;
            if (!t) {
                me.copState = 1;
                break;
            }
            if (std::abs(sliceDiff(t->slice, me.slice) * me.way) > give && lose()) {
                me.copState = 1;
                break;
            }
            // 0x402910: turned to a target coming the other way when it
            // isn't 12 slices past it that way.
            if (t->speed >= 3.0f && t->way != me.way && sliceDiff(me.slice, t->slice) * t->way < 12) {
                me.way = t->way;
                me.speed = 0;
                me.sideSpeed = me.carSide = me.heading = me.yawRate = 0;
                    me.worldKept = false;
                me.reversing = false;
            }
            call.kind = CopCall::Chase;
            call.chase = true;
            aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            overspeed(*t);
            if (stopCheck(*t)) {
                // cop_stop_begin (0x401e40): the nearest cop on the record
                // does the ticket, the siren off; the first sets the
                // record's +0x28 to the ticket's wait.
                me.copState = 6;
                me.siren = false;
                me.reversing = false;
                if (++t->stopping == 1)
                    t->recordStep = step + ticketTimes(t->tickets).wait;
                break;
            }
            callForHelp(*t);
            break;
        }
        case 6: {
            me.wanted = 0;
            me.speed *= 0.5f;
            me.sideSpeed *= 0.5f;
            AiCar* t = target();
            if (t && step <= t->recordStep)
                break;
            if (t) {
                // cop_ticket (0x401ff0): the record cleared (its chases lost,
                // +0x6F0 too), a ticket more, free of chases for
                // t_ticket_times' +8; cop_after_ticket (0x4040e0) radios
                // (0x40b670), and every cop watching the racer (its strip
                // taken up) or on it lets it go, no chase lost, and is sent
                // on patrol.
                const int ti = static_cast<int>(t - cars.data());
                t->pulledOver = t->spiked = t->recordOpen = false;
                t->stopping = 0;
                t->chasesLost = 0;
                t->overspeed = 0;
                ++t->tickets;
                // At the race's tickets out (0x6fd4bc) the racer is out of
                // the race (+0x2C4 4, 0x4020a1): it coasts (aiFinishedCoast).
                if (t->ticketsOut > 0 && t->tickets >= t->ticketsOut)
                    t->raceState = 4;
                t->chaseFree = step + ticketTimes(t->tickets).free;
                radioCall(me, 0x40b670, step);
                for (size_t i = 0; i < cars.size(); ++i) {
                    AiCar& c = cars[i];
                    if (!c.cop || (c.watching != ti && c.chasing != ti))
                        continue;
                    if (c.watching == ti && c.stripWanted) {
                        c.stripWanted = c.stripOn = c.stripNear = false;
                        t->stripCop = -1;
                    }
                    c.blockWatch = false;
                    if (c.chasing == ti) {
                        c.chasing = -1;
                        c.holding = c.reversing = false;
                        if (i != self)
                            c.copState = 1;
                    }
                    c.siren = false;
                    aiOrderPatrol(c);
                }
            } else {
                lose();
                aiOrderPatrol(me);
            }
            me.stripWanted = me.stripOn = false;
            me.copState = 1;
            break;
        }
        case 7:
        case 8:
            call.k = 2.0f;
            aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            pick();
            if (target()) {
                me.copState = 5;
                break;
            }
            if (me.orderKind < 0)
                break; // the page's run with the siren, with no order
            if (!inRange(me.slice))
                head();
            else if (me.copState == 8)
                me.copState = 9;
            else {
                // The run's end: an order of kind 2, patrol, the siren off.
                me.orderKind = 2;
                me.copState = 1;
                me.siren = false;
            }
            break;
        case 9:
            call.k = 0;
            call.plus = 6.0f;
            aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            pick();
            if (target()) {
                me.copState = 5;
                break;
            }
            if (!inRange(me.slice))
                head();
            else if (std::fabs(me.speed) <= 6.5f) {
                me.copState = 10;
                radioCall(me, 0x40bac0, step);
            }
            break;
        case 10: {
            if (!inRange(me.slice)) {
                head();
                call.k = 0;
                call.plus = 6.0f;
                aiDrive(cars, self, col, {}, data, step, track, weather, &call);
            } else {
                // cop_ai_roadblock_turn (0x403180): with no racer within
                // 240 m, a quarter turn on its slice, speed 0: state 11.
                bool near = false;
                for (size_t i = 0; i < cars.size(); ++i)
                    near = near || (i != self && cars[i].racer && aiDistance(col, me, cars[i]) < 240.0f);
                if (!near) {
                    me.across = true;
                    me.wanted = 0;
                    me.speed *= 0.5f;
                    me.sideSpeed *= 0.5f;
                    me.copState = 11;
                } else {
                    call.kind = CopCall::LanesOnly;
                    aiDrive(cars, self, col, {}, data, step, track, weather, &call);
                }
            }
            pick();
            if (target()) {
                me.copState = 5;
                me.across = false;
            }
            break;
        }
        }
    }
    if (phase == AiPhase::Drive)
        return;
    aiMove(me, data, weather, step, col, track);
    if (me.putBackDue)
        aiPutBack(cars, self, col);
    aiAdvance(me, col); // AiPhase::Both: the place moved too (car_move)
}

std::vector<DumpedCar> parseCarDump(const std::vector<uint8_t>& bytes) {
    constexpr size_t kStride = 0x9ac;
    std::vector<DumpedCar> out;
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    auto f32 = [&](size_t at) {
        float v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    for (size_t base = 0; base + kStride <= bytes.size(); base += kStride) {
        DumpedCar c;
        c.inPlay = bytes[base + 0x89] != 0;
        const uint32_t flags = u32(base + 0x200);
        c.human = (flags & 4) != 0;
        c.cop = (flags & 0x20) != 0;
        c.hidden = (flags & 0x40) != 0;
        c.siren = c.cop && u32(base + 0x734) != 0;
        c.braking = bytes[base + 0x509] != 0;
        c.slice = static_cast<int>(u32(base + 0x1c));
        c.beams = static_cast<int>(u32(base + 0x660));
        c.beamScroll = static_cast<int>(u32(base + 0x944) & 0x7ff);
        c.speed = f32(base + 0x7c4);
        c.offset = f32(base + 0x7b8);
        for (size_t k = 0; k < 3; ++k)
            c.paint[k] = static_cast<int>(u32(base + 0x8a8 + 4 * k));
        const uint32_t setup = u32(base + 0x220);
        if (setup >= 0x6fd52c && (setup - 0x6fd52c) % 0x6c == 0 && (setup - 0x6fd52c) / 0x6c < 16)
            c.setup = static_cast<int>((setup - 0x6fd52c) / 0x6c);
        for (int k = 0; k < 3; ++k) {
            c.at[size_t(k)] = f32(base + 0x98 + 4 * size_t(k));
            c.right[size_t(k)] = f32(base + 0xc0 + 4 * size_t(k));
            c.up[size_t(k)] = f32(base + 0xcc + 4 * size_t(k));
            c.forward[size_t(k)] = f32(base + 0xd8 + 4 * size_t(k));
            c.officerRight[size_t(k)] = f32(base + 0x8ec + 4 * size_t(k));
            c.officerUp[size_t(k)] = f32(base + 0x8f8 + 4 * size_t(k));
            c.officerForward[size_t(k)] = f32(base + 0x904 + 4 * size_t(k));
            c.officerAt[size_t(k)] = f32(base + 0x910 + 4 * size_t(k));
        }
        c.officer = u32(base + 0x8e4) != 0 && u32(base + 0x8e8) != 0;
        for (size_t k = 0; k < 3; ++k)
            c.velocity[k] = f32(base + 0xa4 + 4 * k);
        c.frontSkid = f32(base + 0x5c0);
        c.rearSkid = f32(base + 0x5c4);
        c.upright = f32(base + 0x10c);
        c.airborne = (u32(base + 0x164) & 0xffff) != 0;
        c.weather = static_cast<int>(u32(base + 0x160));
        c.surface = static_cast<int>(u32(base + 0x18c) & 0x3f);
        for (size_t w = 0; w < 4; ++w)
            for (size_t k = 0; k < 3; ++k)
                c.wheelAt[w][k] = static_cast<float>(static_cast<int32_t>(u32(base + 0x304 + 0x78 * w + 4 * k))) / 65536.0f;
        out.push_back(c);
    }
    return out;
}

std::vector<Particle> parseParticleDump(const std::vector<uint8_t>& bytes) {
    std::vector<Particle> out;
    auto i32 = [&](size_t at) {
        int32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    auto f32 = [&](size_t at) {
        float v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    for (size_t base = 0; base + 128 <= bytes.size() && out.size() < size_t(ParticleSystem::kSlots); base += 128) {
        Particle p;
        p.life = i32(base);
        if (p.life == 0)
            continue;
        p.kind = i32(base + 4);
        p.lifeMax = i32(base + 8);
        for (size_t k = 0; k < 3; ++k) {
            p.position[k] = f32(base + 0xc + 4 * k);
            p.velocity[k] = f32(base + 0x18 + 4 * k);
        }
        p.size = f32(base + 0x24);
        p.baseSize = f32(base + 0x28);
        p.growth = f32(base + 0x2c);
        p.angle = f32(base + 0x30);
        p.spin = f32(base + 0x34);
        p.wobbleAngle = f32(base + 0x40);
        p.wobbleTurn = f32(base + 0x44);
        p.wobble = f32(base + 0x48);
        p.wobbleGrowth = f32(base + 0x4c);
        p.colour = static_cast<uint32_t>(i32(base + 0x54));
        p.halved = bytes[base + 0x7d] != 0;
        p.children = bytes[base + 0x7e];
        out.push_back(p);
    }
    return out;
}

} // namespace reluva::nfs
