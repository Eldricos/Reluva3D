// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/fce.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::nfs {

namespace {

uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

float f32(const uint8_t* p) {
    const uint32_t v = le32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

Model::Vec3 vec3(const uint8_t* p) { return {f32(p), f32(p + 4), f32(p + 8)}; }

std::string name(const uint8_t* p) {
    return std::string(reinterpret_cast<const char*>(p), std::find(p, p + 64, 0) - p);
}

// The header's fields (docs/formats/nfs/fce.md).
constexpr size_t kTriangles = 0x04, kVertices = 0x08;
constexpr size_t kVertexTable = 0x10, kNormalTable = 0x14, kTriangleTable = 0x18;
constexpr size_t kHalfSize = 0x28, kDummyCount = 0x34, kDummies = 0x38;
constexpr size_t kPartCount = 0xf8, kPartPositions = 0xfc;
constexpr size_t kFirstVertices = 0x3fc, kVertexCounts = 0x4fc, kFirstTriangles = 0x5fc, kTriangleCounts = 0x6fc;
constexpr size_t kPrimaryCount = 0x7fc, kPrimaryColours = 0x800, kSecondaryCount = 0x900, kSecondaryColours = 0x904;
constexpr size_t kDummyNames = 0xa04, kPartNames = 0xe04;
constexpr size_t kDataStart = 0x1f04; // the tables' offsets count from here
constexpr size_t kTriangleSize = 56;
constexpr uint32_t kMaxParts = 64, kMaxDummies = 16, kMaxColours = 16;

} // namespace

Error parseModel(const std::vector<uint8_t>& data, Model& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .fce model: " + why); };
    if (data.size() < kDataStart)
        return bad("too short");
    const uint8_t* d = data.data();
    const uint32_t triangles = le32(d + kTriangles), vertices = le32(d + kVertices);
    const uint32_t parts = le32(d + kPartCount), dummies = le32(d + kDummyCount);
    if (parts > kMaxParts || dummies > kMaxDummies)
        return bad("too many parts or dummies");
    const uint64_t vertexTable = kDataStart + uint64_t(le32(d + kVertexTable));
    const uint64_t normalTable = kDataStart + uint64_t(le32(d + kNormalTable));
    const uint64_t triangleTable = kDataStart + uint64_t(le32(d + kTriangleTable));
    if (vertexTable + 12ull * vertices > data.size() || normalTable + 12ull * vertices > data.size() ||
        triangleTable + uint64_t(kTriangleSize) * triangles > data.size())
        return bad("its tables run past the end");

    out.halfSize = vec3(d + kHalfSize);
    out.vertices.resize(vertices);
    out.normals.resize(vertices);
    for (uint32_t i = 0; i < vertices; ++i) {
        out.vertices[i] = vec3(d + vertexTable + 12 * i);
        out.normals[i] = vec3(d + normalTable + 12 * i);
    }

    for (uint32_t p = 0; p < parts; ++p) {
        Model::Part part;
        part.name = name(d + kPartNames + 64 * p);
        part.position = vec3(d + kPartPositions + 12 * p);
        part.firstVertex = le32(d + kFirstVertices + 4 * p);
        part.vertexCount = le32(d + kVertexCounts + 4 * p);
        part.firstTriangle = le32(d + kFirstTriangles + 4 * p);
        part.triangleCount = le32(d + kTriangleCounts + 4 * p);
        if (uint64_t(part.firstVertex) + part.vertexCount > vertices ||
            uint64_t(part.firstTriangle) + part.triangleCount > triangles)
            return bad("part " + std::to_string(p) + " runs past the model's vertices or triangles");
        out.parts.push_back(std::move(part));
    }

    // Triangles index their part's vertices; they're made to index the
    // model's here.
    out.triangles.resize(triangles);
    for (const Model::Part& part : out.parts)
        for (uint32_t t = part.firstTriangle; t < part.firstTriangle + part.triangleCount; ++t) {
            const uint8_t* p = d + triangleTable + kTriangleSize * t;
            Model::Triangle& tri = out.triangles[t];
            tri.texturePage = le32(p);
            for (int k = 0; k < 3; ++k) {
                const uint32_t index = le32(p + 4 + 4 * k);
                if (index >= part.vertexCount)
                    return bad("triangle " + std::to_string(t) + " uses a vertex outside its part");
                tri.vertices[k] = part.firstVertex + index;
                tri.u[k] = f32(p + 32 + 4 * k);
                tri.v[k] = f32(p + 44 + 4 * k);
            }
            tri.flags = le32(p + 28);
        }

    for (uint32_t k = 0; k < dummies; ++k)
        out.dummies.push_back({name(d + kDummyNames + 64 * k), vec3(d + kDummies + 12 * k)});
    auto colours = [&](size_t countAt, size_t at, std::vector<Model::Colour>& list) {
        const uint32_t n = std::min(le32(d + countAt), kMaxColours);
        for (uint32_t i = 0; i < n; ++i) {
            Model::Colour c;
            for (int k = 0; k < 4; ++k)
                c.hsbx[k] = le32(d + at + 16 * i + 4 * k);
            list.push_back(c);
        }
    };
    colours(kPrimaryCount, kPrimaryColours, out.primaryColours);
    colours(kSecondaryCount, kSecondaryColours, out.secondaryColours);
    return Error::success();
}

// One image after another, to the end: u32 width, u32 height, then the
// pixels, 4 bytes each (blue, green, red, alpha), top row first.
Error parseArt(const std::vector<uint8_t>& data, std::vector<image::Image>& pages) {
    pages.clear();
    size_t at = 0;
    while (at < data.size()) {
        if (data.size() - at < 8)
            return Error::fail(".art: a page header runs past the end");
        uint32_t w, h;
        std::memcpy(&w, data.data() + at, 4);
        std::memcpy(&h, data.data() + at + 4, 4);
        at += 8;
        if (w == 0 || h == 0 || w > 4096 || h > 4096 || uint64_t(w) * h * 4 > data.size() - at)
            return Error::fail(".art: a page's pixels run past the end");
        image::Image img;
        img.w = int(w);
        img.h = int(h);
        img.rgba.resize(size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const uint8_t* p = data.data() + at + 4 * i;
            img.rgba[4 * i] = p[2];
            img.rgba[4 * i + 1] = p[1];
            img.rgba[4 * i + 2] = p[0];
            img.rgba[4 * i + 3] = p[3];
        }
        at += size_t(w) * h * 4;
        pages.push_back(std::move(img));
    }
    return Error::success();
}

CarShade carShade(uint32_t colour) {
    CarShade s;
    const int r = static_cast<int>((colour >> 16) & 0xff), g = static_cast<int>((colour >> 8) & 0xff),
              b = static_cast<int>(colour & 0xff);
    s.base = {r / 3, g / 3, b >> 1};
    s.top = {std::min(2 * r, 255), std::min(2 * g, 255), std::min(2 * b, 255)};
    return s;
}

uint32_t carVertexColour(const CarShade& shade, float normalDotLight) {
    uint32_t out = 0xff000000u;
    const int k = normalDotLight > 0 ? static_cast<int>(std::lrint(normalDotLight * 65536.0f)) : 0;
    for (int i = 0; i < 3; ++i) {
        int c = shade.base[size_t(i)];
        if (k > 0) {
            const uint32_t add =
                static_cast<uint32_t>(k) * static_cast<uint32_t>(shade.top[size_t(i)] - shade.base[size_t(i)]) >> 16;
            c = static_cast<int>(std::min<uint32_t>(static_cast<uint32_t>(c) + add, static_cast<uint32_t>(shade.top[size_t(i)])));
        }
        out |= static_cast<uint32_t>(std::clamp(c, 0, 255)) << (16 - 8 * i);
    }
    return out;
}

std::array<float, 3> carLightDirection(int rotationDegrees) {
    // 0x55d1f0 = degrees x 65536 / 360 (integer), the table's 1024 steps
    // a turn (0x4f2380 sine, 0x4f23f0 cosine).
    const int units = rotationDegrees * 65536 / 360;
    const double turn = static_cast<double>(units) / 65536.0 * 6.283185307179586;
    std::array<float, 3> d{static_cast<float>(std::sin(turn)), 0.5f, static_cast<float>(std::cos(turn))};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (float& v : d)
        v /= length;
    return d;
}

uint32_t carShadeColour(const std::array<uint32_t, 4>& corners) {
    uint32_t out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t sum = 0;
        for (uint32_t c : corners)
            sum += (c >> shift) & 0xff;
        out |= (sum >> 2) << shift;
    }
    return out;
}

