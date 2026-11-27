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
#include "engines/tpw/saves.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/buy_window.h"
#include "engines/tpw/ui/hire_window.h"
#include "engines/tpw/ui/fly_in.h"
#include "engines/tpw/ui/park_hud.h"
#include "engines/tpw/ui/park_screen.h"
#include "engines/tpw/ui/sign_painter.h"
#include "image/image.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
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
// The mission a command's arguments name with -mission <n>, the player's
// progress through the campaign; 0 without.
int missionArgument(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-mission")
            return std::atoi(argv[i + 1]);
    return 0;
}

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
        std::fprintf(
            stderr,
            "usage: reluva-tool tpw park-shot <game folder> <world folder> <out.png> [seconds] [yaw "
            "pitch distance [x y]] [-place <id>] [-weather <fine|heatwave|rain|storm|snow>] [-hire <kind> <n>] [-mission <n>]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], missionArgument(argc, argv), files, park, screen))
        return 1;
    // -place <id>: that attraction placed first, where it fits nearest the
    // entrance with its entrance by the path.
    for (int i = 5; i + 1 < argc; ++i) {
        if (std::string(argv[i]) != "-place")
            continue;
        const reluva::tpw::Attractions::Attraction *a = park.attractions().find(std::atoi(argv[i + 1]));
        if (!a) {
            std::fprintf(stderr, "no attraction %s\n", argv[i + 1]);
            return 1;
        }
        const reluva::tpw::Park::Cell in = park.fixedItems().entrance[0];
        int best = -1;
        reluva::tpw::Park::Cell at;
        int angle = 0;
        for (int y = 0; y < park.groundHeight(); ++y)
            for (int x = 0; x < park.groundWidth(); ++x)
                for (int turn = 0; turn < 360; turn += 90) {
                    const auto f = reluva::tpw::Park::footprint(a->shape, {x, y}, turn);
                    if (!f.hasEntrance || !park.canPlace(f))
                        continue;
                    bool byPath = false;
                    for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                        byPath = byPath || park.isPath({f.entrance.x + dx, f.entrance.y + dy});
                    const int d = std::abs(x - in.x) + std::abs(y - in.y);
                    if (byPath && (best < 0 || d < best)) {
                        best = d;
                        at = {x, y};
                        angle = turn;
                    }
                }
        const int queue =
            best < 0 ? reluva::tpw::Park::kNotPlaced : park.place(a->id, a->price, a->shape, a->hasQueue, at, angle);
        if (queue == reluva::tpw::Park::kNotPlaced) {
            std::fprintf(stderr, "nowhere to place %d\n", a->id);
            return 1;
        }
        // A ride's queue, joined straight to the path beside its entrance.
        if (queue >= 0) {
            const reluva::tpw::Park::Cell e = park.queues()[size_t(queue)].entrance;
            for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                if (park.isPath({e.x + dx, e.y + dy}) && !park.queues()[size_t(queue)].atPath)
                    park.layQueue(queue, {e, {e.x + dx, e.y + dy}});
        }
        std::printf("placed %d at %d, %d, %d degrees\n", a->id, at.x, at.y, angle);
    }
    // -weather <fine|heatwave|rain|storm|snow>: set first; it holds till its next change.
    static constexpr const char *kWeathers[] = {"fine", "heatwave", "rain", "storm", "snow"};
    for (int i = 5; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-weather")
            for (size_t w = 0; w < std::size(kWeathers); ++w)
                if (std::string(argv[i + 1]) == kWeathers[w])
                    park.setWeather(reluva::tpw::Park::Weather(w));
    // -hire <engineer|janitor|gardener|guard|entertainer|scientist> <count>: the first so many candidates hired.
    static constexpr const char *kKinds[] = {"engineer", "janitor", "gardener", "guard", "entertainer", "scientist"};
    for (int i = 5; i + 2 < argc; ++i)
        if (std::string(argv[i]) == "-hire")
            for (size_t k = 0; k < std::size(kKinds); ++k)
                if (std::string(argv[i + 1]) == kKinds[k])
                    for (int n = std::atoi(argv[i + 2]); n > 0; --n)
                        if (park.hire(reluva::tpw::Park::StaffKind(k), 0) != reluva::tpw::Park::Hiring::kHired)
                            std::printf("couldn't hire a %s\n", kKinds[k]);
    // In frames, so that the rides' scripts and the visitors keep step.
    constexpr uint32_t kFrameMs = 50;
    const uint32_t runMs = argc > 5 && argv[5][0] != '-' ? uint32_t(std::strtof(argv[5], nullptr) * 1000) : 0;
    screen.advance(park);
    for (uint32_t t = 0; t < runMs; t += kFrameMs) {
        park.advance(std::min(kFrameMs, runMs - t));
        screen.advance(park);
    }
    reluva::tpw::ParkScreen::Camera camera = screen.startCamera(park);
    if (argc > 8 && argv[6][0] != '-') {
        camera.yaw = std::strtof(argv[6], nullptr);
        camera.pitch = std::strtof(argv[7], nullptr);
        camera.distance = std::strtof(argv[8], nullptr);
    }
    if (argc > 10 && argv[9][0] != '-')
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
    static constexpr const char *kDoing[] = {"coming", "walking", "queueing", "riding", "leaving", "going home"};
    size_t vomit = 0;
    for (const auto &l : park.litter())
        vomit += l.vomit;
    std::printf("%d points, %d visitors wanted; %s; %zu litter, %zu of it vomit; %zu stink bombs\n", park.points(),
                park.visitorsWanted(), kWeathers[size_t(park.weather())], park.litter().size(), vomit,
                park.stinkBombs().size());
    static constexpr const char *kStaffDoing[] = {"working",        "going to rest", "resting",
                                                  "going to train", "training",      "striking"};
    for (const reluva::tpw::Park::StaffMember &m : park.staff())
        std::printf("staff %d: %s, %s, %d experience, $ %d a month, energy %.0f, happiness %.0f, motivation %.0f, at "
                    "%.1f, %.1f%s\n",
                    m.id, kKinds[size_t(m.kind)], kStaffDoing[size_t(m.doing)], m.experience, m.wage, double(m.energy),
                    double(m.happiness), double(m.motivation), double(m.x), double(m.z), m.working ? ", at work" : "");
    for (size_t i = 0; i < park.objects().size(); ++i) {
        const reluva::tpw::Park::PlacedObject &o = park.objects()[i];
        const reluva::tpw::Attractions::Attraction *a = park.attractions().find(o.id);
        if (a && (a->wearRate > 0 || a->requiresWatering))
            std::printf("object %zu (%d): repair %.0f%s, water %.0f, %d users last month, $ %d taken\n", i, o.id,
                        double(o.repair), o.broken ? ", broken" : "", double(o.water), o.lastUsers, o.lastTakings);
    }
    for (const reluva::tpw::Park::Visitor &v : park.visitorsAbout())
        std::printf("visitor %d (type %d, person %d): %s at %.1f, %.1f, %d rides, cash %d, happiness %.0f, hunger "
                    "%.0f, thirst %.0f, toilet %.0f, sickness %.0f%s, thinking %d\n",
                    v.id, v.type, v.person, kDoing[size_t(v.doing)], double(v.x), double(v.z), v.rides, v.cash,
                    double(v.happiness), double(v.hunger), double(v.thirst), double(v.toilet), double(v.sickness),
                    v.appearance == 3   ? ", an umbrella"
                    : v.appearance == 2 ? ", a costume"
                    : v.appearance == 1 ? ", a balloon"
                                        : "",
                    v.thoughtUntil > park.millis() ? int(v.thought) : 0);
    return 0;
}

// The briefing over a new park in a world, its letter to `player` or its
// details, to a PNG; the letter's lines printed.
int cmdBriefingShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: reluva-tool tpw briefing-shot <game folder> <world folder> <out.png> [player [-details | "
                     "-challenges]] [-mission <n>]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], missionArgument(argc, argv), files, park, screen))
        return 1;
    reluva::tpw::Briefing briefing;
    if (reluva::Error err = briefing.load(files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string player = argc > 5 ? argv[5] : "Tester";
    reluva::tpw::Briefing::View view;
    view.details = argc > 6 && std::string(argv[6]) == "-details";
    // The Challenges tab, the park run on 60 days for some to be offered,
    // the first offered chosen.
    view.challenges = argc > 6 && std::string(argv[6]) == "-challenges";
    if (view.challenges) {
        while (park.day() < 60)
            park.advance(1000);
        for (size_t i = 0; i < park.challenges().size() && view.chosen < 0; ++i)
            if (park.challenges()[i].status == reluva::tpw::Park::ChallengeStatus::kAvailable)
                view.chosen = int(i);
    }
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, screen.startCamera(park));
    briefing.draw(surface, park, player, view);
    writePng(surface, argv[4]);
    if (view.challenges) {
        for (size_t i = 0; i < park.challenges().size(); ++i)
            std::printf("%d: %s\n", park.challenges()[i].challenge.number,
                        briefing.challengeStatus(park.challenges()[i]).c_str());
        if (view.chosen >= 0)
            for (const std::string &line : briefing.challengeDetails(park, size_t(view.chosen)))
                std::printf("%s\n", line.c_str());
        return 0;
    }
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
    // Esc opens the game's menu; its Quit Game leaves.
    key(45000, 0, reluva::key::kEscape);
    click(45200, centre(reluva::tpw::GameMenu::itemRect(7)));

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
    const bool left = briefed && reluva::tpw::playPark(sys, screen, park, [&](const auto &drawPark, bool challenges) {
                          ++briefings;
                          return reluva::tpw::showBriefing(sys, briefing, park, "Tester", drawPark, challenges);
                      });
    int paths = 0;
    for (int y = 0; y < park.groundHeight(); ++y)
        for (int x = 0; x < park.groundWidth(); ++x)
            paths += park.isPath({x, y});
    size_t queueCells = 0;
    size_t queues = 0;
    size_t kiosks = 0;
    for (const reluva::tpw::Park::Queue &q : park.queues()) {
        queueCells += q.cells.size();
        queues += !q.kiosk;
        kiosks += q.kiosk;
    }
    std::printf("park: %d screens shown, %s at %llu ms, %s, cash %d, %d visitors, %d path cells, %zu objects, %zu "
                "queues of %zu cells, %zu kiosks, %d briefings, tool %d; md5 %s\n",
                sys.shown, left ? "left" : "closed", static_cast<unsigned long long>(sys.getMillis()),
                screen.dateText(park.day()).c_str(), park.cash(), park.visitors(), paths, park.objects().size(), queues,
                queueCells, kiosks, briefings, int(screen.hud().tool()), sys.all.hexDigest().c_str());
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
            std::printf("tab %zu: %d %s, %s, $ %d%s\n", kind, a->id, window.names().shown(*a).c_str(), a->name.c_str(),
                        a->price, a->hasQueue ? ", a queue" : "");
    return 0;
}

// A world's go-kart ride (or water ride, with -water) bought and placed
// on open land with a loop of track round beside it, the park run on so
// many seconds, drawn close over it; its pieces listed.
int cmdTrackShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: reluva-tool tpw track-shot <game folder> <world folder> <out.png> [seconds] [-water]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    bool water = false;
    for (int i = 5; i < argc; ++i)
        water = water || std::string(argv[i]) == "-water";
    const reluva::tpw::Attractions::Attraction *ride = nullptr;
    for (const reluva::tpw::Attractions::Attraction &a : park.attractions().all())
        if (a.trackType == (water ? 2 : 1))
            ride = &a;
    if (!ride) {
        std::fprintf(stderr, "no %s ride in %s\n", water ? "water" : "go-kart", argv[3]);
        return 1;
    }
    using Cell = reluva::tpw::Park::Cell;
    // From beside the ride: north a block, west two, south two, east two,
    // and north back in.
    const std::vector<Cell> legs{{0, -2}, {-4, -2}, {-4, 2}, {0, 2}, {0, 0}};
    bool placed = false;
    for (int y = 4; y < park.groundHeight() - 6 && !placed; ++y)
        for (int x = 6; x < park.groundWidth() - 4 && !placed; ++x) {
            bool open = true;
            for (int dy = -3; dy <= 4 && open; ++dy)
                for (int dx = -6; dx <= 3 && open; ++dx)
                    open = park.ground({x + dx, y + dy}) == reluva::tpw::Park::Ground::kOpen;
            if (!open ||
                park.place(ride->id, 0, ride->shape, ride->hasQueue, {x, y}, 0) == reluva::tpw::Park::kNotPlaced)
                continue;
            placed = true;
        }
    if (!placed) {
        std::fprintf(stderr, "no room for %s\n", ride->name.c_str());
        return 1;
    }
    const size_t object = park.objects().size() - 1;
    const Cell start = park.trackStart(object);
    for (const Cell &leg : legs)
        park.layTrack(object, {start.x + leg.x, start.y + leg.y});
    const uint32_t runMs = argc > 5 && argv[5][0] != '-' ? uint32_t(std::strtof(argv[5], nullptr) * 1000) : 1000;
    screen.advance(park);
    for (uint32_t t = 0; t < runMs; t += 50) {
        park.advance(50);
        screen.advance(park);
    }
    reluva::tpw::ParkScreen::Camera camera = screen.startCamera(park);
    camera.target = reluva::tpw::Park::cellCentre({start.x - 1, start.y});
    camera.distance = 160;
    camera.pitch = 0.75f;
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, camera);
    writePng(surface, argv[4]);
    std::printf("%s at %d,%d: track from %d,%d of %zu blocks, %s\n", ride->name.c_str(), park.objects()[object].at.x,
                park.objects()[object].at.y, start.x, start.y, park.objects()[object].track.size(),
                park.trackComplete(object) ? "whole" : "not whole");
    for (const reluva::tpw::Park::TrackPiece &p : park.trackPieces(object))
        std::printf("  %d,%d: piece %d, %d degrees\n", p.at.x, p.at.y, int(p.model), p.angle);
    return 0;
}

// Every attraction of a world: its id, folder, settings' name, the name
// shown and its description.
int cmdNames(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool tpw names <game folder> <world folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    reluva::tpw::Attractions attractions;
    reluva::tpw::BuyWindow window;
    reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings);
    if (!err)
        err = attractions.load(files, argv[3]);
    if (!err)
        err = window.load(files);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const reluva::tpw::Attractions::Attraction &a : attractions.all())
        std::printf("%d %s \"%s\": \"%s\"; %s\n", a.id, a.folder.c_str(), a.name.c_str(),
                    window.names().shown(a).c_str(), window.names().description(a).c_str());
    return 0;
}

