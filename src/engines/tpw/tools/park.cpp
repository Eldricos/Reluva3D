// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the park being played.

#include "common/headless_system.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/saved_park.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/park/attractions.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/buy_window.h"
#include "engines/tpw/ui/park_hud.h"
#include "engines/tpw/ui/park_screen.h"
#include "image/image.h"

#include <array>
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
// camera's start (or turned, tilted and as far as given, looking at a cell) to a PNG; its
// money, date and fixed items printed.
int cmdParkShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw park-shot <game folder> <world folder> <out.png> [seconds] [yaw "
                             "pitch distance [x y]]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    if (argc > 5)
        park.advance(uint32_t(std::strtof(argv[5], nullptr) * 1000));
    screen.advance(park);
    reluva::tpw::ParkScreen::Camera camera = screen.startCamera(park);
    if (argc > 8) {
        camera.yaw = std::strtof(argv[6], nullptr);
        camera.pitch = std::strtof(argv[7], nullptr);
        camera.distance = std::strtof(argv[8], nullptr);
    }
    if (argc > 10)
        camera.target = reluva::tpw::Park::cellCentre({std::atoi(argv[9]), std::atoi(argv[10])});
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
        std::fprintf(stderr,
                     "usage: reluva-tool tpw park-run <game folder> <world folder> [-shots <folder> <ms>...]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    // What the run does, in order, each at its time: the briefing turned
    // to the details and back and Proceed; the path tool chosen, a length
    // of four laid across the open land beside the path, another of three
    // on from it, then Esc dropping the next; Build Attraction chosen from
    // the Attractions menu, the second ride bought, turned and back,
    // placed right of the entrance's path, its queue laid down a cell,
    // along, up a cell and into the path; the Park menu opened and its Objectives
    // chosen, the briefing shown again and Proceed; Up held for a second,
    // E for the next, Page Down for the third, the compass's left turn for
    // the fourth; the bulldozer chosen; the Attractions menu opened and
    // closed by a click beside it; Esc at 45 s.
    using reluva::EventType;
    using reluva::tpw::Briefing;
    using reluva::tpw::ParkHud;
    using Cell = reluva::tpw::Park::Cell;
    const reluva::tpw::ParkScreen::Camera start = screen.startCamera(park);
    auto centre = [](const reluva::tpw::ScreenRect &r) { return std::array<int, 2>{r.x + r.w / 2, r.y + r.h / 2}; };
    auto onCell = [&](const Cell &c) {
        std::array<int, 2> at{};
        screen.cellOnScreen(start, c, at[0], at[1]);
        return at;
    };
    const Cell end = {park.fixedItems().entrance[0].x, park.fixedItems().entrance[0].y + 4};
    struct Step {
        uint64_t at;
        EventType type;
        uint32_t key;
        std::array<int, 2> mouse;
    };
    std::vector<Step> script;
    auto click = [&](uint64_t at, std::array<int, 2> where) {
        script.push_back({at, EventType::kMouseMove, 0, where});
        script.push_back({at, EventType::kMouseDown, 0, where});
        script.push_back({at + 50, EventType::kMouseUp, 0, where});
    };
    auto key = [&](uint64_t down, uint64_t up, uint32_t k) {
        script.push_back({down, EventType::kKeyDown, k, {}});
        if (up)
            script.push_back({up, EventType::kKeyUp, k, {}});
    };
    click(500, centre(Briefing::placeOf(Briefing::Control::kTurn)));
    click(1000, centre(Briefing::placeOf(Briefing::Control::kTurn)));
    click(1500, centre(Briefing::placeOf(Briefing::Control::kProceed)));
    click(2000, centre(ParkHud::toolRect(ParkHud::Tool::kPath)));
    click(2200, onCell({end.x + 2, end.y + 5}));
    click(2400, onCell({end.x + 5, end.y + 5}));
    click(2600, onCell({end.x + 5, end.y + 8}));
    key(2800, 2850, reluva::key::kEscape);
    using reluva::tpw::BuyWindow;
    const Cell ride{end.x + 7, end.y - 2};
    click(3000, centre(ParkHud::gizmoRect(ParkHud::Gizmo::kAttractions)));
    click(3400, centre(ParkHud::menuItemRect(ParkHud::Gizmo::kAttractions, 0)));
    click(3600, centre(BuyWindow::placeOf(BuyWindow::Control::kRow, 1)));
    click(3800, centre(BuyWindow::placeOf(BuyWindow::Control::kBuy)));
    key(4000, 4050, '.');
    key(4100, 4150, ',');
    click(4300, onCell(ride));
    click(4500, onCell({ride.x + 2, ride.y - 1}));
    click(4600, onCell({end.x + 3, ride.y - 1}));
    click(4700, onCell({end.x + 3, ride.y}));
    click(4800, onCell({end.x + 1, ride.y}));
    click(5000, centre(ParkHud::gizmoRect(ParkHud::Gizmo::kPark)));
    click(5500, centre(ParkHud::menuItemRect(ParkHud::Gizmo::kPark, 3)));
    click(6000, centre(Briefing::placeOf(Briefing::Control::kProceed)));
    key(7000, 8000, reluva::key::kUp);
    key(8000, 9000, 'e');
    key(9000, 10000, reluva::key::kPageDown);
    const std::array<int, 2> turnLeft = centre(ParkHud::compassRect(ParkHud::CompassButton::kTurnLeft));
    script.push_back({10000, EventType::kMouseMove, 0, turnLeft});
    script.push_back({10000, EventType::kMouseDown, 0, turnLeft});
    script.push_back({11000, EventType::kMouseUp, 0, turnLeft});
    click(11000, centre(ParkHud::toolRect(ParkHud::Tool::kBulldozer)));
    click(12000, centre(ParkHud::gizmoRect(ParkHud::Gizmo::kAttractions)));
    click(13000, {320, 200});
    key(45000, 0, reluva::key::kEscape);

    struct Player : reluva::HeadlessSystem {
        reluva::Md5 all;
        int shown = 0;
        size_t sent = 0;
        const std::vector<Step> *script = nullptr;
        // With -shots, the screen at each of these times, written out.
        std::vector<uint64_t> shotTimes;
        std::string shotFolder;
        size_t shots = 0;
        void updateScreen(const reluva::Surface &s) override {
            HeadlessSystem::updateScreen(s);
            all.update(s.pixels.data(), s.pixels.size());
            ++shown;
            if (shots < shotTimes.size() && getMillis() >= shotTimes[shots]) {
                writePng(s, (shotFolder + "/park-" + std::to_string(shotTimes[shots]) + ".png").c_str());
                ++shots;
            }
        }
        bool pollEvent(reluva::Event &ev) override {
            ev = reluva::Event{};
            if (sent >= script->size() || getMillis() < (*script)[sent].at)
                return false;
            const Step &step = (*script)[sent++];
            ev.type = step.type;
            ev.key = step.key;
            ev.mouseX = step.mouse[0];
            ev.mouseY = step.mouse[1];
            if (ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp)
                ev.button = reluva::MouseButton::kLeft;
            return true;
        }
    } sys;
    sys.script = &script;
    for (int i = 4; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-shots") {
            sys.shotFolder = argv[i + 1];
            for (int j = i + 2; j < argc; ++j)
                sys.shotTimes.push_back(std::strtoull(argv[j], nullptr, 10));
            break;
        }
    reluva::tpw::Briefing briefing;
    if (reluva::Error err = briefing.load(files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::soft3d::Rasterizer rasterizer;
    const bool briefed = reluva::tpw::showBriefing(
        sys, briefing, park, "Tester", [&](reluva::Surface &s) { screen.draw(s, rasterizer, park, start); });
    int briefings = 1;
    const bool left = briefed && reluva::tpw::playPark(sys, screen, park, [&](const auto &drawPark) {
                          ++briefings;
                          return reluva::tpw::showBriefing(sys, briefing, park, "Tester", drawPark);
                      });
    int paths = 0;
    for (int y = 0; y < park.groundHeight(); ++y)
        for (int x = 0; x < park.groundWidth(); ++x)
            paths += park.isPath({x, y});
    size_t queueCells = 0;
    for (const reluva::tpw::Park::Queue &q : park.queues())
        queueCells += q.cells.size();
    std::printf("park: %d screens shown, %s at %llu ms, %s, cash %d, %d visitors, %d path cells, %zu objects, %zu "
                "queues of %zu cells, %d briefings, tool %d; md5 %s\n",
                sys.shown, left ? "left" : "closed", static_cast<unsigned long long>(sys.getMillis()),
                screen.dateText(park.day()).c_str(), park.cash(), park.visitors(), paths, park.objects().size(),
                park.queues().size(), queueCells, briefings, int(screen.hud().tool()), sys.all.hexDigest().c_str());
    return left ? 0 : 1;
}

// A saved park read: its version, its state's size and checksum, its
// cells' kinds and what's built on them, and the world whose attribute map
// its tiles are.
int cmdTpwsCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw tpws-check <game folder> [save in Data] [-objects]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const bool listing = argc > 3 && std::string(argv[argc - 1]) == "-objects";
    const std::string name = argc > 3 && !(listing && argc == 4) ? argv[3] : "PreBuilt/prebuilt.TPWS";
    std::vector<uint8_t> data;
    reluva::tpw::SavedPark saved;
    reluva::Error err = files.read(name, data);
    if (!err)
        err = saved.parse(data);
    if (err) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.message.c_str());
        return 1;
    }
    using Built = reluva::tpw::SavedPark::Built;
    constexpr int kSide = reluva::tpw::SavedPark::kSide;
    std::array<int, 256> kinds{};
    int path = 0;
    int queues = 0;
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            const reluva::tpw::SavedPark::Cell &c = saved.cell(x, y);
            ++kinds[c.kind];
            path += c.built == Built::kPath;
            queues += c.built == Built::kQueue;
        }
    std::string world = "none";
    for (const char *folder : {"levels/science", "levels/arabian", "levels/water", "levels/wIntro"}) {
        std::vector<uint8_t> mapData;
        reluva::tpw::AttributeMap map;
        if (files.read(std::string(folder) + "/terrain/base.map", mapData) || map.parse(mapData) ||
            map.width() != kSide || map.height() != kSide)
            continue;
        bool same = true;
        for (int x = 0; x < kSide && same; ++x)
            for (int y = 0; y < kSide && same; ++y)
                same = saved.cell(x, y).tile == map.at(y, x);
        if (same)
            world = folder;
    }
    // The queues' pieces as Reluva works them out from their shapes, set
    // against those the save gives: its 2, 4 and 5 are the straight, the
    // bend and the piece by the path; a straight turned a half is the same.
    int agree = 0;
    int pieces = 0;
    reluva::tpw::Park park;
    if (world != "none" && !park.load(files, world, 0)) {
        using Model = reluva::tpw::Park::QueueModel;
        for (const reluva::tpw::Park::QueuePiece &q : park.queuePieces()) {
            const reluva::tpw::SavedPark::Cell &c = saved.cell(q.at.x, q.at.y);
            const int number = q.model == Model::kStraight ? 2
                               : q.model == Model::kBend   ? 4
                               : q.model == Model::kJoin   ? 5
                                                           : -1;
            const int half = q.model == Model::kStraight ? 180 : 360;
            ++pieces;
            agree += number == c.piece && q.angle % half == c.angle % half;
        }
    }
    reluva::Md5 md5;
    md5.update(saved.state().data(), saved.state().size());
    size_t placed = 0;
    for (const reluva::tpw::SavedPark::Object &o : saved.objects()) {
        placed += o.placed();
        if (listing)
            std::printf("  object %u at %.1f, %.1f, %u degrees%s\n", o.id, o.x, o.y, o.angle,
                        o.placed() ? "" : " (not placed)");
    }
    std::printf("%s: version %s, state %zu bytes, %d cells (%d of kind 3, %d of 7), %d path, %d queue (%d of %d "
                "pieces as saved), %zu objects (%zu placed), world %s; md5 %s\n",
                name.c_str(), saved.version().c_str(), saved.state().size(), kSide * kSide, kinds[3], kinds[7], path,
                queues, agree, pieces, saved.objects().size(), placed, world.c_str(), md5.hexDigest().c_str());
    return 0;
}

