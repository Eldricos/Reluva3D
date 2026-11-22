// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco command for drawing a data viewer page into a
// picture, without a window (docs/engines/legoloco/systems/data-viewer.md).

#include "common/events.h"
#include "common/headless_system.h"
#include "devtools/tool.h"
#include "engines/legoloco/game_files.h"
#include "engines/legoloco/pages/viewers.h"
#include "image/image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

namespace {

using namespace reluva;
using namespace reluva::tool;

// The page's item: the next one `item` times from the first. Each page's
// keys after it, as letters, are pressed in turn.
int cmdView(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: reluva-tool legoloco view <game folder> <boards|pictures|definitions|sounds> "
                             "<item> <out.png> [keys]\n");
        return 2;
    }
    legoloco::GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    HeadlessSystem sys;
    Surface screen;
    screen.create(legoloco::kScreenW, legoloco::kScreenH, PixelFormat::kRGBA32);
    const std::string which = argv[3];
    std::unique_ptr<legoloco::Page> page;
    if (which == "boards")
        page = std::make_unique<legoloco::BoardPage>(sys, screen, files);
    else if (which == "pictures")
        page = std::make_unique<legoloco::PicturePage>(sys, screen, files);
    else if (which == "definitions")
        page = std::make_unique<legoloco::DefinitionPage>(sys, screen, files);
    else if (which == "sounds")
        page = std::make_unique<legoloco::SoundPage>(sys, screen, files, nullptr);
    else {
        std::fprintf(stderr, "no page %s\n", argv[3]);
        return 2;
    }
    if (page->count() == 0) {
        std::fprintf(stderr, "nothing on the %s page\n", argv[3]);
        return 1;
    }
    const size_t item = size_t(std::strtoul(argv[4], nullptr, 10)) % page->count();
    page->activate();
    const uint32_t next = which == "sounds" ? key::kDown : key::kRight;
    for (size_t i = 0; i < item; ++i)
        page->handleKey(next);
    if (argc > 6)
        for (const char *k = argv[6]; *k; ++k)
            page->handleKey(*k == '>' ? key::kDown : *k == '<' ? key::kUp : uint32_t(*k));
    image::Image img;
    img.w = screen.w;
    img.h = screen.h;
    img.rgba.assign(screen.pixels.begin(), screen.pixels.end());
    const auto png = image::encodePng(img);
    std::ofstream out(argv[5], std::ios::binary);
    if (!out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()))) {
        std::fprintf(stderr, "can't write %s\n", argv[5]);
        return 1;
    }
    return 0;
}

const Commands kCommands("legoloco",
                         {
                             {"view", "<game folder> <boards|pictures|definitions|sounds> <item> <out.png> [keys]",
                              "draw a data viewer page's item into a PNG; keys are the page's letters, with > and < "
                              "for Down and Up",
                              cmdView},
                         });

} // namespace