// Every attraction's sign of a world as Reluva letters it, half size, on
// grey, to a PNG; each one's font and lines printed.
int cmdSignShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw sign-shot <game folder> <world folder> <out.png>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    reluva::tpw::Attractions attractions;
    reluva::tpw::BuyWindow window;
    reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings);
    if (!err)
        err = attractions.load(files, argv[3]);
    if (!err)
        err = window.load(files);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    constexpr int kColumns = 3;
    constexpr int kCellW = reluva::tpw::SignPainter::kCanvasWidth / 2;
    constexpr int kCellH = reluva::tpw::SignStyle::kCanvasHeight / 2;
    constexpr uint8_t kGrey = 96;
    reluva::tpw::SignPainter painter;
    std::vector<reluva::image::Image> signs;
    for (const reluva::tpw::Attractions::Attraction &a : attractions.all()) {
        const size_t dot = a.model.rfind('.');
        std::vector<uint8_t> data;
        reluva::tpw::SignStyle style;
        if (dot == std::string::npos || files.read(a.model.substr(0, dot) + ".sgn", data) || style.parse(data))
            continue;
        const std::array<std::string, 2> lines = window.names().lines(a);
        std::printf("%d %s: \"%s\" / \"%s\"; %s (%s) %d at %d, %s %d at %d\n", a.id, a.folder.c_str(), lines[0].c_str(),
                    lines[1].c_str(), style.fontFile().c_str(), style.lines()[0].face.c_str(), style.lines()[0].height,
                    style.lines()[0].top, style.lines()[1].face.c_str(), style.lines()[1].height, style.lines()[1].top);
        signs.push_back(painter.paint(files, style, lines));
    }
    const int rows = int((signs.size() + kColumns - 1) / kColumns);
    reluva::Surface sheet;
    sheet.create(kColumns * kCellW, std::max(1, rows) * kCellH, reluva::PixelFormat::kRGBA32);
    for (size_t i = 0; i < signs.size(); ++i)
        for (int y = 0; y < kCellH; ++y)
            for (int x = 0; x < kCellW; ++x) {
                uint8_t *to = sheet.row(int(i / kColumns) * kCellH + y) + (int(i % kColumns) * kCellW + x) * 4;
                // The four pixels it halves, over grey.
                for (int c = 0; c < 3; ++c) {
                    int sum = 0;
                    for (int q = 0; q < 4; ++q) {
                        const uint8_t *p = signs[i].pixel(2 * x + (q & 1), 2 * y + (q >> 1));
                        sum += (p[c] * p[3] + kGrey * (255 - p[3])) / 255;
                    }
                    to[c] = uint8_t(sum / 4);
                }
                to[3] = 255;
            }
    writePng(sheet, argv[4]);
    std::printf("%zu signs lettered\n", signs.size());
    return 0;
}

