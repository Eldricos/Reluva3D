// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the stharbinger tests share: a System that draws nothing and plays
// nothing, and the game's files built in code (archives, scripts, name
// lists, animations), never the game's own.

#pragma once

#include "common/headless_system.h"
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

/** The tests' System: no window, no sound, a clock moved only by delays. */
using NullSystem = HeadlessSystem;

/** The options to run the game in `dir`. */
inline Options optionsFor(const std::filesystem::path &dir) {
    Options o;
    o.paths.push_back(dir.string());
    return o;
}

/** Appends a value, little-endian. */
inline void putU32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++)
        v.push_back(uint8_t(x >> (8 * i)));
}

/** Writes the bytes to a file, making its folders. */
inline void putFile(const std::filesystem::path &path, const std::vector<uint8_t> &data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), data.size());
}

/** A string's bytes, without a terminating 0. */
inline std::vector<uint8_t> bytes(const std::string &s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

/** Builds a directive list; unused argument slots are padded with type 00. */
struct ScriptBuilder {
    std::vector<uint8_t> body;
    uint16_t count = 0;

    /** Adds a directive; its arguments are (type, value) pairs, as var(), sym() and num() make. */
    ScriptBuilder &cmd(uint8_t command, std::vector<std::pair<uint8_t, uint16_t>> args = {}) {
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
    /** The list: the directives' 16-bit count, then the directives. */
    std::vector<uint8_t> build() const {
        std::vector<uint8_t> out = {uint8_t(count), uint8_t(count >> 8)};
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
};

/** A directive's argument: a variable or a symbol (both kArgSymbol), or a constant. */
///@{
inline std::pair<uint8_t, uint16_t> var(uint16_t n) {
    return {kArgSymbol, n};
}
inline std::pair<uint8_t, uint16_t> sym(uint16_t n) {
    return {kArgSymbol, n};
}
inline std::pair<uint8_t, uint16_t> num(uint16_t n) {
    return {kArgConst, n};
}
///@}

/** A name list: each name, then `source`, each ending with a 0, and one more 0. */
inline std::vector<uint8_t> nameList(const std::vector<std::string> &names, const std::string &source) {
    std::vector<uint8_t> out;
    for (const std::string &n : names) {
        out.insert(out.end(), n.begin(), n.end());
        out.push_back(0);
        out.insert(out.end(), source.begin(), source.end());
        out.push_back(0);
        out.push_back(0);
    }
    return out;
}

/**
 * Writes `<name>`.ALL and `<name>`.LST with one entry per scene slot: the
 * entries one after another, and their offsets with the end's after them.
 */
inline void writeArchive(const std::filesystem::path &dir, const std::string &name,
                         const std::vector<std::vector<uint8_t>> &entries) {
    std::vector<uint8_t> all;
    std::vector<uint8_t> lst;
    for (const auto &e : entries) {
        putU32(lst, uint32_t(all.size()));
        all.insert(all.end(), e.begin(), e.end());
    }
    putU32(lst, uint32_t(all.size()));
    putFile(dir / (name + ".ALL"), all);
    putFile(dir / (name + ".LST"), lst);
}

/** A 3-frame 1x1 animation, its pixel colour 1, 2 then 3. */
inline std::vector<uint8_t> makeRle() {
    std::vector<uint8_t> file = {0x00, 0x00, 0x01, 0x01, 0x0A, 0xFF, 0x00, 0x00, 0x03, 0x00};
    for (uint8_t color = 1; color <= 3; color++) {
        std::vector<uint8_t> hdr(25, 0);
        std::vector<uint8_t> body = {0x40, 0x01, color};
        hdr[1] = 1;
        hdr[3] = 1;
        hdr[21] = uint8_t(body.size());
        file.insert(file.end(), hdr.begin(), hdr.end());
        file.insert(file.end(), body.begin(), body.end());
    }
    return file;
}

} // namespace reluva::stharbinger::testing
