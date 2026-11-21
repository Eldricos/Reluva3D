// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The SI files' paths (.gph): docs/engines/legoisland/formats/obj.md.

#include "engines/legoisland/formats/gph.h"

#include "common/bytes.h"

#include <cstring>

namespace reluva::legoisland {

namespace {

// Reads fields in order, failing once if any ran past the end.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _data(data) {}

    bool failed() const { return _failed; }
    size_t pos() const { return _pos; }

    uint8_t u8() { return fits(1) ? _data[_pos++] : 0; }
    uint16_t u16() {
        if (!fits(2))
            return 0;
        const uint16_t v = readLE16(&_data[_pos]);
        _pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!fits(4))
            return 0;
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
    // A name after its length, a byte.
    std::string str() {
        const uint8_t n = u8();
        if (!fits(n))
            return std::string();
        std::string s(reinterpret_cast<const char *>(&_data[_pos]), n);
        _pos += n;
        return s;
    }

private:
    bool fits(size_t n) {
        if (_failed || n > _data.size() - _pos) {
            _failed = true;
            return false;
        }
        return true;
    }

    const std::vector<uint8_t> &_data;
    size_t _pos = 0;
    bool _failed = false;
};

// An edge bounding two faces has a second face, and edges round it. Its
// flags are 15 and the others' 4, so which of these bits says so isn't known.
constexpr uint16_t kTwoFaces = 0x0B;

} // namespace

Error GphFile::parse(const std::vector<uint8_t> &data) {
    *this = GphFile();
    Reader r(data);
    const uint16_t kindCount = r.u16();
    const uint16_t pointCount = r.u16();
    const uint16_t edgeCount = r.u16();
    const uint16_t faceCount = r.u16();
    for (uint16_t i = 0; i < kindCount && !r.failed(); i++) {
        GphTriggerKind k;
        k.name = r.str();
        k.flags = r.u32();
        kinds.push_back(std::move(k));
    }
    points.resize(pointCount);
    for (auto &p : points)
        r.floats(p);
    for (uint16_t i = 0; i < edgeCount && !r.failed(); i++) {
        GphEdge e;
        e.flags = r.u16();
        e.from = r.u16();
        e.to = r.u16();
        for (uint16_t &v : e.first)
            v = r.u16();
        e.twoFaces = (e.flags & kTwoFaces) != 0;
        if (e.twoFaces)
            for (uint16_t &v : e.second)
                v = r.u16();
        r.floats(e.direction);
        e.length = r.f32();
        if (e.from >= pointCount || e.to >= pointCount || e.first[0] >= faceCount)
            return Error::fail("edge " + std::to_string(i) + " past the points or faces");
        edges.push_back(e);
    }
    for (uint16_t i = 0; i < faceCount && !r.failed(); i++) {
        GphFace f;
        const uint8_t n = r.u8();
        for (uint8_t k = 0; k < n; k++) {
            f.edges.push_back(r.u16());
            if (f.edges.back() >= edgeCount)
                return Error::fail("face " + std::to_string(i) + " has an edge past the edges");
        }
        for (uint8_t &u : f.unknown)
            u = r.u8();
        f.name = r.str();
        r.floats(f.plane);
        f.edgePlanes.resize(n);
        for (auto &p : f.edgePlanes)
            r.floats(p);
        r.floats(f.center);
        f.radius = r.f32();
        const uint8_t triggers = r.u8();
        for (uint8_t t = 0; t < triggers; t++) {
            GphTrigger tr;
            tr.kind = r.u16();
            tr.value = r.u32();
            tr.along = r.f32();
            f.triggers.push_back(tr);
        }
        if (triggers)
            r.floats(f.triggerDirection);
        faces.push_back(std::move(f));
    }
    if (r.failed())
        return Error::fail("cut short");
    if (r.pos() != data.size())
        return Error::fail(std::to_string(data.size() - r.pos()) + " bytes after the last face");
    return Error::success();
}

} // namespace reluva::legoisland
