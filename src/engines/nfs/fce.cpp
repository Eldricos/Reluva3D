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

std::array<int, 3> rgbToHsv(const std::array<int, 3>& rgb) {
    const int r = rgb[0], g = rgb[1], b = rgb[2];
    const int mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    int64_t h = 0;
    int s = 0;
    // 16.16 sixths of a turn: (x % d << 16) / d + (x / d << 16).
    auto fixed = [&](int x) { return static_cast<int64_t>(x % d) * 65536 / d + static_cast<int64_t>(x / d) * 65536; };
    if (mx != 0 && d != 0) {
        s = d * 256 / mx;
        if (mx == r)
            h = fixed(g - b);
        else if (mx == g)
            h = fixed(b - r) + 0x20000;
        else
            h = fixed(r - g) + 0x40000;
        h /= 6;
        if (h < 0)
            h += 0x10000;
    }
    // x 256, rounded half up (0x49c9b3).
    const int hue = static_cast<int>((h * 256 + 0x8000) >> 16);
    return {std::clamp(hue, 0, 255), std::clamp(s, 0, 255), std::clamp(mx, 0, 255)};
}

std::array<int, 3> hsvToRgb(const std::array<int, 3>& hsv) {
    const int h = hsv[0], s = hsv[1], v = hsv[2];
    if (v < 1)
        return {0, 0, 0};
    if (s < 1)
        return {std::min(v, 255), std::min(v, 255), std::min(v, 255)};
    const int sector = h * 6 >> 8;
    const int64_t f = static_cast<int64_t>(h * 6 - (sector << 8)) * 256; // 16.16
    auto rounded = [](int64_t x) { return static_cast<int>((x + 0x8000) >> 16); };
    const int q = (256 - rounded(s * f)) * v >> 8;
    const int t = (256 - rounded(s * (0x10000 - f))) * v >> 8;
    const int p = v * (256 - s) >> 8;
    std::array<int, 3> out{};
    switch (sector) {
    case 0: out = {v, t, p}; break;
    case 1: out = {q, v, p}; break;
    case 2: out = {p, v, t}; break;
    case 3: out = {p, q, v}; break;
    case 4: out = {t, p, v}; break;
    case 5: out = {v, p, q}; break;
    default: break;
    }
    for (int& c : out)
        c = std::clamp(c, 0, 255);
    return out;
}

CarReflection carReflection(uint32_t carColour, const std::array<int, 3>& sky, int paintValue, int coverDepth) {
    const std::array<int, 3> car{static_cast<int>(carColour >> 16 & 0xff), static_cast<int>(carColour >> 8 & 0xff),
                                 static_cast<int>(carColour & 0xff)};
    const std::array<int, 3> carHsv = rgbToHsv(car);
    std::array<int, 3> mixed = sky;
    int dim = 0x100, weight = 0x100;
    if (coverDepth > 0) {
        dim = 0x100 - 16 * coverDepth;
        weight = 0x100 - 32 * coverDepth;
        for (int i = 0; i < 3; ++i)
            mixed[size_t(i)] = sky[size_t(i)] * weight / 256 + car[size_t(i)] * (0x100 - weight) / 256;
    }
    const std::array<int, 3> skyHsv = rgbToHsv(mixed);
    const int value = skyHsv[2] * carHsv[2] / 512 + 0x80;
    const int normalValue = std::clamp(value - (paintValue >> 2), 0x7f, 0xff);
    const int strongValue = std::clamp(value, 0x7f, 0xff);
    auto argb = [](int alpha, const std::array<int, 3>& c) {
        return static_cast<uint32_t>(alpha) << 24 | static_cast<uint32_t>(c[0]) << 16 | static_cast<uint32_t>(c[1]) << 8 |
               static_cast<uint32_t>(c[2]);
    };
    const uint32_t normal = argb(normalValue, hsvToRgb({skyHsv[0], std::clamp(skyHsv[1] * 0x60 / 256, 0, 255), normalValue}));
    const uint32_t strong = argb(strongValue, hsvToRgb({skyHsv[0], std::clamp(skyHsv[1] >> 1, 0, 255), strongValue}));
    const uint32_t k = static_cast<uint32_t>(std::min(dim * 0x100, 0xffff) >> 8 & 0xff);
    auto scale = [&](uint32_t c) {
        return (((c & 0xff00ff00u) >> 8) * k & 0xff00ff00u) | ((c & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu);
    };
    return {scale(normal), scale(strong)};
}

std::array<std::array<float, 3>, 3> reflectionFrame(const std::array<float, 3>& d) {
    constexpr float kTurn = 6.28318530718f;
    const float yaw = (std::atan2(d[0], d[2]) / kTurn - 0.5f) * kTurn;
    const float pitch = (std::atan2(std::sqrt(d[0] * d[0] + d[2] * d[2]), d[1]) / kTurn - 0.25f) * kTurn;
    const float c = std::cos(yaw), s = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    // The columns of Ry(yaw) Rx(pitch), rows of vectors on the left.
    return {{{c, 0, s}, {s * sp, cp, -c * sp}, {-s * cp, sp, c * cp}}};
}

RainFlare rainFlare(const std::array<float, 3>& light, const std::array<float, 3>& camera,
                    const std::array<float, 3>& facing, float size, uint32_t glowColour) {
    RainFlare f;
    float dx = light[0] - camera[0], dz = light[2] - camera[2];
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length == 0)
        return f;
    dx /= length;
    dz /= length;
    const float seen = -(facing[0] * dx + facing[2] * dz) * length;
    const float half = size * 0.5f;
    const float streak = half <= seen * 0.75f ? std::min(seen * 0.75f, 30.0f) : half;
    // d x up (0x4e0160), and the streak back towards the camera.
    const float sx = -dz * half, sz = dx * half, tx = -dx * streak, tz = -dz * streak;
    f.corners = {{{light[0] - sx, light[2] - sz},
                  {light[0] + sx, light[2] + sz},
                  {light[0] + sx + tx, light[2] + sz + tz},
                  {light[0] - sx + tx, light[2] - sz + tz}}};
    f.colour = (((glowColour & 0xff00ff00u) >> 8) * 0xa6 & 0xff00ff00u) | ((glowColour & 0x00ff00ffu) * 0xa6 >> 8 & 0x00ff00ffu);
    f.depth = seen;
    f.shown = true;
    return f;
}

