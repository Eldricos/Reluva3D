// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The HUD's places and the camera's start are measured on the recording:
// docs/engines/tpw/games/simcoaster/systems/park.md.

#include "engines/tpw/ui/park_screen.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/text.h"
#include "engines/tpw/ui/ui_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>

namespace reluva::tpw {

namespace {

constexpr int kWidth = kOpeningWidth;
constexpr int kHeight = kOpeningHeight;
constexpr uint32_t kFrameMillis = 40;

// The HUD at 640 × 480: the money at the top left, the date in its box at
// the top right.
// The money right of the thermometer, its first digit at x 108 as the
// recording's.
constexpr int kCashX = 65;
constexpr int kCashY = 5;
constexpr Color kHudText{255, 255, 255};
// The money's shadow, down and right of it.
constexpr Color kHudShadow{0, 0, 0};
constexpr int kShadow = 2;

// The camera's start: over the entrance, looking into the park from the
// road, as the recording's first view after the briefing; how far it may
// go in and out.
constexpr float kStartYaw = 0;
constexpr float kStartPitch = 0.45f;
constexpr float kStartDistance = 200;
constexpr float kStartInside = 10; // units past the entrance it looks at
constexpr float kNearest = 60;
constexpr float kFarthest = 1600;

// How fast the keys move it: units a second at the start's distance,
// radians a second, and a factor a second.
constexpr float kPanPerSecond = 240;
constexpr float kTurnPerSecond = 1.6f;
constexpr float kZoomPerSecond = 2.5f;
constexpr float kWheelZoom = 1.15f;

// The fixed items' markers, by kind, until their models are placed.
constexpr Color kEntranceColour{255, 220, 0};
constexpr Color kBoothColour{255, 80, 40};
constexpr Color kBusStopColour{80, 160, 255};
// The visitors' sprites (esprites.wad), by generation: the children's, the
// parents', the grandparents'.
constexpr std::array<std::array<const char *, 4>, 3> kVisitorSprites{{
    {"esprites/Generic/Kids/SPR_KB", "esprites/Generic/Kids/SPR_GK", "esprites/Generic/Kids/SPR_BD",
     "esprites/Generic/Kids/SPR_GB"},
    {"esprites/Generic/Parents/SPR_FA", "esprites/Generic/Parents/SPR_MO"},
    {"esprites/Generic/Grandparents/SPR_GR", "esprites/Generic/Grandparents/SPR_GM"},
}};
constexpr size_t kWalkAnimation = 1;
constexpr size_t kStandAnimation = 0;
// A walking frame each unit walked: a step cycle of 8 units, about one
// and a half of an adult's heights (guessed).
constexpr float kUnitsPerWalkFrame = 1;
// A pixel of a frame, in the world's units: the recording's visitors stand
// about half a path's cell tall, and the settings hold an umbrella over a
// child at 5.7 units (Standard.sam's PersonSpriteUmbrellaModifiers), over
// sprites 50 to 60 pixels tall.
constexpr float kSpritePixelUnits = 0.1f;
constexpr Color kVisitorColour{255, 255, 255}; // a visitor whose sprite isn't read

void fillRect(Surface &s, const ScreenRect &r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(s.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.w, r.x + r.w); ++x) {
            uint8_t *p = s.row(y) + 4 * x;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
}

// A square at a point of the world, `units` across.
void mark(Surface &screen, const soft3d::OrbitCamera &camera, const gfx::Vec3 &at, float units, Color c) {
    const soft3d::ViewVertex v = camera.toView(toViewerWorld(at));
    if (v.z < camera.nearZ)
        return;
    const soft3d::ScreenVertex s = camera.project(v);
    const int size = std::max(1, int(units * camera.focal / v.z / 2));
    fillRect(screen, {int(s.x) - size, int(s.y) - size, 2 * size, 2 * size}, c);
}

void mark(Surface &screen, const soft3d::OrbitCamera &camera, const Park::Cell &cell, Color c) {
    mark(screen, camera, Park::cellCentre(cell), 4, c);
}

WorldView viewOf(const ParkScreen::Camera &camera) {
    WorldView view;
    view.aimed = true;
    view.target = camera.target;
    view.yaw = camera.yaw;
    view.pitch = camera.pitch;
    view.distance = camera.distance;
    view.objects = false;
    return view;
}

} // namespace

Error ParkScreen::load(const SearchSet &files, const Park &park) {
    if (Error err = _world.load(files, park.folder()))
        return err;
    _hud = false;
    _furnitureRead = !_furniture.load(files);
    for (size_t generation = 0; generation < kVisitorSprites.size(); ++generation) {
        _visitorLooks[generation].clear();
        for (const char *name : kVisitorSprites[generation]) {
            LoadedSprite sprite;
            if (name && !sprite.load(files, name))
                _visitorLooks[generation].push_back(std::move(sprite));
        }
    }
    const std::string language = languageOf(files);
    if (language.empty())
        return Error::success();
    const std::string folder = "Language/" + language + "/";
    std::vector<uint8_t> data;
    _hud = !files.read(folder + "MBToUni.dat", data) && !_characters.parse(data) &&
           !readGameFont(files, folder, "CASHMED", _cashFont) && !readGameFont(files, folder, "DATESMALL", _dateFont) &&
           !readStringTable(files, folder, "MONTHS", _characters, _months);
    return Error::success();
}

ParkScreen::Camera ParkScreen::startCamera(const Park &park) const {
    const auto &entrance = park.fixedItems().entrance;
    const gfx::Vec3 a = Park::cellCentre(entrance[0]);
    const gfx::Vec3 b = Park::cellCentre(entrance[1]);
    return {{(a[0] + b[0]) / 2, 0, (a[2] + b[2]) / 2 + kStartInside}, kStartYaw, kStartPitch, kStartDistance};
}

std::string ParkScreen::dateText(uint32_t day) const {
    const Park::Date d = Park::dateOf(day);
    const std::string month =
        _months.size() > size_t(d.month) ? _months.text(size_t(d.month)) : std::to_string(d.month + 1);
    char text[48];
    std::snprintf(text, sizeof text, "%d.%s.%02d", d.day, month.c_str(), d.year % 100);
    return text;
}

void ParkScreen::drawVisitors(Surface &screen, const soft3d::OrbitCamera &eye, const Park &park, float yaw) const {
    // Farthest first, each at the point it stands on.
    struct Seen {
        float depth;
        const Park::Visitor *visitor;
        soft3d::ScreenVertex at;
    };
    std::vector<Seen> seen;
    for (const Park::Visitor &v : park.visitorsAbout()) {
        const soft3d::ViewVertex view = eye.toView(toViewerWorld({v.x, 0, v.z}));
        if (view.z >= eye.nearZ)
            seen.push_back({view.z, &v, eye.project(view)});
    }
    std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.depth > b.depth; });
    for (const Seen &s : seen) {
        const Park::Visitor &v = *s.visitor;
        const std::vector<LoadedSprite> &looks = _visitorLooks[size_t(std::clamp(v.generation, 0, 2))];
        if (looks.empty()) {
            mark(screen, eye, {v.x, 1, v.z}, 2, kVisitorColour);
            continue;
        }
        const LoadedSprite &look = looks[v.look % looks.size()];
        const bool walking = v.headingX != 0 || v.headingZ != 0;
        const auto &animation = look.set.animations()[walking ? kWalkAnimation : kStandAnimation];
        // The heading as the camera sees it: 0 away from it, a quarter
        // turn to its right.
        const float forward = v.headingX * std::sin(yaw) + v.headingZ * std::cos(yaw);
        const float right = v.headingX * std::cos(yaw) - v.headingZ * std::sin(yaw);
        const SpritePose pose = spritePose(animation, walking ? std::atan2(right, forward) : 3.14159265f,
                                           walking ? uint32_t(v.walked / kUnitsPerWalkFrame) : 0);
        drawSpriteFrame(screen, look.sheet, pose.frame, s.at.x, s.at.y, kSpritePixelUnits * eye.focal / s.depth,
                        pose.mirror);
    }
}

