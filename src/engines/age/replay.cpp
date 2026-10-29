// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The replay file: "RLVRPLY", a version byte, the start (strings as a
// length byte and their bytes, numbers little-endian), the frames, the
// marks.

#include "engines/age/replay.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace reluva::age {

namespace {

constexpr char kMagic[] = "RLVRPLY";
constexpr uint8_t kVersion = 1;

class Out {
public:
    void u8(uint8_t v) { bytes.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void f32(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void str(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8(uint8_t(n));
        bytes.insert(bytes.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
    std::vector<uint8_t> bytes;
};

class In {
public:
    explicit In(const std::vector<uint8_t>& d) : data(d) {}
    uint8_t u8() {
        if (at >= data.size()) {
            ok = false;
            return 0;
        }
        return data[at++];
    }
    uint16_t u16() { return uint16_t(u8() | u8() << 8); }
    uint32_t u32() { return uint32_t(u16()) | uint32_t(u16()) << 16; }
    float f32() {
        const uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    std::string str() {
        const size_t n = u8();
        if (at + n > data.size()) {
            ok = false;
            return {};
        }
        std::string s(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
    const std::vector<uint8_t>& data;
    size_t at = 0;
    bool ok = true;
};

} // namespace

float Replay::duration() const {
    double t = 0;
    for (const ReplayFrame& f : frames)
        t += f.dt;
    return float(t);
}

std::vector<uint8_t> encodeReplay(const Replay& r) {
    Out o;
    for (const char* c = kMagic; *c; ++c)
        o.u8(uint8_t(*c));
    o.u8(kVersion);
    o.str(r.name);
    o.str(r.city);
    o.u8(uint8_t(r.kind));
    o.u16(uint16_t(r.race));
    o.u8(uint8_t(r.level));
    o.str(r.car);
    o.u8(uint8_t(r.paint));
    o.u8(r.autoReverse ? 1 : 0);
    o.f32(r.steerRate);
    o.f32(r.pedestrians);
    o.u32(uint32_t(r.frames.size()));
    for (const ReplayFrame& f : r.frames) {
        o.f32(f.dt);
        o.u16(f.keys);
        o.u8(uint8_t(f.shift));
        o.u8(f.flags);
    }
    o.u32(uint32_t(r.marks.size()));
    for (const Vec3& m : r.marks)
        for (float v : m)
            o.f32(v);
    return o.bytes;
}

Error decodeReplay(const std::vector<uint8_t>& data, Replay& out) {
    In in(data);
    for (const char* c = kMagic; *c; ++c)
        if (in.u8() != uint8_t(*c))
            return Error::fail("not a Reluva replay");
    if (in.u8() != kVersion)
        return Error::fail("a replay of another version");
    Replay r;
    r.name = in.str();
    r.city = in.str();
    r.kind = in.u8();
    r.race = in.u16();
    r.level = in.u8();
    r.car = in.str();
    r.paint = in.u8();
    r.autoReverse = in.u8() != 0;
    r.steerRate = in.f32();
    r.pedestrians = in.f32();
    const uint32_t frames = in.u32();
    if (!in.ok || size_t(frames) * 8 > data.size())
        return Error::fail("a replay cut short");
    r.frames.resize(frames);
    for (ReplayFrame& f : r.frames) {
        f.dt = in.f32();
        f.keys = in.u16();
        f.shift = int8_t(in.u8());
        f.flags = in.u8();
    }
    const uint32_t marks = in.u32();
    if (!in.ok || size_t(marks) * 12 > data.size())
        return Error::fail("a replay cut short");
    r.marks.resize(marks);
    for (Vec3& m : r.marks)
        for (float& v : m)
            v = in.f32();
    if (!in.ok)
        return Error::fail("a replay cut short");
    out = std::move(r);
    return Error::success();
}

std::filesystem::path replayFolder(const std::filesystem::path& saveFolder) { return saveFolder / "replays"; }

std::string replayFileName(const std::string& name) {
    std::string out;
    for (char c : name)
        out += std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' ? c : '_';
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return (out.empty() ? std::string("replay") : out) + ".rpl";
}

Error saveReplay(const std::filesystem::path& saveFolder, const Replay& replay) {
    if (saveFolder.empty())
        return Error::fail("no save folder");
    std::error_code ec;
    std::filesystem::create_directories(replayFolder(saveFolder), ec);
    const std::filesystem::path file = replayFolder(saveFolder) / replayFileName(replay.name);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::vector<uint8_t> bytes = encodeReplay(replay);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return out ? Error::success() : Error::fail("can't write " + file.string());
}

Error loadReplay(const std::filesystem::path& file, Replay& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return Error::fail("can't read " + file.string());
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeReplay(data, out);
}

std::vector<ReplayEntry> listReplays(const std::filesystem::path& saveFolder) {
    std::vector<ReplayEntry> out;
    std::error_code ec;
    if (saveFolder.empty())
        return out;
    for (const auto& e : std::filesystem::directory_iterator(replayFolder(saveFolder), ec)) {
        if (e.path().extension() != ".rpl")
            continue;
        Replay r;
        if (loadReplay(e.path(), r))
            continue;
        out.push_back({e.path(), r.name, r.city, r.kind, r.race, r.duration()});
    }
    std::sort(out.begin(), out.end(), [](const ReplayEntry& a, const ReplayEntry& b) { return a.name < b.name; });
    return out;
}

Error renameReplay(const std::filesystem::path& saveFolder, const ReplayEntry& entry, const std::string& name) {
    Replay r;
    if (Error err = loadReplay(entry.file, r))
        return err;
    r.name = name;
    if (Error err = saveReplay(saveFolder, r))
        return err;
    if (replayFolder(saveFolder) / replayFileName(name) != entry.file) {
        std::error_code ec;
        std::filesystem::remove(entry.file, ec);
    }
    return Error::success();
}

Error deleteReplay(const ReplayEntry& entry) {
    std::error_code ec;
    return std::filesystem::remove(entry.file, ec) ? Error::success()
                                                   : Error::fail("can't delete " + entry.file.string());
}

} // namespace reluva::age