uint32_t ambientScaled(uint32_t colour, const std::array<int, 3>& ambientPercent) {
    uint32_t out = colour & 0xff000000u;
    for (int i = 0; i < 3; ++i) {
        const int shift = 16 - 8 * i;
        const int c = ambientPercent[size_t(i)] * static_cast<int>((colour >> shift) & 0xff) / 100;
        out |= (static_cast<uint32_t>(c) & 0xff) << shift;
    }
    return out;
}

float carGlowBrightness(uint32_t colour) {
    const float sum = static_cast<float>(((colour >> 16) & 0xff) + ((colour >> 8) & 0xff) + (colour & 0xff));
    // 0x53ffc8 = 1/765, 0x53ffd0 = 0.3; under 0.7 (0x53ffd8) it is 0.7.
    const float b = 1.0f - sum * (1.0f / 765.0f) * 0.3f;
    return b < 0.7f ? 0.7f : std::min(b, 1.0f);
}

namespace {
uint32_t glowAlpha(float value) {
    const long a = std::lrint(value);
    return static_cast<uint32_t>(std::clamp(a, 0L, 255L)) << 24;
}
} // namespace

CarGlow carLightGlow(const std::string& dummy, float facing, int beams, bool braking, bool siren, int step,
                     float brightness) {
    CarGlow g;
    auto letter = [&](size_t i) { return i < dummy.size() ? dummy[i] : '\0'; };
    // The siren's phase (0x42cff0): (step mod 32) / 32; the flashing
    // headlight's letter O for its first half, E for the second, and the
    // roof lights' share, up and down twice as fast (0x540004, 0x540008).
    const float phase = static_cast<float>(((step % 32) + 32) % 32) * 0.03125f;
    const char flash = siren ? (phase <= 0.5f ? 'O' : 'E') : '\0';
    float share = phase * 2;
    if (share > 1)
        share = 2 - share;
    float seen = 0; // how much the light faces the camera
    if (letter(1) == 'R' || letter(1) == 'F') {
        seen = letter(1) == 'R' ? -facing : facing;
        if (seen > 0) {
            const float across = seen > 0.25f ? 1.0f : seen * 4; // 0x540010, 0x540018
            if (letter(1) == 'R') {
                // Red, brighter when braking (198 instead of 128).
                g.size = std::max(seen, 0.25f) * 0.8f * brightness;
                g.colour = glowAlpha((braking ? 198.0f : 128.0f) * across * brightness) | 0xff0000u;
            } else {
                // White, half as large again on high beams.
                g.size = brightness * seen * 0.8f * (beams == 2 ? 1.5f : 1.0f);
                g.colour = glowAlpha(across * 255.0f * brightness) | 0xffffffu;
            }
        }
    }
    switch (letter(0)) {
    case 'T': g.shown = (beams != 0 || braking) && seen > 0; break;
    case 'H': g.shown = (beams != 0 || (flash != '\0' && flash == letter(3))) && seen > 0; break;
    case 'S':
        if (siren) {
            // Blue on the left (+0x7A4), red on the right (+0x7A8), in turn.
            const float v = letter(2) == 'L' ? share : 1 - share;
            g.size = brightness * std::max(v, 0.25f) * 1.5f; // 0x540038
            g.colour = glowAlpha(v * 255.0f * brightness) | (letter(2) == 'L' ? 0x8080ffu : 0xff8080u);
            g.nearer = 0.75f;
            g.shown = true;
        }
        break;
    default: break;
    }
    return g;
}

std::array<float, 2> reflectionUv(float x, float y, float z) {
    const float k = 0.707106f / std::sqrt(2 * (1 + std::fabs(z)));
    return {x * k + 0.5f, y * k + 0.5f};
}

} // namespace reluva::nfs
