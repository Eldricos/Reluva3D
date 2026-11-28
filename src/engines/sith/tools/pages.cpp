// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith command for its data viewer's pages.

#include "common/events.h"
#include "common/fs.h"
#include "common/stream.h"
#include "devtools/tool.h"
#include "engines/sith/pages/pages.h"
#include "image/image.h"

#include <cstdio>
#include <map>
#include <string>

namespace {

using namespace reluva::tool;
using reluva::Error;
namespace sith = reluva::sith;

// A key as written on the command line: a name, or a character.
bool parseKey(const std::string &arg, uint32_t &out) {
    namespace key = reluva::key;
    static const std::map<std::string, uint32_t> kNames = {
        {"left", key::kLeft},   {"right", key::kRight},   {"up", key::kUp},     {"down", key::kDown},
        {"pgup", key::kPageUp}, {"pgdn", key::kPageDown}, {"home", key::kHome}, {"end", key::kEnd},
    };
    auto it = kNames.find(reluva::fs::toLower(arg));
    if (it != kNames.end()) {
        out = it->second;
        return true;
    }
    if (arg.size() == 1) {
        out = uint8_t(arg[0]);
        return true;
    }
    return false;
}

// One of the viewer's pages, drawn as the viewer would after the keys
// given, saved as a PNG: for checking pages without a window.
int cmdPage(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool sith page <install> <page> [keys...] <out.png>\n"
                             "  pages: pictures, materials, colormaps, fonts, strings\n"
                             "  keys: left right up down pgup pgdn home end, or a character\n");
        return 2;
    }
    sith::ViewerData data;
    if (Error err = data.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    auto pages = sith::makePages(data);
    sith::Page *page = nullptr;
    for (auto &p : pages)
        if (reluva::fs::toLower(p->name()) == reluva::fs::toLower(argv[3]))
            page = p.get();
    if (!page || page->count() == 0) {
        std::fprintf(stderr, "no page %s, or nothing on it\n", argv[3]);
        return 1;
    }
    for (int i = 4; i < argc - 1; ++i) {
        uint32_t key = 0;
        if (!parseKey(argv[i], key)) {
            std::fprintf(stderr, "not a key: %s\n", argv[i]);
            return 2;
        }
        page->handleKey(key);
    }
    reluva::Surface screen;
    screen.create(sith::kViewerW, sith::kViewerH, reluva::PixelFormat::kRGBA32);
    page->draw(screen);
    reluva::image::Image img;
    img.w = screen.w;
    img.h = screen.h;
    img.rgba.assign(screen.pixels.begin(), screen.pixels.end());
    if (!reluva::writeFile(argv[argc - 1], reluva::image::encodePng(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[argc - 1]);
        return 1;
    }
    std::printf("%s\n", argv[argc - 1]);
    return 0;
}

const Commands kCommands("sith", {
                                     {"page", "<install> <page> [keys...] <out.png>",
                                      "one of the data viewer's pages drawn after the keys given, as a PNG", cmdPage},
                                 });

} // namespace
