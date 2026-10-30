// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's view controls a driving page keeps, as its help's keyboard
// table has them: Thrill Cam, looking round in the car, Wide Angle, the
// dashboard, Camera Pan, Opponent Position and the controls' list (F1).
// See docs/engines/age-menus.md.

#include "engines/age/car.h"
#include "engines/age/car_actor.h"
#include "engines/age/hud_pictures.h"
#include "engines/age/music.h"
#include "engines/age/options.h"

#include <functional>
#include <string>
#include <vector>

namespace reluva::age {

struct ViewControls {
    bool thrill = false;       // Thrill Cam
    bool thrillPlaced = false; // where it stands is chosen
    Vec3 thrillAt{};           // and that
    float look = 0;            // in the car: radians round from ahead, left positive
    bool wide = false;         // Wide Angle: a wider view, no dashboard
    bool dashboard = true;     // Dashboard On/Off (in the car)
    bool panning = false;      // Camera Pan held
    float pan = 0;             // radians the follow camera's gone round the car
    bool opponent = false;     // Opponent Position: the little arrow to an opponent
    bool controlsList = false; // F1: the controls' list

    // A control pressed (true) or let go (false); whether it's one of these.
    bool press(Control c, bool down);
    // The view this frame with them: the Thrill Cam's place, the look round,
    // the pan.
    CameraView apply(const CameraView& view, const CarCamera& cam, const CarSim& sim, float dt);
    float fov(float fov) const { return wide ? fov * kWide : fov; }
    bool showDashboard() const { return dashboard && !wide; }

    static constexpr float kWide = 1.35f; // Wide Angle's share more of the view
};

// The CD player (Toggle CD Player, Start/Stop CD, Prev. and Next CD
// Track): the game's tunes as its tracks (GameMusic::trackNames).
struct CdPlayer {
    bool shown = false;
    bool playing = true;
    size_t track = 0;
    bool chosen = false; // a track picked (else the page's own tune plays)
    int pressed = 0;     // -1 previous, 1 next: lit a moment
    float pressedFor = 0;
    std::vector<std::string> tracks;

    // A CD control: true if it was one.
    bool press(Control c, GameMusic& music, const SearchSet& files, audio::Mixer* mixer);
    void update(float dt) {
        pressedFor = std::max(0.0f, pressedFor - dt);
        if (pressedFor <= 0)
            pressed = 0;
    }
    // Bottom middle of a 640 x 480 overlay, between the gauges and the map
    // (Reluva's place for it).
    void draw(Surface& overlay, const HudPictures& pictures) const;
};

// The controls' list as F1 shows it, over `overlay` (640 x 480): each
// control's name (`name` gives the game's words) and its keys.
void drawControlsList(Surface& overlay, const GameOptions& options, const std::function<std::string(Control)>& name);

} // namespace reluva::age
