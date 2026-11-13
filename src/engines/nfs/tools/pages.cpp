// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's reluva-tool command for its launcher pages:
// one drawn after given keys, as a picture.

#include "backends/networking/sockets/socket_net.h"
#include "audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "image/image.h"
#include "emulation/x86/pokes.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/fce.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/formats/hrz.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/pages/play_page.h"
#include "engines/nfs/formats/refpack.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/pages/viewers.h"
#include "engines/nfs/formats/viv.h"
#include "devtools/tool.h"
#include "engines/nfs/tools/helpers.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using reluva::Error;
using reluva::nfs::ShapeBank;
using reluva::nfs::VivArchive;
using namespace reluva::nfs::tools;
namespace stdfs = std::filesystem;

// Draws one of the engine's viewer pages after the keys given, as the
// engine would, without a window: for checking pages and taking pictures
// of them.
int cmdNfsPage(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool nfs page <game folder> <out.tga> <page> [key...]\n"
                             "  keys: left right up down pgup pgdn home end enter esc, or a letter or symbol;\n"
                             "  also +key and -key (held, let go), wait<ms>, click and move<dx>/<dy>\n"
                             "  (the Play page writes to nfs page-save)\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    auto pages = reluva::nfs::makePages("Need for Speed III: Hot Pursuit", folders, files, nullptr, "nfs page-save");
    reluva::nfs::Page *page = nullptr;
    for (auto &p : pages)
        if (lowerAscii(p->name()) == lowerAscii(argv[4]))
            page = p.get();
    if (!page) {
        std::fprintf(stderr, "no page '%s'; there are:", argv[4]);
        for (auto &p : pages)
            std::fprintf(stderr, " %s", p->name());
        std::fprintf(stderr, "\n");
        return 1;
    }
    // The Play page's game starts at once, as it would when shown.
    if (auto *play = dynamic_cast<reluva::nfs::PlayPage *>(page))
        play->start();
    const std::pair<const char *, uint32_t> names[] = {
        {"left", reluva::key::kLeft}, {"right", reluva::key::kRight}, {"up", reluva::key::kUp},
        {"down", reluva::key::kDown}, {"pgup", reluva::key::kPageUp}, {"pgdn", reluva::key::kPageDown},
        {"home", reluva::key::kHome}, {"end", reluva::key::kEnd},     {"enter", reluva::key::kReturn},
        {"esc", reluva::key::kEscape}};
    int mouseX = 320;
    int mouseY = 240;
    auto mouse = [&](reluva::EventType type) {
        reluva::Event ev;
        ev.type = type;
        ev.mouseX = mouseX;
        ev.mouseY = mouseY;
        ev.button = reluva::MouseButton::kLeft;
        page->handleEvent(ev);
    };
    mouse(reluva::EventType::kMouseMove);
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("wait", 0) == 0 && arg.size() > 4) { // wait<ms>: the page ticking meanwhile
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::atoi(argv[i] + 4));
            while (std::chrono::steady_clock::now() < until) {
                page->tick(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }
        if (arg == "click") {
            mouse(reluva::EventType::kMouseDown);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mouse(reluva::EventType::kMouseUp);
            continue;
        }
        if (arg.rfind("move", 0) == 0 && arg.find('/') != std::string::npos) { // move<dx>/<dy>
            mouseX += std::atoi(arg.c_str() + 4);
            mouseY += std::atoi(arg.c_str() + arg.find('/') + 1);
            mouse(reluva::EventType::kMouseMove);
            continue;
        }
        // +key and -key: pressed, let go; else both.
        const bool press = arg[0] != '-';
        const bool release = arg[0] != '+';
        const std::string keyArg = arg.size() > 1 && (arg[0] == '+' || arg[0] == '-') ? arg.substr(1) : arg;
        uint32_t key = 0;
        for (const auto &[name, code] : names)
            if (keyArg == name)
                key = code;
        if (!key && keyArg.size() == 1)
            key = static_cast<unsigned char>(keyArg[0]);
        if (!key) {
            std::fprintf(stderr, "unknown key '%s'\n", argv[i]);
            return 2;
        }
        reluva::Event ev;
        ev.key = key;
        if (press) {
            ev.type = reluva::EventType::kKeyDown;
            page->handleEvent(ev);
        }
        if (release) {
            ev.type = reluva::EventType::kKeyUp;
            page->handleEvent(ev);
        }
    }
    reluva::Surface screen;
    screen.create(640, 480, reluva::PixelFormat::kRGBA32);
    page->draw(screen);
    reluva::image::Image img;
    img.w = screen.w;
    img.h = screen.h;
    img.rgba = screen.pixels;
    if (!writeFile(argv[3], reluva::image::encodeTga(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    return 0;
}

const reluva::tool::Commands
    kCommands("nfs",
              {
                  {"page", "<game folder> <out.tga> <page> [key...]",
                   "one of the engine's pages drawn after the keys given, as a TGA, without a window", cmdNfsPage},
              });

} // namespace
