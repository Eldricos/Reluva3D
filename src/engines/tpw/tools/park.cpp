// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the park being played.

#include "common/headless_system.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/park_hud.h"
#include "engines/tpw/ui/park_screen.h"
#include "image/image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

void writePng(const reluva::Surface &surface, const char *path) {
    reluva::image::Image out;
    out.w = surface.w;
    out.h = surface.h;
    out.rgba.resize(size_t(surface.w) * size_t(surface.h) * 4);
    for (int y = 0; y < surface.h; ++y)
        std::copy(surface.row(y), surface.row(y) + surface.w * 4, out.pixel(0, y));
    const std::vector<uint8_t> png = reluva::image::encodePng(out);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(png.data()), long(png.size()));
}

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
    writePng(surface, argv[4]);
    const auto &fixed = park.fixedItems();
    std::printf("%s: mission %d (statistic %d to %d), cash %d, admission %d, %s; entrance %d,%d and %d,%d; %zu ticket "
                "booths; boundary of %zu clicks\n",
                argv[3], park.mission().number, park.mission().statistic, park.mission().target, park.cash(),
                park.admissionFee(), screen.dateText(park.day()).c_str(), fixed.entrance[0].x, fixed.entrance[0].y,
                fixed.entrance[1].x, fixed.entrance[1].y, fixed.ticketBooths.size(), park.mission().boundary.size());
    return 0;
}

// The briefing over a new park in a world, its letter to `player` or its
// details, to a PNG; the letter's lines printed.
int cmdBriefingShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(
            stderr,
            "usage: reluva-tool tpw briefing-shot <game folder> <world folder> <out.png> [player [-details]]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    reluva::tpw::Briefing briefing;
    if (reluva::Error err = briefing.load(files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string player = argc > 5 ? argv[5] : "Tester";
    reluva::tpw::Briefing::View view;
    view.details = argc > 6 && std::string(argv[6]) == "-details";
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, screen.startCamera(park));
    briefing.draw(surface, park, player, view);
    writePng(surface, argv[4]);
    for (const std::string &line : view.details ? briefing.details(park) : briefing.letter(park, player))
        std::printf("%s\n", line.c_str());
    return 0;
}

// A new park played headless, by a clock that moves only as it waits:
// its briefing turned to the details and back, then Proceed; the camera
// moved by keys a second at a time (forward, a turn, out); Esc at 45 s,
// a visitor having come; the screens shown, the date reached, the money
// and visitors, with a checksum.
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
            // The briefing's bar clicked at 0.5 s and 1 s, Proceed at 1.5;
            // then Up held for a second, E for the next, Page Down for the
            // third; the compass's left turn held for the next, the
            // bulldozer chosen; Esc at 45 s.
            using Control = reluva::tpw::Briefing::Control;
            using reluva::tpw::ParkHud;
            using reluva::EventType;
            enum class On { kNone, kBriefing, kTurnLeft, kBulldozer };
            static const struct {
                uint64_t at;
                EventType type;
                uint32_t key;
                On on;
                Control click;
            } kScript[] = {{500, EventType::kMouseDown, 0, On::kBriefing, Control::kTurn},
                           {1000, EventType::kMouseDown, 0, On::kBriefing, Control::kTurn},
                           {1500, EventType::kMouseDown, 0, On::kBriefing, Control::kProceed},
                           {2000, EventType::kKeyDown, reluva::key::kUp, On::kNone, Control::kNone},
                           {3000, EventType::kKeyUp, reluva::key::kUp, On::kNone, Control::kNone},
                           {3000, EventType::kKeyDown, 'e', On::kNone, Control::kNone},
                           {4000, EventType::kKeyUp, 'e', On::kNone, Control::kNone},
                           {4000, EventType::kKeyDown, reluva::key::kPageDown, On::kNone, Control::kNone},
                           {5000, EventType::kKeyUp, reluva::key::kPageDown, On::kNone, Control::kNone},
                           {5000, EventType::kMouseMove, 0, On::kTurnLeft, Control::kNone},
                           {5000, EventType::kMouseDown, 0, On::kTurnLeft, Control::kNone},
                           {6000, EventType::kMouseUp, 0, On::kTurnLeft, Control::kNone},
                           {6000, EventType::kMouseMove, 0, On::kBulldozer, Control::kNone},
                           {6000, EventType::kMouseDown, 0, On::kBulldozer, Control::kNone},
                           {6100, EventType::kMouseUp, 0, On::kBulldozer, Control::kNone},
                           {45000, EventType::kKeyDown, reluva::key::kEscape, On::kNone, Control::kNone}};
            if (sent >= int(std::size(kScript)) || getMillis() < kScript[sent].at)
                return false;
            ev.type = kScript[sent].type;
            ev.key = kScript[sent].key;
            if (kScript[sent].on != On::kNone) {
                reluva::tpw::ScreenRect r;
                switch (kScript[sent].on) {
                case On::kBriefing:
                    r = reluva::tpw::Briefing::placeOf(kScript[sent].click);
                    break;
                case On::kTurnLeft:
                    r = ParkHud::compassRect(ParkHud::CompassButton::kTurnLeft);
                    break;
                default:
                    r = ParkHud::toolRect(ParkHud::Tool::kBulldozer);
                    break;
                }
                if (ev.type != EventType::kMouseMove)
                    ev.button = reluva::MouseButton::kLeft;
                ev.mouseX = r.x + r.w / 2;
                ev.mouseY = r.y + r.h / 2;
            }
            ++sent;
            return true;
        }
    } sys;
    reluva::tpw::Briefing briefing;
    if (reluva::Error err = briefing.load(files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::soft3d::Rasterizer rasterizer;
    const reluva::tpw::ParkScreen::Camera start = screen.startCamera(park);
    const bool briefed = reluva::tpw::showBriefing(
        sys, briefing, park, "Tester", [&](reluva::Surface &s) { screen.draw(s, rasterizer, park, start); });
    const bool left = briefed && reluva::tpw::playPark(sys, screen, park);
    std::printf("park: %d screens shown, %s at %llu ms, %s, cash %d, %d visitors, tool %d; md5 %s\n", sys.shown,
                left ? "left" : "closed", static_cast<unsigned long long>(sys.getMillis()),
                screen.dateText(park.day()).c_str(), park.cash(), park.visitors(), int(screen.hud().tool()),
                sys.all.hexDigest().c_str());
    return left ? 0 : 1;
}

const Commands
    kCommands("tpw",
              {
                  {"park-shot", "<game folder> <world folder> <out.png> [seconds] [yaw pitch distance]",
                   "set up a new park in a world and draw it, its HUD over it, to a PNG", cmdParkShot},
                  {"briefing-shot", "<game folder> <world folder> <out.png> [player [-details]]",
                   "draw the mission's briefing over a new park, to a PNG", cmdBriefingShot},
                  {"park-run", "<game folder> <world folder>",
                   "play a new park headless: its briefing, the camera moved by keys and the compass, a tool chosen, a "
                   "visitor, Esc",
                   cmdParkRun},
              });

} // namespace