FogBeam fogBeam(const std::array<float, 3>& lamp, const std::array<float, 3>& right, const std::array<float, 3>& up,
                const std::array<float, 3>& forward, const std::array<float, 3>& camera, uint32_t colour) {
    FogBeam b;
    constexpr float kTurn = 6.28318530718f;
    std::array<float, 3> d{camera[0] - lamp[0], camera[1] - lamp[1], camera[2] - lamp[2]};
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (distance == 0 || distance > 100 || (colour & 0x7fffffff) == 0)
        return b;
    for (float& c : d)
        c /= distance;
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& x) {
        return a[0] * x[0] + a[1] * x[1] + a[2] * x[2];
    };
    const float f = dot(d, forward), r = dot(d, right), u = dot(d, up);
    // Truncated (0x4dfd56), then 3-12.
    const int n = std::clamp(static_cast<int>((1 - distance * 0.01f) * 12), 3, 12);
    const float start = 0.5f * (-0.5f - (r + u) * 0.5f);
    float w = std::atan2(0.75f, 6.0f) / kTurn * 5;
    w = w > 1 ? 1 : w < 0.05f ? 0.05f : w;
    if (f > 1 - w) {
        const float k = 1 - (f - 1 + w) / w;
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8)
            out |= static_cast<uint32_t>(std::clamp(static_cast<int>(static_cast<float>((colour >> shift) & 0xff) * k), 0, 255))
                   << shift;
        colour = out;
    }
    b.colour = colour;
    for (int i = 0; i <= n; ++i) {
        const float a = (start + static_cast<float>(i) / static_cast<float>(n)) * kTurn;
        for (int ring = 0; ring < 2; ++ring) {
            const float radius = ring ? 0.75f : 0.1f, ahead = ring ? 6.0f : 0.0f;
            std::array<float, 3> p{};
            for (int k = 0; k < 3; ++k)
                p[size_t(k)] = lamp[size_t(k)] + right[size_t(k)] * std::cos(a) * radius + up[size_t(k)] * std::sin(a) * radius +
                               forward[size_t(k)] * ahead;
            (ring ? b.farRing : b.nearRing).push_back(p);
        }
    }
    b.shown = true;
    return b;
}

uint32_t fogBeamColour(float fogPercent, int beams) {
    if (fogPercent <= 30)
        return 0;
    float share = std::min((fogPercent - 30) / 30.0f, 1.0f);
    if (beams == 1)
        share *= 0.5f;
    const int alpha = std::clamp(static_cast<int>(std::nearbyint(share * 228)), 0, 255);
    return static_cast<uint32_t>(alpha) << 24 | 0xffffffu;
}

} // namespace reluva::nfs