void ParkScreen::draw(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const Camera &camera) const {
    std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    const WorldView view = viewOf(camera);
    drawWorld(screen, rasterizer, _world, view);
    const soft3d::OrbitCamera eye = worldCamera(_world, view, screen.w, screen.h);
    const Park::FixedItems &fixed = park.fixedItems();
    for (const Park::Cell &c : fixed.entrance)
        mark(screen, eye, c, kEntranceColour);
    for (const Park::Cell &c : fixed.ticketBooths)
        mark(screen, eye, c, kBoothColour);
    for (const Park::Cell &c : fixed.busStops)
        mark(screen, eye, c, kBusStopColour);
    drawVisitors(screen, eye, park, camera.yaw);
    if (_furnitureRead) {
        ParkHud::View hudView;
        hudView.canZoomIn = camera.distance > kNearest;
        hudView.canZoomOut = camera.distance < kFarthest;
        _furniture.draw(screen, hudView);
    }
    if (!_hud)
        return;
    // The money font's "$" is the game's currency sign, drawn like a "§".
    const std::string cash = "$ " + std::to_string(park.cash());
    drawGameText(screen, _cashFont, _characters, kCashX + kShadow, kCashY + kShadow, cash, kHudShadow);
    drawGameText(screen, _cashFont, _characters, kCashX, kCashY, cash, kHudText);
    const ScreenRect field = ParkHud::dateField();
    if (!_furnitureRead)
        fillRect(screen, field, Color{0, 0, 0});
    const std::string date = dateText(park.day());
    const int w = gameTextWidth(_dateFont, _characters, date);
    drawGameText(screen, _dateFont, _characters, field.x + (field.w - w) / 2, field.y + 3, date, kHudText);
}

