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

} // namespace reluva::nfs
