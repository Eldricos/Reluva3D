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

// The side offset's spring (1/s/s), damping (1/s) and greatest
// acceleration (m/s/s, x (1 + 0.0015 x speed) as 0x4076b0 has its 38),
// fitted to car 1 +0x7B8 against its +0x7C8 over 376 steps of a
// Hometown race (t6-watch: 1.1 m rms, as good as any from 0.6 to 1 of
// critical damping; the rest is the road and the heading, not modelled).
constexpr float kAiSideSpring = 100.0f, kAiSideDamping = 14.0f, kAiSideAccel = 70.0f;

// The aim and the speed (0x408870, 0x4066c0 with 0x4064f0 and 0x4063e0),
// for one step of 64 a second. The computer's model (car_ai_physics
// 0x4088f0) is a 32 Hz task: on the even steps only, its time step
// 0x56ec58 1/32 s, so the aim moves 0.2 every other step (3.2 in the
// half second, as t6-log's dumps have it). The side offset as driven is
// the car's own motion, every step.
void aiMove(AiCar& me, const AiCarData& data, bool weather, int step, float chord, float cut) {
    // The side offset as driven (+0x7B8): the side controller of 0x4076b0
    // pulls the car towards the aim, the heading term damping it; here a
    // spring and damper fitted to nfs-host's car 1 on Hometown (frd.md).
    // What it steers by is the offset as the game measures it (0x4a2bc0:
    // the car's place less its slice's road point, along the road's right
    // there), which moves by the road's bend across the slice (`chord`)
    // while the car's own place across the road (`lateral`) doesn't.
    if (!me.sideKept) {
        me.side = me.offset;
        me.lateral = me.side - me.along * chord;
        me.sideSpeed = 0;
        me.sideKept = true;
    } else {
        me.side = me.lateral + me.along * chord;
        const float limit = kAiSideAccel * (1 + 0.0015f * me.speed);
        const float a = std::clamp(kAiSideSpring * (me.offset + cut - me.side) - kAiSideDamping * me.sideSpeed, -limit, limit);
        me.sideSpeed += a / 64.0f;
        me.lateral += me.sideSpeed / 64.0f;
        me.side = me.lateral + me.along * chord;
    }
    if ((step & 1) != 0)
        return;
    // ai_lane_ramp (0x408870): the aim 0.2 a call towards it; then
    // ai_reverse_end: reversing, the aim is the road's point.
    if (me.offset < me.target)
        me.offset = std::min(me.offset + 0.2f, me.target);
    else
        me.offset = std::max(me.offset - 0.2f, me.target);
    if (me.reversing)
        me.offset = 0;
    // The speed (0x4066c0): towards the speed wanted by the acceleration
    // (0x4064f0) or the brakes (0x4063e0), not past it, over 1/32 s.
    const float error = me.wanted - me.speed;
    if (error > 0) {
        me.braking = false;
        int at = static_cast<int>(std::nearbyint(me.speed - 0.5f));
        at = std::clamp(at, 0, 111);
        float a = data.accel[size_t(at)];
        if (me.racer && (me.speed > data.topSpeedCap * me.scale || me.speed > data.topSpeedCap))
            a = 0;
        a *= 0.9f; // ai Normal past race distance 100
        if (weather && at < 45)
            a *= 0.9f;
        me.speed = std::min(me.speed + a / 32.0f, me.wanted);
    } else {
        float b = data.maxBrake;
        if (me.cop)
            b *= 2;
        else
            b *= 1.25f; // an AI racer (+0x200 bit 8)
        me.braking = error <= -5.0f;
        me.speed = std::max(me.speed - b / 32.0f, me.wanted);
    }
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

float aiBendCut(const TrackCollision& col, const AiCar& me) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || me.reversing)
        return 0;
    auto at = [&](int s) -> const TrackCollision::RoadPoint& { return col.road[size_t(((s % n) + n) % n)]; };
    const auto& here = at(me.slice);
    // 0x406980: 4 slices on, then to the look-ahead slice (0x40e1b0: speed
    // x 1.7 / 6, 4 to 20), stopping where the road has turned 45 degrees
    // (0x40ef70: the forwards' x-z dot 0.7 or less).
    const int far = std::clamp(static_cast<int>(std::fabs(me.speed) * 1.7f / 6.0f), 4, 20);
    int k = 4;
    for (; k < far; ++k) {
        const auto& f = at(me.slice + me.way * (k + 1)).forward;
        if (f[0] * here.forward[0] + f[2] * here.forward[2] <= 0.7f)
            break;
    }
    const auto& ahead = at(me.slice + me.way * k);
    float dx = 0, dz = 0;
    for (size_t i = 0; i < 3; ++i) {
        const float d = ahead.position[i] - here.position[i];
        dx += d * here.right[i];
        dz += d * here.forward[i];
    }
    if (std::fabs(dz) < 1)
        return 0;
    // A car chasing a point L on along a steady bend of curvature k sits
    // k L^2 / 2 inside it, which is the point's offset across the car's
    // slice (dx): pure pursuit's own geometry, not fitted (frd.md, The
    // computer cars' driving: 6 m at most).
    return std::clamp(dx, -6.0f, 6.0f);
}

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
    auto ahead = [&](int k) { return wrap(me.slice + me.way * k); }; // slice_ahead (0x40e160)
    auto sliceDiff = [&](int a, int b) { // 0x40e120
        int d = a - b;
        if (d > n / 2)
            d -= n;
        else if (d < -(n / 2))
            d += n;
        return d;
    };
    const TrackCollision::RoadPoint& here = point(me.slice);
    const int leftLanes = here.leftLanes(), rightLanes = here.rightLanes();
    int votes[14][3] = {}; // 0x56ee90: left, same, right of the car's lane, by rule
    auto vote = [&](int row, int column, int amount) { votes[row][column] += amount; };

    // lane_votes_start (0x40dd60): the car's lanes (0x40e350).
    if (!me.sideKept)
        me.side = me.lateral = me.offset;
    else
        me.side = me.lateral + me.along * aiChord(col, me.slice);
    me.lane = aiLaneAt(here, me.side);
    me.laneLeft = aiLaneAt(here, me.side - me.halfWidth + 0.5f);
    me.laneRight = aiLaneAt(here, me.side + me.halfWidth - 0.5f);
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
    const int reach = std::max(static_cast<int>(std::fabs(me.speed) * 2.0f * 0.1667f), 10);
    std::array<const AiCar*, 3> carAhead{}, carBehind{};
    std::array<int, 3> aheadBy{reach, reach, reach}, behindBy{-reach, -reach, -reach};
    std::array<bool, 3> aheadCentre{}, behindCentre{};
    for (size_t i = 0; i < cars.size(); ++i) {
        if (i == self)
            continue;
        const AiCar& o = cars[i];
        const int d = sliceDiff(o.slice, me.slice) * me.way;
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
    const int toTarget = target ? sliceDiff(target->slice, me.slice) * me.way : 0;
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
                    me.sideSpeed = 0;
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
        // pair, by the scale, under the top speed cap by the track's factor.
        me.wanted = std::min(aiSlicesSpeed(speeds, me.slice) * std::min(me.scale, 1.0f),
                             data.topSpeedCap * aiTrackFactor(track));
    } else if (kind != CopCall::Chase || !target || chaseSection) {
        // ai_speed_wanted_section (0x401430): the section's speed x k + plus,
        // at most the top speed.
        const float k = chaseSection ? 1.19f : cop->k, plus = chaseSection ? 0.0f : cop->plus;
        me.wanted = std::min(trackSectionSpeed(track, me.slice) * k + plus, top);
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
    if (!cop && carAhead[1] && aheadBy[1] < 2 && me.speed + 8.9406f < me.wanted && !me.flashing) {
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
    for (int c = 0; c < 3 && !cop; ++c) {
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
    if (!cop && me.way > 0) {
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
    } else if (track == 6 && me.way > 0 && !cop && me.slice >= 0x127 && me.slice <= 0x168) {
        vote(2, 0, -40);
    } else if ((lane < 7 - there.leftLanes() || lane > there.rightLanes() + 6) && 7 - leftLanes <= lane &&
               lane <= rightLanes + 6) {
        vote(2, lane < 7 ? 2 : 0, 40);
    }
    // 0x40d1f0: no road in a lane here (-1000, and a nudge away), or
    // ahead (-65).
    int nudge = 0;
    if (!roadside) {
        std::vector<int> slices{me.slice}; // up to the slice reached, not it
        bool reached = false;
        if (far != me.slice)
            for (int i = 1; i < 20 && !reached; ++i) {
                reached = ahead(i) == far;
                if (!reached)
                    slices.push_back(ahead(i));
            }
        if (far != me.slice)
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
        const int d = sliceDiff(c.stripSlice, me.slice) * me.way;
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
            const AiCarData& data, int step, int track, bool weather) {
    if (self >= cars.size())
        return;
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
            me.speed *= 0.5f; // car_stop_hard
            if (!me.pulledOver)
                me.pursuit = 1;
            if (copsOn() == 0) {
                me.pursuit = 1;
                me.pulledOver = false;
            }
        } else
            aiDrive(cars, self, col, speeds, data, step, track, weather, nullptr);
    }
    aiMove(me, data, weather, step, aiChord(col, me.slice), aiBendCut(col, me));
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

void aiCopStep(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const AiCarData& data, int step,
               int track, bool weather) {
    const int n = static_cast<int>(col.road.size());
    if (n < 2 || self >= cars.size())
        return;
    AiCar& me = cars[self];
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
            me.sideSpeed = 0;
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
                me.sideSpeed = 0;
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
    aiMove(me, data, weather, step, aiChord(col, me.slice), aiBendCut(col, me));
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
        }
        out.push_back(c);
    }
    return out;
}

} // namespace reluva::nfs
