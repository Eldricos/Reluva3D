// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the stharbinger tests share: a System that draws nothing and plays
// nothing, and the game's files built in code (archives, scripts, name
// lists, animations), never the game's own.

#pragma once

#include "common/system.h"
#include "engines/stharbinger/formats/script.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace reluva::stharbinger::testing {

// A System with no window and no sound, its clock moved only by delays.
class NullSystem : public System {
public:
    Error initGraphics(int width, int height, PixelFormat) override {
        w = width;
        h = height;
        return Error::success();
    }
    void setPalette(const Color*, int, int) override {}
    void updateScreen(const Surface&) override { ++frames; }
    void setWindowTitle(const std::string& t) override { title = t; }
    bool pollEvent(Event&) override { return false; }
    uint64_t getMillis() const override { return millis; }
    void delayMillis(uint32_t ms) override { millis += ms; }
    Error startAudio(AudioCallback) override { return Error::fail("no sound in tests"); }
    void stopAudio() override {}
    int audioRate() const override { return 22050; }

    int w = 0, h = 0, frames = 0;
    uint64_t millis = 0;
    std::string title;
};

// The game in `dir`, as the engine runs it.
inline Options optionsFor(const std::filesystem::path& dir) {
    Options o;
    o.paths.push_back(dir.string());
    return o;
}

inline void putU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++)
        v.push_back(uint8_t(x >> (8 * i)));
}

inline void putFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
}

inline std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// Builds a directive list; unused slots are padded with type 00.
struct ScriptBuilder {
    std::vector<uint8_t> body;
    uint16_t count = 0;

    // Arguments are (type, value) pairs.
    ScriptBuilder& cmd(uint8_t command, std::vector<std::pair<uint8_t, uint16_t>> args = {}) {
        body.push_back(command);
        int argc = getArgCount(command);
        for (int slot = 0; slot < argc; slot++) {
            if (slot < int(args.size())) {
                uint8_t type = args[slot].first;
                body.push_back(type);
                int n = getArgSize(type);
                for (int i = 0; i < n; i++)
                    body.push_back(uint8_t(args[slot].second >> (8 * i)));
            } else {
                body.push_back(0);
            }
        }
        count++;
        return *this;
    }
    std::vector<uint8_t> build() const {
        std::vector<uint8_t> out = {uint8_t(count), uint8_t(count >> 8)};
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
};

inline std::pair<uint8_t, uint16_t> var(uint16_t n) { return {kArgSymbol, n}; }
inline std::pair<uint8_t, uint16_t> sym(uint16_t n) { return {kArgSymbol, n}; }
inline std::pair<uint8_t, uint16_t> num(uint16_t n) { return {kArgConst, n}; }

inline std::vector<uint8_t> nameList(const std::vector<std::string>& names, const std::string& source) {
    std::vector<uint8_t> out;
    for (const std::string& n : names) {
        out.insert(out.end(), n.begin(), n.end());
        out.push_back(0);
        out.insert(out.end(), source.begin(), source.end());
        out.push_back(0);
        out.push_back(0);
    }
    return out;
}

// Writes NAME.ALL and NAME.LST with one entry per scene slot.
inline void writeArchive(const std::filesystem::path& dir, const std::string& name,
                         const std::vector<std::vector<uint8_t>>& entries) {
    std::vector<uint8_t> all, lst;
    for (const auto& e : entries) {
        putU32(lst, uint32_t(all.size()));
        all.insert(all.end(), e.begin(), e.end());
    }
    putU32(lst, uint32_t(all.size()));
    putFile(dir / (name + ".ALL"), all);
    putFile(dir / (name + ".LST"), lst);
}

// A 3-frame 1x1 animation.
inline std::vector<uint8_t> makeRle() {
    std::vector<uint8_t> file = {0x00, 0x00, 0x01, 0x01, 0x0A, 0xFF, 0x00, 0x00, 0x03, 0x00};
    for (uint8_t color = 1; color <= 3; color++) {
        std::vector<uint8_t> hdr(25, 0), body = {0x40, 0x01, color};
        hdr[1] = 1;
        hdr[3] = 1;
        hdr[21] = uint8_t(body.size());
        file.insert(file.end(), hdr.begin(), hdr.end());
        file.insert(file.end(), body.begin(), body.end());
    }
    return file;
}

} // namespace reluva::stharbinger::testing
