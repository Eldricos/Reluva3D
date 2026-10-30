// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/view_controls.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kPi = 3.14159265358979f;
// The Thrill Cam (Reluva's: the help says only "a cool angle while you
// perform a big stunt"): stood ahead of the car, off to one side and a
// little up, it watches the car go by, and moves on ahead once the car's
// past it or far from it.
constexpr float kThrillAhead = 35, kThrillSide = 8, kThrillUp = 3, kThrillFar = 80;
constexpr float kPanRate = 1.2f; // radians a second round the car, Camera Pan held

Vec3 flatNormal(Vec3 v) {
    v[1] = 0;
    return length(v) > 1e-3f ? normalized(v) : Vec3{0, 0, -1};
}

} // namespace

bool ViewControls::press(Control c, bool down) {
    if (c == Control::CameraPan) {
        panning = down;
        return true;
    }
    if (!down)
        return false;
    switch (c) {
    case Control::ThrillCam:
        thrill = !thrill;
        thrillPlaced = false;
        return true;
    case Control::LookLeft:
        look = kPi / 2;
        return true;
    case Control::LookRight:
        look = -kPi / 2;
        return true;
    case Control::LookBack:
        look = kPi;
        return true;
    case Control::LookForward:
        look = 0;
        return true;
    case Control::WideAngle:
        wide = !wide;
        return true;
    case Control::Dashboard:
        dashboard = !dashboard;
        return true;
    case Control::OpponentPosition:
        opponent = !opponent;
        return true;
    default:
        return false;
    }
}

CameraView ViewControls::apply(const CameraView& view, const CarCamera& cam, const CarSim& sim, float dt) {
    CameraView v = view;
    if (thrill) {
        const Vec3 heading = flatNormal(length(sim.velocity) > 2 ? sim.velocity : -sim.orientation.z);
        const Vec3 side{-heading[2], 0, heading[0]};
        const Vec3 to = thrillAt - sim.position;
        if (!thrillPlaced || length(to) > kThrillFar || (dot(to, heading) < -10 && length(to) > 20)) {
            thrillAt = sim.position + heading * kThrillAhead + side * kThrillSide + Vec3{0, kThrillUp, 0};
            thrillPlaced = true;
        }
        v.eye = thrillAt;
        v.target = sim.position + Vec3{0, 1, 0};
        v.up = {0, 1, 0};
        return v;
    }
    if (cam.inside && look != 0) {
        // Round the car's up from where it looks.
        const Vec3 f = v.target - v.eye;
        const Vec3 u = sim.orientation.y;
        const Vec3 r = cross(u, f);
        v.target = v.eye + f * std::cos(look) + r * std::sin(look);
        return v;
    }
    // Camera Pan: round the car while held, back again after.
    pan = panning ? pan + kPanRate * dt : pan * (1 - std::min(1.0f, dt * 3));
    if (!cam.inside && std::fabs(pan) > 1e-3f) {
        const Vec3 d = v.eye - v.target;
        const float c = std::cos(pan), s = std::sin(pan);
        v.eye = v.target + Vec3{d[0] * c + d[2] * s, d[1], -d[0] * s + d[2] * c};
    }
    return v;
}

bool CdPlayer::press(Control c, GameMusic& music, const SearchSet& files, audio::Mixer* mixer) {
    if (c != Control::CdToggle && c != Control::CdPlay && c != Control::CdPrev && c != Control::CdNext)
        return false;
    if (tracks.empty())
        tracks = GameMusic::trackNames(files);
    if (c == Control::CdToggle) {
        shown = !shown;
        return true;
    }
    if (!mixer || tracks.empty())
        return true;
    if (c == Control::CdPlay) {
        if (playing) {
            music.stop();
            playing = false;
        } else {
            music.startTrack(files, *mixer, track);
            playing = chosen = true;
        }
        return true;
    }
    const int step = c == Control::CdNext ? 1 : -1;
    track = (track + tracks.size() + size_t(step + int(tracks.size()))) % tracks.size();
    music.startTrack(files, *mixer, track);
    playing = chosen = true;
    pressed = step;
    pressedFor = 0.3f;
    return true;
}

void CdPlayer::draw(Surface& overlay, const HudPictures& pictures) const {
    if (!shown)
        return;
    constexpr int kX = 230, kY = 372;
    const std::string text = chosen && track < tracks.size() ? std::to_string(track + 1) + " " + tracks[track] : "";
    pictures.cdPlayer(overlay, kX, kY, playing, pressed, text);
}

void drawControlsList(Surface& overlay, const GameOptions& options, const std::function<std::string(Control)>& name) {
    // A dark box with two columns: the control, its keys.
    constexpr int kX = 70, kY = 40, kW = 500, kRow = 12;
    const int rows = kControls;
    const int h = rows * kRow + 30;
    for (int y = kY; y < kY + h && y < overlay.h; ++y) {
        uint8_t* p = overlay.row(y) + kX * 4;
        for (int x = 0; x < kW; ++x, p += 4) {
            p[0] = 10;
            p[1] = 14;
            p[2] = 40;
            p[3] = 220;
        }
    }
    font::drawText(overlay, kX + 10, kY + 6, "Controls (F1 to go on)", {255, 230, 120});
    for (int i = 0; i < rows; ++i) {
        const int y = kY + 22 + i * kRow;
        font::drawText(overlay, kX + 10, y, name(Control(i)), {130, 230, 120});
        std::string keys = keyName(options.keys[size_t(i)][0]);
        if (options.keys[size_t(i)][1])
            keys += ", " + keyName(options.keys[size_t(i)][1]);
        font::drawText(overlay, kX + 230, y, keys, {255, 255, 255});
    }
}

} // namespace reluva::age
