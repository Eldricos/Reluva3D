// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's pictures: EA's
// banks (.fsh) and the plain ones (.tga, .png, .jpg).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/sprites.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/loading_screen.h"
#include "image/fsh.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// A picture of the game's decoded: an .fsh bank's first shape (each bank
// here holds one), or a plain picture.
reluva::Error decodePicture(const std::string &name, const std::vector<uint8_t> &data, reluva::image::Image &out,
                            std::string &kind) {
    if (reluva::fs::toLower(extensionOf(name)) != ".fsh") {
        kind = reluva::fs::toLower(extensionOf(name));
        return reluva::image::decode(data, out);
    }
    reluva::image::ShapeBank bank;
    if (reluva::Error err = reluva::image::parseShapeBank(data, bank, reluva::tpw::kShapeOptions))
        return err;
    if (bank.shapes.empty())
        return reluva::Error::fail("no shapes");
    const reluva::image::Shape &shape = bank.shapes[0];
    kind = std::string(".fsh ") + reluva::image::shapeTypeName(shape.type) + (shape.compressed ? ", compressed" : "");
    return reluva::image::decodeShape(shape, out);
}

// Every picture of a game decoded: how many of each kind, with a checksum.
int cmdPictureCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw picture-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::map<std::string, int> kinds;
    reluva::Md5 all;
    int failed = 0;
    int read = 0;
    for (const std::string &name : files.list()) {
        const std::string ext = reluva::fs::toLower(extensionOf(name));
        if (ext != ".fsh" && ext != ".tga" && ext != ".png" && ext != ".jpg")
            continue;
        std::vector<uint8_t> data;
        reluva::image::Image img;
        std::string kind;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = decodePicture(name, data, img, kind);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        ++kinds[kind];
        const int size[] = {img.w, img.h};
        all.update(size, sizeof(size));
        all.update(img.rgba.data(), img.rgba.size());
    }
    for (const auto &[kind, n] : kinds)
        std::printf("  %-28s %d\n", kind.c_str(), n);
    std::printf("%d pictures decoded, %d failed; md5 %s\n", read, failed, all.hexDigest().c_str());
    return failed ? 1 : 0;
}

// One picture of a game written out as a TGA, to look at.
int cmdPictureDump(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw picture-dump <game folder> <name in Data> <out.tga>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    std::vector<uint8_t> data;
    reluva::image::Image img;
    std::string kind;
    reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings);
    if (!err)
        err = files.read(argv[3], data);
    if (!err)
        err = decodePicture(argv[3], data, img, kind);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::vector<uint8_t> tga = reluva::image::encodeTga(img, true);
    std::ofstream(argv[4], std::ios::binary)
        .write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
    std::printf("%s: %s, %dx%d\n", argv[3], kind.c_str(), img.w, img.h);
    return 0;
}

// The loading screen, put together as the game shows it, with a checksum
// of it; written out as a PNG if asked.
int cmdLoadingScreen(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw loading-screen <game folder> [out.png]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::image::Image screen;
    if (reluva::Error err = reluva::tpw::composeLoadingScreen(files, reluva::tpw::languageOf(files), screen)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (argc > 3) {
        const std::vector<uint8_t> png = reluva::image::encodePng(screen);
        std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char *>(png.data()), long(png.size()));
    }
    reluva::Md5 md5;
    md5.update(screen.rgba.data(), screen.rgba.size());
    std::printf("%dx%d, md5 %s\n", screen.w, screen.h, md5.hexDigest().c_str());
    return 0;
}

// Every sprite sheet (.TPC, .FPC) and animation set (.ESP) of a game
// read: their frames, and each set's animations within its sheets' frames.
int cmdSpriteCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw sprite-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::Md5 all;
    int sheets = 0;
    int sets = 0;
    int failed = 0;
    int outside = 0;
    size_t frames = 0;
    std::map<std::string, size_t> framesOf; // by the file's name without its ending
    std::vector<std::pair<std::string, reluva::tpw::SpriteSet>> animations;
    for (const std::string &name : files.list()) {
        const std::string ext = reluva::fs::toLower(extensionOf(name));
        if (ext != ".tpc" && ext != ".fpc" && ext != ".esp")
            continue;
        std::vector<uint8_t> data;
        reluva::Error err = files.read(name, data);
        const std::string stem = reluva::fs::toLower(name.substr(0, name.size() - ext.size()));
        if (ext == ".esp") {
            reluva::tpw::SpriteSet set;
            if (!err)
                err = set.parse(data);
            if (!err) {
                ++sets;
                animations.emplace_back(stem, set);
            }
        } else {
            reluva::tpw::SpriteSheet sheet;
            if (!err)
                err = sheet.parse(data);
            if (!err) {
                ++sheets;
                frames += sheet.frames().size();
                // A set plays either sheet: the two must have the same frames.
                if (ext == ".tpc" || !framesOf.count(stem))
                    framesOf[stem] = sheet.frames().size();
                for (const auto &f : sheet.frames()) {
                    all.update(f.pixels.data(), f.pixels.size());
                    if (f.scaleX != 128 || f.scaleY != 128)
                        std::printf("%s: a frame scaled %d, %d\n", name.c_str(), f.scaleX, f.scaleY);
                }
            }
        }
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
        }
    }
    for (const auto &[stem, set] : animations)
        for (const auto &a : set.animations()) {
            const size_t end = size_t(a.first) + size_t(a.frames) * size_t(std::max(1, a.directions));
            if (a.frames && framesOf.count(stem) && end > framesOf[stem]) {
                std::printf("%s: an animation past the sheet's %zu frames: %d + %d x %d\n", stem.c_str(),
                            framesOf[stem], a.first, a.frames, a.directions);
                ++outside;
            }
        }
    std::printf("%d sheets, %d animation sets, %d failed; %zu frames; %d animations past their frames; md5 %s\n",
                sheets, sets, failed, frames, outside, all.hexDigest().c_str());
    return failed || sheets == 0 ? 1 : 0;
}

const Commands kCommands(
    "tpw",
    {
        {"picture-check", "<game folder>", "decode every picture: .fsh banks, .tga, .png and .jpg", cmdPictureCheck},
        {"picture-dump", "<game folder> <name in Data> <out.tga>", "write one picture out as a TGA", cmdPictureDump},
        {"sprite-check", "<game folder>", "read every sprite sheet (.TPC, .FPC) and its animations (.ESP)",
         cmdSpriteCheck},
        {"loading-screen", "<game folder> [out.png]",
         "put the loading screen together as the game shows it; write it out", cmdLoadingScreen},
    });

} // namespace
