// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What Star Trek: Away Team's tests make of the game's formats, to read.

#pragma once

#include "common/archive.h"

#include <cstdint>
#include <map>
#include <cstring>
#include <string>
#include <vector>

namespace reluva::stawayteam {

inline void put16(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
inline void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, x & 0xffff);
    put16(v, x >> 16);
}

// A frame as the game stores it: the header, then each block that has
// rows, then where each row starts in it. A run-length coded block begins
// with its own size, so its rows start at 4; a raw one's at 0.
inline std::vector<uint8_t> frameFile(int w, int h, int originX, int originY, uint32_t flags,
                                      const std::vector<std::vector<std::vector<uint8_t>>> &blocks, bool raw = false) {
    std::vector<uint8_t> out = {0x2e, 16};
    put16(out, uint16_t(originX));
    put16(out, uint16_t(originY));
    put16(out, uint16_t(w));
    put16(out, uint16_t(h));
    put16(out, 0);
    put32(out, flags);
    std::vector<std::vector<uint8_t>> data;
    std::vector<std::vector<uint32_t>> starts;
    for (size_t b = 0; b < 3; ++b) {
        std::vector<uint8_t> block;
        std::vector<uint32_t> rows;
        if (b < blocks.size() && !blocks[b].empty()) {
            if (!raw)
                put32(block, 0);
            for (const auto &row : blocks[b]) {
                rows.push_back(uint32_t(block.size()));
                block.insert(block.end(), row.begin(), row.end());
            }
            if (!raw) {
                const auto size = uint32_t(block.size());
                for (int i = 0; i < 4; ++i)
                    block[size_t(i)] = uint8_t(size >> (8 * i));
            }
        }
        put32(out, uint32_t(block.size()));
        data.push_back(block);
        starts.push_back(rows);
    }
    for (size_t b = 0; b < 3; ++b) {
        if (data[b].empty())
            continue;
        out.insert(out.end(), data[b].begin(), data[b].end());
        for (uint32_t r : starts[b])
            put32(out, r);
    }
    return out;
}

/** Bytes as the game's binary writer writes them (formats/binary_object.h). */
struct BinaryWriter {
    std::vector<uint8_t> out;

    BinaryWriter &i32(int32_t v) {
        put32(out, uint32_t(v));
        return *this;
    }
    BinaryWriter &f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        put32(out, bits);
        return *this;
    }
    BinaryWriter &u8(uint8_t v) {
        out.push_back(v);
        return *this;
    }
    BinaryWriter &str(const std::string &s) {
        i32(int32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
        return *this;
    }
    BinaryWriter &bytes(const std::vector<uint8_t> &v) {
        out.insert(out.end(), v.begin(), v.end());
        return *this;
    }
    /** An object's start: the format's version, then its class's. */
    BinaryWriter &object(float classVersion) { return f32(26.0f).f32(classVersion); }
};

/** CSequence's ten properties (version 586), with `frames` frames. */
inline void sequenceProperties(BinaryWriter &w, int frames, float fps) {
    w.i32(frames).f32(fps).u8(1).str("Transparent Mask").u8(0).u8(0).u8(0).u8(0).i32(0).i32(0);
}

/** A sequence's own data: its bounds, then each frame there, with its byte. */
inline void sequenceFrames(BinaryWriter &w, const std::vector<std::vector<uint8_t>> &frames) {
    w.i32(-4).i32(-4).i32(4).i32(4);
    for (const auto &f : frames)
        w.u8(1).bytes(f);
}

/** A raw frame of `w` x `h` pixels of one colour, its origin at its middle. */
inline std::vector<uint8_t> solidFrame(int w, int h, uint16_t colour) {
    std::vector<std::vector<uint8_t>> rows(static_cast<size_t>(h));
    for (auto &row : rows)
        for (int x = 0; x < w; ++x) {
            row.push_back(uint8_t(colour));
            row.push_back(uint8_t(colour >> 8));
        }
    return frameFile(w, h, w / 2, h / 2, 0x20, {rows}, true);
}

/** A .seq16: a CSequence of these frames. */
inline std::vector<uint8_t> sequenceFile(const std::vector<std::vector<uint8_t>> &frames, float fps) {
    BinaryWriter w;
    w.object(586.0f);
    sequenceProperties(w, int(frames.size()), fps);
    sequenceFrames(w, frames);
    return w.out;
}

/** A font's .seq16: a CFont (version 30079) of characters from `first`. */
inline std::vector<uint8_t> fontFile(const std::vector<std::vector<uint8_t>> &glyphs, int first) {
    BinaryWriter w;
    w.object(30079.0f);
    sequenceProperties(w, int(glyphs.size()), 30.0f);
    w.i32(11).i32(first).i32(first + int(glyphs.size()) - 1).u8(0);
    sequenceFrames(w, glyphs);
    return w.out;
}

/**
 * A .mdl16: a CModel (version 1603) of its thirty properties, with one
 * rotation sequence, "Idle", of `rotations` directions of `frames` frames
 * each (CRotationSequence, version 30575: CSequence's properties, then its
 * own fourteen); its Hit Points `hitPoints`, its Bounds Poly `footprint`,
 * a thrown thing's Explosion Radial Damage `explosionRadius` (needing a
 * clear line, with `needsSight`) and Max Velocity `maxVelocity`.
 */
inline std::vector<uint8_t> modelFile(const std::string &name, int rotations, int frames, float hitPoints = 100.0f,
                                      const std::vector<std::pair<float, float>> &footprint = {},
                                      float explosionRadius = 0.0f, bool needsSight = false, float maxVelocity = 0.0f) {
    BinaryWriter w;
    w.object(1603.0f);
    w.str(name).str("").str("Model Descriptions").str("A test model").str("").i32(0).str("");
    w.f32(hitPoints).f32(0.0f).str("").f32(0.0f).f32(explosionRadius).u8(needsSight ? 1 : 0).i32(0).str("Idle");
    for (int i = 0; i < 6; ++i)
        w.str("");
    w.f32(maxVelocity);
    for (int i = 0; i < 5; ++i)
        w.f32(0.0f);
    w.bytes({'P', 'O', 'L', 'Y', '2', 'D', 0, 7}).i32(int32_t(footprint.size()));
    for (const auto &[x, y] : footprint)
        w.f32(x).f32(y);
    w.str("").str("");
    w.i32(1).str("").u8(1).str("Idle").u8(0);
    const int count = rotations * frames;
    w.object(30575.0f);
    sequenceProperties(w, count, 10.0f);
    w.i32(rotations).i32(0).i32(0).u8(0).u8(0).u8(0).u8(0).u8(0).i32(0).f32(0.0f).f32(6.2831855f).i32(0);
    w.i32(0).i32(0);
    std::vector<std::vector<uint8_t>> each;
    for (int i = 0; i < count; ++i)
        each.push_back(solidFrame(4 + i, 4, uint16_t(0x07e0 + i)));
    sequenceFrames(w, each);
    for (int i = 0; i < count; ++i)
        w.i32(0).i32(0);
    return w.out;
}

// Files in memory, by name as given.
class MemoryArchive : public Archive {
public:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files;

    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto &[name, data] : files)
            names.push_back(name);
        return names;
    }
    bool has(std::string_view name) const override { return files.find(name) != files.end(); }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override {
        const auto it = files.find(name);
        if (it == files.end())
            return Error::fail(std::string(name) + " isn't there");
        out = it->second;
        return Error::success();
    }
    std::string describe() const override { return "memory"; }
};

} // namespace reluva::stawayteam
