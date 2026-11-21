// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Away Team commands for the game's pictures in
// DATA.dat (formats/frame.h).

#include "devtools/tool.h"

#include "common/fs.h"
#include "common/zip.h"
#include "engines/stawayteam/formats/frame.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stawayteam;
using namespace reluva::tool;

bool isFrame(const std::string &name) {
    return fs::toLower(name).ends_with(".frm16");
}

bool openArchive(const char *path, ZipArchive &zip) {
    if (Error err = zip.open(path)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// Every .frm16 in DATA.dat decoded, with how many of each kind.
int cmdFrameCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam frame-check <DATA.dat>\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    std::map<std::string, int> kinds;
    int failed = 0;
    int others = 0;
    std::vector<uint8_t> data;
    for (const auto &entry : zip.entries()) {
        if (!isFrame(entry.name))
            continue;
        if (Error err = zip.read(entry, data)) {
            std::fprintf(stderr, "%s: %s\n", entry.name.c_str(), err.message.c_str());
            return 1;
        }
        // Not every .frm16 is one: Levels/Mission 16/16 Borg Rescue.frm16
        // starts "FRAME", a format the game's loader refuses, and nothing
        // names it.
        if (data.empty() || data[0] != 0x2e) {
            std::printf("not a frame: %s\n", entry.name.c_str());
            ++others;
            continue;
        }
        Frame frame;
        size_t used = 0;
        if (Error err = readFrame(data.data(), data.size(), frame, &used)) {
            std::printf("FAILED  %s: %s\n", entry.name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (used != data.size()) {
            std::printf("FAILED  %s: %zu bytes after the frame\n", entry.name.c_str(), data.size() - used);
            ++failed;
            continue;
        }
        bool blended = false;
        for (uint8_t a : frame.alpha)
            blended = blended || (a != 0 && a != 255);
        char kind[64];
        std::snprintf(kind, sizeof kind, "flags 0x%x%s", frame.flags, blended ? ", with blended pixels" : "");
        ++kinds[kind];
    }
    int frames = 0;
    for (const auto &[kind, count] : kinds) {
        std::printf("%6d  %s\n", count, kind.c_str());
        frames += count;
    }
    std::printf("%d frames read, %d not frames, %d failed\n", frames, others, failed);
    return failed ? 1 : 0;
}

// Frames written out as PNG files, their alpha kept.
int cmdFramePng(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam frame-png <DATA.dat> <output folder> [part of a name]\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    const std::filesystem::path out = argv[3];
    const std::string filter = argc > 4 ? fs::toLower(argv[4]) : "";
    int written = 0;
    std::vector<uint8_t> data;
    for (const auto &entry : zip.entries()) {
        if (!isFrame(entry.name) || fs::toLower(entry.name).find(filter) == std::string::npos)
            continue;
        Frame frame;
        if (Error err = zip.read(entry, data); err || readFrame(data.data(), data.size(), frame)) {
            std::printf("skipped %s: not a frame Reluva reads\n", entry.name.c_str());
            continue;
        }
        std::filesystem::path path = out / std::filesystem::path(entry.name).relative_path();
        path.replace_extension(".png");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        const std::vector<uint8_t> png = image::encodePng(frameImage(frame));
        std::ofstream file(path, std::ios::binary);
        if (!file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()))) {
            std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d pictures written to %s\n", written, out.string().c_str());
    return 0;
}

const Commands kCommands("stawayteam",
                         {
                             {"frame-check", "<DATA.dat>", "decode every .frm16 picture in DATA.dat", cmdFrameCheck},
                             {"frame-png", "<DATA.dat> <output folder> [part of a name]",
                              "write .frm16 pictures out as PNG files", cmdFramePng},
                         });

} // namespace