bool playPark(System &sys, ParkScreen &screen, Park &park) {
    Surface surface;
    surface.create(kWidth, kHeight, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    ParkScreen::Camera camera = screen.startCamera(park);
    std::set<uint32_t> held;
    uint64_t last = sys.getMillis();
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if (screen.hud().handle(ev))
                continue;
            if (ev.type == EventType::kKeyDown) {
                if (ev.key == key::kEscape)
                    return true;
                if (ev.key == key::kHome)
                    camera = screen.startCamera(park);
                held.insert(ev.key);
            } else if (ev.type == EventType::kKeyUp) {
                held.erase(ev.key);
            } else if (ev.type == EventType::kMouseWheel && ev.wheelY) {
                camera.distance *= ev.wheelY > 0 ? 1 / kWheelZoom : kWheelZoom;
            }
        }
        const uint64_t now = sys.getMillis();
        const float seconds = float(now - last) / 1000;
        park.advance(uint32_t(now - last));
        last = now;

        // The arrows move along the ground as the camera faces; farther
        // out, faster.
        const float pan = kPanPerSecond * seconds * camera.distance / kStartDistance;
        const float forward = (held.count(key::kUp) ? 1.0f : 0.0f) - (held.count(key::kDown) ? 1.0f : 0.0f);
        const float sideways = (held.count(key::kRight) ? 1.0f : 0.0f) - (held.count(key::kLeft) ? 1.0f : 0.0f);
        camera.target[0] += pan * (forward * std::sin(camera.yaw) + sideways * std::cos(camera.yaw));
        camera.target[2] += pan * (forward * std::cos(camera.yaw) - sideways * std::sin(camera.yaw));
        // The compass's buttons as Q, E, Page Up and Page Down, while held.
        const ParkHud &hud = screen.hud();
        const bool turnLeft = held.count('q') || hud.held(ParkHud::CompassButton::kTurnLeft);
        const bool turnRight = held.count('e') || hud.held(ParkHud::CompassButton::kTurnRight);
        camera.yaw += kTurnPerSecond * seconds * ((turnRight ? 1.0f : 0.0f) - (turnLeft ? 1.0f : 0.0f));
        if (held.count(key::kPageUp) || hud.held(ParkHud::CompassButton::kZoomIn))
            camera.distance /= std::pow(kZoomPerSecond, seconds);
        if (held.count(key::kPageDown) || hud.held(ParkHud::CompassButton::kZoomOut))
            camera.distance *= std::pow(kZoomPerSecond, seconds);
        camera.distance = std::clamp(camera.distance, kNearest, kFarthest);

        screen.draw(surface, rasterizer, park, camera);
        sys.updateScreen(surface);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
