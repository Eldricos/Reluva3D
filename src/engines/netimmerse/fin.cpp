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
