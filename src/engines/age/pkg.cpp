// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's models; see
// docs/formats/age/pkg.md. The vertex layouts are Direct3D's flexible
// vertex format codes (D3DFVF_*), from Microsoft's published DirectX
// documentation.

#include "engines/age/pkg.h"

#include <cstring>

namespace reluva::age {

namespace {

constexpr uint32_t kFvfXyz = 0x2, kFvfPositionMask = 0xe, kFvfNormal = 0x10, kFvfPointSize = 0x20,
                   kFvfDiffuse = 0x40, kFvfSpecular = 0x80;
constexpr uint32_t kTriangleList = 3; // the only primitive type the game's models use
constexpr uint32_t kByteColours = 0x80; // in the shaders' header

// Reads little-endian values, failing (sticky) when the data runs out.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool need(size_t n) {
        if (ok && size_t(end - p) < n)
            ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? *p++ : 0; }
    uint16_t u16() {
        if (!need(2))
            return 0;
        const uint16_t v = static_cast<uint16_t>(p[0] | p[1] << 8);
        p += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        p += 4;
        return v;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    void skip(size_t n) {
        if (need(n))
            p += n;
    }
    // A name stored as a length byte (counting its terminating zero) and the text.
    std::string name() {
        const size_t n = u8();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        if (const size_t zero = s.find('\0'); zero != std::string::npos)
            s.resize(zero);
        return s;
    }
};

Error readMesh(Reader r, PkgMesh& mesh) {
    const uint32_t groups = r.u32();
    const uint32_t vertexTotal = r.u32(), indexTotal = r.u32();
    const uint32_t groups2 = r.u32();
    mesh.fvf = r.u32();
    if (!r.ok)
        return Error::fail("truncated header");
    if (groups2 != groups)
        return Error::fail("group counts disagree");
    if ((mesh.fvf & kFvfPositionMask) != kFvfXyz)
        return Error::fail("unsupported vertex format " + std::to_string(mesh.fvf));
    const size_t texSets = (mesh.fvf >> 8) & 0xf;
    const bool hasNormal = mesh.fvf & kFvfNormal;
    // What follows the position and normal, before the texture coordinates.
    const size_t extra = (mesh.fvf & kFvfPointSize ? 4 : 0) + (mesh.fvf & kFvfDiffuse ? 4 : 0) +
                         (mesh.fvf & kFvfSpecular ? 4 : 0);
    const size_t stride = 12 + (hasNormal ? 12 : 0) + extra + 8 * texSets;

    uint64_t vertices = 0, indices = 0;
    for (uint32_t g = 0; g < groups && r.ok; ++g) {
        const uint32_t strips = r.u32();
        const uint32_t shader = r.u32();
        for (uint32_t s = 0; s < strips && r.ok; ++s) {
            PkgGroup group;
            group.shader = shader;
            const uint32_t type = r.u32();
            const uint32_t count = r.u32();
            if (type != kTriangleList)
                return Error::fail("unknown primitive type " + std::to_string(type));
            if (!r.need(uint64_t(count) * stride))
                break;
            group.vertices.resize(count);
            for (PkgVertex& v : group.vertices) {
                const uint8_t* start = r.p;
                for (float& c : v.pos)
                    c = r.f32();
                if (hasNormal)
                    for (float& c : v.normal)
                        c = r.f32();
                r.skip(extra);
                if (texSets) {
                    v.uv[0] = r.f32();
                    v.uv[1] = r.f32();
                }
                r.p = start + stride;
            }
            const uint32_t indexCount = r.u32();
            if (indexCount % 3 != 0)
                return Error::fail("index count not a multiple of 3");
            if (!r.need(uint64_t(indexCount) * 2))
                break;
            group.indices.resize(indexCount);
            for (uint16_t& i : group.indices) {
                i = r.u16();
                if (i >= count)
                    return Error::fail("index past the vertices");
            }
            vertices += count;
            indices += indexCount;
            mesh.groups.push_back(std::move(group));
        }
    }
    if (!r.ok)
        return Error::fail("truncated");
    if (vertices != vertexTotal || indices != indexTotal)
        return Error::fail("vertex or index totals disagree");
    if (r.p != r.end)
        return Error::fail("data left over");
    return Error::success();
}

Error readShaders(Reader r, Pkg& pkg) {
    const uint32_t kind = r.u32();
    pkg.shadersPerJob = r.u32();
    pkg.paintJobs = kind & 0x7f;
    const bool bytes = kind & kByteColours;
    const uint64_t count = uint64_t(pkg.paintJobs) * pkg.shadersPerJob;
    if (!r.ok || count > 0x10000)
        return Error::fail("bad shader header");
    pkg.shaders.resize(size_t(count));
    for (PkgShader& s : pkg.shaders) {
        s.texture = r.name();
        if (bytes) {
            // R, G, B, A bytes (not Direct3D's B, G, R, A: the traffic
            // lights' red, amber and green glows come out right this way).
            for (auto* c : {&s.diffuse, &s.ambient, &s.emissive})
                for (float& x : *c)
                    x = float(r.u8()) / 255;
        } else {
            for (auto* c : {&s.diffuse, &s.ambient, &s.specular, &s.emissive})
                for (float& x : *c)
                    x = r.f32();
        }
        s.shininess = r.f32();
    }
    if (!r.ok)
        return Error::fail("truncated");
    if (r.p != r.end)
        return Error::fail("data left over");
    return Error::success();
}

Error readXrefs(Reader r, Pkg& pkg) {
    const uint32_t count = r.u32();
    if (!r.need(uint64_t(count) * (48 + 32)))
        return Error::fail("truncated");
    pkg.xrefs.resize(count);
    for (PkgXref& x : pkg.xrefs) {
        for (float& f : x.matrix)
            f = r.f32();
        // A fixed 32-byte field; what follows the name's zero is left
        // over from earlier names.
        x.name.assign(reinterpret_cast<const char*>(r.p), 32);
        x.name.resize(x.name.find('\0') == std::string::npos ? 32 : x.name.find('\0'));
        r.skip(32);
    }
    return Error::success();
}

} // namespace