// The Buy window over a new park, on a tab, a row chosen; what it lists
// printed.
int cmdBuyShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw buy-shot <game folder> <world folder> <out.png> [tab [row]]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    reluva::tpw::Attractions attractions;
    reluva::tpw::BuyWindow window;
    reluva::Error err = attractions.load(files, argv[3]);
    if (!err)
        err = window.load(files);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const int tab = argc > 5 ? std::atoi(argv[5]) : 0;
    window.open(attractions, reluva::tpw::Attractions::Kind(tab), park);
    if (argc > 6) {
        reluva::Event click;
        click.type = reluva::EventType::kMouseDown;
        click.button = reluva::MouseButton::kLeft;
        const reluva::tpw::ScreenRect r =
            reluva::tpw::BuyWindow::placeOf(reluva::tpw::BuyWindow::Control::kRow, size_t(std::atoi(argv[6])));
        click.mouseX = r.x + r.w / 2;
        click.mouseY = r.y + r.h / 2;
        window.handle(click);
    }
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, screen.startCamera(park));
    window.advance(1500);
    window.draw(surface);
    writePng(surface, argv[4]);
    for (size_t kind = 0; kind < reluva::tpw::Attractions::kKinds; ++kind)
        for (const reluva::tpw::Attractions::Attraction *a : attractions.onSale(reluva::tpw::Attractions::Kind(kind)))
            std::printf("tab %zu: %d %s, %s, $ %d%s\n", kind, a->id, window.shownName(*a).c_str(), a->name.c_str(),
                        a->price, a->hasQueue ? ", a queue" : "");
    return 0;
}

const Commands kCommands(
    "tpw",
    {
        {"buy-shot", "<game folder> <world folder> <out.png> [tab [row]]",
         "draw the Buy window over a new park, on a tab, a row chosen, to a PNG, and list what's on sale", cmdBuyShot},
        {"tpws-check", "<game folder> [save in Data] [-objects]",
         "read a saved park (.TPWS), the prebuilt one unless named: its cells, its path, its objects (listed with "
         "-objects) and its world",
         cmdTpwsCheck},
        {"park-shot", "<game folder> <world folder> <out.png> [seconds] [yaw pitch distance [x y]]",
         "set up a new park in a world and draw it, its HUD over it, to a PNG", cmdParkShot},
        {"briefing-shot", "<game folder> <world folder> <out.png> [player [-details]]",
         "draw the mission's briefing over a new park, to a PNG", cmdBriefingShot},
        {"park-run", "<game folder> <world folder> [-shots <folder> <ms>...]",
         "play a new park headless: its briefing, path laid, the Park menu's Objectives, the camera moved "
         "by keys and the compass, a tool chosen, a menu, a visitor, Esc; -shots writes the screen at those "
         "times",
         cmdParkRun},
    });

} // namespace