// The Hire Staff window over a new park, on a tab, a row chosen, to a
// PNG; the candidates of every kind printed.
int cmdHireShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw hire-shot <game folder> <world folder> <out.png> [tab [row]]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    reluva::tpw::HireWindow &window = screen.hireWindow();
    window.open(park, reluva::tpw::Park::StaffKind(argc > 5 ? std::atoi(argv[5]) : 0));
    if (argc > 6) {
        reluva::Event click;
        click.type = reluva::EventType::kMouseDown;
        click.button = reluva::MouseButton::kLeft;
        const reluva::tpw::ScreenRect r =
            reluva::tpw::HireWindow::placeOf(reluva::tpw::HireWindow::Control::kRow, size_t(std::atoi(argv[6])));
        click.mouseX = r.x + r.w / 2;
        click.mouseY = r.y + r.h / 2;
        window.handle(click);
    }
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, screen.startCamera(park));
    writePng(surface, argv[4]);
    for (size_t k = 0; k < reluva::tpw::Park::kStaffKinds; ++k)
        for (const reluva::tpw::Park::Candidate &c : park.candidates(reluva::tpw::Park::StaffKind(k)))
            std::printf("tab %zu: %s, %d experience, $ %d a month, agency %d\n", k,
                        window.nameOf(reluva::tpw::Park::StaffKind(k), c.name).c_str(), c.experience, c.wage, c.agency);
    return 0;
}

