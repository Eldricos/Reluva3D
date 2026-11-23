// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's pictures
// (docs/engines/storm3d/games/armada/formats/textures.md).
//
//   reluva-tool storm3d image-check <game folder>
//   reluva-tool storm3d image <game folder> <name> <out.png>

#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/image_page.h"
#include "devtools/tool.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::storm3d::GameFiles;
using reluva::storm3d::ImagePage;

int cmdImageCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d image-check <game folder>\n");
        return 2;
    }
    GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    std::map<std::string, int> sizes; // "256x256" -> how many
    std::vector<uint8_t> data;
    for (const std::string &name : files.list()) {
        if (!ImagePage::isPicture(name))
            continue;
        reluva::image::Image img;
        if (Error err = files.read(name, data); err || reluva::image::decode(data, img)) {
            std::printf("not read: %s\n", name.c_str());
            ++failed;
            continue;
        }
        ++read;
        ++sizes[std::to_string(img.w) + "x" + std::to_string(img.h)];
    }
    std::vector<std::pair<int, std::string>> common;
    for (const auto &[size, n] : sizes)
        common.push_back({n, size});
    std::sort(common.rbegin(), common.rend());
    for (size_t i = 0; i < common.size() && i < 10; ++i)
        std::printf("%6d  %s\n", common[i].first, common[i].second.c_str());
    std::printf("%d pictures read, %d failed\n", read, failed);
    return failed ? 1 : 0;
}

int cmdImage(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool storm3d image <game folder> <name> <out.png>\n");
        return 2;
    }
    GameFiles files;
    std::vector<uint8_t> data;
    reluva::image::Image img;
    if (Error err = files.open(argv[2]);
        err || (err = files.read(argv[3], data)) || (err = reluva::image::decode(data, img))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::vector<uint8_t> png = reluva::image::encodePng(img);
    std::ofstream out(argv[4], std::ios::binary);
    if (!out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()))) {
        std::fprintf(stderr, "Cannot write %s\n", argv[4]);
        return 1;
    }
    std::printf("%s: %dx%d\n", argv[3], img.w, img.h);
    return 0;
}

const reluva::tool::Commands
    kCommands("storm3d",
              {
                  {"image-check", "<game folder>", "decode every picture the data viewer shows, and count them by size",
                   cmdImageCheck},
                  {"image", "<game folder> <name> <out.png>", "write one of the install's pictures as PNG", cmdImage},
              });

} // namespace
