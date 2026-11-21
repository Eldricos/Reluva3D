// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The worlds' *INF.DTA files: docs/engines/legoisland/formats/dta.md.

#include "engines/legoisland/formats/dta.h"

#include "common/bytes.h"
#include "common/stream.h"

#include <cstring>

namespace reluva::legoisland {

namespace {

// Reads fields in order, failing once if any ran past the end.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _data(data) {}

    bool failed() const { return _failed; }
    size_t pos() const { return _pos; }

    uint8_t u8() {
        if (!fits(1))
            return 0;
        return _data[_pos++];
    }
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
    void floats(std::array<float, 3> &out) {
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

} // namespace

Error DtaFile::read(const std::filesystem::path &path) {
    std::vector<uint8_t> data;
    if (!readFile(path, data))
        return Error::fail("Can't read " + path.string());
    if (Error e = parse(data))
        return Error::fail(path.filename().string() + ": " + e.message);
    return Error::success();
}

Error DtaFile::parse(const std::vector<uint8_t> &data) {
    animations.clear();
    Reader r(data);
    version = r.u32();
    if (version != 3)
        return Error::fail("version " + std::to_string(version) + ", not 3");
    const uint16_t count = r.u16();
    for (uint16_t i = 0; i < count && !r.failed(); i++) {
        DtaAnimation a;
        a.name = r.str();
        a.id = r.u32();
        a.spot = r.u16();
        for (uint8_t &f : a.flags)
            f = r.u8();
        r.floats(a.center);
        a.radius = r.f32();
        const uint8_t models = r.u8();
        for (uint8_t m = 0; m < models && !r.failed(); m++) {
            DtaModel model;
            model.name = r.str();
            model.kind = r.u8();
            r.floats(model.location);
            r.floats(model.direction);
            r.floats(model.up);
            model.flag = r.u8();
            a.models.push_back(std::move(model));
        }
        animations.push_back(std::move(a));
    }
    if (r.failed())
        return Error::fail("cut short at animation " + std::to_string(animations.size()));
    if (r.pos() != data.size())
        return Error::fail(std::to_string(data.size() - r.pos()) + " bytes after the last animation");
    return Error::success();
}

} // namespace reluva::legoisland
