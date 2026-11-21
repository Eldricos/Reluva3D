// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WORLD.WDB, the world database: docs/engines/legoisland/formats/wdb.md.

#include "engines/legoisland/formats/wdb.h"

#include "common/bytes.h"
#include "common/stream.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <utility>

namespace reluva::legoisland {

namespace {

// Deeper than any model's tree on the CD (5 levels).
constexpr int kMaxDepth = 64;
// A back-reference in a mesh's corners has this bit clear in its second
// half; a new corner has it set, with its normal in the other bits.
constexpr uint16_t kNewCorner = 0x8000;

// Reads fields in order from the file, failing once, with the offset, if
// any ran past the end.
class Reader {
public:
    Reader(const std::vector<uint8_t> &data, size_t pos, size_t end) : _data(data), _pos(pos), _end(end) {}

    bool fits(size_t n) const { return !_failed && n <= _end - _pos; }
    bool failed() const { return _failed; }
    size_t pos() const { return _pos; }
    void seek(size_t pos) {
        if (pos > _end)
            fail();
        else
            _pos = pos;
    }

    uint8_t u8() {
        if (!fits(1))
            return fail(), 0;
        return _data[_pos++];
    }
    uint16_t u16() {
        if (!fits(2))
            return fail(), 0;
        const uint16_t v = readLE16(&_data[_pos]);
        _pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!fits(4))
            return fail(), 0;
        const uint32_t v = readLE32(&_data[_pos]);
        _pos += 4;
        return v;
    }
    float f32() {
        const uint32_t bits = u32();
        float v = 0;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    template<size_t N>
    void floats(std::array<float, N> &out) {
        for (float &v : out)
            v = f32();
    }
    // A string after its length. The worlds' names end with a NUL and the
    // rest don't; either way the NUL isn't kept.
    std::string str() {
        const uint32_t n = u32();
        if (!fits(n))
            return fail(), std::string();
        std::string s(reinterpret_cast<const char *>(&_data[_pos]), n);
        _pos += n;
        while (!s.empty() && s.back() == '\0')
            s.pop_back();
        return s;
    }
    void bytes(size_t n, std::vector<uint8_t> &out) {
        if (!fits(n)) {
            fail();
            out.clear();
            return;
        }
        out.assign(_data.begin() + std::ptrdiff_t(_pos), _data.begin() + std::ptrdiff_t(_pos + n));
        _pos += n;
    }

    Error error(const std::string &what) const { return Error::fail(what + " at offset " + std::to_string(_pos)); }

private:
    void fail() {
        _failed = true;
        _pos = _end;
    }

    const std::vector<uint8_t> &_data;
    size_t _pos;
    size_t _end;
    bool _failed = false;
};

Error readImage(Reader &r, WdbImage &out) {
    out.width = r.u32();
    out.height = r.u32();
    const uint32_t colors = r.u32();
    if (colors > 256)
        return r.error("a palette of " + std::to_string(colors) + " colours");
    r.bytes(size_t(colors) * 3, out.palette);
    if (!r.fits(size_t(out.width) * out.height))
        return r.error("a picture cut short");
    r.bytes(size_t(out.width) * out.height, out.pixels);
    return Error::success();
}

Error readTextures(Reader &r, uint32_t count, std::vector<WdbTexture> &out) {
    out.clear();
    for (uint32_t i = 0; i < count && !r.failed(); i++) {
        WdbTexture t;
        t.name = r.str();
        t.images.resize(t.name.starts_with('^') ? 2 : 1);
        for (WdbImage &img : t.images)
            if (Error e = readImage(r, img))
                return e;
        out.push_back(std::move(t));
    }
    return r.failed() ? r.error("textures cut short") : Error::success();
}

Error readMesh(Reader &r, const WdbLod &lod, WdbMesh &out) {
    const uint16_t faces = r.u16();
    const uint16_t corners = r.u16();
    out.corners.reserve(corners);
    out.triangles.reserve(size_t(faces) * 3);
    // Which triangle corners made a new corner, for the texture coordinates.
    std::vector<bool> made;
    made.reserve(size_t(faces) * 3);
    for (size_t i = 0; i < size_t(faces) * 3 && !r.failed(); i++) {
        const uint16_t first = r.u16();
        const uint16_t second = r.u16();
        made.push_back(second & kNewCorner);
        if (second & kNewCorner) {
            WdbCorner c;
            c.vertex = first;
            c.normal = second & ~kNewCorner;
            if (c.vertex >= lod.vertices.size() || c.normal >= lod.normals.size())
                return r.error("a corner past its LOD's points");
            out.triangles.push_back(uint16_t(out.corners.size()));
            out.corners.push_back(c);
        } else {
            if (first >= out.corners.size())
                return r.error("a corner not yet defined");
            out.triangles.push_back(first);
        }
    }
    if (out.corners.size() != corners)
        return r.error("a mesh's corners not as counted");

    // One for each triangle corner, as the faces list them: a new corner's
    // texture coordinates, or the corner a back-reference names, again.
    const uint32_t texIndices = r.u32();
    if (texIndices != 0 && texIndices != made.size())
        return r.error("texture coordinates not one a triangle corner");
    size_t corner = 0;
    for (uint32_t i = 0; i < texIndices && !r.failed(); i++) {
        const uint32_t index = r.u32();
        if (!made[i])
            continue;
        if (index >= lod.texCoords.size())
            return r.error("texture coordinates past its LOD's");
        out.corners[corner++].texCoord = index;
    }
    for (uint8_t &c : out.color)
        c = r.u8();
    out.transparency = r.f32();
    out.shading = r.u8();
    for (uint8_t &b : out.unknown)
        b = r.u8();
    out.texture = r.str();
    out.material = r.str();
    return r.failed() ? r.error("a mesh cut short") : Error::success();
}

Error readLods(Reader &r, uint32_t count, std::vector<WdbLod> &out) {
    out.resize(count);
    for (WdbLod &lod : out) {
        lod.flags = r.u32();
        const uint32_t meshes = r.u32();
        // An empty LOD stops here, without the counts of its points.
        if (meshes == 0)
            continue;
        const uint16_t vertices = r.u16();
        // Twice the normals', in every LOD on the CD.
        const uint16_t normals = r.u16() / 2;
        const uint32_t texCoords = r.u32();
        if (!r.fits((size_t(vertices) + normals) * 12 + size_t(texCoords) * 8))
            return r.error("a LOD's points cut short");
        lod.vertices.resize(vertices);
        for (auto &v : lod.vertices)
            r.floats(v);
        lod.normals.resize(normals);
        for (auto &n : lod.normals)
            r.floats(n);
        lod.texCoords.resize(texCoords);
        for (auto &t : lod.texCoords)
            r.floats(t);
        if (meshes > 0xFFFF)
            return r.error("too many meshes");
        lod.meshes.resize(meshes);
        for (WdbMesh &m : lod.meshes)
            if (Error e = readMesh(r, lod, m))
                return e;
    }
    return Error::success();
}

// The LODs and the offset of their end, from `base`, which they must reach.
Error readLodsTo(Reader &r, size_t base, uint32_t count, std::vector<WdbLod> &out) {
    const uint32_t end = r.u32();
    if (Error e = readLods(r, count, out))
        return e;
    if (r.pos() != base + end)
        return r.error("LODs not ending where their ROI says");
    return Error::success();
}

Error readModelRoi(Reader &r, size_t base, WdbRoi &out, int depth) {
    if (depth > kMaxDepth)
        return r.error("ROIs nested too deeply");
    out.name = r.str();
    r.floats(out.sphereCenter);
    out.sphereRadius = r.f32();
    r.floats(out.boxMin);
    r.floats(out.boxMax);
    out.indirectName = r.str();
    out.usesPart = r.u8() != 0;
    if (!out.usesPart) {
        if (const uint32_t lods = r.u32())
            if (Error e = readLodsTo(r, base, lods, out.lods))
                return e;
    }
    const uint32_t children = r.u32();
    for (uint32_t i = 0; i < children && !r.failed(); i++) {
        out.children.emplace_back();
        if (Error e = readModelRoi(r, base, out.children.back(), depth + 1))
            return e;
    }
    return r.failed() ? r.error("an ROI cut short") : Error::success();
}

void readKeys(Reader &r, int values, std::vector<WdbKey> &out) {
    const uint16_t count = r.u16();
    for (uint16_t i = 0; i < count && !r.failed(); i++) {
        WdbKey k;
        const uint32_t word = r.u32();
        k.time = word & 0xFFFFFF;
        k.flags = uint8_t(word >> 24);
        if (values == 0)
            k.value[0] = float(r.u8());
        for (int v = 0; v < values; v++)
            k.value[v] = r.f32();
        out.push_back(k);
    }
}

Error readAnimNode(Reader &r, WdbAnimNode &out, int depth) {
    if (depth > kMaxDepth)
        return r.error("animation nested too deeply");
    out.name = r.str();
    readKeys(r, 3, out.translation);
    readKeys(r, 4, out.rotation);
    readKeys(r, 3, out.scale);
    readKeys(r, 0, out.morph); // a byte each
    const uint32_t children = r.u32();
    for (uint32_t i = 0; i < children && !r.failed(); i++) {
        out.children.emplace_back();
        if (Error e = readAnimNode(r, out.children.back(), depth + 1))
            return e;
    }
    return r.failed() ? r.error("an animation cut short") : Error::success();
}

// A part, from `base` to `end`: its offsets count from `base`.
Error readPartAt(const std::vector<uint8_t> &data, size_t base, size_t end, WdbPart &out) {
    Reader r(data, base, end);
    const uint32_t texOffset = r.u32();
    const uint32_t rois = r.u32();
    for (uint32_t i = 0; i < rois && !r.failed(); i++) {
        WdbRoi roi;
        roi.name = r.str();
        if (const uint32_t lods = r.u32())
            if (Error e = readLodsTo(r, base, lods, roi.lods))
                return e;
        out.rois.push_back(std::move(roi));
    }
    if (r.failed() || r.pos() != base + texOffset)
        return r.error("a part's ROIs not ending at its textures");
    if (Error e = readTextures(r, r.u32(), out.textures))
        return e;
    if (r.pos() != end)
        return r.error("a part not ending where its world says");
    return Error::success();
}

Error checkRange(const std::vector<uint8_t> &data, uint32_t offset, uint32_t size, const std::string &name) {
    if (offset > data.size() || size > data.size() - offset)
        return Error::fail("'" + name + "' past the file's end");
    return Error::success();
}

} // namespace

void WdbImage::toRGBA(std::vector<uint8_t> &out) const {
    out.resize(pixels.size() * 4);
    const size_t colors = palette.size() / 3;
    for (size_t i = 0; i < pixels.size(); i++) {
        uint8_t *px = &out[i * 4];
        if (pixels[i] < colors)
            std::memcpy(px, &palette[size_t(pixels[i]) * 3], 3);
        else
            px[0] = px[1] = px[2] = 0;
        px[3] = 255;
    }
}

std::string WdbRoi::partName() const {
    std::string s = name;
    while (!s.empty() && s.back() >= '0' && s.back() <= '9')
        s.pop_back();
    return s;
}

Error WdbFile::open(const std::filesystem::path &path) {
    std::vector<uint8_t> data;
    if (!readFile(path, data))
        return Error::fail("Can't read " + path.string());
    if (Error e = load(std::move(data)))
        return Error::fail(path.filename().string() + ": " + e.message);
    return Error::success();
}

Error WdbFile::load(std::vector<uint8_t> data) {
    _data = std::move(data);
    _worlds.clear();
    Reader r(_data, 0, _data.size());
    const uint32_t worlds = r.u32();
    if (worlds > 1000)
        return r.error("not a world database: " + std::to_string(worlds) + " worlds");
    for (uint32_t w = 0; w < worlds && !r.failed(); w++) {
        WdbWorld world;
        world.name = r.str();
        const uint32_t parts = r.u32();
        for (uint32_t i = 0; i < parts && !r.failed(); i++) {
            WdbPartRef p;
            p.name = r.str();
            p.size = r.u32();
            p.offset = r.u32();
            if (Error e = checkRange(_data, p.offset, p.size, p.name))
                return e;
            world.parts.push_back(std::move(p));
        }
        const uint32_t models = r.u32();
        for (uint32_t i = 0; i < models && !r.failed(); i++) {
            WdbModelRef m;
            m.name = r.str();
            m.size = r.u32();
            m.offset = r.u32();
            m.presenter = r.str();
            r.floats(m.location);
            r.floats(m.direction);
            r.floats(m.up);
            m.flag = r.u8();
            if (Error e = checkRange(_data, m.offset, m.size, m.name))
                return e;
            world.models.push_back(std::move(m));
        }
        _worlds.push_back(std::move(world));
    }
    if (r.failed())
        return r.error("the worlds cut short");

    // The shared textures, then the shared part, each after its size.
    const uint32_t texSize = r.u32();
    const size_t texEnd = r.pos() + texSize;
    if (!r.fits(texSize))
        return r.error("the shared textures cut short");
    if (Error e = readTextures(r, r.u32(), _sharedTextures))
        return e;
    if (r.pos() != texEnd)
        return r.error("the shared textures not as long as their size");
    const uint32_t partSize = r.u32();
    if (!r.fits(partSize))
        return r.error("the shared part cut short");
    _sharedPart = WdbPart();
    return readPartAt(_data, r.pos(), r.pos() + partSize, _sharedPart);
}

std::vector<WdbPartRef> WdbFile::allParts() const {
    std::vector<WdbPartRef> out;
    std::set<uint32_t> seen;
    for (const WdbWorld &w : _worlds)
        for (const WdbPartRef &p : w.parts)
            if (seen.insert(p.offset).second)
                out.push_back(p);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    return out;
}

std::vector<WdbModelRef> WdbFile::allModels() const {
    std::vector<WdbModelRef> out;
    std::set<uint32_t> seen;
    for (const WdbWorld &w : _worlds)
        for (const WdbModelRef &m : w.models)
            if (seen.insert(m.offset).second)
                out.push_back(m);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    return out;
}

Error WdbFile::readPart(const WdbPartRef &ref, WdbPart &out) const {
    out = WdbPart();
    if (Error e = checkRange(_data, ref.offset, ref.size, ref.name))
        return e;
    if (Error e = readPartAt(_data, ref.offset, size_t(ref.offset) + ref.size, out))
        return Error::fail(ref.name + ": " + e.message);
    return Error::success();
}

Error WdbFile::readModel(const WdbModelRef &ref, WdbModel &out) const {
    out = WdbModel();
    if (Error e = checkRange(_data, ref.offset, ref.size, ref.name))
        return e;
    const size_t base = ref.offset;
    const size_t end = base + ref.size;
    Reader r(_data, base, end);
    out.version = r.u32();
    const uint32_t texOffset = r.u32();
    for (uint32_t &v : out.animHeader)
        v = r.u32();
    Error e = readAnimNode(r, out.animation, 0);
    if (!e)
        e = readModelRoi(r, base, out.roi, 0);
    if (!e && r.pos() != base + texOffset)
        e = r.error("a model's ROIs not ending at its textures");
    if (!e) {
        const uint32_t count = r.u32();
        r.u32(); // 0 in every model
        e = readTextures(r, count, out.textures);
    }
    if (!e && r.pos() != end)
        e = r.error("a model not ending where its world says");
    if (e)
        return Error::fail(ref.name + ": " + e.message);
    return Error::success();
}

} // namespace reluva::legoisland
