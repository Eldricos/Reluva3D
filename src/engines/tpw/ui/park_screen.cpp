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
constexpr int kCashX = 22;
constexpr int kCashY = 2;
constexpr ScreenRect kDateBox{566, 97, 60, 16};
constexpr Color kHudText{255, 255, 255};

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

void mark(Surface &screen, const soft3d::OrbitCamera &camera, const Park::Cell &cell, Color c) {
    const soft3d::ViewVertex v = camera.toView(toViewerWorld(Park::cellCentre(cell)));
    if (v.z < camera.nearZ)
        return;
    const soft3d::ScreenVertex s = camera.project(v);
    const int size = std::max(2, int(4 * Park::kCellUnits * camera.focal / v.z / 10));
    fillRect(screen, {int(s.x) - size, int(s.y) - size, 2 * size, 2 * size}, c);
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
    const std::string language = languageOf(files);
    if (language.empty())
        return Error::success();
    const std::string folder = "Language/" + language + "/";
    std::vector<uint8_t> data;
    _hud = !files.read(folder + "MBToUni.dat", data) && !_characters.parse(data) &&
           !readGameFont(files, folder, "CASHSMALL", _cashFont) &&
           !readGameFont(files, folder, "DATESMALL", _dateFont) &&
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
    if (!_hud)
        return;
    drawGameText(screen, _cashFont, _characters, kCashX, kCashY, "\u00a7 " + std::to_string(park.cash()), kHudText);
    fillRect(screen, kDateBox, Color{0, 0, 0});
    const std::string date = dateText(park.day());
    const int w = gameTextWidth(_dateFont, _characters, date);
    drawGameText(screen, _dateFont, _characters, kDateBox.x + (kDateBox.w - w) / 2, kDateBox.y + 3, date, kHudText);
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
        camera.yaw += kTurnPerSecond * seconds * ((held.count('e') ? 1.0f : 0.0f) - (held.count('q') ? 1.0f : 0.0f));
        if (held.count(key::kPageUp))
            camera.distance /= std::pow(kZoomPerSecond, seconds);
        if (held.count(key::kPageDown))
            camera.distance *= std::pow(kZoomPerSecond, seconds);
        camera.distance = std::clamp(camera.distance, kNearest, kFarthest);

        screen.draw(surface, rasterizer, park, camera);
        sys.updateScreen(surface);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