// A left click at the middle of a place on the screen.
reluva::Event clickOn(const reluva::tpw::ScreenRect &r) {
    reluva::Event ev;
    ev.type = reluva::EventType::kMouseDown;
    ev.button = reluva::MouseButton::kLeft;
    ev.mouseX = r.x + r.w / 2;
    ev.mouseY = r.y + r.h / 2;
    return ev;
}

// One of the park's smaller windows over a new park whose staff are one of
// each kind but scientists, run on so many seconds, to a PNG.
int cmdWindowShot(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(
            stderr,
            "usage: reluva-tool tpw window-shot <game folder> <world folder> <out.png> "
            "<attraction|gates|staff|list|training|finance|demand|menu|lab|projects|assign|summary|rename|stats|loans|owing|portfolio|prices|report|visitors|visitor|visitorlist|attractions|parkstats|awards|messages|gamemenu|map|map-layers|opinion|overview|advisor|save|load|options|options-sound|options-game|activate|ability|employee> [seconds] [-mission <n>]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], missionArgument(argc, argv), files, park, screen))
        return 1;
    using Kind = reluva::tpw::Park::StaffKind;
    for (Kind k : {Kind::kEngineer, Kind::kJanitor, Kind::kGardener, Kind::kGuard, Kind::kEntertainer})
        park.hire(k, 0);
    const std::string which = argv[5];
    // For the research windows: a lab where one fits, two scientists, a
    // project begun with one of them on it.
    if (which == "lab" || which == "projects" || which == "assign") {
        for (const reluva::tpw::Attractions::Attraction &a : park.attractions().all())
            for (int y = 0; a.lab && !park.hasResearchLab() && y < park.groundHeight(); ++y)
                for (int x = 0; x < park.groundWidth() && !park.hasResearchLab(); ++x)
                    if (park.canPlace(reluva::tpw::Park::footprint(a.shape, {x, y}, 0)))
                        park.place(a.id, 0, a.shape, a.hasQueue, {x, y}, 0);
        park.hire(Kind::kScientist, 0);
        park.hire(Kind::kScientist, 0);
        if (const auto choices = park.projectChoices(); !choices.empty() && which != "projects") {
            park.startProject(0, choices.front());
            for (const reluva::tpw::Park::StaffMember &m : park.staff())
                if (m.kind == Kind::kScientist) {
                    park.assign(0, m.id);
                    break;
                }
        }
    }
    const uint32_t runMs = argc > 6 && argv[6][0] != '-' ? uint32_t(std::strtof(argv[6], nullptr) * 1000) : 60000;
    screen.advance(park);
    for (uint32_t t = 0; t < runMs; t += 50) {
        park.advance(50);
        screen.advance(park);
    }
    // The robot: the prebuilt park's ride with a queue.
    size_t ride = 0;
    for (size_t i = 0; i < park.objects().size(); ++i)
        if (park.queueOf(i) >= 0 && park.objects()[i].id == 1101)
            ride = i;
    if (which == "attraction")
        screen.attractionWindow().open(park, ride, screen.nameOf(park, ride), screen.modelOf(park, ride));
    else if (which == "gates")
        screen.gateWindow().open();
    else if (which == "staff" && !park.staff().empty())
        screen.staffWindow().open(park.staff()[0].id);
    else if (which == "list")
        screen.staffList().open(Kind::kJanitor);
    else if (which == "training")
        screen.trainingWindow().open();
    else if (which == "finance")
        screen.staffFinance().open();
    else if (which == "demand" && park.wageDemand())
        screen.wageDemand().open(park.wageDemand()->raise);
    else if (which == "menu")
        screen.popup().open(
            screen.nameOf(park, ride), {524, 526, 529, 531, 532, 533, 534, 514, 516}, 260, 100,
            {{25, std::to_string(park.attractions().find(1101)->price)}, {27, std::to_string(park.scrapValue(ride))}});
    else if (which == "lab")
        screen.researchLab().open();
    else if (which == "projects")
        screen.projectWindow().open(0);
    else if (which == "assign")
        screen.assignWindow().open(0);
    else if (which == "summary")
        screen.financeSummary().open(ride, screen.nameOf(park, ride));
    else if (which == "rename")
        screen.renameWindow().open(ride, screen.nameOf(park, ride));
    else if (which == "stats")
        screen.financeStats().open();
    else if (which == "loans" || which == "owing") {
        if (which == "owing")
            park.takeLoan(park.availableLoans().front().offer);
        screen.loansWindow().open();
        if (which == "owing")
            screen.loansWindow().handle(
                clickOn(reluva::tpw::LoansWindow::placeOf(reluva::tpw::LoansWindow::Control::kOutstanding)), park);
    } else if (which == "portfolio")
        screen.portfolio().open("Tester", 1);
    else if (which == "prices")
        screen.attractionFinance().open();
    else if (which == "visitors")
        screen.visitorStats().open();
    else if (which == "visitor" || which == "visitorlist") {
        int first = 0;
        for (const reluva::tpw::Park::Visitor &v : park.visitorsAbout())
            if (v.inPark && !first)
                first = v.id;
        if (which == "visitor")
            screen.visitorWindow().open(first);
        else
            screen.visitorList().open();
    } else if (which == "attractions")
        screen.attractionList().open();
    else if (which == "parkstats")
        screen.parkStats().open();
    else if (which == "awards") {
        screen.awards().open(screen.kit().worldName(park.theme()));
        for (const reluva::tpw::Park::AwardStanding &a : park.awards())
            std::printf("%s %d of %d%s\n", a.measured ? "measured" : "unmeasured", a.value, a.needed,
                        a.held ? " held" : "");
    } else if (which == "messages") {
        // Messages to read: the park's own, to its first annual report.
        while (park.day() < 367)
            park.advance(1000);
        screen.takeMessages(park);
        screen.messageCenter().open();
    } else if (which == "gamemenu")
        screen.gameMenu().open();
    else if (which == "map")
        screen.parkMap().open();
    else if (which == "map-layers") {
        // Every layer on.
        screen.parkMap().open();
        for (size_t l = 0; l < reluva::tpw::ParkMapWindow::kLayers; ++l)
            if (!screen.parkMap().shows(reluva::tpw::ParkMapWindow::Layer(l)))
                screen.parkMap().handle(
                    clickOn(reluva::tpw::ParkMapWindow::layerRect(reluva::tpw::ParkMapWindow::Layer(l))));
    } else if (which == "overview") {
        // As a player who's opened Polar Zone, too, would see it.
        screen.setWorld(2, true, 10);
        screen.overviewMap().open(screen.worlds(park), park.theme());
    } else if (which == "options" || which == "options-sound" || which == "options-game") {
        screen.optionsWindow().open(screen.options());
        const size_t tab = which == "options" ? 0 : which == "options-sound" ? 1 : 2;
        screen.optionsWindow().handle(
            clickOn(reluva::tpw::OptionsWindow::placeOf(reluva::tpw::OptionsWindow::Control::kTab, tab)));
    } else if (which == "save" || which == "load") {
        // As a player with two saves would see it, the first chosen.
        screen.saveLoad().open(which == "save" ? reluva::tpw::SaveLoadWindow::Mode::kSave
                                               : reluva::tpw::SaveLoadWindow::Mode::kLoad,
                               {{"Expanding", screen.dateText(40)}, {"Before the lake", screen.dateText(12)}});
        const reluva::tpw::ScreenRect row =
            reluva::tpw::SaveLoadWindow::placeOf(reluva::tpw::SaveLoadWindow::Control::kRow, 1);
        screen.saveLoad().handle(clickOn(row));
    } else if (which == "advisor") {
        screen.advisor().cue(reluva::tpw::Advisor::Cue::kPathTool);
        screen.advisor().advance(500);
        std::printf("advisor says %u\n", screen.advisor().said().empty() ? 0 : screen.advisor().said().back());
    } else if (which == "opinion")
        screen.parkMap().open(reluva::tpw::ParkMapWindow::Mode::kOpinion);
    else if (which == "ability") {
        // The guard's abilities to learn, as if its training had begun.
        for (const reluva::tpw::Park::StaffMember &m : park.staff())
            if (m.kind == Kind::kGuard)
                screen.abilityWindow().open(m.id, m.kind, m.ability, park.learnable(m.kind));
    } else if (which == "employee") {
        std::vector<int> ids;
        for (const reluva::tpw::Park::StaffMember &m : park.staff())
            ids.push_back(m.id);
        screen.employeeWindow().open(ids);
    } else if (which == "activate") {
        // The first special ride waiting to be activated.
        for (size_t i = park.objects().size(); i-- > 0;)
            if (park.awaitsActivation(i))
                screen.activateRide().open(i, screen.nameOf(park, i));
    } else if (which == "report") {
        // On to the new year's first day, for its annual report.
        while (park.day() < 367)
            park.advance(1000);
        for (const reluva::tpw::Park::Message &m : park.takeMessages())
            if (m.title == 262)
                screen.messageWindow().open(m);
    } else {
        std::fprintf(stderr, "no %s to show\n", which.c_str());
        return 1;
    }
    screen.attractionWindow().advance(1500);
    screen.staffWindow().advance(1500);
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    screen.draw(surface, rasterizer, park, screen.startCamera(park));
    writePng(surface, argv[4]);
    return 0;
}

int cmdFlyinShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool tpw flyin-shot <game folder> <world folder> <out.png> [frame]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], missionArgument(argc, argv), files, park, screen))
        return 1;
    reluva::tpw::FlyIn flyIn;
    if (reluva::Error err = flyIn.load(files, argv[3])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const float frame = argc > 5 && argv[5][0] != '-' ? std::strtof(argv[5], nullptr) : 0;
    screen.advance(park);
    reluva::Surface surface;
    surface.create(640, 480, reluva::PixelFormat::kRGBA32);
    reluva::soft3d::Rasterizer rasterizer;
    const reluva::soft3d::OrbitCamera eye = flyIn.camera(frame, surface.w, surface.h);
    screen.drawScene(surface, rasterizer, park, eye, flyIn.yaw(frame));
    writePng(surface, argv[4]);
    std::printf("frame %.0f of %.0f: eye %.1f, %.1f, %.1f, looking %.2f, %.2f, %.2f\n", frame, flyIn.frames(),
                eye.eye.x, eye.eye.y, eye.eye.z, eye.forward.x, eye.forward.y, eye.forward.z);
    return 0;
}

