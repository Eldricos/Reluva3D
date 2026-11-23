// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's pictures: EA's
// banks (.fsh) and the plain ones (.tga, .png, .jpg).

#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/loading_screen.h"
#include "image/fsh.h"
#include "image/image.h"

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

const Commands kCommands(
    "tpw",
    {
        {"picture-check", "<game folder>", "decode every picture: .fsh banks, .tga, .png and .jpg", cmdPictureCheck},
        {"picture-dump", "<game folder> <name in Data> <out.tga>", "write one picture out as a TGA", cmdPictureDump},
        {"loading-screen", "<game folder> [out.png]",
         "put the loading screen together as the game shows it; write it out", cmdLoadingScreen},
    });

} // namespace
