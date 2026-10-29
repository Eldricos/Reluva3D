// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/fin.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string_view>

namespace reluva::netimmerse {

namespace {

constexpr std::string_view kHeader = "Dweezil 23\n";
constexpr std::string_view kTopLevel = "Top Level Object";
constexpr std::string_view kEndOfFile = "End Of File";

// Every class seen in the retail models. Objects are found by these names,
// so a new one has to be added here to be seen at all.
const std::set<std::string_view, std::less<>> kClasses = {
    "NiNode", "NiBillboardNode", "NiLODNode", "Ni3dsAnimationNode", "Ni3dsBone",
    "NiTriShape", "NiEnvMappedTriShape", "Ni3dsMorphShape", "Ni3dsSkin", "NiLight",
    "NiMaterialProperty", "NiAlphaProperty", "NiTextureProperty", "NiMultiTextureProperty",
    "NiTextureModeProperty", "NiVertexColorProperty", "NiZBufferProperty", "NiSpecularProperty",
    "NiShadeProperty", "NiImage", "NiFlipTextures", "Ni3dsColorAnimator", "Ni3dsAlphaAnimator",
    "DDUnit", "DDEnv", "DDCorona", "DDActorSharedData",
};

// What each node class adds after NiNode's fields (bytes), as far as it's
// known: a billboard's mode, a DDUnit's or DDEnv's link to its shared data,
// a 3ds node's animation settings.
const std::map<std::string_view, uint32_t, std::less<>> kNodeExtra = {
    {"NiNode", 0}, {"NiBillboardNode", 4}, {"DDUnit", 4}, {"DDEnv", 4},
    {"Ni3dsAnimationNode", 48}, {"Ni3dsBone", 48},
};

// Reads little-endian fields within an object, failing (and staying
// failed) on reading past its end.
struct Reader {
    const uint8_t* p;
    size_t size, pos = 0;
    bool ok = true;

