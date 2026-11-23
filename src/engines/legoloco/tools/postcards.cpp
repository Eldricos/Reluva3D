// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco command for postcards
// (docs/engines/legoloco/formats/crd.md).

#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoloco/formats/postcard.h"
#include "engines/legoloco/formats/resource_file.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

void print(const std::string &name, const legoloco::Postcard &card, const legoloco::ResourceFile *rf) {
    // A picture is looked for in the resource file, if there's one to look in.
    auto picture = [&](uint8_t set, uint8_t number) {
        const std::string file = legoloco::clipArtName(set, number);
        return rf && !rf->has("CLIPART/" + file) ? file + " (missing)" : file;
    };
    std::printf("%s: card %07u to \"%s\" from \"%s\", message \"%s\"\n", name.c_str(), unsigned(card.number),
                card.to.c_str(), card.from.c_str(), card.message.c_str());
    std::printf("  stamp %s, background %s, %zu stickers\n",
                card.stamp ? picture(legoloco::clipArtSet(31, 0), card.stamp).c_str() : "none",
                card.background && card.backgroundSet
                    ? picture(legoloco::clipArtSet(30, card.backgroundSet - 1), card.background).c_str()
                    : "none",
                card.stickers.size());
    for (const legoloco::PostcardSticker &s : card.stickers)
        std::printf("    %-22s at %3d,%3d, %3d by %3d\n", picture(s.set, s.number).c_str(), s.x, s.y, s.width,
                    s.height);
}

// A card, or every card under a game or art-res folder (the CD's in
// POSTBAG\Easter, a player's in the album and the sorting room's
// folders), its pictures looked for in the resource file.
int cmdCrd(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco crd <file.crd | game or art-res folder>\n");
        return 2;
    }
    const std::filesystem::path path = argv[2];
    std::vector<std::filesystem::path> files;
    legoloco::ResourceFile rf;
    bool haveRf = false;
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        files.push_back(path);
    } else {
        std::filesystem::path folder = path;
        if (const auto found = fs::findDirectory(folder, "art-res"))
            folder = *found;
        haveRf = !rf.open(folder);
        for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file() && fs::toLower(it->path().extension().string()) == ".crd")
                files.push_back(it->path());
        std::sort(files.begin(), files.end());
    }
    if (files.empty()) {
        std::fprintf(stderr, "no postcards in %s\n", argv[2]);
        return 1;
    }
    int failed = 0;
    for (const auto &f : files) {
        std::ifstream in(f, std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        legoloco::Postcard card;
        if (Error err = legoloco::parsePostcard(data, card)) {
            std::printf("%s: %s\n", f.string().c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        print(f.string(), card, haveRf ? &rf : nullptr);
    }
    if (files.size() > 1)
        std::printf("%zu postcards, %d not read\n", files.size(), failed);
    return failed ? 1 : 0;
}

const Commands kCommands("legoloco", {
                                         {"crd", "<file.crd | game or art-res folder>",
                                          "print a postcard, or every one in a copy, with its pictures", cmdCrd},
                                     });

} // namespace