std::string PkgMesh::part() const {
    const size_t us = name.rfind('_');
    return us == std::string::npos ? std::string() : name.substr(0, us);
}

std::string PkgMesh::level() const {
    const size_t us = name.rfind('_');
    return us == std::string::npos ? name : name.substr(us + 1);
}

bool isLightPart(const std::string& part) {
    std::string p = part;
    while (!p.empty() && p.back() >= '0' && p.back() <= '9')
        p.pop_back();
    for (const char* name : {"HEADLIGHT", "HLIGHT", "TLIGHT", "BLIGHT", "RLIGHT", "SLIGHT", "SIREN", "SRN"})
        if (p == name)
            return true;
    return p.find("GLOW") != std::string::npos || p.rfind("WALK_", 0) == 0 || p.rfind("NOWALK_", 0) == 0;
}

const PkgMesh* Pkg::mesh(std::string_view name) const {
    for (const PkgMesh& m : meshes)
        if (m.name == name)
            return &m;
    return nullptr;
}

const PkgShader* Pkg::shader(uint32_t paintJob, uint32_t index) const {
    const uint64_t i = uint64_t(paintJob) * shadersPerJob + index;
    return index < shadersPerJob && i < shaders.size() ? &shaders[size_t(i)] : nullptr;
}

Error readPkg(const std::vector<uint8_t>& data, Pkg& out) {
    out = Pkg{};
    Reader r{data.data(), data.data() + data.size()};
    if (data.size() < 4 || std::memcmp(data.data(), "PKG", 3) != 0)
        return Error::fail("not a model (no PKG header)");
    if (data[3] != '3')
        return Error::fail(std::string("model version PKG") + char(data[3]) + " isn't supported, only PKG3");
    r.skip(4);
    while (r.ok && r.p != r.end) {
        if (!r.need(4) || std::memcmp(r.p, "FILE", 4) != 0)
            return Error::fail("expected FILE at byte " + std::to_string(r.p - data.data()));
        r.skip(4);
        const std::string name = r.name();
        const uint32_t size = r.u32();
        if (!r.need(size))
            return Error::fail(name + " runs past the end of the file");
        const Reader section{r.p, r.p + size};
        r.skip(size);
        if (name == "shaders") {
            if (Error err = readShaders(section, out))
                return Error::fail("shaders: " + err.message);
        } else if (name == "offset") {
            Reader o = section;
            for (float& f : out.offset)
                f = o.f32();
            out.hasOffset = o.ok;
        } else if (name == "xrefs") {
            if (Error err = readXrefs(section, out))
                return Error::fail("xrefs: " + err.message);
        } else {
            PkgMesh mesh;
            mesh.name = name;
            if (Error err = readMesh(section, mesh))
                return Error::fail(name + ": " + err.message);
            out.meshes.push_back(std::move(mesh));
        }
    }
    if (!r.ok)
        return Error::fail("truncated");
    return Error::success();
}

Error readMtx(const std::vector<uint8_t>& data, Mtx& out) {
    if (data.size() != 48)
        return Error::fail("a placement is 48 bytes, this is " + std::to_string(data.size()));
    Reader r{data.data(), data.data() + data.size()};
    for (Vec3* v : {&out.min, &out.max, &out.pivot, &out.origin})
        for (float& f : *v)
            f = r.f32();
    return Error::success();
}

} // namespace reluva::age
