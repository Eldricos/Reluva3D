// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Clue commands for its files: what a .res, .a, .o or .lvl
// file holds, its images written out, and every file of a game read
// (docs/engines/clue/). Listed by reluva-tool help clue.

#include "common/fs.h"
#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/resi.h"
#include "engines/clue/resources.h"
#include "tools/tool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

int cmdClueList(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool clue list <file.res | file.a | file.o | file.lvl>\n");
        return 2;
    }
    reluva::clue::DataDrive drive;
    if (reluva::Error err = drive.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const auto& e : drive.entries()) {
        const std::vector<uint8_t> bytes = drive.read(e);
        std::string tag;
        for (size_t i = 0; i < 4 && i < bytes.size(); ++i)
            tag += std::isprint(bytes[i]) ? char(bytes[i]) : '.';
        std::printf("%8u %8u  %-4s  %s\n", e.offset, e.size, tag.c_str(), e.name.c_str());
    }
    std::printf("%zu entries\n", drive.entries().size());
    return 0;
}

int cmdClueExtract(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool clue extract <file.res> <output dir> [name filter]\n");
        return 2;
    }
    reluva::clue::DataDrive drive;
    if (reluva::Error err = drive.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path dir = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string filter = argc > 4 ? reluva::fs::toLower(argv[4]) : "";
    int written = 0, failed = 0;
    for (const auto& e : drive.entries()) {
        if (!filter.empty() && reluva::fs::toLower(e.name).find(filter) == std::string::npos)
            continue;
        reluva::clue::Image img;
        if (reluva::Error err = reluva::clue::decodeImage(drive.read(e), img)) {
            std::fprintf(stderr, "%s: %s\n", e.name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        const std::vector<uint8_t> tga = reluva::image::encodeTga(img.pixels, true);
        std::ofstream f(dir / (e.name + ".tga"), std::ios::binary);
        f.write(reinterpret_cast<const char*>(tga.data()), std::streamsize(tga.size()));
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", (dir / (e.name + ".tga")).string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d image(s) written to %s, %d failed\n", written, dir.string().c_str(), failed);
    return failed ? 1 : 0;
}

// Opens a Clue install or CD as the engine does and decodes everything:
// every .res file's images, every .a file's animations and the images
// they name.
int cmdClueCheck(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool clue check <game folder> [-uk]\n");
        return 2;
    }
    const bool uk = argc > 3 && std::strcmp(argv[3], "-uk") == 0;
    reluva::clue::GameData data;
    if (reluva::Error err = data.open(argv[2], uk ? reluva::clue::Language::UK : reluva::clue::Language::US)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("%zu files in %zu folders\n", data.files().size(), data.folders().size());
    int images = 0, imageFails = 0, anims = 0, animFails = 0, refs = 0, refFails = 0;
    std::map<int, int> versions;
    for (const auto& path : data.withExtension(".res")) {
        const std::string drive = path.stem().string();
        const reluva::clue::DataDrive* d = data.drive(drive, 'I');
        if (!d) {
            std::printf("  %s\n", data.lastError().c_str()); // it names the file
            continue;
        }
        for (const auto& e : d->entries()) {
            ++images;
            const reluva::clue::Image* img = data.image(drive + ":" + e.name);
            if (!img) {
                ++imageFails;
                std::printf("  %s\n", data.lastError().c_str());
            } else {
                ++versions[img->version];
            }
        }
    }
    for (const auto& path : data.withExtension(".a")) {
        const std::string drive = path.stem().string();
        const reluva::clue::DataDrive* d = data.drive(drive, 'A');
        if (!d) {
            std::printf("  %s\n", data.lastError().c_str()); // it names the file
            continue;
        }
        for (const auto& e : d->entries()) {
            ++anims;
            const reluva::clue::Animation* a = data.animation(drive + ":" + e.name);
            if (!a) {
                ++animFails;
                std::printf("  %s\n", data.lastError().c_str());
                continue;
            }
            for (const std::string& name : a->images) {
                ++refs;
                if (!data.image(name)) {
                    ++refFails;
                    std::printf("  %s:%s names %s: %s\n", drive.c_str(), e.name.c_str(), name.c_str(),
                                data.lastError().c_str());
                }
            }
        }
    }
    int objects = 0, objectFails = 0;
    for (const auto& path : data.withExtension(".o")) {
        const std::string drive = path.stem().string();
        const reluva::clue::DataDrive* d = data.drive(drive, 'O');
        if (!d) {
            std::printf("  %s\n", data.lastError().c_str()); // it names the file
            continue;
        }
        for (const auto& e : d->entries()) {
            ++objects;
            if (!data.object(drive + ":" + e.name)) {
                ++objectFails;
                std::printf("  %s\n", data.lastError().c_str());
            }
        }
    }
    int placements = 0, buildProblems = 0;
    bool levelOk = false;
    if (const std::filesystem::path* lvl = data.find("clue.lvl")) {
        reluva::clue::DataDrive file;
        reluva::clue::Level level;
        reluva::clue::Mansion mansion;
        if (reluva::Error err = file.open(*lvl))
            std::printf("  %s\n", err.message.c_str());
        else if (reluva::Error err2 = reluva::clue::parseLevel(file, level))
            std::printf("  clue.lvl: %s\n", err2.message.c_str());
        else if (reluva::Error err3 = mansion.build(data, level))
            std::printf("  clue.lvl: %s\n", err3.message.c_str());
        else {
            levelOk = true;
            placements = int(level.placements.size());
            buildProblems = int(mansion.problems().size());
            for (const std::string& p : mansion.problems())
                std::printf("  clue.lvl: %s\n", p.c_str());
        }
    } else {
        std::printf("  no clue.lvl\n");
    }
    // dialogInfo.res: every font, background and control picture it names.
    int dialogs = 0, controls = 0, missing = 0;
    bool resiOk = false;
    if (const std::filesystem::path* path = data.find("dialogInfo.res")) {
        std::ifstream in(*path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        reluva::clue::Resi resi;
        if (reluva::Error err = reluva::clue::parseResi(bytes, resi)) {
            std::printf("  dialogInfo.res: %s\n", err.message.c_str());
        } else {
            resiOk = true;
            const auto check = [&](const std::string& name, const std::string& where) {
                if (name.empty() || name.find(':') == std::string::npos)
                    return;
                const size_t colon = name.find(':');
                const bool isImage = colon + 1 < name.size() && (name[colon + 1] == 'I' || name[colon + 1] == 'i');
                if (isImage ? !data.image(name) : !data.animation(name)) {
                    ++missing;
                    std::printf("  dialogInfo.res: %s names %s: %s\n", where.c_str(), name.c_str(),
                                data.lastError().c_str());
                }
            };
            for (const auto& f : resi.fonts)
                check(f.animation, "font " + std::to_string(f.id));
            for (const auto& d : resi.dialogs) {
                ++dialogs;
                check(d.background, "dialog " + std::to_string(d.id));
                for (const auto& c : d.controls) {
                    ++controls;
                    const bool picture = c.type == reluva::clue::ResiControl::Image ||
                                         c.type == reluva::clue::ResiControl::ImageButton ||
                                         c.type == reluva::clue::ResiControl::Slider ||
                                         c.type == reluva::clue::ResiControl::ScrollKnob;
                    check(picture ? c.text : c.animation,
                          "dialog " + std::to_string(d.id) + " control " + std::to_string(c.id));
                }
            }
        }
    }
    std::printf("images: %d, %d failed (version 0x100: %d, 0x200: %d)\n", images, imageFails, versions[0x100],
                versions[0x200]);
    std::printf("animations: %d, %d failed; images they name: %d, %d missing or failed\n", anims, animFails, refs,
                refFails);
    std::printf("objects: %d, %d failed\n", objects, objectFails);
    std::printf("clue.lvl: %s, %d placements, %d couldn't be built\n", levelOk ? "read" : "FAILED", placements,
                buildProblems);
    std::printf("dialogInfo.res: %s, %d dialogs, %d controls; pictures they name: %d missing or failed\n",
                resiOk ? "read" : "FAILED", dialogs, controls, missing);
    return imageFails || animFails || refFails || objectFails || !levelOk || buildProblems || !resiOk || missing ? 1
                                                                                                               : 0;
}

// A level's object names and placements: a summary, or each one with -v.

const Commands commands("clue", {
    {"list", "<file.res | file.a | file.o | file.lvl>", "list a Clue .res, .a, .o or .lvl file's entries", cmdClueList},
    {"extract", "<file.res> <output dir> [name filter]", "write a Clue .res file's images as 32-bit TGA files",
     cmdClueExtract},
    {"check", "<game folder> [-uk]",
     "decode every image, animation, object and level of a Clue install or CD, and build its mansion", cmdClueCheck},
});

} // namespace