// A park played on, saved to a file, and loaded again over a new park of
// its world: the same state, and the same as both play on.
int cmdSaveCheck(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool tpw save-check <game folder> <world folder> [seconds]\n");
        return 2;
    }
    reluva::SearchSet files;
    reluva::tpw::Park park;
    reluva::tpw::ParkScreen screen;
    if (!newPark(argv[2], argv[3], 0, files, park, screen))
        return 1;
    const uint32_t runMs = argc > 4 ? uint32_t(std::strtof(argv[4], nullptr) * 1000) : 60000;
    for (uint32_t t = 0; t < runMs; t += 50) {
        park.advance(50);
        screen.advance(park);
        screen.takeMessages(park);
    }
    reluva::tpw::SavedGame save;
    save.name = "Check";
    save.player = "Tester";
    save.world = park.folder();
    save.mission = park.progress();
    save.day = park.day();
    save.park = park.saveState();
    save.screen = screen.saveState();
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "reluva-save-check.rsv";
    reluva::tpw::SavedGame read;
    if (reluva::Error err = reluva::tpw::writeSave(file, save); err || (err = reluva::tpw::readSave(file, read))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::error_code ec;
    std::filesystem::remove(file, ec);
    reluva::tpw::Park again;
    reluva::tpw::ParkScreen againScreen;
    if (reluva::Error err = again.load(files, read.world, read.mission);
        err || (err = again.restoreState(read.park)) || (err = againScreen.load(files, again)) ||
        (err = againScreen.restoreState(read.screen))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const bool same = again.saveState() == park.saveState() && againScreen.saveState() == screen.saveState();
    for (uint32_t t = 0; t < runMs; t += 50) {
        park.advance(50);
        again.advance(50);
    }
    const bool still = again.saveState() == park.saveState();
    reluva::Md5 md5;
    md5.update(save.park.data(), save.park.size());
    const std::string digest = md5.hexDigest();
    std::printf("saved: %s, day %u, %zu bytes of park, %zu of screen; %s as saved, %s %.0f s on; %d visitors, %zu "
                "objects, cash %d; md5 %s\n",
                read.world.c_str(), read.day, read.park.size(), read.screen.size(), same ? "the same" : "different",
                still ? "the same" : "different", double(runMs) / 1000, again.visitors(), again.objects().size(),
                again.cash(), digest.c_str());
    return same && still ? 0 : 1;
}

const Commands kCommands(
    "tpw",
    {
        {"buy-shot", "<game folder> <world folder> <out.png> [tab [row]]",
         "draw the Buy window over a new park, on a tab, a row chosen, to a PNG, and list what's on sale", cmdBuyShot},
        {"hire-shot", "<game folder> <world folder> <out.png> [tab [row]]",
         "draw the Hire Staff window over a new park, on a tab, a row chosen, to a PNG, and list the candidates",
         cmdHireShot},
        {"window-shot",
         "<game folder> <world folder> <out.png> <attraction|gates|staff|list|training|finance|demand|menu|lab|projects|assign|summary|rename|stats|loans|owing|portfolio|prices|report|visitors|visitor|visitorlist|attractions|parkstats|awards|messages|gamemenu|map|map-layers|opinion|overview|advisor|save|load|options|options-sound|options-game|activate|ability|employee> [seconds] [-mission <n>]",
         "draw one of the park's smaller windows over a new park, its staff one of each kind, run on, to a PNG",
         cmdWindowShot},
        {"sign-shot", "<game folder> <world folder> <out.png>",
         "letter every attraction's sign of a world, to a PNG, half size, and list their fonts and lines", cmdSignShot},
        {"track-shot", "<game folder> <world folder> <out.png> [seconds] [-water]",
         "place a world's go-kart ride (or water ride) with a loop of track, run the park on and draw it, to a PNG",
         cmdTrackShot},
        {"names", "<game folder> <world folder>",
         "list every attraction of a world: its settings' name, the name the game shows and its description", cmdNames},
        {"tpws-check", "<game folder> [save in Data] [-objects]",
         "read a saved park (.TPWS), the prebuilt one unless named: its cells, its path, its objects (listed with "
         "-objects) and its world",
         cmdTpwsCheck},
        {"park-shot",
         "<game folder> <world folder> <out.png> [seconds] [yaw pitch distance [x y]] [-place <id>] [-weather <w>] [-hire <kind> <n>] [-mission <n>]",
         "set up a new park in a world, as a -mission leaves it, -place an attraction by its entrance's path, "
         "set the -weather, -hire staff, run it on "
         "and draw it, "
         "its HUD over it, to a PNG; and list its visitors",
         cmdParkShot},
        {"briefing-shot", "<game folder> <world folder> <out.png> [player [-details | -challenges]] [-mission <n>]",
         "draw the mission's briefing over a new park, the player's campaign at a -mission, to a PNG", cmdBriefingShot},
        {"flyin-shot", "<game folder> <world folder> <out.png> [frame]",
         "draw the fly-in along the road into a new park at a frame of its camera's path, to a PNG", cmdFlyinShot},
        {"save-check", "<game folder> <world folder> [seconds]",
         "play a new park on, save it to a file and load it back over a new park: the same, then and as both "
         "play on",
         cmdSaveCheck},
        {"park-run", "<game folder> <world folder> [-shots <folder> <ms>...]",
         "play a new park headless: its briefing, path laid, the Park menu's Objectives, the camera moved "
         "by keys and the compass, a tool chosen, a menu, a visitor, Esc; -shots writes the screen at those "
         "times",
         cmdParkRun},
    });

} // namespace