    bool need(size_t n) {
        if (ok && n <= size - pos)
            return true;
        ok = false;
        return false;
    }
    void skip(size_t n) {
        if (need(n))
            pos += n;
    }
    uint8_t u8() { return need(1) ? p[pos++] : 0; }
    uint16_t u16() {
        if (!need(2))
            return 0;
        const uint16_t v = uint16_t(p[pos] | p[pos + 1] << 8);
        pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t v;
        std::memcpy(&v, p + pos, 4); // little-endian hosts only, as elsewhere in Reluva
        pos += 4;
        return v;
    }
    float f32() {
        const uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    void floats(float* out, size_t n) {
        for (size_t i = 0; i < n; ++i)
            out[i] = f32();
    }
    std::vector<float> floats(size_t n) {
        std::vector<float> v;
        if (need(n * 4)) {
            v.resize(n);
            floats(v.data(), n);
        }
        return v;
    }
    std::string string() {
        const uint32_t n = u32();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char*>(p + pos), n);
        pos += n;
        return s;
    }
    std::vector<uint32_t> links() {
        const uint32_t n = u32();
        std::vector<uint32_t> v;
        if (!need(size_t(n) * 4))
            return v;
        for (uint32_t i = 0; i < n; ++i)
            v.push_back(u32());
        return v;
    }
};

// NiObjectNET: the name, extra data written inline (each its class name
// and fields), then a byte of flags. Bit 0 hides an object (and what's
// under it): it's set only on effects shown on events.
std::string readObjectNet(Reader& r, uint8_t* flags = nullptr) {
    std::string name = r.string();
    const uint32_t extras = r.u32();
    for (uint32_t i = 0; i < extras && r.ok; ++i) {
        const std::string type = r.string();
        if (type == "Ni3dsPropAnimExtraData")
            r.skip(8); // a number and a link to its animator
        else if (type == "TexturePropExtraData")
            r.skip(12); // a number, a link to its NiFlipTextures and a number
        else
            r.ok = false;
    }
    const uint8_t f = r.u8();
    if (flags)
        *flags = f;
    return name;
}

// A bounding volume: its type, then its fields. Sphere, box and capsule
// end with a byte; a half-space doesn't; types 4 and 6 are lists of others.
void skipBoundingVolume(Reader& r, int depth = 0) {
    const uint32_t type = r.u32();
    switch (type) {
    case 0: r.skip(16 + 1); break; // sphere: centre, radius
    case 1: r.skip(60 + 1); break; // box: centre, three axes, extents
    case 2: r.skip(28 + 1); break; // capsule
    case 5: r.skip(16); break;     // half-space: a plane
    case 4:
    case 6: {
        const uint32_t n = r.u32();
        if (n > 1024 || depth > 8) {
            r.ok = false;
            break;
        }
        for (uint32_t i = 0; i < n && r.ok; ++i)
            skipBoundingVolume(r, depth + 1);
        break;
    }
    default: r.ok = false; break;
    }
}

// NiAVObject's fields after NiObjectNET's.
void readAvObject(Reader& r, FinAvObject& o) {
    uint8_t flags = 0;
    o.name = readObjectNet(r, &flags);
    o.hidden = (flags & 1) != 0;
    r.floats(o.translation.data(), 3);
    r.floats(o.rotation.data(), 9);
    o.scale = r.f32();
    r.skip(12); // velocity
    o.properties = r.links();
    r.u32(); // 1 to 5, meaning unknown
    if (r.u32() != 0) // the collision volume, if it has one
        skipBoundingVolume(r);
}

void readNode(Reader& r, FinNode& n, uint32_t extra) {
    readAvObject(r, n);
    r.skip(9); // two numbers and a byte, meaning unknown
    n.children = r.links();
    r.links(); // effects (lights)
    r.skip(extra);
}

// NiLODNode: a node, five bytes, the number of levels (one per child) and
// 20 bytes for each (its distances), then a byte.
void readLodNode(Reader& r, FinNode& n) {
    readNode(r, n, 5);
    const uint32_t levels = r.u32();
    if (levels != n.children.size())
        r.ok = false;
    r.skip(size_t(levels) * 20 + 1);
    n.levelsOfDetail = true;
}

// A shape's fields; a DDCorona (a glow) stops after the colours, with a
// link and nine numbers of its own instead of planes and triangles.
void readShape(Reader& r, FinShape& s, bool corona = false) {
    readAvObject(r, s);
    // Each array follows a non-zero word (its address when saved) and is
    // left out after a zero one.
    const uint32_t nv = s.vertexCount = r.u16();
    if (r.u32())
        s.vertices = r.floats(size_t(nv) * 3);
    if (r.u32())
        s.normals = r.floats(size_t(nv) * 3);
    r.floats(s.bound.data(), 4);
    const uint16_t nt = r.u16();
    s.uvSets = r.u16();
    if (r.u32())
        s.uvw = r.floats(size_t(s.uvSets) * nv * 3);
    if (r.u32())
        s.colors = r.floats(size_t(nv) * 4);
    if (corona) {
        r.skip(4 + 36);
        return;
    }
    if (r.u32())
        r.skip(size_t(nt) * 16); // a plane per triangle
    if (r.need(size_t(nt) * 6)) {
        s.triangles.resize(size_t(nt) * 3);
        for (uint16_t& i : s.triangles)
            i = r.u16();
    }
    if (std::any_of(s.triangles.begin(), s.triangles.end(), [&](uint16_t i) { return i >= nv; }))
        r.ok = false;
}

// Ni3dsSkin: a shape, two bytes (1 and 1), then for each vertex the number
// of its influences (16 bits) and each: weight, position in the bone's
// space (3 floats), the bone's id.
void readSkin(Reader& r, FinSkin& s) {
    readShape(r, s);
    r.skip(2);
    s.firstInfluence.reserve(size_t(s.vertexCount) + 1);
    for (uint16_t v = 0; v < s.vertexCount && r.ok; ++v) {
        s.firstInfluence.push_back(uint32_t(s.influences.size()));
        const uint16_t n = r.u16();
        if (n > 16) {
            r.ok = false;
            break;
        }
        for (uint16_t i = 0; i < n; ++i) {
            FinSkin::Influence in;
            in.weight = r.f32();
            r.floats(in.offset.data(), 3);
            in.bone = r.u32();
            s.influences.push_back(in);
        }
    }
    s.firstInfluence.push_back(uint32_t(s.influences.size()));
}

// The settings a time controller starts with, after NiObjectNET's fields
// and seven bytes (0, 0, 0, 1, 1, 0, 1 in every file): the cycle, a number
// (0), frequency, phase, start, stop and the target.
void readControllerTail(Reader& r, FinController& c) {
    c.cycle = r.u32();
    r.u32();
    c.frequency = r.f32();
    c.phase = r.f32();
    c.start = r.f32();
    c.stop = r.f32();
    c.target = r.u32();
}

// NiFlipTextures: NiObjectNET's fields (its byte the cycle), three bytes,
// frequency, phase, the cycle's length and the target.
void readFlip(Reader& r, FinFlip& f) {
    uint8_t cycle = 0;
    readObjectNet(r, &cycle);
    f.cycle = cycle;
    r.skip(3);
    f.frequency = r.f32();
    f.phase = r.f32();
    f.stop = r.f32();
    f.target = r.u32();
}

// Ni3dsColorAnimator (with the colour's number first) and
// Ni3dsAlphaAnimator: a controller, then the keys: their number and type,
// each its time and value; type 2 adds tangents (and, for a colour, 24
// bytes more).
void readMaterialAnimator(Reader& r, FinMaterialAnimator& a, bool alpha) {
    readObjectNet(r);
    r.skip(7);
    readControllerTail(r, a);
    a.alpha = alpha;
    if (!alpha)
        a.channel = r.u32();
    const uint32_t n = r.u32();
    const uint32_t type = n ? r.u32() : 0;
    const size_t values = alpha ? 1 : 3;
    size_t extra = 0;
    if (type == 2)
        extra = alpha ? 8 : 48;
    else if (type != 1 && n)
        r.ok = false;
    for (uint32_t i = 0; i < n && r.ok; ++i) {
        FinMaterialAnimator::Key k;
        k.time = r.f32();
        r.floats(k.value.data(), values);
        r.skip(extra);
        a.keys.push_back(k);
    }
}

// Ni3dsMorphShape: a shape, then a controller (two numbers first), two
// bytes, the numbers of targets and keys, five numbers, the keys (each its
// time, six numbers, the number of targets again, then a weight, an
// incoming and an outgoing tangent for each target, all the weights
// first), and each target's vertices.
void readMorph(Reader& r, FinMorph& m) {
    readShape(r, m);
    FinController c;
    r.skip(8);
    c.cycle = r.u32();
    r.u32();
    c.frequency = r.f32();
    c.phase = r.f32();
    c.start = r.f32();
    c.stop = r.f32();
    r.skip(2);
    const uint32_t targets = r.u32();
    const uint32_t keys = r.u32();
    r.skip(16 + 4); // four numbers, and one (5 in every file)
    if (targets > 64 || keys > 4096) {
        r.ok = false;
        return;
    }
    for (uint32_t i = 0; i < keys && r.ok; ++i) {
        FinMorph::Key k;
        k.time = r.f32();
        r.skip(24);
        if (r.u32() != targets) {
            r.ok = false;
            return;
        }
        k.weights = r.floats(targets);
        r.skip(size_t(targets) * 8);
        m.keys.push_back(std::move(k));
    }
    for (uint32_t t = 0; t < targets && r.ok; ++t)
        m.targets.push_back(r.floats(size_t(m.vertexCount) * 3));
}

// NiLight: NiAVObject's fields, 12 bytes (0), a direction, a byte, a
// spot's angle, a number, the multiplier, ambient, diffuse and specular
// colours, the range and whether it's used, a byte, the kind and seven
// bytes.
void readLight(Reader& r, FinLight& l) {
    readAvObject(r, l);
    r.skip(12);
    r.floats(l.direction.data(), 3);
    r.u8();
    l.spotAngle = r.f32();
    r.skip(4);
    l.multiplier = r.f32();
    r.floats(l.ambient.data(), 3);
    r.floats(l.diffuse.data(), 3);
    r.floats(l.specular.data(), 3);
    l.range = r.f32();
    l.attenuated = r.f32() == 1;
    r.u8();
    l.kind = r.u8();
    r.skip(7);
}

// DDActorSharedData. See docs/formats/netimmerse/fin.md for the layout.
void readActor(Reader& r, FinActor& a) {
    a.id = r.string();
    r.string();
    a.name = r.string();
    a.textureSet = r.string();
    r.skip(2 + 8); // a number, then two whose meaning is unknown
    const uint32_t clips = r.u32();
    for (uint32_t i = 0; i < clips && r.ok && i < 1024; ++i) {
        FinActor::Clip c;
        c.name = r.string();
        c.start = r.f32();
        c.end = r.f32();
        if (r.u8()) {
            // Its settings: 4 bytes (0), three flags (the first set on
            // looping clips), 4 bytes (0), then volume, falloff and the
            // distances out to which and within which it's heard.
            c.sound = r.string();
            r.skip(4);
            c.loop = r.u8() != 0;
            r.skip(2 + 4);
            c.volume = r.f32();
            c.falloff = r.f32();
            c.maxDistance = r.f32();
            c.minDistance = r.f32();
        }
        for (int list = 0; list < 3 && r.ok; ++list) // three lists of numbers, meaning unknown
            r.skip(size_t(r.u32()) * 2);
        a.clips.push_back(std::move(c));
    }
    const uint32_t tracks = r.u32();
    if (tracks > 4096)
        r.ok = false;
    // Each key starts with its time and value; the types add more after.
    auto keySize = [](char kind, uint32_t type) -> size_t {
        switch (kind) {
        case 'r': return type == 1 ? 44 : type == 2 ? 64 : type == 3 ? 96 : 0;
        case 'p': return type == 1 ? 16 : type == 2 ? 64 : type == 3 ? 76 : 0;
        case 's': return type == 1 ? 8 : type == 2 ? 16 : 0;
        case 'f': return type == 1 ? 8 : type == 2 ? 16 : type == 3 ? 20 : 0;
        default: return 0;
        }
    };
    // A track: the number of keys, then (if any) their type and the keys.
    auto track = [&](char kind, auto&& read) {
        const uint32_t n = r.u32();
        if (!n)
            return;
        const uint32_t type = r.u32();
        const size_t size = keySize(kind, type);
        if (!size || n > 100000 || !r.need(size * n)) {
            r.ok = false;
            return;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const size_t at = r.pos;
            read(type, at);
            r.pos = at + size;
        }
    };
    // A number key: its time and value; type 2 adds the tangents in and out.
    auto floatKey = [&](uint32_t type, size_t) {
        FinActor::FloatKey k;
        k.time = r.f32();
        k.value = r.f32();
        if (type == 2) {
            k.in = r.f32();
            k.out = r.f32();
            k.curve = true;
        }
        return k;
    };
    for (uint32_t i = 0; i < tracks && r.ok; ++i) {
        FinActor::Track t;
        // Rotations: type 4 is a single key, 46 bytes, then a float track
        // for each axis.
        const size_t at = r.pos;
        if (r.u32() == 1 && r.u32() == 4) {
            r.skip(46);
            for (auto& axis : t.euler)
                track('f', [&](uint32_t type, size_t key) { axis.push_back(floatKey(type, key)); });
        } else {
            r.pos = at;
            // The time, angle and axis, the quaternion and two numbers; type
            // 2 adds a control quaternion (and its angle); type 3 tension,
            // continuity and bias, then controls in and out (and angles).
            track('r', [&](uint32_t type, size_t key) {
                FinActor::RotationKey k;
                k.time = r.f32();
                r.skip(16);
                r.floats(k.value.data(), 4);
                if (type == 2) {
                    r.pos = key + 44;
                    r.floats(k.in.data(), 4);
                    k.out = k.in;
                    k.curve = true;
                } else if (type == 3) {
                    r.pos = key + 56;
                    r.floats(k.in.data(), 4);
                    r.floats(k.out.data(), 4);
                    k.curve = true;
                }
                t.rotations.push_back(k);
            });
        }
        // The time and position; type 2 adds the tangents in and out, then
        // the curve's a and b; type 3 tension, continuity and bias, the
        // tangents, a and b.
        track('p', [&](uint32_t type, size_t key) {
            FinActor::PositionKey k;
            k.time = r.f32();
            r.floats(k.value.data(), 3);
            if (type == 2 || type == 3) {
                r.pos = key + (type == 2 ? 28 : 40);
                r.floats(k.out.data(), 3);
                r.floats(k.a.data(), 3);
                r.floats(k.b.data(), 3);
                k.curve = true;
            }
            t.positions.push_back(k);
        });
        track('s', [&](uint32_t type, size_t key) { t.scales.push_back(floatKey(type, key)); });
        const uint32_t shown = r.u32(); // no type: each key a time and a byte
        for (uint32_t k = 0; k < shown && r.ok && k < 100000; ++k)
            t.visible.push_back({r.f32(), r.u8() != 0});
        a.tracks.push_back(std::move(t));
    }
    const uint32_t corners = r.u32();
    for (uint32_t i = 0; i < corners && r.ok && i < 1024; ++i) {
        std::array<float, 3> c{};
        r.floats(c.data(), 3);
        a.footprint.push_back(c);
    }
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// The length-prefixed string at `at` (1 to maxLen characters), or an empty
// view if there's none.
std::string_view stringAt(const std::vector<uint8_t>& d, size_t at, size_t maxLen) {
    if (at + 4 > d.size())
        return {};
    const uint32_t n = le32(&d[at]);
    if (n == 0 || n > maxLen || at + 4 + n > d.size())
        return {};
    return std::string_view(reinterpret_cast<const char*>(&d[at + 4]), n);
}

} // namespace

std::vector<uint32_t> FinFile::animatedNodes() const {
    std::vector<uint32_t> out;
    std::vector<uint32_t> stack(roots_.rbegin(), roots_.rend());
    std::set<uint32_t> seen;
    while (!stack.empty()) {
        const uint32_t id = stack.back();
        stack.pop_back();
        auto node = nodes.find(id);
        if (node == nodes.end() || !seen.insert(id).second)
            continue;
        const std::string type = typeOf(id);
        if (type == "Ni3dsAnimationNode" || type == "Ni3dsBone")
            out.push_back(id);
        stack.insert(stack.end(), node->second.children.rbegin(), node->second.children.rend());
    }
    return out;
}

std::string FinFile::typeOf(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? std::string() : objects_[it->second].type;
}

Error FinFile::parse(const std::vector<uint8_t>& data) {
    *this = FinFile();
    if (data.size() < kHeader.size() || std::memcmp(data.data(), kHeader.data(), kHeader.size()) != 0)
        return Error::fail("not an Alpha Team model (no 'Dweezil 23' header)");

    // Find every object, Top Level Object marker and the end. An object's
    // header is its class name (one of kClasses, length-prefixed), then
    // its id.
    struct Mark {
        size_t at;      // the length word
        size_t fields;  // after the id (objects only)
        std::string_view type; // empty for markers
        uint32_t id;
        bool topLevel;
    };
    std::vector<Mark> marks;
    size_t end = data.size();
    for (size_t i = kHeader.size(); i + 8 <= data.size(); ++i) {
        // A length word is small: its upper three bytes are zero.
        if (data[i] == 0 || data[i + 1] || data[i + 2] || data[i + 3])
            continue;
        const std::string_view s = stringAt(data, i, 32);
        if (s.empty())
            continue;
        if (s == kTopLevel) {
            marks.push_back({i, 0, {}, 0, true});
        } else if (s == kEndOfFile && i + 4 + s.size() == data.size()) {
            end = i;
            break;
        } else if (kClasses.count(s) && i + 4 + s.size() + 4 <= data.size()) {
            const size_t idAt = i + 4 + s.size();
            marks.push_back({i, idAt + 4, s, le32(&data[idAt]), false});
        }
    }
    if (end == data.size())
        return Error::fail("no 'End Of File' at the end");

    bool nextIsRoot = false;
    for (size_t m = 0; m < marks.size(); ++m) {
        if (marks[m].topLevel) {
            nextIsRoot = true;
            continue;
        }
        const size_t objectEnd = m + 1 < marks.size() ? marks[m + 1].at : end;
        FinObjectInfo info;
        info.type = std::string(marks[m].type);
        info.id = marks[m].id;
        info.offset = uint32_t(marks[m].fields);
        info.size = uint32_t(objectEnd - marks[m].fields);
        if (info.id == 0 || byId_.count(info.id))
            return Error::fail("object " + std::to_string(objects_.size()) + " (" + info.type +
                               ") has an id of 0 or one already used");
        if (nextIsRoot)
            roots_.push_back(info.id);
        nextIsRoot = false;

        Reader r{data.data() + info.offset, info.size};
        const std::string_view type = marks[m].type;
        info.read = true;
        if (type == "NiLODNode") {
            readLodNode(r, nodes[info.id]);
        } else if (auto extra = kNodeExtra.find(type); extra != kNodeExtra.end()) {
            readNode(r, nodes[info.id], extra->second);
        } else if (type == "NiTriShape" || type == "NiEnvMappedTriShape") {
            readShape(r, shapes[info.id]);
        } else if (type == "Ni3dsSkin") {
            readSkin(r, skins[info.id]);
        } else if (type == "Ni3dsMorphShape") {
            readMorph(r, morphs[info.id]);
        } else if (type == "NiLight") {
            readLight(r, lights[info.id]);
        } else if (type == "NiFlipTextures") {
            readFlip(r, flips[info.id]);
        } else if (type == "Ni3dsColorAnimator" || type == "Ni3dsAlphaAnimator") {
            readMaterialAnimator(r, materialAnimators[info.id], type == "Ni3dsAlphaAnimator");
        } else if (type == "DDActorSharedData") {
            readActor(r, actors[info.id]);
        } else if (type == "DDCorona") {
            readShape(r, coronas[info.id], true);
        } else if (type == "NiMaterialProperty") {
            FinMaterial& mat = materials[info.id];
            readObjectNet(r);
            r.floats(mat.ambient.data(), 3);
            r.floats(mat.diffuse.data(), 3);
            r.floats(mat.specular.data(), 3);
            r.floats(mat.emissive.data(), 3);
            mat.glossiness = r.f32();
            mat.alpha = r.f32();
        } else if (type == "NiAlphaProperty") {
            FinAlpha& a = alphas[info.id];
            readObjectNet(r);
            a.blend = r.u8() != 0;
            a.source = r.u32();
            a.destination = r.u32();
        } else if (type == "NiTextureProperty") {
            readObjectNet(r);
            r.u32(); // 0 in every file
            textures[info.id] = r.links();
        } else if (type == "NiMultiTextureProperty") {
            readObjectNet(r);
            const uint32_t count = r.u32();
            multiTextures[info.id] = r.u32();
            r.skip(24); // six numbers, the same in every file
            if (count != 1)
                r.ok = false;
        } else if (type == "NiImage") {
            readObjectNet(r);
            images[info.id] = r.string();
            r.u32(); // 7 in every file
        } else if (type == "NiVertexColorProperty") {
            readObjectNet(r);
            vertexColorModes[info.id] = r.u32();
        } else if (type == "NiZBufferProperty") {
            readObjectNet(r);
            zBuffers[info.id] = {r.u8(), r.u8()};
        } else if (type == "NiTextureModeProperty") {
            readObjectNet(r);
            r.skip(12);
        } else if (type == "NiSpecularProperty") {
            readObjectNet(r);
            r.u8();
        } else if (type == "NiShadeProperty") {
            readObjectNet(r);
        } else {
            info.read = false;
        }
        info.exact = info.read && r.ok && r.pos == r.size;
        byId_[info.id] = objects_.size();
        objects_.push_back(std::move(info));
    }
    if (roots_.empty())
        return Error::fail("no top-level object");
    return Error::success();
}

} // namespace reluva::netimmerse
