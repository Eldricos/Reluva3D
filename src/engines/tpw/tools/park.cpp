// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the park being played.

#include "common/headless_system.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/park_screen.h"
#include "image/image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// Mounts the game and sets up a new park in a world for a mission, with
// its screen; false, having said why, if it can't.
bool newPark(const char *game, const char *world, int mission, reluva::SearchSet &files, reluva::tpw::Park &park,
             reluva::tpw::ParkScreen &screen) {
    std::vector<std::string> warnings;
    reluva::Error err = reluva::tpw::mountGame(files, game, warnings);
    if (!err)
        err = park.load(files, world, mission);
    if (!err)
        err = screen.load(files, park);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// A new park in a world, its clock run on so many seconds, drawn from the
// camera's start (or turned, tilted and as far as given) to a PNG; its
// money, date and fixed items printed.
int cmdParkShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw park-shot <game folder> <world folder> <out.png> [seconds] [yaw "
                             "pitch distance]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    if (argc > 5)
        park.advance(uint32_t(std::strtof(argv[5], nullptr) * 1000));
    reluva::tpw::ParkScreen::Camera camera = screen.startCamera(park);
    if (argc > 8) {
        camera.yaw = std::strtof(argv[6], nullptr);
        camera.pitch = std::strtof(argv[7], nullptr);
        camera.distance = std::strtof(argv[8], nullptr);
    }
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, camera);
    reluva::image::Image out;
    out.w = surface.w;
    out.h = surface.h;
    out.rgba.resize(size_t(surface.w) * size_t(surface.h) * 4);
    for (int y = 0; y < surface.h; ++y)
        std::copy(surface.row(y), surface.row(y) + surface.w * 4, out.pixel(0, y));
    const std::vector<uint8_t> png = reluva::image::encodePng(out);
    std::ofstream(argv[4], std::ios::binary).write(reinterpret_cast<const char *>(png.data()), long(png.size()));
    const auto &fixed = park.fixedItems();
    std::printf("%s: mission %d (statistic %d to %d), cash %d, admission %d, %s; entrance %d,%d and %d,%d; %zu ticket "
                "booths; boundary of %zu clicks\n",
                argv[3], park.mission().number, park.mission().statistic, park.mission().target, park.cash(),
                park.admissionFee(), screen.dateText(park.day()).c_str(), fixed.entrance[0].x, fixed.entrance[0].y,
                fixed.entrance[1].x, fixed.entrance[1].y, fixed.ticketBooths.size(), park.mission().boundary.size());
    return 0;
}

// A new park played headless, by a clock that moves only as it waits:
// the camera moved by keys a second at a time (forward, a turn, out),
// then Esc; the screens shown, the date reached, with a checksum.
int cmdParkRun(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool tpw park-run <game folder> <world folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    struct Player : reluva::HeadlessSystem {
        reluva::Md5 all;
        int shown = 0;
        int sent = 0;
        void updateScreen(const reluva::Surface &s) override {
            HeadlessSystem::updateScreen(s);
            all.update(s.pixels.data(), s.pixels.size());
            ++shown;
        }
        bool pollEvent(reluva::Event &ev) override {
            ev = reluva::Event{};
            // Up held for the first second, E for the next, Page Down for
            // the third; Esc at five.
            static const struct {
                uint64_t at;
                reluva::EventType type;
                uint32_t key;
            } kScript[] = {{0, reluva::EventType::kKeyDown, reluva::key::kUp},
                           {1000, reluva::EventType::kKeyUp, reluva::key::kUp},
                           {1000, reluva::EventType::kKeyDown, 'e'},
                           {2000, reluva::EventType::kKeyUp, 'e'},
                           {2000, reluva::EventType::kKeyDown, reluva::key::kPageDown},
                           {3000, reluva::EventType::kKeyUp, reluva::key::kPageDown},
                           {5000, reluva::EventType::kKeyDown, reluva::key::kEscape}};
            if (sent >= int(std::size(kScript)) || getMillis() < kScript[sent].at)
                return false;
            ev.type = kScript[sent].type;
            ev.key = kScript[sent].key;
            ++sent;
            return true;
        }
    } sys;
    const bool left = reluva::tpw::playPark(sys, screen, park);
    std::printf("park: %d screens shown, %s at %llu ms, %s, cash %d; md5 %s\n", sys.shown, left ? "left" : "closed",
                static_cast<unsigned long long>(sys.getMillis()), screen.dateText(park.day()).c_str(), park.cash(),
                sys.all.hexDigest().c_str());
    return left ? 0 : 1;
}

const Commands kCommands("tpw",
                         {
                             {"park-shot", "<game folder> <world folder> <out.png> [seconds] [yaw pitch distance]",
                              "set up a new park in a world and draw it, its HUD over it, to a PNG", cmdParkShot},
                             {"park-run", "<game folder> <world folder>",
                              "play a new park headless: the camera moved by keys, then Esc", cmdParkRun},
                         });

} // namespace
